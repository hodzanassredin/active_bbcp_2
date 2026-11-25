MODULE ObxHttpCodecTask;
(** Те же слои-кодеки, что у ObxHttpCodec, но кормит их задача.

	Слои не знают, кто их зовёт: водитель ObxHttpCodec переставляет
	Services.Action, а здесь задача спит в AsyncIO.Read и отдаёт
	прочитанное Parser.Feed. Разбор, состояние и срезы одни и те же. *)

	IMPORT Kernel, Tasks, CommStreams, CommTCP, AsyncIO, Services,
		ObxHttpCodec, Console, Strings, Dialog;

	CONST
		bufSize = 2048;
		stackSize = 64 * 1024;
		timeout = 20000;

		errNone = ObxHttpCodec.errNone;
		errUrl = ObxHttpCodec.errUrl;
		errConnect = ObxHttpCodec.errConnect;
		errRedirect = ObxHttpCodec.errRedirect;
		errTimeout = ObxHttpCodec.errTimeout;
		errClosed = ObxHttpCodec.errClosed;

	TYPE
		Job = POINTER TO RECORD (Tasks.Task)
			url: ARRAY 512 OF CHAR;
			host: ARRAY 128 OF CHAR;
			path: ARRAY 512 OF CHAR;
			port: INTEGER;
			p: ObxHttpCodec.Parser;
			status, err: INTEGER;
			done, measure: BOOLEAN
		END;

	VAR
		measureJobs: ARRAY 1024 OF Job;
		measureConnected: INTEGER;
		stackLimit: INTEGER;

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

	PROCEDURE (j: Job) Request (OUT req: ARRAY OF BYTE; OUT reqLen: INTEGER), NEW;
		VAR s: ARRAY 1024 OF CHAR; t: ARRAY 16 OF CHAR; i: INTEGER;
	BEGIN
		s := "GET " + j.path$ + " HTTP/1.1" + 0DX + 0AX + "Host: " + j.host$;
		IF j.port # 80 THEN
			Strings.IntToString(j.port, t); s := s$ + ":" + t$
		END;
		s := s$ + 0DX + 0AX + "Connection: close" + 0DX + 0AX + 0DX + 0AX;
		i := 0; reqLen := 0;
		WHILE (s[i] # 0X) & (reqLen < LEN(req)) DO
			req[reqLen] := SHORT(SHORT(ORD(s[i]))); INC(reqLen); INC(i)
		END
	END Request;

	PROCEDURE (j: Job) Run;
		VAR s: CommStreams.Stream; res, n, sent, k, redirects: INTEGER;
			adr: ARRAY 64 OF CHAR; t: ARRAY 16 OF CHAR;
			req: ARRAY 1024 OF BYTE; reqLen: INTEGER;
			x: ARRAY bufSize OF BYTE;
			loc: ARRAY 256 OF CHAR;
			host: ARRAY 128 OF CHAR; path: ARRAY 512 OF CHAR; port: INTEGER;
	BEGIN
		j.err := errNone; j.status := 0; j.done := FALSE;
		IF ~ParseUrl(j.url, j.host, j.port, j.path) THEN
			j.err := errUrl; j.done := TRUE; RETURN
		END;
		j.p.Reset;
		redirects := 0;
		LOOP
			adr := j.host$;
			IF j.port # 80 THEN
				Strings.IntToString(j.port, t); adr := adr$ + ":" + t$
			END;
			s := NIL;
			CommTCP.NewStream("", adr, s, res);
			IF (res # CommStreams.done) OR (s = NIL) THEN
				j.err := errConnect; j.done := TRUE; RETURN
			END;
			j.Request(req, reqLen);
			sent := 0;
			WHILE sent < reqLen DO
				AsyncIO.Write(s, req, sent, reqLen - sent, k);
				IF k <= 0 THEN j.err := errClosed; s.Close; j.done := TRUE; RETURN END;
				INC(sent, k)
			END;
			IF j.measure THEN INC(measureConnected) END;
			LOOP
				IF j.p.err # errNone THEN EXIT END;
				IF j.p.redirect OR j.p.finished THEN EXIT END;
				AsyncIO.Read(s, x, 0, LEN(x), n);
				IF n > 0 THEN j.p.Feed(x, n)
				ELSIF ~s.IsConnected() THEN j.p.Eof
				END
			END;
			IF j.p.err # errNone THEN
				j.err := j.p.err; s.Close; j.done := TRUE; RETURN
			END;
			IF j.p.finished & ~j.p.redirect THEN
				j.status := j.p.status; s.Close; j.done := TRUE; RETURN
			END;
			(* переадресация *)
			CopyStr(j.p.loc, loc);
			s.Close;
			IF (redirects >= 1) OR (loc = "") THEN
				j.err := errRedirect; j.done := TRUE; RETURN
			END;
			INC(redirects);
			IF MatchPrefix(loc, "http://") THEN
				IF ~ParseUrl(loc, host, port, path) THEN
					j.err := errUrl; j.done := TRUE; RETURN
				END;
				CopyStr(host, j.host); j.port := port; CopyStr(path, j.path)
			ELSIF loc[0] = "/" THEN
				CopyStr(loc, j.path)
			ELSE
				j.path := "/" + loc$
			END;
			j.p.Reset
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
		VAR url: ARRAY 512 OF CHAR; res, n, i, st: INTEGER; deadline: LONGINT;
			t: ARRAY 32 OF CHAR;
	BEGIN
		Dialog.GetPar("HTTP_URL", url, res);
		IF (res # 0) OR (url = "") THEN
			Console.WriteStr("ObxHttpCodecTask: задайте HTTP_URL"); Console.WriteLn; RETURN
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
		Console.WriteStr("ready style=codecTask n="); Strings.IntToString(n, t); Console.WriteStr(t);
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
		VAR j: Job; i: INTEGER; deadline: LONGINT;
	BEGIN
		NEW(j); j.url := url$; j.done := FALSE;
		j.status := 0; j.err := errNone;
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
			bodyLen := j.p.bodyLen;
			IF bodyLen > LEN(body) THEN bodyLen := LEN(body) END;
			i := 0;
			WHILE i < bodyLen DO body[i] := j.p.body[i]; INC(i) END
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
		VAR url: ARRAY 512 OF CHAR; body: ARRAY ObxHttpCodec.bodySize OF BYTE;
			status, bodyLen, err, res, i: INTEGER; t: ARRAY 16 OF CHAR;
	BEGIN
		Dialog.GetPar("HTTP_URL", url, res);
		IF (res # 0) OR (url = "") THEN
			Console.WriteStr("ObxHttpCodecTask: задайте HTTP_URL"); Console.WriteLn; RETURN
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
END ObxHttpCodecTask.
