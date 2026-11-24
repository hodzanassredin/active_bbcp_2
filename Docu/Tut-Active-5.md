[← Chapter 4](Tut-Active-4.md) · [Contents](Tut-Active-TOC.md) · [Chapter 6 →](Tut-Active-6.md)

# 5. Messages instead of shared data

## 5.1. The cashier at the window

In chapters 2–4 the accounts lay in shared variables, and every cashier changed them on his own. The
more tasks touch shared data, the more there is to think about: where the switch points are, which
monitor, in which order. A real bank does not work like that. Clients do not walk up to the safe:
they queue at the window, hand the cashier a request and wait for the answer. Only the cashier opens
the safe.

A program can be built the same way. The accounts belong to one task, the cashier. The other tasks
do not see them; they send the cashier *messages*: "transfer 60 from account 0 to account 1". The
cashier takes the requests one at a time, carries them out and replies. Since only one task touches
the data, there is no question of who else changes it, and no monitors are needed.

Messages travel over a *channel*. The `Async` library has three kinds, and they differ in whether
the sender waits:

| Kind | Creation | Sender | Receiver |
|---|---|---|---|
| rendezvous | `AsyncChan.NewChan()` | waits until the receiver takes the message | waits for a message |
| buffer of `n` places | `AsyncChan.NewBuffer(n)` | waits only if all places are taken | waits if the buffer is empty |
| mailbox | `AsyncChan.NewMailbox()` | never waits | waits if the mailbox is empty |

A *rendezvous* is like handing something from hand to hand: both sides must be at the window at the
same moment. A *buffer* is a tray for a few papers: the sender puts a paper down and leaves until the
tray is full; this way a fast sender does not run too far ahead of a slow receiver. A *mailbox*
accepts everything at once; the sender never waits, but also never learns how many letters have piled
up. A message is an extension of the record `AsyncSelect.Msg`; `Send` sends it and `Recv` receives
it. All three kinds are described in detail in sections 2–4 of the guide `Docu/Async.md`; the
examples are `ObxChan0`, `ObxChan1` and `ObxChan2`.

## 5.2. Example

The cashier's window is the mailbox `window`: a client puts a request in and does not wait for the
cashier to be free. The cashier returns the answer over the rendezvous channel `reply`, which the
client attached to the request: the client waits on it until the cashier answers. The manager waits
for all the clients and puts `NIL` into the window, a sign that the cashier's office is closing.

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

The command prints:

```
done, client 0
done, client 1
refused, client 2
balance 40
balance 60
balance 200
total 300
```

The array `balance` is a local variable of the cashier task. Another task simply cannot reach it, and
this is better than any rule "do not touch without a monitor": a rule can be broken, but here there
is nothing to break it with. The requests are carried out strictly one at a time, in the order they
arrive in the mailbox, so the balance check and the transfer are indivisible, although there is no
monitor and no transaction.

## 5.3. What is good about this and what is bad

The good part is that everything about the accounts is written in one place, in `Cashier.Run`. To
understand whether an account can go negative, it is enough to read one loop. The bad part is that
there is one cashier: while he carries out a request, the others wait. If there are many requests,
several cashiers are made, and the accounts are divided among them (the even-numbered accounts go to
the first, the odd-numbered to the second). A transfer between accounts of different cashiers again
becomes a conversation between two parties; this is the same trouble as accounts on different
machines (chapter 8).

A message costs more than a procedure call. A round trip through two rendezvous channels costs
389–483 ns, one message through a buffer of 64 places costs 117–186 ns (Linux x86-64, one thread,
under load; `docs/measurements.md`, section 2.2). For a bank request this is not noticeable; for
adding two numbers it is too expensive.

## 5.4. How to break it

If the cashier forgets to answer a request, for example by leaving the loop by mistake, the client
waits for the answer forever. When all tasks are waiting and nobody sleeps with a deadline, the
scheduler prints the same deadlock log as in chapter 3, only without monitors: `Tasks: deadlock: no
ready tasks and no wake-up deadlines`. The same log would appear in the example if the command `Do`
itself waited for the clients and only then put `NIL` into the window: after the last answer the
cashier waits for a request, there are no other tasks, and the scheduler step declares a deadlock
before the command closes the office. This is why the task `Manager` closes the office.

The second mistake is a wrong order of conversation: a client sends a "transfer" before
"introduce yourself", or sends two requests without waiting for the answer to the first. The error
shows up at the cashier, far from the place where it was made. For this reason a *protocol* can be
attached to a channel, a state machine written as a string:

```
start: Hello > talk; talk: Data > talk; talk: Bye > done; done:
```

The channel checks every message sent against the protocol and traps on a wrong step right at the
sender. How it works is described in section 6 of the guide `Docu/Async.md` and in the example
`ObxProto0`.
