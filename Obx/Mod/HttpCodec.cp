MODULE ObxHttpCodec;
(** HTTP-клиент без задач и без стека: разбор сделан слоями-кодеками,
	водитель — Services.Action.

	Слои одной формы (ObxCodec.Decoder): заголовок ответа до пустой
	строки, тело по Content-Length, кусок Transfer-Encoding: chunked.
	Водитель носит буфер, поля декодеров и небольшое поле порядка слоёв
	(заголовок → тело); между вызовами Do больше ничего не живёт.
	Пока данных нет, действие переставляется в очередь главного цикла и
	не занимает задачу: стека на соединение нет, только запись.

	Буфер один на соединение. Тело по длине и кусок chunked считаются
	целой единицей: пока unit не собран, слой ничего не съедает. Поэтому
	буфер должен вмещать заголовок и всё тело сразу; большое тело этот
	разбор не отдаёт потоком. *)

	IMPORT ObxCodec, CommStreams, CommTCP, Services, Dialog, Console,
		Strings, Kernel;

	CONST
		bufSize* = 8192;
		bodySize* = 8192;

		(* коды отказа; те же числа у ObxHttpTask и ObxHttpCodecTask *)
		errNone* = 0;
		errUrl* = 1;		(* URL не разобран *)
		errConnect* = 2;	(* соединение не открылось *)
		errStatus* = 3;		(* мусор вместо строки состояния *)
		errHeader* = 4;		(* заголовок испорчен *)
		errLength* = 5;		(* Content-Length не число *)
		errChunk* = 6;		(* длина куска не шестнадцатеричная *)
		errClosed* = 7;		(* обрыв соединения посреди единицы *)
		errRedirect* = 8;	(* вторая переадресация или нет Location *)
		errTimeout* = 9;
		errContract* = 10;	(* слой нарушил договор: more с used # 0 *)
		errBuffer* = 11;	(* единица не помещается в буфер *)

		(* результат слоя *)
		kindNone = 0; kindLength = 1; kindChunked = 2;

		(* порядок слоёв у водителя *)
		phaseHeader = 0; phaseLength = 1; phaseChunk = 2; phaseDone = 3;

		timeout = 20000;

	TYPE
		HeaderDecoder* = RECORD (ObxCodec.Decoder)
			scan*: INTEGER;		(* докуда уже искали CRLFCRLF; от beg *)
			code*: INTEGER;
			kind*: INTEGER;
			length*: INTEGER;
			locBeg*, locLen*: INTEGER;	(* Location; от beg, -1 — нет *)
			unitBeg*, unitLen*: INTEGER;
			badStatus: BOOLEAN
		END;

		BodyDecoder* = RECORD (ObxCodec.Decoder)
			remaining*: INTEGER;
			unitBeg*, unitLen*: INTEGER
		END;

		ChunkDecoder* = RECORD (ObxCodec.Decoder)
			state*: INTEGER;	(* 0 — ищем строку длины, 1 — ждём данные *)
			scan*: INTEGER;		(* докуда искали CR в строке длины; от beg *)
			size*: INTEGER;
			lineEnd*: INTEGER;	(* конец строки длины; от beg *)
			unitBeg*, unitLen*: INTEGER;
			last*: BOOLEAN		(* нулевой кусок *)
		END;

		Parser* = RECORD
			buf*: ARRAY bufSize OF BYTE;
			beg*, end*: INTEGER;	(* живые байты [beg, end) *)
			phase*: INTEGER;
			hd*: HeaderDecoder;
			bd*: BodyDecoder;
			cd*: ChunkDecoder;
			body*: ARRAY bodySize OF BYTE;
			bodyLen*: INTEGER;
			status*: INTEGER;
			err*: INTEGER;
			redirect*: BOOLEAN;
			loc*: ARRAY 256 OF CHAR;
			finished*: BOOLEAN
		END;

		Client = POINTER TO RECORD (Services.Action)
			s: CommStreams.Stream;
			p: Parser;
			host: ARRAY 128 OF CHAR;
			port: INTEGER;
			path: ARRAY 512 OF CHAR;
			req: ARRAY 1024 OF BYTE;
			reqLen, sent: INTEGER;
			status: INTEGER;
			err*: INTEGER;
			followed: BOOLEAN;
			finished*: BOOLEAN;
			deadline: LONGINT
		END;

	VAR
		io: ARRAY 1024 OF BYTE;	(* окно чтения; в записи соединения не нужно *)
		measureClients: ARRAY 1024 OF Client;

	(* --- мелкие разборы --------------------------------------------------- *)

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

	PROCEDURE FindEnd (IN buf: ARRAY OF BYTE; beg, from, len: INTEGER): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := from;
		WHILE i + 3 < len DO
			IF (buf[beg + i] = 13) & (buf[beg + i + 1] = 10) &
				(buf[beg + i + 2] = 13) & (buf[beg + i + 3] = 10) THEN
				RETURN i
			END;
			INC(i)
		END;
		RETURN - 1
	END FindEnd;

	PROCEDURE FindCr (IN buf: ARRAY OF BYTE; beg, from, limit: INTEGER): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := from;
		WHILE i < limit DO
			IF buf[beg + i] = 13 THEN RETURN i END;
			INC(i)
		END;
		RETURN - 1
	END FindCr;

	PROCEDURE ParseStatus (IN buf: ARRAY OF BYTE; beg, s, e: INTEGER; OUT code: INTEGER): BOOLEAN;
		VAR i, k: INTEGER; c: BYTE;
	BEGIN
		code := 0;
		IF e - s < 12 THEN RETURN FALSE END;
		IF (buf[beg + s] # 72) OR (buf[beg + s + 1] # 84) OR
			(buf[beg + s + 2] # 84) OR (buf[beg + s + 3] # 80) OR
			(buf[beg + s + 4] # 47) OR (buf[beg + s + 5] # 49) OR
			(buf[beg + s + 6] # 46) THEN
			RETURN FALSE
		END;
		i := s + 7;
		WHILE (i < e) & (buf[beg + i] # 32) DO INC(i) END;
		IF i >= e THEN RETURN FALSE END;
		WHILE (i < e) & (buf[beg + i] = 32) DO INC(i) END;
		IF i + 3 > e THEN RETURN FALSE END;
		k := 0;
		WHILE k < 3 DO
			c := buf[beg + i + k];
			IF (c < 48) OR (c > 57) THEN RETURN FALSE END;
			code := code * 10 + (c - 48); INC(k)
		END;
		RETURN TRUE
	END ParseStatus;

	PROCEDURE EqualName (IN buf: ARRAY OF BYTE; beg, s, e: INTEGER; IN name: ARRAY OF CHAR): BOOLEAN;
		VAR i, n: INTEGER; c: BYTE;
	BEGIN
		n := 0;
		WHILE (n < LEN(name)) & (name[n] # 0X) DO INC(n) END;
		IF e - s # n THEN RETURN FALSE END;
		i := 0;
		WHILE i < n DO
			c := buf[beg + s + i];
			IF (c >= 65) & (c <= 90) THEN c := SHORT(SHORT(c + 32)) END;
			IF c # ORD(name[i]) THEN RETURN FALSE END;
			INC(i)
		END;
		RETURN TRUE
	END EqualName;

	PROCEDURE IsChunked (c: BYTE; i: INTEGER): BOOLEAN;
		VAR d: BYTE;
	BEGIN
		d := c;
		IF (d >= 65) & (d <= 90) THEN d := SHORT(SHORT(d + 32)) END;
		CASE i OF
			0: RETURN d = 99	(* c *)
		| 1: RETURN d = 104	(* h *)
		| 2: RETURN d = 117	(* u *)
		| 3: RETURN d = 110	(* n *)
		| 4: RETURN d = 107	(* k *)
		| 5: RETURN d = 101	(* e *)
		| 6: RETURN d = 100	(* d *)
		END;
		RETURN FALSE
	END IsChunked;

	PROCEDURE ContainsChunked (IN buf: ARRAY OF BYTE; beg, from, to: INTEGER): BOOLEAN;
		VAR i, j: INTEGER; ok: BOOLEAN;
	BEGIN
		i := from;
		WHILE i + 7 <= to DO
			j := 0; ok := TRUE;
			WHILE ok & (j < 7) DO ok := IsChunked(buf[beg + i + j], j); INC(j) END;
			IF ok THEN RETURN TRUE END;
			INC(i)
		END;
		RETURN FALSE
	END ContainsChunked;

	PROCEDURE HexVal (c: BYTE; OUT v: INTEGER): BOOLEAN;
	BEGIN
		IF (c >= 48) & (c <= 57) THEN v := c - 48; RETURN TRUE
		ELSIF (c >= 65) & (c <= 70) THEN v := c - 55; RETURN TRUE
		ELSIF (c >= 97) & (c <= 102) THEN v := c - 87; RETURN TRUE
		ELSE RETURN FALSE
		END
	END HexVal;

	PROCEDURE HeaderLine (IN buf: ARRAY OF BYTE; beg, s, e: INTEGER; VAR d: HeaderDecoder);
		VAR i, p, v, n, digits: INTEGER; ok: BOOLEAN;
	BEGIN
		i := s;
		WHILE (i < e) & (buf[beg + i] # 58) DO INC(i) END;
		IF i = e THEN RETURN END;
		IF EqualName(buf, beg, s, i, "content-length") THEN
			p := i + 1;
			WHILE (p < e) & ((buf[beg + p] = 32) OR (buf[beg + p] = 9)) DO INC(p) END;
			n := 0; ok := FALSE;
			WHILE (p < e) & (buf[beg + p] >= 48) & (buf[beg + p] <= 57) DO
				IF n > 100000000 THEN ok := FALSE; p := e
				ELSE n := n * 10 + (buf[beg + p] - 48); ok := TRUE; INC(p)
				END
			END;
			IF ok & (d.kind = kindNone) THEN d.kind := kindLength; d.length := n END
		ELSIF EqualName(buf, beg, s, i, "transfer-encoding") THEN
			IF ContainsChunked(buf, beg, i + 1, e) THEN d.kind := kindChunked END
		ELSIF EqualName(buf, beg, s, i, "location") THEN
			p := i + 1;
			WHILE (p < e) & ((buf[beg + p] = 32) OR (buf[beg + p] = 9)) DO INC(p) END;
			v := e;
			WHILE (v > p) & ((buf[beg + v - 1] = 32) OR (buf[beg + v - 1] = 9)) DO DEC(v) END;
			IF d.locBeg < 0 THEN d.locBeg := p; d.locLen := v - p END
		END
	END HeaderLine;

	(* --- слои ------------------------------------------------------------- *)

	PROCEDURE (VAR d: HeaderDecoder) Reset*, NEW;
	BEGIN
		d.scan := 0; d.code := 0; d.kind := kindNone; d.length := 0;
		d.locBeg := - 1; d.locLen := 0; d.unitBeg := 0; d.unitLen := 0;
		d.badStatus := FALSE
	END Reset;

	PROCEDURE (VAR d: HeaderDecoder) Decode* (IN buf: ARRAY OF BYTE;
			beg, len: INTEGER; OUT used, res: INTEGER);
		VAR sep, p, e: INTEGER; ok: BOOLEAN;
	BEGIN
		used := 0; res := ObxCodec.more;
		sep := FindEnd(buf, beg, d.scan, len);
		IF sep < 0 THEN
			IF len > 3 THEN d.scan := len - 3 ELSE d.scan := 0 END;
			RETURN
		END;
		e := FindCr(buf, beg, 0, sep + 2);
		IF e < 0 THEN res := ObxCodec.error; RETURN END;
		ok := ParseStatus(buf, beg, 0, e, d.code);
		IF ~ok THEN d.badStatus := TRUE; res := ObxCodec.error; RETURN END;
		d.kind := kindNone; d.length := 0; d.locBeg := - 1; d.locLen := 0;
		p := e + 2;
		WHILE p < sep DO
			e := FindCr(buf, beg, p, sep + 2);
			IF e < 0 THEN res := ObxCodec.error; RETURN END;
			HeaderLine(buf, beg, p, e, d);
			p := e + 2
		END;
		IF p # sep + 2 THEN res := ObxCodec.error; RETURN END;
		d.unitBeg := 0; d.unitLen := sep + 4;
		used := d.unitLen; res := ObxCodec.done
	END Decode;

	PROCEDURE (VAR d: BodyDecoder) Reset* (n: INTEGER), NEW;
	BEGIN
		d.remaining := n; d.unitBeg := 0; d.unitLen := 0
	END Reset;

	PROCEDURE (VAR d: BodyDecoder) Decode* (IN buf: ARRAY OF BYTE;
			beg, len: INTEGER; OUT used, res: INTEGER);
	BEGIN
		used := 0;
		IF len >= d.remaining THEN
			used := d.remaining; d.unitBeg := 0; d.unitLen := d.remaining;
			res := ObxCodec.done
		ELSE
			res := ObxCodec.more
		END
	END Decode;

	PROCEDURE (VAR d: ChunkDecoder) Reset*, NEW;
	BEGIN
		d.state := 0; d.scan := 0; d.size := 0; d.lineEnd := 0;
		d.unitBeg := 0; d.unitLen := 0; d.last := FALSE
	END Reset;

	PROCEDURE (VAR d: ChunkDecoder) Decode* (IN buf: ARRAY OF BYTE;
			beg, len: INTEGER; OUT used, res: INTEGER);
		VAR i, p, v, n, digits: INTEGER; ok: BOOLEAN;
	BEGIN
		used := 0; res := ObxCodec.more;
		IF d.state = 0 THEN
			i := d.scan; ok := FALSE;
			WHILE (i + 1 < len) & ~ok DO
				ok := (buf[beg + i] = 13) & (buf[beg + i + 1] = 10);
				IF ~ok THEN INC(i) END
			END;
			IF ~ok THEN
				IF len > 1 THEN d.scan := len - 1 ELSE d.scan := 0 END;
				RETURN
			END;
			p := 0; n := 0; ok := TRUE; digits := 0;
			WHILE ok & (p < i) DO
				IF buf[beg + p] = 59 THEN	(* ; — расширение, конец размера *)
					p := i
				ELSIF HexVal(buf[beg + p], v) THEN
					IF n > 100000000 THEN ok := FALSE
					ELSE n := n * 16 + v; INC(p); INC(digits)
					END
				ELSE ok := FALSE
				END
			END;
			IF ~ok OR (digits = 0) THEN res := ObxCodec.error; RETURN END;
			d.size := n; d.lineEnd := i + 2; d.state := 1
		END;
		IF d.lineEnd + d.size + 2 > len THEN RETURN END;
		IF (buf[beg + d.lineEnd + d.size] # 13) OR
			(buf[beg + d.lineEnd + d.size + 1] # 10) THEN
			res := ObxCodec.error; RETURN
		END;
		d.unitBeg := d.lineEnd; d.unitLen := d.size;
		d.last := d.size = 0;
		used := d.lineEnd + d.size + 2; res := ObxCodec.done
	END Decode;

	(* --- водитель разбора ------------------------------------------------- *)

	PROCEDURE (VAR p: Parser) Fail (code: INTEGER), NEW;
	BEGIN
		IF p.err = errNone THEN p.err := code END;
		p.finished := TRUE
	END Fail;

	PROCEDURE (VAR p: Parser) Advance (n: INTEGER), NEW;
	BEGIN
		INC(p.beg, n);
		IF p.beg >= p.end THEN p.beg := 0; p.end := 0 END
	END Advance;

	PROCEDURE (VAR p: Parser) Take (at, n: INTEGER), NEW;
		VAR i: INTEGER;
	BEGIN
		IF p.bodyLen + n > LEN(p.body) THEN p.Fail(errBuffer); RETURN END;
		i := 0;
		WHILE i < n DO
			p.body[p.bodyLen + i] := p.buf[at + i]; INC(i)
		END;
		INC(p.bodyLen, n)
	END Take;

	PROCEDURE (VAR p: Parser) Reset*, NEW;
	BEGIN
		p.beg := 0; p.end := 0; p.phase := phaseHeader;
		p.hd.Reset; p.bd.Reset(0); p.cd.Reset;
		p.bodyLen := 0; p.status := 0; p.err := errNone;
		p.redirect := FALSE; p.finished := FALSE; p.loc := ""
	END Reset;

	PROCEDURE (VAR p: Parser) Drive*, NEW;
		VAR used, res, i, n: INTEGER;
	BEGIN
		LOOP
			IF p.finished OR (p.err # errNone) THEN RETURN END;
			CASE p.phase OF
				phaseHeader:
					p.hd.Decode(p.buf, p.beg, p.end - p.beg, used, res);
					IF res = ObxCodec.more THEN
						IF used # 0 THEN p.Fail(errContract) END;
						RETURN
					ELSIF res # ObxCodec.done THEN
						IF p.hd.badStatus THEN p.Fail(errStatus) ELSE p.Fail(errHeader) END;
						RETURN
					END;
					p.status := p.hd.code;
					IF p.hd.locBeg >= 0 THEN
						n := p.hd.locLen;
						IF n > LEN(p.loc) - 1 THEN n := LEN(p.loc) - 1 END;
						i := 0;
						WHILE i < n DO
							p.loc[i] := CHR(p.buf[p.beg + p.hd.locBeg + i]); INC(i)
						END;
						p.loc[i] := 0X
					ELSE p.loc := ""
					END;
					p.Advance(used);
					IF (p.hd.code = 301) OR (p.hd.code = 302) THEN
						p.redirect := TRUE; p.finished := TRUE; RETURN
					END;
					CASE p.hd.kind OF
						kindNone: p.finished := TRUE; RETURN
					| kindLength:
						IF p.hd.length > LEN(p.buf) THEN p.Fail(errBuffer); RETURN END;
						p.bd.Reset(p.hd.length); p.phase := phaseLength
					| kindChunked:
						p.cd.Reset; p.phase := phaseChunk
					ELSE p.Fail(errHeader); RETURN
					END
			| phaseLength:
					p.bd.Decode(p.buf, p.beg, p.end - p.beg, used, res);
					IF res = ObxCodec.more THEN
						IF used # 0 THEN p.Fail(errContract) END;
						RETURN
					ELSIF res # ObxCodec.done THEN
						p.Fail(errLength); RETURN
					END;
					p.Take(p.beg + p.bd.unitBeg, p.bd.unitLen);
					IF p.err # errNone THEN RETURN END;
					p.Advance(used);
					p.finished := TRUE; RETURN
			| phaseChunk:
					p.cd.Decode(p.buf, p.beg, p.end - p.beg, used, res);
					IF res = ObxCodec.more THEN
						IF used # 0 THEN p.Fail(errContract); RETURN END;
						IF (p.cd.state = 1) &
							(p.cd.lineEnd + p.cd.size + 2 > LEN(p.buf)) THEN
							p.Fail(errBuffer)
						END;
						RETURN
					ELSIF res # ObxCodec.done THEN
						p.Fail(errChunk); RETURN
					END;
					p.Take(p.beg + p.cd.unitBeg, p.cd.unitLen);
					IF p.err # errNone THEN RETURN END;
					p.Advance(used);
					IF p.cd.last THEN
						p.finished := TRUE; RETURN
					ELSE
						p.cd.Reset
					END
			| phaseDone:
					p.finished := TRUE; RETURN
			ELSE
					p.Fail(errHeader); RETURN
			END
		END
	END Drive;

	PROCEDURE (VAR p: Parser) FeedFrom* (IN x: ARRAY OF BYTE; beg, len: INTEGER), NEW;
		VAR i, room: INTEGER;
	BEGIN
		IF p.finished OR (p.err # errNone) THEN RETURN END;
		IF beg < 0 THEN beg := 0 END;
		IF len > LEN(x) - beg THEN len := LEN(x) - beg END;
		room := LEN(p.buf) - p.end;
		IF len > room THEN
			IF p.beg > 0 THEN
				i := 0;
				WHILE p.beg + i < p.end DO
					p.buf[i] := p.buf[p.beg + i]; INC(i)
				END;
				p.end := i; p.beg := 0;
				room := LEN(p.buf) - p.end
			END
		END;
		IF len > room THEN p.Fail(errBuffer); RETURN END;
		i := 0;
		WHILE i < len DO p.buf[p.end + i] := x[beg + i]; INC(i) END;
		INC(p.end, len);
		p.Drive
	END FeedFrom;

	PROCEDURE (VAR p: Parser) Feed* (IN x: ARRAY OF BYTE; len: INTEGER), NEW;
	BEGIN
		p.FeedFrom(x, 0, len)
	END Feed;

	PROCEDURE (VAR p: Parser) Eof*, NEW;
	BEGIN
		IF ~p.finished & (p.err = errNone) THEN p.Fail(errClosed) END
	END Eof;

	(* --- водитель-действие ------------------------------------------------ *)

	PROCEDURE (c: Client) Again, NEW;
	BEGIN
		Services.DoLater(c, Services.now)
	END Again;

	PROCEDURE (c: Client) Fail (code: INTEGER), NEW;
	BEGIN
		IF c.err = errNone THEN c.err := code END;
		c.finished := TRUE;
		IF c.s # NIL THEN c.s.Close; c.s := NIL END;
		Services.RemoveAction(c)
	END Fail;

	PROCEDURE (c: Client) Complete, NEW;
	BEGIN
		c.status := c.p.status;
		c.err := errNone;
		c.finished := TRUE;
		IF c.s # NIL THEN c.s.Close; c.s := NIL END;
		Services.RemoveAction(c)
	END Complete;

	PROCEDURE (c: Client) App (IN s: ARRAY OF CHAR), NEW;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(s)) & (s[i] # 0X) & (c.reqLen < LEN(c.req)) DO
			c.req[c.reqLen] := SHORT(SHORT(ORD(s[i]))); INC(c.reqLen); INC(i)
		END
	END App;

	PROCEDURE (c: Client) BuildRequest, NEW;
		VAR t: ARRAY 16 OF CHAR;
	BEGIN
		c.reqLen := 0;
		c.App("GET "); c.App(c.path); c.App(" HTTP/1.1" + 0DX + 0AX);
		c.App("Host: "); c.App(c.host);
		IF c.port # 80 THEN
			Strings.IntToString(c.port, t); c.App(":"); c.App(t)
		END;
		c.App(0DX + 0AX + "Connection: close" + 0DX + 0AX + 0DX + 0AX)
	END BuildRequest;

	PROCEDURE (c: Client) Open (): BOOLEAN, NEW;
		VAR adr: ARRAY 64 OF CHAR; t: ARRAY 16 OF CHAR; res: INTEGER;
	BEGIN
		adr := c.host$;
		IF c.port # 80 THEN
			Strings.IntToString(c.port, t); adr := adr$ + ":" + t$
		END;
		c.s := NIL;
		CommTCP.NewStream("", adr, c.s, res);
		RETURN (res = CommStreams.done) & (c.s # NIL)
	END Open;

	PROCEDURE (c: Client) Init (IN url: ARRAY OF CHAR), NEW;
	BEGIN
		c.s := NIL; c.p.Reset;
		c.reqLen := 0; c.sent := 0;
		c.status := 0; c.err := errNone;
		c.followed := FALSE; c.finished := FALSE;
		c.deadline := Kernel.Time() + timeout;
		IF ~ParseUrl(url, c.host, c.port, c.path) THEN
			c.err := errUrl; c.finished := TRUE; RETURN
		END;
		c.BuildRequest
	END Init;

	PROCEDURE (c: Client) Follow, NEW;
		VAR loc: ARRAY 256 OF CHAR; host: ARRAY 128 OF CHAR;
			path: ARRAY 512 OF CHAR; port, i, j: INTEGER;
	BEGIN
		CopyStr(c.p.loc, loc);
		IF c.s # NIL THEN c.s.Close; c.s := NIL END;
		IF c.followed THEN c.Fail(errRedirect); RETURN END;
		c.followed := TRUE;
		IF loc = "" THEN c.Fail(errRedirect); RETURN END;
		IF MatchPrefix(loc, "http://") THEN
			IF ~ParseUrl(loc, host, port, path) THEN c.Fail(errUrl); RETURN END;
			CopyStr(host, c.host); c.port := port; CopyStr(path, c.path)
		ELSIF loc[0] = "/" THEN
			CopyStr(loc, c.path)
		ELSE
			path[0] := "/"; j := 1; i := 0;
			WHILE (loc[i] # 0X) & (j < LEN(path) - 1) DO
				path[j] := loc[i]; INC(i); INC(j)
			END;
			path[j] := 0X;
			CopyStr(path, c.path)
		END;
		c.p.Reset; c.sent := 0; c.BuildRequest; c.Again
	END Follow;

	PROCEDURE (c: Client) Do;
		VAR n, used, res: INTEGER;
	BEGIN
		IF c.finished THEN RETURN END;
		IF Kernel.Time() > c.deadline THEN c.Fail(errTimeout); RETURN END;
		IF c.s = NIL THEN
			IF ~c.Open() THEN c.Fail(errConnect); RETURN END
		END;
		WHILE c.sent < c.reqLen DO
			c.s.WriteBytes(c.req, c.sent, c.reqLen - c.sent, n);
			IF n > 0 THEN INC(c.sent, n)
			ELSIF ~c.s.IsConnected() THEN c.Fail(errClosed); RETURN
			ELSE c.Again; RETURN
			END
		END;
		IF c.p.err # errNone THEN c.Fail(c.p.err); RETURN END;
		IF c.p.redirect THEN c.Follow; RETURN END;
		IF c.p.finished THEN c.Complete; RETURN END;
		c.s.ReadBytes(io, 0, LEN(io), n);
		IF n > 0 THEN
			c.p.Feed(io, n);
			IF c.p.err # errNone THEN c.Fail(c.p.err); RETURN END;
			IF c.p.redirect THEN c.Follow; RETURN END;
			IF c.p.finished THEN c.Complete; RETURN END
		ELSIF ~c.s.IsConnected() THEN
			c.p.Eof;
			IF c.p.err # errNone THEN c.Fail(c.p.err) ELSE c.Complete END;
			RETURN
		END;
		c.Again
	END Do;

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
		печатая ready. Задачи не заводятся: столько же действий в очереди
		Services. *)
		VAR url: ARRAY 512 OF CHAR; res, n, i, connected: INTEGER;
			deadline: LONGINT; t: ARRAY 32 OF CHAR;
	BEGIN
		Dialog.GetPar("HTTP_URL", url, res);
		IF (res # 0) OR (url = "") THEN
			Console.WriteStr("ObxHttpCodec: задайте HTTP_URL"); Console.WriteLn; RETURN
		END;
		n := IntPar("HTTP_N", 1000);
		IF n > LEN(measureClients) THEN n := LEN(measureClients) END;
		FOR i := 0 TO n - 1 DO
			NEW(measureClients[i]); measureClients[i].Init(url$);
			IF ~measureClients[i].finished THEN
				Services.DoLater(measureClients[i], Services.now)
			END
		END;
		deadline := Kernel.Time() + 20000;
		LOOP
			connected := 0;
			FOR i := 0 TO n - 1 DO
				IF (measureClients[i].s # NIL) &
					(measureClients[i].sent = measureClients[i].reqLen) THEN
					INC(connected)
				END
			END;
			IF (connected >= n) OR (Kernel.Time() > deadline) THEN EXIT END;
			Services.actionHook.Step
		END;
		Console.WriteStr("ready style=codec n="); Strings.IntToString(n, t); Console.WriteStr(t);
		Console.WriteStr(" connected="); Strings.IntToString(connected, t); Console.WriteStr(t);
		Console.WriteStr(" tasks=0 actions=");
		Strings.IntToString(Services.actionsInTheQueue, t); Console.WriteStr(t);
		Console.WriteLn;
		deadline := Kernel.Time() + 5000;
		WHILE Kernel.Time() <= deadline DO Services.actionHook.Step END;
		FOR i := 0 TO n - 1 DO
			Services.RemoveAction(measureClients[i]);
			IF measureClients[i].s # NIL THEN measureClients[i].s.Close; measureClients[i].s := NIL END
		END
	END Measure;

	PROCEDURE Get* (IN url: ARRAY OF CHAR;
			OUT status: INTEGER; OUT body: ARRAY OF BYTE; OUT bodyLen: INTEGER; OUT err: INTEGER);
	(** Пред: вызов с главного стека (не из задачи) — водитель крутит
		очередь действий Services. Пост: err = errNone — status и body
		ответа; иначе status = 0, bodyLen = 0. *)
		VAR c: Client; i: INTEGER; t: LONGINT;
	BEGIN
		NEW(c); c.Init(url);
		t := Kernel.Time() + timeout + 1000;
		IF ~c.finished THEN
			Services.DoLater(c, Services.now);
			WHILE ~c.finished & (Kernel.Time() <= t) DO Services.actionHook.Step END;
			IF ~c.finished THEN c.Fail(errTimeout) END
		END;
		status := c.status; err := c.err;
		IF err = errNone THEN
			bodyLen := c.p.bodyLen;
			IF bodyLen > LEN(body) THEN bodyLen := LEN(body) END;
			i := 0;
			WHILE i < bodyLen DO body[i] := c.p.body[i]; INC(i) END
		ELSE
			bodyLen := 0
		END;
		Services.RemoveAction(c)
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
	(** GET по адресу из внешней переменной HTTP_URL; печатает
		status=<код> err=<код> len=<длина> body=<шестнадцатеричные байты>. *)
		VAR url: ARRAY 512 OF CHAR; body: ARRAY bodySize OF BYTE;
			status, bodyLen, err, res, i: INTEGER; t: ARRAY 16 OF CHAR;
	BEGIN
		Dialog.GetPar("HTTP_URL", url, res);
		IF (res # 0) OR (url = "") THEN
			Console.WriteStr("ObxHttpCodec: задайте HTTP_URL"); Console.WriteLn; RETURN
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

END ObxHttpCodec.
