MODULE Obx_devHttpCodec;
(** Тесты слоёв ObxHttpCodec без сети: один и тот же буфер ответа
	подаётся двумя кусками во всех возможных позициях разреза, а итог
	сверяется с разбором целого буфера. Отдельно — обрыв на каждой
	позиции: недоданный ответ обязан кончиться errClosed, а не висеть.
	Слой на `more` не смеет съедать байты. *)

	IMPORT Strings, UnitAssert, ObxCodec, ObxHttpCodec;

	CONST
		max = 512;

	PROCEDURE ToBytes (IN s: ARRAY OF CHAR; VAR x: ARRAY OF BYTE): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (s[i] # 0X) & (i < LEN(x)) DO
			x[i] := SHORT(SHORT(ORD(s[i]))); INC(i)
		END;
		RETURN i
	END ToBytes;

	PROCEDURE Same (IN a, b: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (a[i] # 0X) & (a[i] = b[i]) DO INC(i) END;
		RETURN (a[i] = 0X) & (b[i] = 0X)
	END Same;

	PROCEDURE Msg (IN base: ARRAY OF CHAR; cut: INTEGER; OUT m: ARRAY OF CHAR);
		VAR t: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(cut, t);
		m := base$ + ", разрез " + t$
	END Msg;

	PROCEDURE Parse (IN data: ARRAY OF BYTE; len, cut: INTEGER;
			VAR body: ARRAY OF BYTE; OUT bodyLen, status, err: INTEGER): BOOLEAN;
		VAR p: ObxHttpCodec.Parser; i: INTEGER;
	BEGIN
		p.Reset;
		IF cut > 0 THEN p.FeedFrom(data, 0, cut) END;
		IF cut < len THEN p.FeedFrom(data, cut, len - cut) END;
		status := p.status; err := p.err; bodyLen := p.bodyLen;
		i := 0;
		WHILE i < bodyLen DO body[i] := p.body[i]; INC(i) END;
		RETURN p.redirect
	END Parse;

	PROCEDURE TestLengthEveryCut*;
		VAR s: ARRAY max OF CHAR; data: ARRAY max OF BYTE; body: ARRAY max OF BYTE;
			n, cut, bodyLen, status, err, i: INTEGER; m: UnitAssert.Message;
			expected: ARRAY 32 OF CHAR;
	BEGIN
		s := "HTTP/1.1 200 OK" + 0DX + 0AX + "Content-Length: 13" + 0DX + 0AX +
			0DX + 0AX + "hello, world!";
		expected := "hello, world!";
		n := ToBytes(s, data);
		FOR cut := 0 TO n DO
			Msg("длина", cut, m);
			IF Parse(data, n, cut, body, bodyLen, status, err) THEN
				UnitAssert.Fail(m)
			END;
			UnitAssert.EqualInt(ObxHttpCodec.errNone, err, m);
			UnitAssert.EqualInt(200, status, m);
			UnitAssert.EqualInt(13, bodyLen, m);
			i := 0;
			WHILE (i < bodyLen) & (body[i] = SHORT(SHORT(ORD(expected[i])))) DO INC(i) END;
			UnitAssert.True((i = bodyLen) & (expected[i] = 0X), m)
		END
	END TestLengthEveryCut;

	PROCEDURE TestChunkedEveryCut*;
		VAR s: ARRAY max OF CHAR; data: ARRAY max OF BYTE; body: ARRAY max OF BYTE;
			n, cut, bodyLen, status, err, i: INTEGER; m: UnitAssert.Message;
			expected: ARRAY 32 OF CHAR;
	BEGIN
		s := "HTTP/1.1 200 OK" + 0DX + 0AX + "Transfer-Encoding: chunked" + 0DX + 0AX +
			0DX + 0AX + "5" + 0DX + 0AX + "alpha" + 0DX + 0AX +
			"4" + 0DX + 0AX + "beta" + 0DX + 0AX +
			"5" + 0DX + 0AX + "gamma" + 0DX + 0AX + "0" + 0DX + 0AX + 0DX + 0AX;
		expected := "alphabetagamma";
		n := ToBytes(s, data);
		FOR cut := 0 TO n DO
			Msg("chunked", cut, m);
			IF Parse(data, n, cut, body, bodyLen, status, err) THEN
				UnitAssert.Fail(m)
			END;
			UnitAssert.EqualInt(ObxHttpCodec.errNone, err, m);
			UnitAssert.EqualInt(200, status, m);
			UnitAssert.EqualInt(14, bodyLen, m);
			i := 0;
			WHILE (i < bodyLen) & (body[i] = SHORT(SHORT(ORD(expected[i])))) DO INC(i) END;
			UnitAssert.True((i = bodyLen) & (expected[i] = 0X), m)
		END
	END TestChunkedEveryCut;

	PROCEDURE TestRedirectEveryCut*;
		VAR s: ARRAY max OF CHAR; data: ARRAY max OF BYTE; body: ARRAY max OF BYTE;
			n, cut, bodyLen, status, err: INTEGER; m: UnitAssert.Message;
	BEGIN
		s := "HTTP/1.1 302 Found" + 0DX + 0AX + "Location: /len" + 0DX + 0AX +
			"Content-Length: 0" + 0DX + 0AX + 0DX + 0AX;
		n := ToBytes(s, data);
		FOR cut := 0 TO n DO
			Msg("переадресация", cut, m);
			IF ~Parse(data, n, cut, body, bodyLen, status, err) THEN
				UnitAssert.Fail(m)
			END;
			UnitAssert.EqualInt(ObxHttpCodec.errNone, err, m);
			UnitAssert.EqualInt(302, status, m);
			UnitAssert.EqualInt(0, bodyLen, m)
		END
	END TestRedirectEveryCut;

	PROCEDURE TestGarbageStatusEveryCut*;
		VAR s: ARRAY max OF CHAR; data: ARRAY max OF BYTE; body: ARRAY max OF BYTE;
			n, cut, bodyLen, status, err: INTEGER; m: UnitAssert.Message;
	BEGIN
		s := "NOT-HTTP blah" + 0DX + 0AX + "Content-Length: 0" + 0DX + 0AX + 0DX + 0AX;
		n := ToBytes(s, data);
		FOR cut := 0 TO n DO
			Msg("мусор", cut, m);
			IF Parse(data, n, cut, body, bodyLen, status, err) THEN
				UnitAssert.Fail(m)
			END;
			UnitAssert.EqualInt(ObxHttpCodec.errStatus, err, m);
			UnitAssert.EqualInt(0, status, m);
			UnitAssert.EqualInt(0, bodyLen, m)
		END
	END TestGarbageStatusEveryCut;

	PROCEDURE TestBadChunkEveryCut*;
		VAR s: ARRAY max OF CHAR; data: ARRAY max OF BYTE; body: ARRAY max OF BYTE;
			n, cut, bodyLen, status, err: INTEGER; m: UnitAssert.Message;
	BEGIN
		s := "HTTP/1.1 200 OK" + 0DX + 0AX + "Transfer-Encoding: chunked" + 0DX + 0AX +
			0DX + 0AX + "zz" + 0DX + 0AX + "abc" + 0DX + 0AX + "0" + 0DX + 0AX + 0DX + 0AX;
		n := ToBytes(s, data);
		FOR cut := 0 TO n DO
			Msg("плохой кусок", cut, m);
			IF Parse(data, n, cut, body, bodyLen, status, err) THEN
				UnitAssert.Fail(m)
			END;
			UnitAssert.EqualInt(ObxHttpCodec.errChunk, err, m);
			UnitAssert.EqualInt(0, bodyLen, m)
		END
	END TestBadChunkEveryCut;

	PROCEDURE TestShortEveryCut*;
	(* Каждая позиция обрыва: чего-то не хватает, разбор обязан сказать
	   errClosed, а не ждать вечно. Полный буфер сюда не входит. *)
		VAR s: ARRAY max OF CHAR; data: ARRAY max OF BYTE; body: ARRAY max OF BYTE;
			p: ObxHttpCodec.Parser; n, cut, bodyLen, status, err, i: INTEGER;
			m: UnitAssert.Message;
	BEGIN
		s := "HTTP/1.1 200 OK" + 0DX + 0AX + "Transfer-Encoding: chunked" + 0DX + 0AX +
			0DX + 0AX + "5" + 0DX + 0AX + "alpha" + 0DX + 0AX + "0" + 0DX + 0AX + 0DX + 0AX;
		n := ToBytes(s, data);
		FOR cut := 0 TO n - 1 DO
			Msg("обрыв", cut, m);
			p.Reset;
			IF cut > 0 THEN p.FeedFrom(data, 0, cut) END;
			IF ~p.finished THEN p.Eof END;
			status := p.status; err := p.err; bodyLen := p.bodyLen;
			i := 0;
			WHILE i < bodyLen DO body[i] := p.body[i]; INC(i) END;
			UnitAssert.EqualInt(ObxHttpCodec.errClosed, err, m)
		END
	END TestShortEveryCut;

	PROCEDURE TestMoreEatsNothing*;
		VAR s: ARRAY max OF CHAR; data: ARRAY max OF BYTE; hd: ObxHttpCodec.HeaderDecoder;
			bd: ObxHttpCodec.BodyDecoder; cd: ObxHttpCodec.ChunkDecoder;
			n, cut, used, res: INTEGER;
	BEGIN
		s := "HTTP/1.1 200 OK" + 0DX + 0AX + "Content-Length: 13" + 0DX + 0AX +
			0DX + 0AX + "hello, world!";
		n := ToBytes(s, data);
		hd.Reset;
		FOR cut := 0 TO n - 1 DO
			hd.Decode(data, 0, cut, used, res);
			IF res = ObxCodec.more THEN
				UnitAssert.EqualInt(0, used, "заголовок: more с used # 0")
			END
		END;
		bd.Reset(13);
		FOR cut := 0 TO 12 DO
			bd.Decode(data, n - 13, cut, used, res);
			IF res = ObxCodec.more THEN
				UnitAssert.EqualInt(0, used, "тело: more с used # 0")
			END
		END;
		s := "5" + 0DX + 0AX + "alpha" + 0DX + 0AX + "0" + 0DX + 0AX + 0DX + 0AX;
		n := ToBytes(s, data);
		cd.Reset;
		FOR cut := 0 TO n - 1 DO
			cd.Decode(data, 0, cut, used, res);
			IF (res = ObxCodec.more) & (cd.state = 0) THEN
				UnitAssert.EqualInt(0, used, "кусок: more с used # 0")
			END
		END
	END TestMoreEatsNothing;

END Obx_devHttpCodec.
