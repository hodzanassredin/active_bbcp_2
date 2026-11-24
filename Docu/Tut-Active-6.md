[← Chapter 5](Tut-Active-5.md) · [Contents](Tut-Active-TOC.md) · [Chapter 7 →](Tut-Active-7.md)

# 6. Selection

## 6.1. Waiting for several things at once

The cashier from chapter 5 can do only one thing: wait for a request in the window. In real life he
has to watch three things at once. A request has come: carry it out. The manager said "we are
closing": close the office. Nobody has come for 50 ms: write to the log that the office is idle.
Which of these happens first is not known in advance.

`window.Recv(m)` waits only for the window. If the cashier falls asleep in it, he will not hear the
closing signal until the next request comes, and there may be none at all. One could poll in turn:
look into the window without waiting, look into the manager's mailbox, sleep a millisecond, repeat.
This is bad twice over. The cashier burns processor time on empty checks, and he is late: an event
that happens right after a check is noticed only one round later.

What is needed is an operation "wait until at least one of these events happens, and tell me which".
It is called *selection*. Each choice is described beforehand in a record `AsyncSelect.Op`, called an
*alternative* (hence `Alt` in the names of the procedures):

- `AsyncChan.RecvAlt(c, op)`: "receive a message from channel `c`";
- `AsyncChan.SendAlt(c, m, op)`: "send `m` into channel `c`";
- `AsyncChan.TimeoutAlt(ms, op)`: "`ms` milliseconds have passed".

Then `AsyncChan.Select(ops)` waits and returns the number of the alternative that fired. Exactly one
fired: if a receive was chosen, the message is in `ops[i].got`, and the other alternatives did
nothing. A message from another channel is not lost, and a send into another channel did not take
place.

## 6.2. A promise of a result

The client in chapter 5 brought its own rendezvous channel for the answer. The answer is needed
exactly once, and for that there is something simpler than a channel: the *single-assignment
variable* `AsyncOnce.Once`. It is filled once (`FillInt`, `FillMsg`); anyone can read it (`ReadInt`,
`ReadMsg`) any number of times, and whoever reads before it is filled waits. This is a promise: "the
result will be here". The client puts an empty single-assignment variable into the request, the
cashier fulfils the promise, and the client takes the result. Filling it a second time traps: a
promise is fulfilled once.

## 6.3. Example

The cashier waits for a request, a closing signal or 50 ms of silence. The first client arrives at
once, the second after 80 ms, and the manager closes the office after 110 ms. The account holds 100,
and both clients want 70 each.

```
MODULE ObxBank6;
	IMPORT AsyncChan, AsyncSelect, AsyncOnce, AsyncIO, Services, Tasks, Console, Strings;

	TYPE
		Request = POINTER TO RECORD (AsyncSelect.Msg)
			sum: INTEGER;
			answer: AsyncOnce.Once	(* обещание: остаток после снятия или -1 *)
		END;
		Cashier = POINTER TO RECORD (Tasks.Task) END;
		Client = POINTER TO RECORD (Tasks.Task) id, delay, sum: INTEGER END;
		Manager = POINTER TO RECORD (Tasks.Task) END;

	VAR window, closing: AsyncChan.Mailbox; done: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (c: Cashier) Run;
		VAR balance: INTEGER; ops: ARRAY 3 OF AsyncSelect.Op; r: AsyncSelect.Msg;
	BEGIN
		balance := 100;
		LOOP
			AsyncChan.RecvAlt(window, ops[0]);	(* заявка *)
			AsyncChan.RecvAlt(closing, ops[1]);	(* касса закрывается *)
			AsyncChan.TimeoutAlt(50, ops[2]);	(* 50 мс никого *)
			CASE AsyncChan.Select(ops) OF
			| 0:
				r := ops[0].got;
				WITH r: Request DO
					IF balance >= r.sum THEN
						balance := balance - r.sum; r.answer.FillInt(balance)
					ELSE
						r.answer.FillInt(-1)
					END
				END
			| 1: EXIT
			| 2: Console.WriteStr("cashier: nobody for 50 ms"); Console.WriteLn
			END
		END;
		Say("cashier: closed, balance ", balance);
		done := TRUE
	END Run;

	PROCEDURE (c: Client) Run;
		VAR r: Request; rest: INTEGER;
	BEGIN
		Tasks.Sleep(c.delay);
		NEW(r); r.sum := c.sum; r.answer := AsyncOnce.New();
		window.Send(r);
		rest := r.answer.ReadInt();	(* ждать исполнения обещания *)
		IF rest >= 0 THEN Say("client got money, left ", rest) ELSE Say("client refused, id ", c.id) END
	END Run;

	PROCEDURE (m: Manager) Run;
		VAR stop: Request;
	BEGIN
		Tasks.Sleep(110); NEW(stop); closing.Send(stop)
	END Run;

	PROCEDURE Do*;
		VAR k: Cashier; c1, c2: Client; m: Manager;
	BEGIN
		window := AsyncChan.NewMailbox(); closing := AsyncChan.NewMailbox(); done := FALSE;
		NEW(k); Tasks.Start(k);
		NEW(c1); c1.id := 1; c1.delay := 0; c1.sum := 70; Tasks.Start(c1);
		NEW(c2); c2.id := 2; c2.delay := 80; c2.sum := 70; Tasks.Start(c2);
		NEW(m); Tasks.Start(m);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END
	END Do;

END ObxBank6.
```

