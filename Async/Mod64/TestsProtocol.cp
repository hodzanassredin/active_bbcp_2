MODULE AsyncTestsProtocol;
(** Тесты протокола диалога канала (этап 10, образец Zonnon).

	Описание автомата: «Запрос → Ответ* → Конец». Правильная
	последовательность проходит; сообщение не по порядку трапует с
	текстом нарушения. Канал без протокола принимает любой порядок. *)

	IMPORT AsyncProto, AsyncChan, AsyncSelect, Tasks, Services, Kernel, UnitAssert;

	CONST
		desc = "start: RequestMsg > wait; wait: ResponseMsg > wait; wait: EndMsg > done; done:";

	TYPE
		RequestMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		ResponseMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		EndMsg = POINTER TO RECORD (AsyncSelect.Msg) END;

		AltSendTask = POINTER TO RECORD (Tasks.Task)
			m: AsyncSelect.Msg; idx, done: INTEGER
		END;

	VAR
		box: AsyncChan.Mailbox;
		proto: AsyncProto.Protocol;
		altSend: AltSendTask;

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

	(* --- отправка через Select -------------------------------------------- *)

	PROCEDURE (t: AltSendTask) Run;
		VAR ops: ARRAY 1 OF AsyncSelect.Op;
	BEGIN
		AsyncChan.SendAlt(box, t.m, ops[0]);
		t.idx := AsyncChan.Select(ops);
		t.done := 1
	END Run;

	PROCEDURE TaskOver (): BOOLEAN;
		VAR st: INTEGER;
	BEGIN
		IF altSend = NIL THEN RETURN TRUE END;
		st := Tasks.State(altSend);
		RETURN (st = Tasks.Finished) OR (st = Tasks.Cancelled)
	END TaskOver;

	PROCEDURE PumpUntil (ms: LONGINT; cond: PROCEDURE (): BOOLEAN): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~cond() & (Kernel.Time() - t0 <= ms) DO Services.actionHook.Step END;
		RETURN cond()
	END PumpUntil;

	PROCEDURE TestAltSendCorrect*;
	(** Select-отправка идёт через тот же автомат: правильный порядок
		переводит состояние дальше. *)
		VAR t: AltSendTask; m: RequestMsg; name: AsyncProto.Message; state: INTEGER;
	BEGIN
		proto := AsyncProto.New(desc);
		UnitAssert.NotNil(proto, "protocol description parsed");
		box := AsyncChan.NewMailbox();
		AsyncChan.SetProtocol(box, proto);
		NEW(m); m.n := 1;
		NEW(t); t.m := m; t.idx := -1; t.done := 0; altSend := t;
		Tasks.Start(t);
		UnitAssert.True(PumpUntil(5000, TaskOver), "alt send finishes");
		UnitAssert.EqualInt(1, t.done, "select completed");
		UnitAssert.EqualInt(0, t.idx, "send alternative won");
		state := box.protoState;
		AsyncProto.StateName(proto, state, name);
		UnitAssert.EqualStr("wait", name, "select send advanced the protocol");
		altSend := NIL
	END TestAltSendCorrect;

	PROCEDURE TestAltSendBadOrder*;
	(** Нарушение порядка в Select-отправке — трап в задаче; состояние
		протокола не меняется, текст нарушения остаётся. *)
		VAR t: AltSendTask; m: EndMsg; msg: AsyncProto.Message; state: INTEGER;
	BEGIN
		proto := AsyncProto.New(desc);
		box := AsyncChan.NewMailbox();
		AsyncChan.SetProtocol(box, proto);
		state := box.protoState;
		NEW(m); NEW(t); t.m := m; t.idx := -1; t.done := 0; altSend := t;
		Tasks.Start(t);
		UnitAssert.True(PumpUntil(5000, TaskOver), "violating alt send task ends");
		UnitAssert.EqualInt(0, t.done, "violating alt send did not complete");
		UnitAssert.True(Tasks.Failed(t), "task trapped on protocol violation");
		UnitAssert.EqualInt(state, box.protoState, "protocol state unchanged");
		AsyncProto.LastViolation(proto, msg);
		UnitAssert.True(Contains(msg, "EndMsg"), "violation names the message");
		altSend := NIL
	END TestAltSendBadOrder;

END AsyncTestsProtocol.
