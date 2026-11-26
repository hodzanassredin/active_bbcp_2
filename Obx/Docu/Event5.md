# Overview by Example: ObxEvent5

The example shows the dining philosophers of Dijkstra, solved with a waiter. The waiter holds the free forks and the requests; when both forks of a philosopher are free, it grants them as one message. Its wait for a request or a release is one event choice: it waits for whichever comes first. A deadlock is impossible because forks are granted in pairs and only by the waiter. The choice is the same primitive that Hopac calls `Alt.choose`.

Source of the module `ObxEvent5` (`Obx/Mod64/Event5.cp`):

```
MODULE ObxEvent5;
(** Обедающие философы: официант выбирает, кому отдать обе вилки.

	Классическая задача Дейкстры. Официант держит свободные вилки и заявки;
	когда обе вилки философа свободны, он отдаёт их одним сообщением.
	Ожидание заявки и освобождения — один выбор AsyncEvent: официант ждёт
	то, что придёт раньше. Так взаимная блокировка невозможна: вилки
	выдаются парой и только официантом. *)

	IMPORT Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console, Strings;

	CONST
		n = 5;		(* философов и вилок *)
		meals = 2;	(* сколько раз поест каждый *)

		kRequest = 0; kRelease = 1; kFinished = 2; kGrant = 3;

	TYPE
		Msg = POINTER TO RECORD (AsyncSelect.Msg)
			kind, id: INTEGER;
			grant: AsyncChan.Mailbox
		END;

		Table = POINTER TO RECORD
			free: ARRAY n OF BOOLEAN;
			eaten: ARRAY n OF INTEGER;
			reqCh, relCh: AsyncChan.Mailbox;
			grant: ARRAY n OF AsyncChan.Mailbox;
			pending: ARRAY n OF INTEGER;
			npend, finished: INTEGER;
			running: BOOLEAN
		END;

		Waiter = POINTER TO RECORD (Tasks.Task) t: Table; done: BOOLEAN END;
		Philosopher = POINTER TO RECORD (Tasks.Task)
			t: Table; id: INTEGER; done: BOOLEAN
		END;

	VAR
		(** Сколько раз поел каждый философ. *)
		Eaten*: ARRAY n OF INTEGER;

	PROCEDURE NewMsg (kind, id: INTEGER; grant: AsyncChan.Mailbox): Msg;
		VAR m: Msg;
	BEGIN
		NEW(m); m.kind := kind; m.id := id; m.grant := grant;
		RETURN m
	END NewMsg;

	PROCEDURE CanEat (t: Table; id: INTEGER): BOOLEAN;
	BEGIN
		RETURN t.free[id] & t.free[(id + 1) MOD n]
	END CanEat;

	PROCEDURE Take (t: Table; id: INTEGER);
	BEGIN
		t.free[id] := FALSE; t.free[(id + 1) MOD n] := FALSE
	END Take;

	PROCEDURE Give (t: Table; id: INTEGER);
	BEGIN
		t.free[id] := TRUE; t.free[(id + 1) MOD n] := TRUE
	END Give;

	PROCEDURE AddPending (t: Table; id: INTEGER);
	BEGIN
		t.pending[t.npend] := id; INC(t.npend)
	END AddPending;

	PROCEDURE RemovePending (t: Table; i: INTEGER);
		VAR j: INTEGER;
	BEGIN
		FOR j := i TO t.npend - 2 DO t.pending[j] := t.pending[j + 1] END;
		DEC(t.npend)
	END RemovePending;

	PROCEDURE GrantOne (t: Table): BOOLEAN;
		VAR i, id: INTEGER;
	BEGIN
		i := 0;
		WHILE i < t.npend DO
			id := t.pending[i];
			IF CanEat(t, id) THEN
				Take(t, id); RemovePending(t, i);
				t.grant[id].Send(NewMsg(kGrant, id, NIL));
				RETURN TRUE
			END;
			INC(i)
		END;
		RETURN FALSE
	END GrantOne;

	PROCEDURE Handle (t: Table; m: AsyncSelect.Msg);
	BEGIN
		WITH m: Msg DO
			IF m.kind = kRequest THEN
				AddPending(t, m.id)
			ELSIF m.kind = kRelease THEN
				Give(t, m.id)
			ELSIF m.kind = kFinished THEN
				INC(t.finished);
				IF t.finished = n THEN t.running := FALSE END
			END
		END
	END Handle;

	PROCEDURE (w: Waiter) Run;
		VAR m: AsyncSelect.Msg; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		WHILE w.t.running DO
			IF ~GrantOne(w.t) THEN
				evs[0] := AsyncEvent.Recv(w.t.reqCh);
				evs[1] := AsyncEvent.Recv(w.t.relCh);
				m := AsyncEvent.Sync(AsyncEvent.Choose(evs));
				Handle(w.t, m)
			END
		END;
		w.done := TRUE
	END Run;

	PROCEDURE (p: Philosopher) Run;
		VAR i: INTEGER; m: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO meals DO
			p.t.reqCh.Send(NewMsg(kRequest, p.id, p.t.grant[p.id]));
			p.t.grant[p.id].Recv(m);	(* ждём решения официанта *)
			INC(p.t.eaten[p.id]);
			Tasks.Sleep(1);			(* едим *)
			p.t.relCh.Send(NewMsg(kRelease, p.id, NIL));
			Tasks.Sleep(1)			(* думаем *)
		END;
		p.t.reqCh.Send(NewMsg(kFinished, p.id, NIL));
		p.done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR t: Table; w: Waiter; p: ARRAY n OF Philosopher; i: INTEGER;
			buf: ARRAY 16 OF CHAR;
	BEGIN
		NEW(t); t.npend := 0; t.finished := 0; t.running := TRUE;
		t.reqCh := AsyncChan.NewMailbox(); t.relCh := AsyncChan.NewMailbox();
		FOR i := 0 TO n - 1 DO
			t.free[i] := TRUE; t.eaten[i] := 0;
			t.grant[i] := AsyncChan.NewMailbox()
		END;
		NEW(w); w.t := t; w.done := FALSE;
		Tasks.Start(w);
		FOR i := 0 TO n - 1 DO
			NEW(p[i]); p[i].t := t; p[i].id := i; p[i].done := FALSE;
			Tasks.Start(p[i])
		END;
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END;
		FOR i := 0 TO n - 1 DO Eaten[i] := t.eaten[i] END;
		Console.WriteStr("ObxEvent5: meals");
		FOR i := 0 TO n - 1 DO
			Strings.IntToString(Eaten[i], buf);
			Console.WriteStr(" "); Console.WriteStr(buf)
		END;
		Console.WriteLn
	END Do;

END ObxEvent5.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxEvent5.Do
```

```sh
echo 'ObxEvent5.Do' | tools/run64.sh
```

The log shows:

```
ObxEvent5: meals 2 2 2 2 2
```

## What happened

1. The waiter starts with all five forks free and waits on a choice: a request or a release. Both are mailboxes, so a send from a philosopher does not wait and wakes the waiter's offer.
2. Each philosopher asks for a meal by sending a request, then waits on its own grant mailbox. Eating and thinking are short sleeps; after eating the philosopher sends a release.
3. When the waiter can grant no pending request, it synchronizes on the choice of request and release. When a request arrives, it goes into the pending list; when a release arrives, the two forks of that philosopher become free.
4. After every message the waiter scans the pending requests in arrival order and grants the first one whose two forks are free. It takes both forks at once and sends one grant message.
5. Each philosopher eats twice. After two meals a philosopher sends a finished message. When all five have finished, the waiter stops.
6. The counts are deterministic: each philosopher ate twice, and the log shows `meals 2 2 2 2 2`.

In this example we have seen a server built on event choice, atomic acquisition of two resources, and a termination condition carried through the same channel as the requests.

More in [the Async guide](../../Docu/Async.md), section 5.1.
