# Overview by Example: ObxProto0

The example shows a dialogue protocol. A state machine "greeting, data any number of times, goodbye" is attached to a mailbox, and each send is checked against it at the moment it takes place.

Source of the module `ObxProto0` (`Obx/Mod64/Proto0.cp`):

```
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
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxProto0.Do
```

```sh
echo 'ObxProto0.Do' | tools/run64.sh
```

The log shows:

```
ObxProto0: state=done
```

## What happened

1. `AsyncProto.New` builds the machine from the text `desc`.
2. `AsyncChan.SetProtocol` attaches it to the mailbox and sets the initial state `start`.
3. The sends `Hello`, `Data`, `Data`, `Bye` move the machine to `talk`, `talk`, `talk`, `done`.
4. `AsyncProto.StateName` names the final state from the field `protoState` of the channel.
5. Sending `Data` before `Hello` would print the line `AsyncChan protocol: …` and trap with code 21.

More in `Docu/Async.md`.

In this example we have seen how to write down the message order of a dialogue as text and leave its checking to the channel.