The command prints:

```
client got money, left 30
cashier: nobody for 50 ms
client refused, id 2
cashier: closed, balance 30
```

The first `Select` returns 0, the request of the first client. The next selection waits 50 ms and
returns 2: nobody came. After 80 ms from the start the second client arrives, and he does not have
enough money. At the 110th ms the manager puts a message into `closing`, `Select` returns 1, and the
cashier leaves the loop. The timeout is restarted at every call of `Select`: it is "50 ms since the
last event", not "every 50 ms".

The timeout of a selection is a `Services` action, not a task, and `Tasks.Step` does not run it.
This is why the command `Do` spins not `Tasks.Step` but the whole main loop
(`Services.actionHook.Step`), and between passes calls `AsyncIO.Poll(1)`, which waits for I/O events
for at most a millisecond so as not to spin idle. A cashier waiting for a timeout is waiting for an
event from outside the tasks, and the scheduler does not count such a situation as a deadlock. In a
BlackBox window the main loop does all this itself.

## 6.4. Where else selection is needed

Selection appears whenever a task serves several parties. A server waits for a new connection or a
stop command. A client waits for an answer or a timeout, after which it will ask another server
(`ObxSelect0`). An intermediary task waits at the same time for a message from a producer and a free
place at a consumer: a selection of a receive and a send. The result of a heavy computation in another
process can also be waited for in a selection next to channels: for that there is
`AsyncComputon.AltPull` (chapter 7).

Your own event can also be made an alternative: extend `AsyncSelect.Signal` and pass it to
`AsyncSelect.AltSignal`. This is needed rarely; the design is described in section 5 of the guide
`Docu/Async.md`.

## 6.5. Violations and cost

The order of the alternatives matters when several are ready at once. `Select` checks them in
ascending order of number, and the first one that can run immediately wins; a timeout is ready only
when nothing else is. This is why the cashier puts the window in place 0 and the closing in place 1:
if a request lies in the window and a signal has arrived at the same time, the request is served
first. Put the closing first, and the requests that arrive together with it stay unserved in the
window. More than 32 alternatives in one selection are not allowed (trap 21), a selection outside a
task traps with 20; the other numbers are in section 5 of the guide `Docu/Async.md`.

A selection between two rendezvous channels costs 315–400 ns, between eight 568–653 ns (Linux
x86-64, one thread, under load; `docs/measurements.md`, section 2.2): more than one `Recv`, but
cheaper than any polling.

This chapter ends part I. Everything in it works on one thread: the tasks share it among
themselves and never run at the same time. Part II is about what to do when one thread is not enough.
