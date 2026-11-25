MODULE ActorKvJournal;
(** Журнал событий в базе KvDb: транзакции, фиксация с Sync.

	Ключи (имя потока — в UTF-8, в нём нет нулевых байтов):

		"e" + поток + 0X + seq (8 байт, старший вперёд)  ->  событие
		"s" + поток                                        ->  снимок

	Значение события: time (8), cause (8), длина имени автора (2), автор
	(по 2 байта на знак), затем кадр. Значение снимка: seq (8), кадр.
	Номера старшим байтом вперёд, поэтому ключи одного потока идут в
	порядке номеров, и Read — проход курсора по диапазону.

	Последний номер потока ищет курсор: Seek на ключ «поток + 0X + восемь
	0FFX» (за последним событием потока), шаг назад, сверка префикса.
	Отдельный ключ-счётчик отвергнут: он дублирует то, что уже есть в
	ключах, и его можно рассогласовать с ними; поиск по дереву — те же
	O(log n) страниц, что и чтение счётчика.

	Append вне задачи — своя пишущая транзакция; из задачи — групповая
	фиксация (Lead): одна транзакция на всех, кто пишет одновременно.
	Транзакция (KvDb.Update): найти последний номер,
	сравнить с expected, записать, зафиксировать. Несовпадение — Abort,
	ничего не записано. Фиксация — страницы, Sync, мета, Sync: событие,
	о котором Append ответил ok, переживает выключение питания, а
	прерванная фиксация оставляет поток таким, каким он был до неё.

	Ожидание. Внутри Update и View точек переключения нет. Begin(TRUE) из
	другой задачи, пока пишущая открыта, ждёт (Tasks.Park); у акторов на
	общем пуле это ожидание исполнителя. Пока Update не переключается,
	другая задача открытой пишущей не застаёт; Sync же держит исполнитель
	до конца записи на носитель. *)

	IMPORT Kernel, Tasks, Utf, KvDb, ActorJournal;

	TYPE
		Bytes = ActorJournal.Bytes;

		Journal = POINTER TO RECORD (ActorJournal.Journal)
			db: KvDb.Db;
			queue, tail: Request;	(* ждут следующей пачки *)
			leading: BOOLEAN	(* пачку собирает и фиксирует задача-ведущий *)
		END;

		Request = POINTER TO RECORD
			next: Request;
			stream: ActorJournal.Name; expected: LONGINT;
			key, val: Bytes;
			res: INTEGER; done: BOOLEAN;
			task: Tasks.Task
		END;

		Batch = POINTER TO RECORD (KvDb.Body) first: Request END;

		Cleaner = POINTER TO RECORD (Kernel.TrapCleaner) j: Journal; batch: Request END;

		(* тела транзакций *)
		Appender = POINTER TO RECORD (KvDb.Body)
			stream: ActorJournal.Name; expected: LONGINT;
			key, val: Bytes; res: INTEGER
		END;
		Reader = POINTER TO RECORD (KvDb.Body)
			stream: ActorJournal.Name; from: LONGINT; sink: ActorJournal.Sink; res: INTEGER
		END;
		Laster = POINTER TO RECORD (KvDb.Body) stream: ActorJournal.Name; last: LONGINT END;
		SnapSaver = POINTER TO RECORD (KvDb.Body)
			stream: ActorJournal.Name; seq: LONGINT; val: Bytes; res: INTEGER
		END;
		SnapLoader = POINTER TO RECORD (KvDb.Body)
			stream: ActorJournal.Name; val: Bytes; found: BOOLEAN; res: INTEGER
		END;

	(* --- ключи и значения --------------------------------------------------- *)

	PROCEDURE Prefix (tag: CHAR; IN stream: ARRAY OF CHAR; extra: INTEGER; OUT n: INTEGER): Bytes;
	(* Пост: b[0 .. n-1] = tag + UTF-8(stream); LEN(b) = n + extra. *)
		VAR s: ARRAY 3 * ActorJournal.nameLen + 1 OF SHORTCHAR; r, i: INTEGER; b: Bytes;
	BEGIN
		Utf.StringToUtf8(stream, s, r); ASSERT(r = 0, 100);
		n := LEN(s$) + 1; NEW(b, n + extra);
		b[0] := SHORT(SHORT(ORD(tag)));
		FOR i := 1 TO n - 1 DO b[i] := SHORT(ORD(s[i - 1])) END;
		RETURN b
	END Prefix;

	PROCEDURE PutNum (VAR b: ARRAY OF BYTE; pos: INTEGER; x: LONGINT; k: INTEGER);
	(* Пост: b[pos .. pos+k-1] — k младших байтов x, старший вперёд. *)
		VAR i: INTEGER;
	BEGIN
		FOR i := pos + k - 1 TO pos BY -1 DO b[i] := SHORT(SHORT(SHORT(x MOD 256))); x := x DIV 256 END
	END PutNum;

	PROCEDURE GetNum (IN b: ARRAY OF BYTE; pos, k: INTEGER): LONGINT;
		VAR i: INTEGER; x: LONGINT;
	BEGIN
		x := 0;
		FOR i := pos TO pos + k - 1 DO x := x * 256 + b[i] MOD 256 END;
		RETURN x
	END GetNum;

	PROCEDURE EventKey (IN stream: ARRAY OF CHAR; seq: LONGINT): Bytes;
	(* seq < 0 — ключ за последним возможным событием потока *)
		VAR b: Bytes; n, i: INTEGER;
	BEGIN
		b := Prefix("e", stream, 9, n); b[n] := 0;
		IF seq < 0 THEN FOR i := n + 1 TO n + 8 DO b[i] := -1 END ELSE PutNum(b, n + 1, seq, 8) END;
		RETURN b
	END EventKey;

	PROCEDURE SnapKey (IN stream: ARRAY OF CHAR): Bytes;
		VAR n: INTEGER;
	BEGIN
		RETURN Prefix("s", stream, 0, n)
	END SnapKey;

	PROCEDURE SameStream (IN key, pat: ARRAY OF BYTE): BOOLEAN;
	(* key — ключ события того же потока, что pat (pat — ключ события) *)
		VAR i: INTEGER;
	BEGIN
		IF LEN(key) # LEN(pat) THEN RETURN FALSE END;
		FOR i := 0 TO LEN(pat) - 9 DO IF key[i] # pat[i] THEN RETURN FALSE END END;
		RETURN TRUE
	END SameStream;

	PROCEDURE LastSeq (tx: KvDb.Tx; IN stream: ARRAY OF CHAR): LONGINT;
		VAR end: Bytes; c: KvDb.Cursor; k: Bytes;
	BEGIN
		end := EventKey(stream, -1);
		c := tx.Seek(end^);
		IF c.Valid() THEN c.Prev ELSE c := tx.Last() END;
		IF ~c.Valid() THEN RETURN 0 END;
		k := c.Key();
		IF SameStream(k^, end^) THEN RETURN GetNum(k^, LEN(k^) - 8, 8) ELSE RETURN 0 END
	END LastSeq;

	PROCEDURE Res (r: INTEGER): INTEGER;
	BEGIN
		IF r = KvDb.ok THEN RETURN ActorJournal.ok
		ELSIF r = KvDb.corrupt THEN RETURN ActorJournal.corrupt
		ELSE RETURN ActorJournal.failed
		END
	END Res;

	(* --- тела транзакций ---------------------------------------------------- *)

	PROCEDURE (a: Appender) Do- (tx: KvDb.Tx);
	BEGIN
		IF LastSeq(tx, a.stream) # a.expected THEN a.res := ActorJournal.conflict; tx.Abort
		ELSE tx.Put(a.key^, a.val^)
		END
	END Do;

	PROCEDURE (r: Reader) Do- (tx: KvDb.Tx);
		VAR start, k, v: Bytes; c: KvDb.Cursor; m: ActorJournal.Meta; i, n, len: INTEGER;
	BEGIN
		start := EventKey(r.stream, MAX(r.from, 1));
		c := tx.Seek(start^);
		LOOP
			IF ~c.Valid() THEN EXIT END;
			k := c.Key();
			IF ~SameStream(k^, start^) THEN EXIT END;
			v := c.Val();
			m.seq := GetNum(k^, LEN(k^) - 8, 8);
			m.time := GetNum(v^, 0, 8); m.cause := GetNum(v^, 8, 8);
			n := SHORT(GetNum(v^, 16, 2));
			IF (n >= ActorJournal.nameLen) OR (18 + 2 * n > LEN(v^)) THEN r.res := ActorJournal.corrupt; EXIT END;
			FOR i := 0 TO n - 1 DO m.author[i] := CHR(GetNum(v^, 18 + 2 * i, 2)) END;
			m.author[n] := 0X;
			len := LEN(v^) - 18 - 2 * n;
			FOR i := 0 TO len - 1 DO v[i] := v[18 + 2 * n + i] END;
			IF ~r.sink.Event(m, v^, len) THEN EXIT END;
			c.Next
		END;
		IF (r.res = ActorJournal.ok) & (tx.res # KvDb.ok) THEN r.res := Res(tx.res) END
	END Do;

	PROCEDURE (l: Laster) Do- (tx: KvDb.Tx);
	BEGIN
		l.last := LastSeq(tx, l.stream)
	END Do;

	PROCEDURE (s: SnapSaver) Do- (tx: KvDb.Tx);
	BEGIN
		IF (s.seq < 1) OR (s.seq > LastSeq(tx, s.stream)) THEN s.res := -1; tx.Abort
		ELSE tx.Put(SnapKey(s.stream)^, s.val^)
		END
	END Do;

	PROCEDURE (s: SnapLoader) Do- (tx: KvDb.Tx);
	BEGIN
		s.found := tx.Get(SnapKey(s.stream)^, s.val); s.res := tx.res
	END Do;

	PROCEDURE (b: Batch) Do- (tx: KvDb.Tx);
	(* Каждый запрос сверяется со своим потоком в той же транзакции:
	   конфликт одного не мешает остальным. *)
		VAR r: Request;
	BEGIN
		r := b.first;
		WHILE r # NIL DO
			IF LastSeq(tx, r.stream) # r.expected THEN r.res := ActorJournal.conflict
			ELSE tx.Put(r.key^, r.val^); r.res := ActorJournal.ok
			END;
			r := r.next
		END
	END Do;

	PROCEDURE Finish (j: Journal; first: Request; res: INTEGER);
	(* Пост: запросы пачки получили итог фиксации res (кроме конфликтов) и разбужены. *)
		VAR r: Request;
	BEGIN
		r := first;
		WHILE r # NIL DO
			IF r.res # ActorJournal.conflict THEN r.res := res END;
			r.done := TRUE;
			IF r.task # Tasks.Current() THEN Tasks.Unpark(r.task) END;
			r := r.next
		END
	END Finish;

	PROCEDURE (c: Cleaner) Cleanup;
	(* Трап ведущего: его пачка и очередь получают failed, ведущего нет. *)
	BEGIN
		Finish(c.j, c.batch, ActorJournal.failed);
		Finish(c.j, c.j.queue, ActorJournal.failed);
		c.j.queue := NIL; c.j.tail := NIL; c.j.leading := FALSE
	END Cleanup;

	PROCEDURE Lead (j: Journal);
	(* Групповая фиксация. Ведущий уступает один раз, чтобы другие задачи
	   успели встать в очередь, затем фиксирует всю очередь одной
	   транзакцией; пока он пишет, точек переключения нет, и новые запросы
	   ждут следующей пачки. *)
		VAR b: Batch; r, res: INTEGER; cl: Cleaner;
	BEGIN
		j.leading := TRUE;
		NEW(cl); cl.j := j; Kernel.PushTrapCleaner(cl);
		Tasks.Yield;
		WHILE j.queue # NIL DO
			NEW(b); b.first := j.queue; cl.batch := b.first; j.queue := NIL; j.tail := NIL;
			KvDb.Update(j.db, b, r);
			res := Res(r);
			Finish(j, b.first, res); cl.batch := NIL
		END;
		j.leading := FALSE;
		Kernel.PopTrapCleaner(cl)
	END Lead;

	(* --- журнал ------------------------------------------------------------- *)

	PROCEDURE (j: Journal) Last (IN stream: ARRAY OF CHAR): LONGINT;
		VAR l: Laster;
	BEGIN
		NEW(l); l.stream := stream$; KvDb.View(j.db, l);
		RETURN l.last
	END Last;

	PROCEDURE (j: Journal) Append (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: ActorJournal.Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR a: Appender; n, i, r: INTEGER; q: Request;
	BEGIN
		ASSERT(stream # "", 20); ASSERT(m.seq = expected + 1, 21); ASSERT((len >= 0) & (len <= LEN(body)), 22);
		NEW(a); a.stream := stream$; a.expected := expected; a.res := ActorJournal.ok;
		a.key := EventKey(stream, m.seq);
		n := LEN(m.author$);
		NEW(a.val, 18 + 2 * n + len);
		PutNum(a.val, 0, m.time, 8); PutNum(a.val, 8, m.cause, 8); PutNum(a.val, 16, n, 2);
		FOR i := 0 TO n - 1 DO PutNum(a.val, 18 + 2 * i, ORD(m.author[i]), 2) END;
		FOR i := 0 TO len - 1 DO a.val[18 + 2 * n + i] := body[i] END;
		IF ~Tasks.InTask() THEN	(* вне задачи ждать некому: своя транзакция *)
			KvDb.Update(j.db, a, r);
			IF a.res = ActorJournal.conflict THEN res := ActorJournal.conflict ELSE res := Res(r) END
		ELSE
			NEW(q); q.stream := a.stream; q.expected := expected; q.key := a.key; q.val := a.val;
			q.task := Tasks.Current(); q.res := ActorJournal.ok;
			IF j.tail = NIL THEN j.queue := q ELSE j.tail.next := q END;
			j.tail := q;
			IF j.leading THEN
				WHILE ~q.done DO Tasks.Park END
			ELSE Lead(j)
			END;
			res := q.res
		END
	END Append;

	PROCEDURE (j: Journal) Read (IN stream: ARRAY OF CHAR; from: LONGINT; k: ActorJournal.Sink; OUT res: INTEGER);
		VAR r: Reader;
	BEGIN
		ASSERT(k # NIL, 20);
		NEW(r); r.stream := stream$; r.from := from; r.sink := k; r.res := ActorJournal.ok;
		KvDb.View(j.db, r);
		res := r.res
	END Read;

	PROCEDURE (j: Journal) SaveSnapshot (IN stream: ARRAY OF CHAR; seq: LONGINT;
		IN data: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR s: SnapSaver; i, r: INTEGER;
	BEGIN
		ASSERT((len >= 0) & (len <= LEN(data)), 21);
		NEW(s); s.stream := stream$; s.seq := seq; s.res := ActorJournal.ok;
		NEW(s.val, 8 + len); PutNum(s.val, 0, seq, 8);
		FOR i := 0 TO len - 1 DO s.val[8 + i] := data[i] END;
		KvDb.Update(j.db, s, r);
		ASSERT(s.res # -1, 20);
		res := Res(r)
	END SaveSnapshot;

	PROCEDURE (j: Journal) LoadSnapshot (IN stream: ARRAY OF CHAR; OUT seq: LONGINT;
		OUT data: Bytes; OUT len: INTEGER; OUT res: INTEGER);
		VAR s: SnapLoader; i: INTEGER;
	BEGIN
		NEW(s); s.stream := stream$; KvDb.View(j.db, s);
		seq := 0; data := NIL; len := 0;
		IF s.res # KvDb.ok THEN res := Res(s.res)
		ELSIF ~s.found THEN res := ActorJournal.absent
		ELSIF LEN(s.val^) < 8 THEN res := ActorJournal.corrupt
		ELSE
			seq := GetNum(s.val^, 0, 8); len := LEN(s.val^) - 8; NEW(data, MAX(len, 1));
			FOR i := 0 TO len - 1 DO data[i] := s.val[8 + i] END;
			res := ActorJournal.ok
		END
	END LoadSnapshot;

	PROCEDURE (j: Journal) Close;
	BEGIN
		IF j.db # NIL THEN j.db.Close; j.db := NIL END
	END Close;

	PROCEDURE New* (db: KvDb.Db): ActorJournal.Journal;
	(** Журнал в открытой базе db. Пред: db # NIL — 20.
		Пост: журнал владеет db: его Close закрывает базу. *)
		VAR j: Journal;
	BEGIN
		ASSERT(db # NIL, 20);
		NEW(j); j.db := db; RETURN j
	END New;

END ActorKvJournal.
