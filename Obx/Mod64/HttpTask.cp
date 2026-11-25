MODULE ObxHttpTask;
(** HTTP-клиент на задачах: обычный последовательный код.

	Задача открывает поток CommStreams, пишет запрос и читает ответ.
	ReadLine, ReadExact и ReadChunk зовут друг друга; ожидание — только
	внизу, в ReadByte → Fill → AsyncIO.Read. Пока данных нет, задача
	спит, а главный цикл опрашивает дескриптор; процессор не занят.
	Состояние — локальные переменные задачи и поле done; полей «номер
	шага» нет.

	Запись соединения одна на задачу, буфер чтения — на стеке задачи.
	Тело пишется сразу в буфер вызывающего. *)

	IMPORT Kernel, Tasks, CommStreams, CommTCP, AsyncIO, Services,
		Console, Strings, Dialog;

	CONST
		bufSize = 4096;
		bodySize* = 8192;
		stackSize = 64 * 1024;
		timeout = 20000;

		(* те же числа, что у ObxHttpCodec *)
		errNone* = 0;
		errUrl* = 1;
		errConnect* = 2;
		errStatus* = 3;
		errHeader* = 4;
		errLength* = 5;
		errChunk* = 6;
		errClosed* = 7;
		errRedirect* = 8;
		errTimeout* = 9;
		errContract* = 10;
		errBuffer* = 11;

		kindNone = 0; kindLength = 1; kindChunked = 2;

	TYPE
		Reader = RECORD
			s: CommStreams.Stream;
			buf: ARRAY bufSize OF BYTE;
			beg, end: INTEGER
		END;

		Job = POINTER TO RECORD (Tasks.Task)
			url: ARRAY 512 OF CHAR;
			body: ARRAY bodySize OF BYTE;
			bodyLen, status, err: INTEGER;
			done, measure: BOOLEAN
		END;

	VAR
		junk: ARRAY 8 OF BYTE;
		measureJobs: ARRAY 1024 OF Job;
		measureConnected: INTEGER;
		stackLimit: INTEGER;

	(* --- чтение ----------------------------------------------------------- *)

	PROCEDURE (VAR r: Reader) Fill (): BOOLEAN, NEW;
	BEGIN
		AsyncIO.Read(r.s, r.buf, 0, LEN(r.buf), r.end);
		IF r.end <= 0 THEN r.beg := 0; r.end := 0; RETURN FALSE END;
		r.beg := 0;
		RETURN TRUE
	END Fill;

	PROCEDURE (VAR r: Reader) ReadByte (OUT b: BYTE): BOOLEAN, NEW;
	BEGIN
		IF r.beg >= r.end THEN
			IF ~r.Fill() THEN RETURN FALSE END
		END;
		b := r.buf[r.beg]; INC(r.beg);
		RETURN TRUE
	END ReadByte;

	PROCEDURE (VAR r: Reader) ReadLine (OUT line: ARRAY OF CHAR): BOOLEAN, NEW;
		VAR n: INTEGER; b: BYTE;
	BEGIN
		n := 0;
		LOOP
			IF ~r.ReadByte(b) THEN RETURN FALSE END;
			IF b = 13 THEN
				IF ~r.ReadByte(b) THEN RETURN FALSE END;
				IF b # 10 THEN RETURN FALSE END;
				line[n] := 0X; RETURN TRUE
			END;
			IF n >= LEN(line) - 1 THEN RETURN FALSE END;
			line[n] := CHR(b); INC(n)
		END
	END ReadLine;

	PROCEDURE (VAR r: Reader) ReadExact (VAR x: ARRAY OF BYTE; at, n: INTEGER): BOOLEAN, NEW;
		VAR i, k, m: INTEGER;
	BEGIN
		i := 0;
		WHILE i < n DO
			IF r.beg >= r.end THEN
				IF ~r.Fill() THEN RETURN FALSE END
			END;
			k := r.end - r.beg;
			IF k > n - i THEN k := n - i END;
			IF at + i + k > LEN(x) THEN RETURN FALSE END;
			m := 0;
			WHILE m < k DO x[at + i + m] := r.buf[r.beg + m]; INC(m) END;
			INC(r.beg, k); INC(i, k)
		END;
		RETURN TRUE
	END ReadExact;

	(* --- разбор строк ----------------------------------------------------- *)

	PROCEDURE Upper (c: CHAR): CHAR;
	BEGIN
		IF (c >= "a") & (c <= "z") THEN RETURN CHR(ORD(c) - 32) END;
		RETURN c
	END Upper;

	PROCEDURE StrLen (IN s: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE s[i] # 0X DO INC(i) END;
		RETURN i
	END StrLen;

	PROCEDURE NameIs (IN line: ARRAY OF CHAR; IN name: ARRAY OF CHAR): BOOLEAN;
		VAR i, n: INTEGER;
	BEGIN
		n := StrLen(name);
		i := 0;
		WHILE i < n DO
			IF Upper(line[i]) # Upper(name[i]) THEN RETURN FALSE END;
			INC(i)
		END;
		RETURN line[n] = ":"
	END NameIs;

	PROCEDURE ParseStatus (IN line: ARRAY OF CHAR; OUT code: INTEGER): BOOLEAN;
		VAR i, k: INTEGER; c: CHAR;
	BEGIN
		code := 0;
		IF StrLen(line) < 12 THEN RETURN FALSE END;
		IF (line[0] # "H") OR (line[1] # "T") OR (line[2] # "T") OR
			(line[3] # "P") OR (line[4] # "/") OR (line[5] # "1") OR
			(line[6] # ".") THEN
			RETURN FALSE
		END;
		i := 7;
		WHILE (line[i] # 0X) & (line[i] # " ") DO INC(i) END;
		IF line[i] = 0X THEN RETURN FALSE END;
		WHILE line[i] = " " DO INC(i) END;
		k := 0;
		WHILE k < 3 DO
			c := line[i + k];
			IF (c < "0") OR (c > "9") THEN RETURN FALSE END;
			code := code * 10 + (ORD(c) - ORD("0")); INC(k)
		END;
		RETURN TRUE
	END ParseStatus;

	PROCEDURE ParseDecimal (IN line: ARRAY OF CHAR; from: INTEGER; OUT n: INTEGER): BOOLEAN;
		VAR i: INTEGER; ok: BOOLEAN;
	BEGIN
		i := from; n := 0; ok := FALSE;
		WHILE (line[i] >= "0") & (line[i] <= "9") DO
			IF n > 100000000 THEN RETURN FALSE END;
			n := n * 10 + (ORD(line[i]) - ORD("0")); INC(i); ok := TRUE
		END;
		RETURN ok
	END ParseDecimal;

	PROCEDURE ParseHex (IN line: ARRAY OF CHAR; OUT n: INTEGER): BOOLEAN;
		VAR i, v: INTEGER; c: CHAR; ok: BOOLEAN;
	BEGIN
		i := 0; n := 0; ok := FALSE;
		LOOP
			c := line[i];
			IF c = ";" THEN EXIT END;
			IF c = 0X THEN EXIT END;
			IF (c >= "0") & (c <= "9") THEN v := ORD(c) - ORD("0")
			ELSIF (c >= "a") & (c <= "f") THEN v := ORD(c) - ORD("a") + 10
			ELSIF (c >= "A") & (c <= "F") THEN v := ORD(c) - ORD("A") + 10
			ELSE RETURN FALSE
			END;
			IF n > 100000000 THEN RETURN FALSE END;
			n := n * 16 + v; ok := TRUE; INC(i)
		END;
		RETURN ok
	END ParseHex;

	PROCEDURE IsChunkedChar (c: CHAR; i: INTEGER): BOOLEAN;
		VAR d: CHAR;
	BEGIN
		IF (c >= "A") & (c <= "Z") THEN d := CHR(ORD(c) + 32) ELSE d := c END;
		CASE i OF
			0: RETURN d = "c"
		| 1: RETURN d = "h"
		| 2: RETURN d = "u"
		| 3: RETURN d = "n"
		| 4: RETURN d = "k"
		| 5: RETURN d = "e"
		| 6: RETURN d = "d"
		END;
		RETURN FALSE
	END IsChunkedChar;

	PROCEDURE ContainsChunked (IN line: ARRAY OF CHAR; from: INTEGER): BOOLEAN;
		VAR i, j: INTEGER; ok: BOOLEAN;
	BEGIN
		i := from;
		WHILE i + 7 <= StrLen(line) DO
			j := 0; ok := TRUE;
			WHILE ok & (j < 7) DO ok := IsChunkedChar(line[i + j], j); INC(j) END;
			IF ok THEN RETURN TRUE END;
			INC(i)
		END;
		RETURN FALSE
	END ContainsChunked;

	PROCEDURE HeaderLine (IN line: ARRAY OF CHAR; VAR kind, length: INTEGER;
			VAR loc: ARRAY OF CHAR);
		VAR i, j, n: INTEGER;
	BEGIN
		IF NameIs(line, "content-length") THEN
			i := 15;
			WHILE (line[i] = " ") OR (line[i] = 09X) DO INC(i) END;
			IF ParseDecimal(line, i, n) & (kind = kindNone) THEN
				kind := kindLength; length := n
			END
		ELSIF NameIs(line, "transfer-encoding") THEN
			IF ContainsChunked(line, 18) THEN kind := kindChunked END
		ELSIF NameIs(line, "location") THEN
			i := 9;
			WHILE (line[i] = " ") OR (line[i] = 09X) DO INC(i) END;
			j := 0;
			WHILE (line[i] # 0X) & (j < LEN(loc) - 1) DO loc[j] := line[i]; INC(i); INC(j) END;
			WHILE (j > 0) & ((loc[j - 1] = " ") OR (loc[j - 1] = 09X)) DO DEC(j) END;
			loc[j] := 0X
		END
	END HeaderLine;

	PROCEDURE ReadHead (VAR r: Reader; OUT status, kind, length: INTEGER;
			OUT loc: ARRAY OF CHAR; OUT err: INTEGER): BOOLEAN;
		VAR line: ARRAY 256 OF CHAR;
	BEGIN
		kind := kindNone; length := 0; loc := "";
		IF ~r.ReadLine(line) THEN err := errClosed; RETURN FALSE END;
		IF ~ParseStatus(line, status) THEN err := errStatus; RETURN FALSE END;
		LOOP
			IF ~r.ReadLine(line) THEN err := errClosed; RETURN FALSE END;
			IF line = "" THEN EXIT END;
			HeaderLine(line, kind, length, loc)
		END;
		RETURN TRUE
	END ReadHead;

	PROCEDURE ReadChunk (VAR r: Reader; VAR body: ARRAY OF BYTE; at: INTEGER;
			OUT n: INTEGER; OUT last: BOOLEAN; OUT err: INTEGER): BOOLEAN;
		VAR line: ARRAY 64 OF CHAR;
	BEGIN
		n := 0; last := FALSE;
		IF ~r.ReadLine(line) THEN err := errClosed; RETURN FALSE END;
		IF ~ParseHex(line, n) THEN err := errChunk; RETURN FALSE END;
		IF n = 0 THEN
			IF ~r.ReadExact(junk, 0, 2) THEN err := errClosed; RETURN FALSE END;
			last := TRUE; RETURN TRUE
		END;
		IF at + n > LEN(body) THEN err := errBuffer; RETURN FALSE END;
		IF ~r.ReadExact(body, at, n) THEN err := errClosed; RETURN FALSE END;
		IF ~r.ReadExact(junk, 0, 2) THEN err := errClosed; RETURN FALSE END;
		RETURN TRUE
	END ReadChunk;

	PROCEDURE ReadBody (VAR r: Reader; VAR body: ARRAY OF BYTE;
			kind, length: INTEGER; OUT bodyLen, err: INTEGER): BOOLEAN;
		VAR n: INTEGER; last: BOOLEAN;
	BEGIN
		bodyLen := 0;
		IF kind = kindLength THEN
			IF length > LEN(body) THEN err := errBuffer; RETURN FALSE END;
			IF ~r.ReadExact(body, 0, length) THEN err := errClosed; RETURN FALSE END;
			bodyLen := length
		ELSIF kind = kindChunked THEN
			LOOP
				IF ~ReadChunk(r, body, bodyLen, n, last, err) THEN RETURN FALSE END;
				INC(bodyLen, n);
				IF last THEN RETURN TRUE END
			END
		END;
		RETURN TRUE
	END ReadBody;

	(* --- задача ----------------------------------------------------------- *)

	PROCEDURE MatchPrefix (IN url: ARRAY OF CHAR; IN p: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (p[i] # 0X) & (url[i] = p[i]) DO INC(i) END;
		RETURN p[i] = 0X
	END MatchPrefix;

	PROCEDURE ParseUrl (IN url: ARRAY OF CHAR;
			OUT host: ARRAY OF CHAR; OUT port: INTEGER; OUT path: ARRAY OF CHAR): BOOLEAN;
		VAR i, j, v: INTEGER; ok: BOOLEAN;
	BEGIN
		host := ""; path := "/"; port := 80;
		IF ~MatchPrefix(url, "http://") THEN RETURN FALSE END;
		i := 7; j := 0;
		WHILE (url[i] # 0X) & (url[i] # ":") & (url[i] # "/") DO
			IF j >= LEN(host) - 1 THEN RETURN FALSE END;
			host[j] := url[i]; INC(i); INC(j)
		END;
		host[j] := 0X;
		IF j = 0 THEN RETURN FALSE END;
		IF url[i] = ":" THEN
			INC(i); v := 0; ok := FALSE;
			WHILE (url[i] >= "0") & (url[i] <= "9") DO
				v := v * 10 + (ORD(url[i]) - ORD("0")); INC(i); ok := TRUE
			END;
			IF ~ok OR (v < 1) OR (v > 65535) THEN RETURN FALSE END;
			port := v
		END;
		IF url[i] = "/" THEN
			j := 0;
			WHILE url[i] # 0X DO
				IF j >= LEN(path) - 1 THEN RETURN FALSE END;
				path[j] := url[i]; INC(i); INC(j)
			END;
			path[j] := 0X
		ELSIF url[i] # 0X THEN
			RETURN FALSE
		END;
		RETURN TRUE
	END ParseUrl;

	PROCEDURE CopyStr (IN src: ARRAY OF CHAR; OUT dst: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(dst) - 1) & (src[i] # 0X) DO dst[i] := src[i]; INC(i) END;
		dst[i] := 0X
	END CopyStr;

	PROCEDURE (j: Job) Run;
		VAR r: Reader; s: CommStreams.Stream; res, n, i, k: INTEGER;
			host: ARRAY 128 OF CHAR; path: ARRAY 512 OF CHAR;
			loc: ARRAY 256 OF CHAR; adr: ARRAY 64 OF CHAR; t: ARRAY 16 OF CHAR;
			req: ARRAY 1024 OF BYTE; reqLen, sent: INTEGER;
			status, kind, length, bodyLen, err, redirects: INTEGER;
			code: ARRAY 1024 OF CHAR;
	BEGIN
		j.err := errNone; j.status := 0; j.bodyLen := 0;
		IF ~ParseUrl(j.url, host, n, path) THEN j.err := errUrl; j.done := TRUE; RETURN END;
		redirects := 0;
		LOOP
			adr := host$;
			IF n # 80 THEN
				Strings.IntToString(n, t); adr := adr$ + ":" + t$
			END;
			s := NIL;
			CommTCP.NewStream("", adr, s, res);
			IF (res # CommStreams.done) OR (s = NIL) THEN
				j.err := errConnect; j.done := TRUE; RETURN
			END;
			(* запрос *)
			reqLen := 0;
			code := "GET " + path$ + " HTTP/1.1" + 0DX + 0AX;
			i := 0;
			WHILE (code[i] # 0X) & (reqLen < LEN(req)) DO
				req[reqLen] := SHORT(SHORT(ORD(code[i]))); INC(reqLen); INC(i)
			END;
			code := "Host: " + host$;
			IF n # 80 THEN Strings.IntToString(n, t); code := code$ + ":" + t$ END;
			code := code$ + 0DX + 0AX + "Connection: close" + 0DX + 0AX + 0DX + 0AX;
			i := 0;
			WHILE (code[i] # 0X) & (reqLen < LEN(req)) DO
				req[reqLen] := SHORT(SHORT(ORD(code[i]))); INC(reqLen); INC(i)
			END;
			sent := 0;
			WHILE sent < reqLen DO
				AsyncIO.Write(s, req, sent, reqLen - sent, k);
				IF k <= 0 THEN j.err := errClosed; s.Close; j.done := TRUE; RETURN END;
				INC(sent, k)
			END;
			IF j.measure THEN INC(measureConnected) END;
			r.s := s; r.beg := 0; r.end := 0;
			IF ~ReadHead(r, status, kind, length, loc, err) THEN
				j.err := err; s.Close; j.done := TRUE; RETURN
			END;
			IF (status = 301) OR (status = 302) THEN
				IF (redirects >= 1) OR (loc = "") THEN
					j.err := errRedirect; s.Close; j.done := TRUE; RETURN
				END;
				INC(redirects);
				s.Close;
				IF MatchPrefix(loc, "http://") THEN
					IF ~ParseUrl(loc, host, n, path) THEN
						j.err := errUrl; j.done := TRUE; RETURN
					END
				ELSIF loc[0] = "/" THEN
					CopyStr(loc, path)
				ELSE
					path := "/" + loc$
				END
			ELSE
				j.status := status;
				IF ~ReadBody(r, j.body, kind, length, bodyLen, err) THEN
					j.err := err; s.Close; j.bodyLen := 0; j.status := 0;
					j.done := TRUE; RETURN
				END;
				j.bodyLen := bodyLen;
				s.Close; j.done := TRUE; RETURN
			END
		END
	END Run;

	PROCEDURE IntPar (IN name: ARRAY OF CHAR; def: INTEGER): INTEGER;
		VAR s: ARRAY 32 OF CHAR; res, v: INTEGER;
	BEGIN
		Dialog.GetPar(name, s, res);
		IF (res = 0) & (s # "") THEN
			Strings.StringToInt(s, v, res);
			IF res = 0 THEN RETURN v END
		END;
		RETURN def
	END IntPar;

	PROCEDURE Measure*;
	(** Открыть HTTP_N простаивающих соединений к HTTP_URL и подержать их,
		печатая ready. Служит замеру памяти; сервер по этому адресу не
		отвечает. *)
		VAR url: ARRAY 512 OF CHAR; res, n, i, st: INTEGER; deadline: LONGINT;
			t: ARRAY 32 OF CHAR;
	BEGIN
		Dialog.GetPar("HTTP_URL", url, res);
		IF (res # 0) OR (url = "") THEN
			Console.WriteStr("ObxHttpTask: задайте HTTP_URL"); Console.WriteLn; RETURN
		END;
		n := IntPar("HTTP_N", 1000);
		st := IntPar("HTTP_STACK", stackSize);
		stackLimit := st;
		IF n > LEN(measureJobs) THEN n := LEN(measureJobs) END;
		measureConnected := 0;
		FOR i := 0 TO n - 1 DO
			NEW(measureJobs[i]);
			measureJobs[i].url := url$; measureJobs[i].done := FALSE;
			measureJobs[i].measure := TRUE; measureJobs[i].err := errNone;
			IF st > 0 THEN Tasks.SetStackSize(measureJobs[i], st) END;
			Tasks.Start(measureJobs[i])
		END;
		deadline := Kernel.Time() + 20000;
		WHILE (measureConnected < n) & (Kernel.Time() <= deadline) DO
			Services.actionHook.Step; AsyncIO.Poll(2)
		END;
		Console.WriteStr("ready style=task n="); Strings.IntToString(n, t); Console.WriteStr(t);
		Console.WriteStr(" connected="); Strings.IntToString(measureConnected, t); Console.WriteStr(t);
		Console.WriteStr(" tasks="); Strings.IntToString(Tasks.Count(), t); Console.WriteStr(t);
		Console.WriteStr(" actions="); Strings.IntToString(Services.actionsInTheQueue, t); Console.WriteStr(t);
		Console.WriteLn;
		deadline := Kernel.Time() + 5000;
		WHILE Kernel.Time() <= deadline DO Services.actionHook.Step; AsyncIO.Poll(5) END;
		FOR i := 0 TO n - 1 DO Tasks.Cancel(measureJobs[i]) END;
		deadline := Kernel.Time() + 1000;
		WHILE (Tasks.Count() > 0) & (Kernel.Time() <= deadline) DO
			Services.actionHook.Step; AsyncIO.Poll(5)
		END
	END Measure;

	PROCEDURE Get* (IN url: ARRAY OF CHAR;
			OUT status: INTEGER; OUT body: ARRAY OF BYTE; OUT bodyLen: INTEGER; OUT err: INTEGER);
	(** Пред: вызов с главного стека. Задача делает запрос, главный стек
		крутит очередь действий и опрос дескрипторов. *)
		VAR j: Job; i: INTEGER; deadline: LONGINT;
	BEGIN
		NEW(j); j.url := url$; j.done := FALSE;
		j.status := 0; j.err := errNone; j.bodyLen := 0;
		Tasks.SetStackSize(j, stackLimit);
		Tasks.Start(j);
		deadline := Kernel.Time() + timeout;
		WHILE ~j.done & (Kernel.Time() <= deadline) DO
			Services.actionHook.Step;
			AsyncIO.Poll(1)
		END;
		IF ~j.done THEN
			Tasks.Cancel(j); err := errTimeout; status := 0; bodyLen := 0; RETURN
		END;
		status := j.status; err := j.err;
		IF err = errNone THEN
			bodyLen := j.bodyLen;
			IF bodyLen > LEN(body) THEN bodyLen := LEN(body) END;
			i := 0;
			WHILE i < bodyLen DO body[i] := j.body[i]; INC(i) END
		ELSE
			bodyLen := 0
		END
	END Get;

	PROCEDURE HexChar (v: INTEGER): CHAR;
	BEGIN
		IF v < 10 THEN RETURN CHR(ORD("0") + v) ELSE RETURN CHR(ORD("a") + v - 10) END
	END HexChar;

	PROCEDURE Hex2 (b: BYTE);
	BEGIN
		Console.WriteChar(HexChar(b DIV 16));
		Console.WriteChar(HexChar(b MOD 16))
	END Hex2;

	PROCEDURE Do*;
		VAR url: ARRAY 512 OF CHAR; body: ARRAY bodySize OF BYTE;
			status, bodyLen, err, res, i: INTEGER; t: ARRAY 16 OF CHAR;
	BEGIN
		Dialog.GetPar("HTTP_URL", url, res);
		IF (res # 0) OR (url = "") THEN
			Console.WriteStr("ObxHttpTask: задайте HTTP_URL"); Console.WriteLn; RETURN
		END;
		Get(url$, status, body, bodyLen, err);
		Console.WriteStr("status="); Strings.IntToString(status, t); Console.WriteStr(t);
		Console.WriteStr(" err="); Strings.IntToString(err, t); Console.WriteStr(t);
		Console.WriteStr(" len="); Strings.IntToString(bodyLen, t); Console.WriteStr(t);
		Console.WriteStr(" body=");
		i := 0;
		WHILE i < bodyLen DO Hex2(body[i]); INC(i) END;
		Console.WriteLn
	END Do;

BEGIN
	stackLimit := stackSize
END ObxHttpTask.
