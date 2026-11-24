# Overview by Example: ObxBank5

The accounts belong to the cashier task; the clients send requests through a mailbox.

The three accounts are local to one task, the cashier. The clients do not touch them: each puts a
request into the mailbox `window` and waits for the answer on its own rendezvous channel. The
cashier takes the requests one at a time, so the balance check and the transfer need no monitor. The
example belongs to chapter 5 of the tutorial, `Docu/Tut-Active-5.md`.

```
MODULE ObxBank5;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	CONST n = 3;

	TYPE
		Request = POINTER TO RECORD (AsyncSelect.Msg)
			from, to, sum: INTEGER;
			reply: AsyncChan.Chan	(* сюда кассир кладёт ответ *)
		END;
		Answer = POINTER TO RECORD (AsyncSelect.Msg) ok: BOOLEAN END;
		Cashier = POINTER TO RECORD (Tasks.Task) END;
		Client = POINTER TO RECORD (Tasks.Task) id, sum: INTEGER END;
		Manager = POINTER TO RECORD (Tasks.Task) END;

	VAR window: AsyncChan.Mailbox;	(* окошко кассы *)

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	(* Счета принадлежат кассиру: их видит только эта задача *)
	PROCEDURE (c: Cashier) Run;
		VAR balance: ARRAY n OF INTEGER; m: AsyncSelect.Msg; a: Answer; i, total: INTEGER;
	BEGIN
		balance[0] := 100; balance[1] := 100; balance[2] := 100;
		LOOP
			window.Recv(m);
			IF m = NIL THEN EXIT END;	(* касса закрывается *)
			WITH m: Request DO
				NEW(a); a.ok := balance[m.from] >= m.sum;
				IF a.ok THEN
					balance[m.from] := balance[m.from] - m.sum;
					balance[m.to] := balance[m.to] + m.sum
				END;
				m.reply.Send(a)
			END
		END;
		total := 0;
		FOR i := 0 TO n - 1 DO Say("balance ", balance[i]); total := total + balance[i] END;
		Say("total ", total)
	END Run;

	PROCEDURE (c: Client) Run;
		VAR r: Request; m: AsyncSelect.Msg;
	BEGIN
		NEW(r); r.from := c.id; r.to := (c.id + 1) MOD n; r.sum := c.sum;
		r.reply := AsyncChan.NewChan();
		window.Send(r);
		r.reply.Recv(m);
		IF m(Answer).ok THEN Say("done, client ", c.id) ELSE Say("refused, client ", c.id) END
	END Run;

	PROCEDURE (m: Manager) Run;
		VAR c: ARRAY n OF Client; i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO
			NEW(c[i]); c[i].id := i; c[i].sum := 60 + 40 * i;
			IF i = 2 THEN c[i].sum := 250 END;	(* столько нет ни на одном счёте *)
			Tasks.Start(c[i])
		END;
		FOR i := 0 TO n - 1 DO Tasks.Join(c[i]) END;
		window.Send(NIL)	(* закрыть кассу *)
	END Run;

	PROCEDURE Do*;
		VAR k: Cashier; m: Manager;
	BEGIN
		window := AsyncChan.NewMailbox();
		NEW(k); Tasks.Start(k); NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxBank5.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Bank5.cp`; it is for amd64
only). In a BlackBox window the command is executed by a commander:

```
   ObxBank5.Do
```

In the console:

```sh
echo 'ObxBank5.Do' | tools/run64.sh
```

The log shows:

```
done, client 0
done, client 1
refused, client 2
balance 40
balance 60
balance 200
total 300
```

## What happened

The cashier waits in `window.Recv`. Clients 0, 1 and 2 ask to transfer 60, 100 and 250. Sending into
the mailbox does not wait, so each client goes straight on to `r.reply.Recv` and sleeps there.

The cashier takes the requests one at a time, in the order they arrived. The balance check and the
transfer run inside one task with no switch point between them. The first two requests are carried
out, the third is refused: no account holds 250. The cashier sends the answer into the client's
rendezvous channel, and the client wakes up.

The manager waits for all the clients in `Tasks.Join` and puts `NIL` into the window. The cashier
leaves the loop and prints the balances. The total is 300.

In this example we have seen how to give data to one task and work with it through requests, how to
return an answer over a rendezvous channel, and how to close a service with a conditional message.
