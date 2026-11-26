# Overview by Example: ObxEvent3

The example shows a multicast, as in Concurrent ML (Reppy). A broadcaster reads the input and chooses to whom to deliver the value first; the action after the choice delivers it to the second consumer. If one consumer lags, the broadcaster waits for it inside the action, so no value is lost.

Source of the module `ObxEvent3` (`Obx/Mod64/Event3.cp`):

```
MODULE ObxEvent3;
(** Мультикаст: одно значение — двум получателям.

	Идея из Concurrent ML (Reppy): процесс читает вход и выбирает, кому
	отдать значение первым; действие Wrap после выбора отдаёт его второму
	получателю. Если один получатель отстаёт, отправитель ждёт его в
	действии — значение не теряется. *)

	IMPORT Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console, Strings;

	CONST
		total = 5;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		SendOther = POINTER TO RECORD (AsyncEvent.Action)
			c: AsyncChan.Chan;
			m: AsyncSelect.Msg
		END;

		Producer = POINTER TO RECORD (Tasks.Task)
			c: AsyncChan.Chan; done: BOOLEAN
		END;

		Consumer = POINTER TO RECORD (Tasks.Task)
			c: AsyncChan.Chan; sum, count: INTEGER; done: BOOLEAN
		END;

		Broadcaster = POINTER TO RECORD (Tasks.Task)
			inCh, a, b: AsyncChan.Chan; n: INTEGER; done: BOOLEAN
		END;

	VAR
		(** Итоги последнего вещания. *)
		SumA*, SumB*, CountA*, CountB*: INTEGER;

	PROCEDURE Num (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END Num;

	PROCEDURE (a: SendOther) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		a.c.Send(a.m);	(* встреча со вторым получателем *)
		y := m
	END Do;

	PROCEDURE (p: Producer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO total DO p.c.Send(Num(i)) END;
		p.done := TRUE
	END Run;

	PROCEDURE (c: Consumer) Run;
		VAR i: INTEGER; m: AsyncSelect.Msg;
	BEGIN
		c.sum := 0; c.count := 0;
		FOR i := 1 TO total DO
			c.c.Recv(m);
			WITH m: NumMsg DO c.sum := c.sum + m.n END;
			INC(c.count)
		END;
		c.done := TRUE
	END Run;

	PROCEDURE (b: Broadcaster) Run;
		VAR i: INTEGER; m: AsyncSelect.Msg;
			s1, s2: SendOther; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		FOR i := 1 TO b.n DO
			b.inCh.Recv(m);
			NEW(s1); s1.c := b.b; s1.m := m;
			NEW(s2); s2.c := b.a; s2.m := m;
			evs[0] := AsyncEvent.Wrap(AsyncEvent.Send(b.a, m), s1);
			evs[1] := AsyncEvent.Wrap(AsyncEvent.Send(b.b, m), s2);
			IF AsyncEvent.Sync(AsyncEvent.Choose(evs)) = NIL THEN END
		END;
		b.done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR inCh, a, b: AsyncChan.Chan; p: Producer; ca, cb: Consumer;
			br: Broadcaster; buf: ARRAY 16 OF CHAR;
	BEGIN
		inCh := AsyncChan.NewChan(); a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(p); p.c := inCh; p.done := FALSE;
		NEW(ca); ca.c := a; ca.done := FALSE;
		NEW(cb); cb.c := b; cb.done := FALSE;
		NEW(br); br.inCh := inCh; br.a := a; br.b := b; br.n := total; br.done := FALSE;
		Tasks.Start(ca); Tasks.Start(cb); Tasks.Start(br); Tasks.Start(p);
		WHILE ~(p.done & ca.done & cb.done & br.done) DO Services.actionHook.Step END;
		SumA := ca.sum; SumB := cb.sum; CountA := ca.count; CountB := cb.count;
		Console.WriteStr("ObxEvent3: a=");
		Strings.IntToString(SumA, buf); Console.WriteStr(buf);
		Console.WriteStr(" b=");
		Strings.IntToString(SumB, buf); Console.WriteStr(buf);
		Console.WriteStr(" each=");
		Strings.IntToString(CountA, buf); Console.WriteStr(buf);
		Console.WriteStr("/");
		Strings.IntToString(CountB, buf); Console.WriteStr(buf);
		Console.WriteLn
	END Do;

END ObxEvent3.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxEvent3.Do
```

```sh
echo 'ObxEvent3.Do' | tools/run64.sh
```

The log shows:

```
ObxEvent3: a=15 b=15 each=5/5
```

## What happened

1. Two consumers start and wait for values on their own channels.
2. A producer sends 1, 2, 3, 4, 5 into the input channel; the broadcaster receives each value in turn.
3. For each value the broadcaster builds a choice of two sends: to the first consumer and to the second. Both alternatives are wrapped by an action that sends the same value to the other consumer.
4. Whichever send is ready first wins; the action then performs the second send. The winning send is a rendezvous, and the second send inside the action is a switch point: the broadcaster waits until the lagging consumer takes the value.
5. Each consumer receives all five values in order. Both sums are 15, and each consumer received five values.

In this example we have seen a choice decide the first of two deliveries, and an action finish the second delivery without losing the value when a consumer is slow.

More in [the Async guide](../../Docu/Async.md), section 5.1.
