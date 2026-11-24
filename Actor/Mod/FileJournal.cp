MODULE ActorFileJournal;
(** Журнал событий в файле «только дозапись»; снимки — блоками CasStore.

	Временная дисковая реализация ActorJournal до упорядоченного хранилища
	ключ -> значение. Все потоки лежат в одном файле подряд, запись за
	записью:

		0A5X, длина тела записи P (4 байта), тело (P байт), сумма (4 байта)

	тело: длина имени потока, имя, seq, time, cause (по 8 байт), длина имени
	автора, имя автора, затем кадр события. Сумма — FNV-1a 32 над телом.
	Числа — младшим байтом вперёд, знаки имени — по два байта.

	Открытие читает файл целиком и строит в памяти указатель потоков:
	метаданные и место тела каждого события. Первая запись, которая не
	дочитана или чья сумма не сошлась, считается недописанным хвостом: она
	и всё после неё отбрасываются, следующая запись ложится на её место.

	Границы. Flush отдаёт данные ОС, но не ждёт записи на носитель (fsync):
	сбой питания может унести последние записи, и они отбросятся как хвост.
	Файл держится открытым исключительно: один процесс на файл. Сумма
	ловит обрыв и случайную порчу, но не злонамеренную подмену. Указатель
	занимает память на каждое событие, открытие — время, пропорциональное
	размеру файла. Снимки лежат в CasStore: ссылка "s" + 40 знаков хеша
	имени потока указывает на блок «seq (8 байт), кадр». *)

	IMPORT Files, CasHash, CasStore, ActorJournal;

	CONST
		magic = 0A5H;
		headLen = 5;	(* magic и длина *)

	TYPE
		Entry = RECORD meta: ActorJournal.Meta; pos, len: INTEGER END;	(* место кадра в файле *)

		Stream = POINTER TO RECORD
			next: Stream;
			name: ActorJournal.Name;
			ev: POINTER TO ARRAY OF Entry;
			n: INTEGER
		END;

		Journal = POINTER TO RECORD (ActorJournal.Journal)
			f: Files.File;
			r: Files.Reader; w: Files.Writer;
			end: INTEGER;	(* конец последней целой записи *)
			streams: Stream;
			snaps: CasStore.Store;
			buf: ActorJournal.Bytes
		END;

	(* --- байты ------------------------------------------------------------ *)

	PROCEDURE Sum (IN b: ARRAY OF BYTE; beg, len: INTEGER): INTEGER;
		VAR h, i: INTEGER;
	BEGIN
		h := -2128831035;	(* 811C9DC5H *)
		FOR i := beg TO beg + len - 1 DO
			h := ORD(BITS(h) / BITS(b[i] MOD 256)); h := h * 16777619
		END;
		RETURN h
	END Sum;

	PROCEDURE Put (VAR b: ActorJournal.Bytes; VAR n: INTEGER; x: LONGINT; k: INTEGER);
	(* Пост: k младших байтов x дописаны в b[n ..]. *)
		VAR i: INTEGER; nb: ActorJournal.Bytes;
	BEGIN
		IF n + k > LEN(b^) THEN
			NEW(nb, 2 * (n + k)); FOR i := 0 TO n - 1 DO nb[i] := b[i] END; b := nb
		END;
		FOR i := 0 TO k - 1 DO b[n] := SHORT(SHORT(SHORT(x MOD 256))); x := x DIV 256; INC(n) END
	END Put;

	PROCEDURE Get (IN b: ARRAY OF BYTE; VAR n: INTEGER; k: INTEGER): LONGINT;
		VAR i: INTEGER; x: LONGINT;
	BEGIN
		x := 0;
		FOR i := k - 1 TO 0 BY -1 DO x := x * 256 + b[n + i] MOD 256 END;
		IF (k < 8) & (x >= ASH(LONG(1), 8 * k - 1)) THEN x := x - ASH(LONG(1), 8 * k) END;
		INC(n, k);
		RETURN x
	END Get;

	PROCEDURE PutName (VAR b: ActorJournal.Bytes; VAR n: INTEGER; IN s: ARRAY OF CHAR);
		VAR i, l: INTEGER;
	BEGIN
		l := LEN(s$); Put(b, n, l, 2);
		FOR i := 0 TO l - 1 DO Put(b, n, ORD(s[i]), 2) END
	END PutName;

	PROCEDURE GetName (IN b: ARRAY OF BYTE; VAR n: INTEGER; lim: INTEGER; OUT s: ActorJournal.Name): BOOLEAN;
		VAR i, l: INTEGER;
	BEGIN
		IF n + 2 > lim THEN RETURN FALSE END;
		l := SHORT(Get(b, n, 2));
		IF (l < 0) OR (l >= ActorJournal.nameLen) OR (n + 2 * l > lim) THEN RETURN FALSE END;
		FOR i := 0 TO l - 1 DO s[i] := CHR(Get(b, n, 2) MOD 65536) END;
		s[l] := 0X;
		RETURN TRUE
	END GetName;

	(* --- указатель потоков ------------------------------------------------ *)

	PROCEDURE (j: Journal) Find (IN stream: ARRAY OF CHAR; make: BOOLEAN): Stream, NEW;
		VAR s: Stream;
	BEGIN
		s := j.streams;
		WHILE (s # NIL) & (s.name # stream) DO s := s.next END;
		IF (s = NIL) & make THEN NEW(s); s.name := stream$; NEW(s.ev, 16); s.next := j.streams; j.streams := s END;
		RETURN s
	END Find;

	PROCEDURE (s: Stream) Add (IN m: ActorJournal.Meta; pos, len: INTEGER), NEW;
		VAR new: POINTER TO ARRAY OF Entry; i: INTEGER;
	BEGIN
		IF s.n = LEN(s.ev^) THEN
			NEW(new, 2 * s.n); FOR i := 0 TO s.n - 1 DO new[i] := s.ev[i] END; s.ev := new
		END;
		s.ev[s.n].meta := m; s.ev[s.n].pos := pos; s.ev[s.n].len := len; INC(s.n)
	END Add;

	PROCEDURE (j: Journal) Scan, NEW;
	(* Пост: указатель построен по целым записям; end — конец последней. *)
		VAR pos, flen, p, n, sum: INTEGER; head: ARRAY headLen + 4 OF BYTE; b: ActorJournal.Bytes;
			name: ActorJournal.Name; m: ActorJournal.Meta; s: Stream; ok: BOOLEAN;
	BEGIN
		flen := j.f.Length(); pos := 0;
		LOOP
			IF pos + headLen > flen THEN EXIT END;
			j.r.SetPos(pos); j.r.ReadBytes(head, 0, headLen);
			IF head[0] # SHORT(SHORT(magic - 256)) THEN EXIT END;
			n := 1; p := SHORT(Get(head, n, 4));
			IF (p <= 0) OR (pos + headLen + p + 4 > flen) THEN EXIT END;
			NEW(b, p + 4); j.r.ReadBytes(b, 0, p + 4);
			n := p; sum := SHORT(Get(b, n, 4));
			IF sum # Sum(b, 0, p) THEN EXIT END;
			n := 0;
			ok := GetName(b, n, p, name) & (n + 24 <= p);
			IF ok THEN
				m.seq := Get(b, n, 8); m.time := Get(b, n, 8); m.cause := Get(b, n, 8);
				ok := GetName(b, n, p, m.author)
			END;
			IF ~ok THEN EXIT END;
			s := j.Find(name, TRUE);
			IF m.seq # s.n + 1 THEN EXIT END;
			s.Add(m, pos + headLen + n, p - n);
			pos := pos + headLen + p + 4
		END;
		j.end := pos
	END Scan;

	(* --- журнал ----------------------------------------------------------- *)

	PROCEDURE (j: Journal) Last (IN stream: ARRAY OF CHAR): LONGINT;
		VAR s: Stream;
	BEGIN
		s := j.Find(stream, FALSE);
		IF s = NIL THEN RETURN 0 ELSE RETURN s.n END
	END Last;

	PROCEDURE (j: Journal) Append (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: ActorJournal.Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR s: Stream; n, p, i: INTEGER;
	BEGIN
		ASSERT(stream # "", 20); ASSERT(m.seq = expected + 1, 21); ASSERT((len >= 0) & (len <= LEN(body)), 22);
		s := j.Find(stream, TRUE);
		IF s.n # expected THEN res := ActorJournal.conflict; RETURN END;
		n := 0;
		Put(j.buf, n, magic, 1); Put(j.buf, n, 0, 4);	(* длина — ниже *)
		PutName(j.buf, n, stream);
		Put(j.buf, n, m.seq, 8); Put(j.buf, n, m.time, 8); Put(j.buf, n, m.cause, 8);
		PutName(j.buf, n, m.author);
		p := n;	(* начало кадра *)
		FOR i := 0 TO len - 1 DO Put(j.buf, n, body[i], 1) END;
		i := 1; Put(j.buf, i, n - headLen, 4);
		Put(j.buf, n, Sum(j.buf, headLen, n - headLen), 4);
		j.w.SetPos(j.end); j.w.WriteBytes(j.buf, 0, n); j.f.Flush;
		s.Add(m, j.end + p, len);
		j.end := j.end + n;
		res := ActorJournal.ok
	END Append;

	PROCEDURE (j: Journal) Read (IN stream: ARRAY OF CHAR; from: LONGINT; k: ActorJournal.Sink; OUT res: INTEGER);
		VAR s: Stream; i: INTEGER; b: ActorJournal.Bytes;
	BEGIN
		ASSERT(k # NIL, 20);
		res := ActorJournal.ok; s := j.Find(stream, FALSE);
		IF s = NIL THEN RETURN END;
		IF from < 1 THEN from := 1 END;
		i := SHORT(from) - 1;
		WHILE i < s.n DO
			NEW(b, MAX(s.ev[i].len, 1));
			j.r.SetPos(s.ev[i].pos); j.r.ReadBytes(b, 0, s.ev[i].len);
			IF ~k.Event(s.ev[i].meta, b, s.ev[i].len) THEN RETURN END;
			INC(i)
		END
	END Read;

	PROCEDURE SnapRef (IN stream: ARRAY OF CHAR; OUT ref: CasStore.RefName);
		VAR b: ActorJournal.Bytes; n: INTEGER; id: CasHash.Id; hex: ARRAY 2 * CasHash.size + 1 OF CHAR;
	BEGIN
		NEW(b, 16); n := 0; PutName(b, n, stream);
		CasHash.Of(b, 0, n, id); CasHash.ToHex(id, hex);
		hex[40] := 0X; ref := "s" + hex
	END SnapRef;

	PROCEDURE (j: Journal) SaveSnapshot (IN stream: ARRAY OF CHAR; seq: LONGINT;
		IN data: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR s: Stream; b: ActorJournal.Bytes; n, i: INTEGER; ref: CasStore.RefName; old, id: CasHash.Id;
	BEGIN
		s := j.Find(stream, FALSE);
		ASSERT((s # NIL) & (seq > 0) & (seq <= s.n), 20); ASSERT((len >= 0) & (len <= LEN(data)), 21);
		NEW(b, len + 8); n := 0; Put(b, n, seq, 8);
		FOR i := 0 TO len - 1 DO b[n] := data[i]; INC(n) END;
		res := ActorJournal.failed;
		j.snaps.Put(b, n, id, i);
		IF i # CasStore.ok THEN RETURN END;
		SnapRef(stream, ref);
		j.snaps.GetRef(ref, old, i);
		IF (i = CasStore.ok) & j.snaps.CasRef(ref, old, id, i) THEN res := ActorJournal.ok END
	END SaveSnapshot;

	PROCEDURE (j: Journal) LoadSnapshot (IN stream: ARRAY OF CHAR; OUT seq: LONGINT;
		OUT data: ActorJournal.Bytes; OUT len: INTEGER; OUT res: INTEGER);
		VAR ref: CasStore.RefName; id: CasHash.Id; b: CasStore.Block; r, n, i: INTEGER;
	BEGIN
		seq := 0; data := NIL; len := 0;
		SnapRef(stream, ref);
		j.snaps.GetRef(ref, id, r);
		IF r # CasStore.ok THEN res := ActorJournal.failed; RETURN END;
		IF CasHash.IsNil(id) THEN res := ActorJournal.absent; RETURN END;
		j.snaps.Get(id, b, r);
		IF r = CasStore.corrupt THEN res := ActorJournal.corrupt; RETURN
		ELSIF (r # CasStore.ok) OR (LEN(b^) < 8) THEN res := ActorJournal.failed; RETURN
		END;
		n := 0; seq := Get(b^, n, 8);
		len := LEN(b^) - 8; NEW(data, MAX(len, 1));
		FOR i := 0 TO len - 1 DO data[i] := b[8 + i] END;
		res := ActorJournal.ok
	END LoadSnapshot;

	PROCEDURE (j: Journal) Close;
	BEGIN
		IF j.f # NIL THEN j.f.Close; j.f := NIL END
	END Close;

	PROCEDURE Open* (loc: Files.Locator; IN name: Files.Name; snaps: CasStore.Store;
		OUT res: INTEGER): ActorJournal.Journal;
	(** Открыть журнал name в каталоге loc; нет файла — создать.
		Пред: loc # NIL — 20; name # "" — 21; snaps # NIL — 22.
		Пост: res = ActorJournal.ok — журнал с событиями всех целых записей
		файла, недописанный хвост отброшен; res = failed — файл не открыт,
		результат NIL. *)
		VAR j: Journal; f: Files.File; r: INTEGER;
	BEGIN
		ASSERT(loc # NIL, 20); ASSERT(name # "", 21); ASSERT(snaps # NIL, 22);
		f := Files.dir.Old(loc, name, Files.exclusive);
		IF f = NIL THEN
			f := Files.dir.New(loc, Files.dontAsk);
			IF f # NIL THEN f.Register(name, "", Files.dontAsk, r); f := Files.dir.Old(loc, name, Files.exclusive) END
		END;
		IF f = NIL THEN res := ActorJournal.failed; RETURN NIL END;
		NEW(j); j.f := f; j.r := f.NewReader(NIL); j.w := f.NewWriter(NIL); j.snaps := snaps;
		NEW(j.buf, 256);
		j.Scan;
		res := ActorJournal.ok;
		RETURN j
	END Open;

END ActorFileJournal.
