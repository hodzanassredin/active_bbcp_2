MODULE Test64Protocol;
(** Тесты протокола диалога канала (этап 10, образец Zonnon).

	Описание автомата: «Запрос → Ответ* → Конец». Правильная
	последовательность проходит; сообщение не по порядку трапует с
	текстом нарушения. Канал без протокола принимает любой порядок. *)

	IMPORT AsyncProto, AsyncChan, AsyncSelect, UnitAssert;

	CONST
		desc = "start: RequestMsg > wait; wait: ResponseMsg > wait; wait: EndMsg > done; done:";

	TYPE
		RequestMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		ResponseMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		EndMsg = POINTER TO RECORD (AsyncSelect.Msg) END;

	VAR
		box: AsyncChan.Mailbox;
		proto: AsyncProto.Protocol;

	PROCEDURE Contains (IN s, pattern: ARRAY OF CHAR): BOOLEAN;
		VAR i, j: INTEGER; found: BOOLEAN;
	BEGIN
		i := 0; found := FALSE;
		WHILE ~found & (s[i] # 0X) DO
			j := 0;
			WHILE (pattern[j] # 0X) & (s[i + j] = pattern[j]) DO INC(j) END;
			IF pattern[j] = 0X THEN found := TRUE ELSE INC(i) END
		END;
		RETURN found
	END Contains;

	PROCEDURE Length (IN s: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE s[i] # 0X DO INC(i) END;
		RETURN i
	END Length;

	PROCEDURE SendRequest;
		VAR m: RequestMsg;
	BEGIN
		NEW(m); m.n := 1; box.Send(m)
	END SendRequest;

	PROCEDURE SendResponse;
		VAR m: ResponseMsg;
	BEGIN
		NEW(m); m.n := 2; box.Send(m)
	END SendResponse;

	PROCEDURE SendEnd;
		VAR m: EndMsg;
	BEGIN
		NEW(m); box.Send(m)
	END SendEnd;

	PROCEDURE BadOrder;
		VAR m: EndMsg;
	BEGIN
		NEW(m); box.Send(m)	(* Конец без Запроса *)
	END BadOrder;

	PROCEDURE TestCorrectDialogue*;
		VAR name: AsyncProto.Message; state: INTEGER;
	BEGIN
		proto := AsyncProto.New(desc);
		UnitAssert.NotNil(proto, "protocol description parsed");
		box := AsyncChan.NewMailbox();
		AsyncChan.SetProtocol(box, proto);
		SendRequest; SendResponse; SendResponse; SendEnd;
		state := box.protoState;
		AsyncProto.StateName(proto, state, name);
		UnitAssert.EqualStr("done", name, "dialogue finished in final state");
		UnitAssert.True(AsyncProto.IsFinal(proto, state), "final state has no transitions")
	END TestCorrectDialogue;

	PROCEDURE TestBadOrder*;
		VAR name: AsyncProto.Message;
	BEGIN
		proto := AsyncProto.New(desc);
		UnitAssert.NotNil(proto, "protocol description parsed");
		box := AsyncChan.NewMailbox();
		AsyncChan.SetProtocol(box, proto);
		UnitAssert.Traps(BadOrder, "message out of protocol order traps");
		AsyncProto.LastViolation(proto, name);
		UnitAssert.True(Contains(name, "EndMsg"), "violation names the message");
		UnitAssert.True(Contains(name, "start"), "violation names the state")
	END TestBadOrder;

	PROCEDURE TestNoProtocol*;
	BEGIN
		box := AsyncChan.NewMailbox();	(* протокол не прикреплён *)
		SendEnd; SendRequest; SendResponse; SendEnd;
		UnitAssert.EqualInt(0, box.protoState, "state untouched without protocol")
	END TestNoProtocol;

	PROCEDURE TestParseError*;
		VAR p: AsyncProto.Protocol; msg: AsyncProto.Message;
	BEGIN
		p := AsyncProto.New("start RequestMsg > wait");
		UnitAssert.True(p = NIL, "malformed description rejected");
		AsyncProto.LastError(msg);
		UnitAssert.True(Length(msg) > 0, "parse error has text")
	END TestParseError;

END Test64Protocol.
