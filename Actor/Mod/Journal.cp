MODULE ActorJournal;
(** Журнал событий и снимки акторов: абстрактный провайдер и реализация в
	памяти.

	Поток — имя актора. Событие потока имеет номер seq: 1, 2, 3 … без
	пропусков. Запись идёт с ожидаемым номером последнего события
	(оптимистическая проверка версии, OCC): не совпал — conflict, ничего не
	записано. Событие и снимок на этом уровне — байты (кадр кодека);
	метаданные события (номер, время, автор, причина) лежат рядом с телом и
	читаются без его разбора.

	Провайдер устроен как Files.Directory: абстрактный тип, переменные dir и
	stdDir, процедура SetDir. Дисковая реализация — ActorFileJournal.

	Раскладка для упорядоченного хранилища ключ -> значение (адаптер к Kv):
	событие — ключ "e" + поток + 0X + seq (8 байт, big-endian), значение —
	метаданные и тело; снимок — ключ "s" + поток, значение — seq и кадр.
	Append — одна пишущая транзакция: найти последний ключ потока
	(Seek на "e" + поток + 0X + FFFF…), сравнить номер, Put. *)

	IMPORT Dates;

	CONST
		(** res *)
		ok* = 0;
		conflict* = 1;	(** ожидаемый номер не совпал с последним *)
		absent* = 2;	(** снимка нет *)
		failed* = 3;	(** ошибка среды: файл не записан, не прочитан *)
		corrupt* = 4;	(** кадр повреждён *)
		nameLen* = 128;
		buckets = 256;

	TYPE
		Bytes* = POINTER TO ARRAY OF BYTE;
		Name* = ARRAY nameLen OF CHAR;

		(** Метаданные события: аудит и происхождение. *)
		Meta* = RECORD
			seq*: LONGINT;	(** номер события в потоке, с 1 *)
			time*: LONGINT;	(** время записи: ГГГГММДДччммсс, UTC *)
			cause*: LONGINT;	(** номер причины: письма или операции, из-за которой событие *)
			author*: Name	(** имя актора, записавшего событие *)
		END;

		(** Приёмник событий Read. *)
		Sink* = POINTER TO ABSTRACT RECORD END;

		Journal* = POINTER TO ABSTRACT RECORD END;

		(* Тела событий потока лежат подряд в одном массиве data: событие без
		   своего указателя не добавляет сборщику работы на каждую разметку. *)
		Event = RECORD meta: Meta; pos, len: INTEGER END;

		Stream = POINTER TO RECORD
			next: Stream;
			name: Name;
			ev: POINTER TO ARRAY OF Event;
			n: INTEGER;	(* событий; ev[i].meta.seq = i + 1 *)
			data: Bytes; used: INTEGER;
			snapSeq: LONGINT;
			snap: Bytes; snapLen: INTEGER
		END;

		Mem = POINTER TO RECORD (Journal)
			tab: ARRAY buckets OF Stream
		END;

	VAR
		dir-, stdDir-: Journal;	(** журнал по умолчанию; первый установленный *)

	PROCEDURE (s: Sink) Event* (IN m: Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN, NEW, ABSTRACT;
	(** Пост: событие m с телом body[0 .. len-1] принято; FALSE — читать дальше не нужно. *)

	PROCEDURE (j: Journal) Last* (IN stream: ARRAY OF CHAR): LONGINT, NEW, ABSTRACT;
	(** Пост: номер последнего события потока; 0 — событий нет. *)

	PROCEDURE (j: Journal) Append* (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
	(** Пред: stream # "" — 20; m.seq = expected + 1 — 21; 0 <= len <= LEN(body) — 22.
		Пост: res = ok — событие m записано последним в потоке;
		res = conflict — последний номер потока # expected, ничего не записано;
		res = failed — ошибка среды, ничего не записано. *)

	PROCEDURE (j: Journal) Read* (IN stream: ARRAY OF CHAR; from: LONGINT; s: Sink; OUT res: INTEGER),
		NEW, ABSTRACT;
	(** Пред: s # NIL — 20.
		Пост: s.Event получил события потока с номерами from, from + 1, … по
		порядку, пока не вернул FALSE; res = ok или failed, corrupt. *)

	PROCEDURE (j: Journal) SaveSnapshot* (IN stream: ARRAY OF CHAR; seq: LONGINT;
		IN data: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
	(** Пред: 0 < seq <= Last(stream) — 20; 0 <= len <= LEN(data) — 21.
		Пост: res = ok — снимок потока после события seq сохранён и заменил прежний. *)

	PROCEDURE (j: Journal) LoadSnapshot* (IN stream: ARRAY OF CHAR; OUT seq: LONGINT;
		OUT data: Bytes; OUT len: INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
	(** Пост: res = ok — последний снимок: после события seq, кадр data[0 .. len-1];
		res = absent — снимка нет, seq = 0, data = NIL. *)

	PROCEDURE (j: Journal) Close*, NEW, EMPTY;
	(** Пост: ресурсы среды освобождены; после Close журналом не пользуются. *)

	PROCEDURE Now* (): LONGINT;
	(** Пост: текущее время UTC числом ГГГГММДДччммсс. *)
		VAR d: Dates.Date; t: Dates.Time;
	BEGIN
		Dates.GetUTCDateTime(d, t);
		RETURN ((((LONG(d.year) * 100 + d.month) * 100 + d.day) * 100 + t.hour) * 100 + t.minute) * 100 + t.second
	END Now;

	(* --- в памяти --------------------------------------------------------- *)

	PROCEDURE Hash (IN s: ARRAY OF CHAR): INTEGER;
		VAR h, i: INTEGER;
	BEGIN
		h := 0; i := 0;
		WHILE (i < LEN(s)) & (s[i] # 0X) DO h := (h * 31 + ORD(s[i])) MOD buckets; INC(i) END;
		RETURN h
	END Hash;

	PROCEDURE (m: Mem) Find (IN stream: ARRAY OF CHAR; make: BOOLEAN): Stream, NEW;
		VAR s: Stream; h: INTEGER;
	BEGIN
		h := Hash(stream); s := m.tab[h];
		WHILE (s # NIL) & (s.name # stream) DO s := s.next END;
		IF (s = NIL) & make THEN
			NEW(s); s.name := stream$; NEW(s.ev, 16); NEW(s.data, 1024); s.next := m.tab[h]; m.tab[h] := s
		END;
		RETURN s
	END Find;

	PROCEDURE Copy (IN data: ARRAY OF BYTE; len: INTEGER): Bytes;
		VAR b: Bytes; i: INTEGER;
	BEGIN
		NEW(b, MAX(len, 1));
		FOR i := 0 TO len - 1 DO b[i] := data[i] END;
		RETURN b
	END Copy;

	PROCEDURE (m: Mem) Last (IN stream: ARRAY OF CHAR): LONGINT;
		VAR s: Stream;
	BEGIN
		s := m.Find(stream, FALSE);
		IF s = NIL THEN RETURN 0 ELSE RETURN s.n END
	END Last;

	PROCEDURE (m: Mem) Append (IN stream: ARRAY OF CHAR; expected: LONGINT; IN meta: Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR s: Stream; new: POINTER TO ARRAY OF Event; i: INTEGER; nb: Bytes;
	BEGIN
		ASSERT(stream # "", 20); ASSERT(meta.seq = expected + 1, 21); ASSERT((len >= 0) & (len <= LEN(body)), 22);
		s := m.Find(stream, TRUE);
		IF s.n # expected THEN res := conflict; RETURN END;
		IF s.n = LEN(s.ev^) THEN
			NEW(new, 2 * s.n);
			FOR i := 0 TO s.n - 1 DO new[i] := s.ev[i] END;
			s.ev := new
		END;
		IF s.used + len > LEN(s.data^) THEN
			NEW(nb, 2 * (s.used + len));
			FOR i := 0 TO s.used - 1 DO nb[i] := s.data[i] END;
			s.data := nb
		END;
		FOR i := 0 TO len - 1 DO s.data[s.used + i] := body[i] END;
		s.ev[s.n].meta := meta; s.ev[s.n].pos := s.used; s.ev[s.n].len := len; INC(s.used, len);
		INC(s.n); res := ok
	END Append;

	PROCEDURE (m: Mem) Read (IN stream: ARRAY OF CHAR; from: LONGINT; k: Sink; OUT res: INTEGER);
		VAR s: Stream; i, j: INTEGER; b: Bytes;
	BEGIN
		ASSERT(k # NIL, 20);
		res := ok; s := m.Find(stream, FALSE);
		IF s = NIL THEN RETURN END;
		IF from < 1 THEN from := 1 END;
		i := SHORT(from) - 1; NEW(b, 256);
		LOOP
			IF i >= s.n THEN EXIT END;
			IF s.ev[i].len > LEN(b^) THEN NEW(b, s.ev[i].len) END;
			FOR j := 0 TO s.ev[i].len - 1 DO b[j] := s.data[s.ev[i].pos + j] END;
			IF ~k.Event(s.ev[i].meta, b^, s.ev[i].len) THEN EXIT END;
			INC(i)
		END
	END Read;

	PROCEDURE (m: Mem) SaveSnapshot (IN stream: ARRAY OF CHAR; seq: LONGINT;
		IN data: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR s: Stream;
	BEGIN
		s := m.Find(stream, FALSE);
		ASSERT((s # NIL) & (seq > 0) & (seq <= s.n), 20); ASSERT((len >= 0) & (len <= LEN(data)), 21);
		s.snapSeq := seq; s.snap := Copy(data, len); s.snapLen := len; res := ok
	END SaveSnapshot;

	PROCEDURE (m: Mem) LoadSnapshot (IN stream: ARRAY OF CHAR; OUT seq: LONGINT;
		OUT data: Bytes; OUT len: INTEGER; OUT res: INTEGER);
		VAR s: Stream;
	BEGIN
		s := m.Find(stream, FALSE);
		IF (s = NIL) OR (s.snap = NIL) THEN seq := 0; data := NIL; len := 0; res := absent
		ELSE seq := s.snapSeq; data := s.snap; len := s.snapLen; res := ok
		END
	END LoadSnapshot;

	PROCEDURE NewMem* (): Journal;
	(** Пост: пустой журнал в памяти. *)
		VAR m: Mem;
	BEGIN
		NEW(m); RETURN m
	END NewMem;

	PROCEDURE SetDir* (j: Journal);
	(** Пред: j # NIL — 20. Пост: dir = j; первый установленный журнал остаётся в stdDir. *)
	BEGIN
		ASSERT(j # NIL, 20);
		dir := j;
		IF stdDir = NIL THEN stdDir := j END
	END SetDir;

BEGIN
	SetDir(NewMem())
END ActorJournal.
