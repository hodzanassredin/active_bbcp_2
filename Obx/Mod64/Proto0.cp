MODULE ObxProto0;
	IMPORT AsyncChan, AsyncSelect, AsyncProto, Console;

	CONST desc = "start: Hello > talk; talk: Data > talk; talk: Bye > done; done:";

	TYPE
		Hello = POINTER TO RECORD (AsyncSelect.Msg) END;
		Data = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Bye = POINTER TO RECORD (AsyncSelect.Msg) END;

	PROCEDURE Do*;
		VAR p: AsyncProto.Protocol; box: AsyncChan.Mailbox;
			h: Hello; d: Data; b: Bye; name: ARRAY 32 OF CHAR;
	BEGIN
		p := AsyncProto.New(desc);
		box := AsyncChan.NewMailbox(); AsyncChan.SetProtocol(box, p);
		NEW(h); box.Send(h);
		NEW(d); d.n := 1; box.Send(d);
		NEW(d); d.n := 2; box.Send(d);
		NEW(b); box.Send(b);
		AsyncProto.StateName(p, box.protoState, name);
		Console.WriteStr("ObxProto0: state="); Console.WriteStr(name); Console.WriteLn
	END Do;

END ObxProto0.
