# Async Library

This is a guide to the `Async` subsystem. It is written for a programmer who knows Component Pascal
and has already read the guide to tasks (`Docu/Tasks.md`). The language part — exclusive procedures,
`ACTIVE.AWAIT` and yield points — is defined in the language report (`Docu/Active-CP-Lang.md`).

The library is written on the language extension and does not change the framework: framework
modules are compiled without the yield key and do not wait. The subsystem is built for AMD64;
tasks, monitors and `AsyncStm` transactions also work on i386. Every complete example of the guide lives in `Obx/Mod64` and
runs with the command `Do`; each has its own page in `Obx/Docu`.

## 1. From Task to Channel

Two tasks share a job: one reads lines from a file, the other parses them. The first needs somewhere
to put what it has read, the second somewhere to take it from. With monitors it is done like this:
one declares a record with an array and a counter, makes the procedures `Put` and `Get` exclusive,
waits `ACTIVE.AWAIT(n > 0)` in `Get` and `ACTIVE.AWAIT(n < LEN(buf))` in `Put`. That is a couple of
dozen lines, and they are written anew for every pair of tasks that must pass something along.

These lines are almost always the same. Only the type of what is passed and the size of the reserve
change. So the library provides them ready-made under the name *channel*: an object with the methods
`Send` and `Recv`. The sending task does not know who will receive; the receiving task does not know
who sent. Both know only the channel. The connection between tasks thus becomes visible in one
place — where the channel is created and handed out — instead of being spread over `AWAIT`
conditions in different modules.

A monitor is still needed when tasks share state that they change in place: a table, a counter, a
tree. A channel is needed when tasks pass values to each other. The difference shows in the question
the programmer asks. "Who may change this table now?" is a question for a monitor. "To whom do I
give this result?" is a question for a channel.

A channel carries a *message* — a pointer to a record that extends `AsyncSelect.Msg`:

```
TYPE Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
```

The channel passes the pointer itself and does not copy the record. So after `Send` the sender no
longer changes the record: the receiver sees the same object, and a change made after sending reaches
it at an unpredictable moment. The compiler does not check this, nor does the library. The receiver
learns the kind of a message with the type guard `WITH m: Num DO … END`; the field `n` is available
only inside the guard.

Channels come in three kinds. They differ in how many messages the channel holds, and therefore in
when the sender waits:

| Kind | Constructor | Holds | `Send` waits |
|---|---|---|---|
| rendezvous `Chan` | `AsyncChan.NewChan()` | nothing | until a receiver takes the message |
| buffer `Buffer` | `AsyncChan.NewBuffer(n)` | up to `n` | while the buffer is full |
| mailbox `Mailbox` | `AsyncChan.NewMailbox()` | any number | never |

The next three sections take them in turn.

## 2. Rendezvous

A producer task computes numbers, a consumer task adds them up. If the consumer lags, the producer
should not run ahead and pile numbers up in memory: let it wait. If the producer lags, the consumer
waits. Each number passes from hand to hand at the moment both tasks are ready. Such a transfer is
called a *rendezvous*.

A rendezvous is the channel `AsyncChan.Chan`. It holds no message at all. The task that comes to the
channel first waits for the second: `Send` waits for a receiver, `Recv` for a sender. The second task
takes the message from the first (or gives it its own) and wakes it. So after returning from `Send`
the sender knows more than "the message lies somewhere": it knows that the receiver has already taken
it. Several waiting senders are served in order of arrival.

```
MODULE ObxChan0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Sender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;
		Receiver = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (s: Sender) Run;
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO NEW(m); m.n := i; s.c.Send(m) END;
		s.c.Send(NIL)
	END Run;

	PROCEDURE (r: Receiver) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			r.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO r.sum := r.sum + m.n END
		END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: AsyncChan.Chan; s: Sender; r: Receiver; d: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(s); s.c := c; NEW(r); r.c := c; done := FALSE;
		Tasks.Start(s); Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(r.sum, d);
		Console.WriteStr("ObxChan0: sum="); Console.WriteStr(d); Console.WriteLn
	END Do;

END ObxChan0.
```

The command `ObxChan0.Do` prints `ObxChan0: sum=15`. The sender ends the stream with the message
`NIL`, and the receiver leaves its loop on it. The command itself runs on the main stack and cannot
wait; it spins `Tasks.Step` until the receiver sets `done`. In windowed BlackBox the same loop is
spun by the framework's main loop, and the command returns right after `Tasks.Start`.

Only a task may wait. If the command itself calls `c.Recv(m)` and the channel has no sender, `Recv`
reaches `Tasks.Park` and traps with code 20: on the main stack there is nobody to yield to. A
framework action handler (`Services.Action`) and a view message broadcast cannot wait either; there
the same `Recv` traps with code 21. If, however, a sender is already waiting at the channel, `Recv`
from the main stack takes its message without waiting, and there is no trap.

The second violation is a lost partner. If the sending of `NIL` is removed from the example, the
receiver stays in `Recv` forever. There are no other tasks and no wake-up deadlines, and the scheduler
prints `Tasks: deadlock: no ready tasks and no wake-up deadlines` to the console together with a list
of monitors. The command `Do` then never returns, because nobody sets `done`.

A rendezvous costs two stack switches per message: the task goes to the scheduler, the scheduler
goes on to the other task. The measurement `pingpong` (`docs/measurements.md`, section 2.2) — a
round trip through two rendezvous channels — gives 389–483 ns per exchange with a machine load of
about 15, twice as slow as single-threaded Go. A rendezvous allocates no memory: the waiting side
takes its wait record from a free list.

## 3. Buffer

A rendezvous makes the producer wait for each number, even when the consumer lags only for a moment.
If the speeds of the tasks fluctuate, it is better to give them a small reserve: the producer runs a
few messages ahead, the consumer catches up later. The reserve is bounded so that a fast producer
does not take all the memory.

The buffer `AsyncChan.Buffer` holds up to `capacity` messages in sending order. `Send` into a buffer
that is not full puts the message and returns at once; `Send` into a full one waits until the
consumer frees a place. `Recv` from a non-empty buffer takes the first message; from an empty one it
waits. The field `count` shows how many messages lie in the buffer now.

```
MODULE ObxChan1;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Producer = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; max: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (p: Producer) Run;
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO
			NEW(m); m.n := i; p.c.Send(m);
			IF p.c.count > p.max THEN p.max := p.c.count END
		END;
		p.c.Send(NIL)
	END Run;

	PROCEDURE (k: Consumer) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			k.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO k.sum := k.sum + m.n END;
			Tasks.Sleep(1)	(* потребитель медленнее производителя *)
		END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: AsyncChan.Buffer; p: Producer; k: Consumer; s: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewBuffer(4);
		NEW(p); p.c := c; NEW(k); k.c := c; done := FALSE;
		Tasks.Start(p); Tasks.Start(k);
		WHILE ~done DO Tasks.Step END;
		Console.WriteStr("ObxChan1: sum="); Strings.IntToString(k.sum, s); Console.WriteStr(s);
		Console.WriteStr(" max="); Strings.IntToString(p.max, s); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan1.
```

The command prints `ObxChan1: sum=55 max=4`. The consumer sleeps a millisecond after each message,
the producer is faster, and the buffer fills up to its capacity. It never holds more than four
messages: the fifth `Send` waits.

The capacity must be positive. `NewBuffer(0)` traps with code 20: a buffer without room would be a
rendezvous, and the rendezvous is a separate kind of channel. The library sets no upper bound on the
capacity.

A buffer is cheaper than a rendezvous when it is neither empty nor full: sending and receiving then
do without a switch. In the measurement `buffer64` (a buffer of 64 messages) there are 0.06 switches
per message and 117–186 ns per message; almost half of this time is the `NEW` of the message in the
measurement itself.

## 4. Mailbox

Some senders may not wait at all. A framework action handler, a menu command, a procedure called from
a message broadcast run on the main stack, and any wait there is a trap. Such a sender needs a
channel whose `Send` never waits.

The mailbox `AsyncChan.Mailbox` is unbounded: it holds any number of messages, and `Send` into it
returns at once. `Recv` waits while the mailbox is empty. Messages come out in sending order.

```
MODULE ObxChan2;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Event = POINTER TO RECORD (AsyncSelect.Msg) code: INTEGER END;
		Handler = POINTER TO RECORD (Tasks.Task) n: INTEGER END;

	VAR box: AsyncChan.Mailbox; done: BOOLEAN;

	PROCEDURE Post (code: INTEGER);
	(* Вызывается с главного стека: ящик не заставляет ждать. *)
		VAR e: Event;
	BEGIN
		NEW(e); e.code := code; box.Send(e)
	END Post;

	PROCEDURE (h: Handler) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			box.Recv(m);
			WITH m: Event DO
				IF m.code = 0 THEN EXIT END;
				INC(h.n)
			END
		END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR h: Handler; s: ARRAY 16 OF CHAR;
	BEGIN
		box := AsyncChan.NewMailbox();
		Post(1); Post(2); Post(3); Post(0);
		NEW(h); done := FALSE; Tasks.Start(h);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(h.n, s);
		Console.WriteStr("ObxChan2: events="); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan2.
```

The command prints `ObxChan2: events=3`. The procedure `Post` is called from the main stack before
the handler task has started: the mailbox accepts all four messages and waits for nothing. The task
then takes them out one by one and leaves on the event with code 0.

There is one exception to the rule "`Send` into a mailbox does not wait". If a task has taken the
monitor of the channel itself (`Kernel.Lock(box)` or an exclusive procedure of a channel extension),
operations on the channel wait until it releases it. From the main stack such a wait is monitor
trap 21.

The price of being unbounded is memory. If the consumer lags forever, the mailbox grows without
limit: the message ring doubles when full and never shrinks back. A mailbox suits event streams that
have a natural limit — key presses, replies to one's own requests — and not pumping data between
tasks of different speed. That is what the buffer is for.

## 5. Selection

A client task sends a request to a server and waits for the reply. The server may hang, and the
client does not want to wait forever: after 50 ms it gives up and does something else. Neither
`Recv` nor `Sleep` fits here: `Recv` waits without a deadline, and `Sleep` does not notice the reply.
One has to wait for two events at once and learn which happened first.

This is done by *selection* — the procedure `AsyncChan.Select`. It takes an array of *alternatives*
of type `AsyncSelect.Op`, waits until exactly one of them executes, and returns its index. An
alternative is built by the procedures:

```
AsyncChan.RecvAlt(c, op)        (* receive from channel c *)
AsyncChan.SendAlt(c, m, op)     (* send m to channel c *)
AsyncChan.TimeoutAlt(ms, op)    (* expire after ms ms *)
AsyncSelect.AltSignal(sig, op)  (* wait for an external signal *)
```

A receive alternative that wins leaves the message in the field `got`; the other alternatives are
not executed at all. A send that lost the selection has not sent its message.

```
MODULE ObxSelect0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Services, AsyncIO, Console;

	TYPE
		Reply = POINTER TO RECORD (AsyncSelect.Msg) END;
		Server = POINTER TO RECORD (Tasks.Task) delay: INTEGER; out: AsyncChan.Mailbox END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR fast, slow: ARRAY 16 OF CHAR; done: BOOLEAN;

	PROCEDURE (s: Server) Run;
		VAR r: Reply;
	BEGIN
		Tasks.Sleep(s.delay); NEW(r); s.out.Send(r)
	END Run;

	PROCEDURE Ask (delay: INTEGER; OUT res: ARRAY OF CHAR);
		VAR s: Server; ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		NEW(s); s.delay := delay; s.out := AsyncChan.NewMailbox(); Tasks.Start(s);
		AsyncChan.RecvAlt(s.out, ops[0]);
		AsyncChan.TimeoutAlt(50, ops[1]);
		IF AsyncChan.Select(ops) = 0 THEN res := "reply" ELSE res := "timeout" END
	END Ask;

	PROCEDURE (c: Client) Run;
	BEGIN
		Ask(10, fast); Ask(200, slow); done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: Client;
	BEGIN
		NEW(c); done := FALSE; Tasks.Start(c);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxSelect0: fast="); Console.WriteStr(fast);
		Console.WriteStr(" slow="); Console.WriteStr(slow); Console.WriteLn
	END Do;

END ObxSelect0.
```

The command prints `ObxSelect0: fast=reply slow=timeout`. The first server replies after 10 ms, and
the receive wins. The second replies after 200 ms, and the timeout expires first. The server replies
into a mailbox rather than a rendezvous, and there is a reason for it: a late reply lands in the
mailbox, and the server finishes. Had it replied into a rendezvous, it would have stayed forever
waiting for a receiver that is gone.

In this example the command spins not `Tasks.Step` but `Services.actionHook.Step` and
`AsyncIO.Poll(1)`. The timeout of a selection is a `Services` action, and `Tasks.Step` does not
execute it; without the action step the deadline would pass unnoticed, and the second selection would
also wait for the reply. BlackBox's main loop does both steps itself; commands with timeouts, worker
threads and I/O have to repeat it.

The order decides when several alternatives are ready at once. `Select` checks them in ascending
index order, and the first that can execute immediately wins. A timeout is ready only when nothing
else is: a timeout with `ms <= 0` turns the selection into a poll without waiting. Of several
timeouts the smallest acts; among equal ones, the first by index.

Violations of selection are caught before it does anything. A call outside a task traps with code
20; more than 32 alternatives, with code 21; an alternative of an invalid kind, with code 22; a
receive or send without a channel, with code 23; a `NIL` signal, with code 24. An unfilled
alternative with a zero kind field looks like a send without a channel and gives trap 23.

Cancelling a task that waits in a selection traps it with code 127 in `Tasks.Park`. Its offers stay
in the channels until the next access to them: a channel considers an offer dead when its task has
reached `Finished` or `Cancelled`, and skips it.

Selection among two rendezvous channels costs 315–400 ns per operation, among eight 568–653 ns
(`select2`, `select8`, `docs/measurements.md`, section 2.2). Selection without a timeout allocates no
memory.

## 6. Dialogue Protocol

A client and a server talk over a channel: the client says hello, sends data, says goodbye. If the
client by mistake sends data before the greeting, the server gets what it does not expect, and the
error shows far from where it was made. It is better to stop a wrong message at the moment it is
sent.

For this a *protocol* is attached to the channel — a state machine written as text:

```
start: Hello > talk; talk: Data > talk; talk: Bye > done; done:
```

The item `state: Message > state` permits sending the message from the state and names the next
state. The arrow may be written `>` or `->`; items are separated by `;` or a line break. The first
state mentioned is the initial one. The item `done:` without an arrow declares a state with no
transitions; such a state is final. A message is named by the name of its record type, as in the
declaration `TYPE Hello = POINTER TO RECORD …`; `NIL` is named `NIL`.

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

The command prints `ObxProto0: state=done`: the four sends went through the machine from `start` to
`done`. A mailbox is chosen here so that the command can send from the main stack.

A violation looks like this. If `NEW(d); box.Send(d)` is inserted into the example before the
greeting, the console shows the line `AsyncChan protocol: …` with the text of the violation and the
expected messages, and `Send` traps with code 21. The message is not delivered, and the protocol
state stays as it was; the violation text is kept by `AsyncProto.LastViolation`. The trap always goes
to the sender. If the sender was waiting in a rendezvous and its message is taken by a receiver, the
sender traps when it wakes, and the receiver goes on waiting for the next one.

The protocol checks a message at the moment the send takes place: on entry to `Send` if a receiver
waits or there is room; when a receiver takes a waiting send; when room frees in a buffer. A send that
lost a selection does not move the protocol. `AsyncChan.SetProtocol` resets the state to the initial
one; `SetProtocol(c, NIL)` removes the protocol. A channel without a protocol pays for the check with
one comparison with `NIL`.

The description has limits: up to 32 states and 128 transitions, a name of up to 31 characters. An
error in the description (an item without `:`, a long name, two transitions from one state on one
message) makes `AsyncProto.New` return `NIL`, and the error text comes from `AsyncProto.LastError`.
The machine is deterministic; the notation has no repetitions `*`, `+` and no choice `|` — they are
expressed with states.

## 7. Single-Assignment Variable

A task starts a long computation and hands several other tasks a promise of the result. Each of them
reads the result when it needs it; if it is not ready yet, it waits. A channel does not fit here: a
message from a channel goes to one receiver, and the result is needed by all.

A *single-assignment variable* `AsyncOnce.Once` is filled once and read any number of times.
`FillInt(x)` or `FillMsg(m)` fill it; `ReadInt()` or `ReadMsg(m)` wait until it is filled and return
the value. The variable is built as a monitor: all four methods are exclusive procedures, and
reading waits in `ACTIVE.AWAIT(o.done)`.

```
MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	TYPE
		Reader = POINTER TO RECORD (Tasks.Task) END;
		Writer = POINTER TO RECORD (Tasks.Task) END;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN;

	PROCEDURE (r: Reader) Run;
	BEGIN
		got := o.ReadInt(); done := TRUE	(* ждёт, пока Writer не заполнит *)
	END Run;

	PROCEDURE (w: Writer) Run;
	BEGIN
		Tasks.Sleep(5); o.FillInt(42)
	END Run;

	PROCEDURE Do*;
		VAR r: Reader; w: Writer; s: ARRAY 16 OF CHAR;
	BEGIN
		o := AsyncOnce.New();
		NEW(r); NEW(w); done := FALSE;
		Tasks.Start(r); Tasks.Start(w);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(got, s);
		Console.WriteStr("ObxOnce: "); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxOnce.
```

The command prints `ObxOnce: 42`. The reader comes first and waits; after 5 ms the writer fills the
variable, and the reader wakes up.

A second fill is an error. `FillInt` on a filled variable traps with code 21, `FillMsg` with code 22.
The variable has one fill flag for both kinds of value, so `FillMsg` after `FillInt` is a second fill
too. The kind is not checked on reading: `ReadInt` after `FillMsg` returns 0, `ReadMsg` after
`FillInt` returns `NIL`.

Filling does not wait and can be called from the main stack while the monitor of the variable is
free. Reading an unfilled variable from the main stack is monitor trap 24: `AWAIT` outside a task.
Reading has no deadline; if a deadline is needed, the result is passed over a channel and awaited in
a selection with a timeout.

## 8. Transactions

A task transfers money from account `a` to account `b`: it withdraws the sum from `a`, then
deposits it to `b`. There is no switch point between the two actions, so other tasks do not see
half a transfer. But a trap can happen between them — say, account `b` accepts no more than 150,
and the limit check traps. The task dies, the sum is already withdrawn from `a` and deposited
nowhere: the money is lost. A monitor does not help here: it excludes other tasks, and the trap
happens in this one.

A second task waits until the buyer's account holds the price and the shop opens. The account and
the "open" flag are different objects with their own monitors. `ACTIVE.AWAIT` waits for a condition
inside one monitor, and a condition over two objects cannot be expressed with it without merging
both objects into one monitor.

Both problems are solved by a *transaction* — the module `AsyncStm`. A transaction is an extension
of `AsyncStm.Tx` with a method `Do`; the data of the transaction lie in the fields of the object,
as the data of a task lie in the fields of `Tasks.Task`. `AsyncStm.Atomically(t)` executes `t.Do`
so that the other tasks see either all of its writes or none. A transaction writes to
*transactional variables*: `IntVar`, `BoolVar`, `RealVar`, `RefVar` (`ANYPTR`) or an own extension
of `Var`. The first write to a variable inside a transaction puts its old value into the undo log,
then changes the value in place. If `Do` traps, `Atomically` restores the values from the log and
passes the trap to the caller with the same number. The transfer example:

```
MODULE ObxStm0;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST limit = 150;	(* больше этого счёт b не принимает *)

	TYPE
		Transfer = POINTER TO RECORD (AsyncStm.Tx)
			from, to: AsyncStm.IntVar; n: INTEGER
		END;
		Clerk = POINTER TO RECORD (Tasks.Task) END;

	VAR a, b: AsyncStm.IntVar;

	PROCEDURE (t: Transfer) Do;
	BEGIN
		t.from.Set(t.from.Get() - t.n);	(* снять *)
		ASSERT(t.to.Get() + t.n <= limit, 30);	(* трап между «снять» и «зачислить» *)
		t.to.Set(t.to.Get() + t.n)	(* зачислить *)
	END Do;

	PROCEDURE Move (n: INTEGER);
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := a; t.to := b; t.n := n;
		AsyncStm.Atomically(t)
	END Move;

	PROCEDURE (c: Clerk) Run;
	BEGIN
		Move(100); Move(100)
	END Run;

	PROCEDURE Show;
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr("a = "); Strings.IntToString(a.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", b = "); Strings.IntToString(b.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", sum = "); Strings.IntToString(a.Get() + b.Get(), s); Console.WriteStr(s);
		Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR c: Clerk;
	BEGIN
		a := AsyncStm.NewInt(300); b := AsyncStm.NewInt(0);
		NEW(c); Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.Failed(c) THEN Console.WriteStr("clerk trapped") ELSE Console.WriteStr("clerk done") END;
		Console.WriteLn;
		Show
	END Do;

END ObxStm0.
```

The command prints `clerk trapped` and `a = 200, b = 100, sum = 300`. The first transfer succeeded,
the second trapped with code 30 after the withdrawal; `Atomically` restored 200 to `a`, and the sum
did not change. A step-by-step walk-through is on the page `Obx/Docu/Stm0.md`.

The undo log is chosen over deferred writes because of the cost of a read. With deferred writes the
new value stays in the log until the end of the transaction, and every read first looks the variable
up in the log, so that the transaction sees its own write. With an undo log the value is always in
the variable itself, and a read costs one comparison. Deferred writes are needed where another
thread may read the variable in the middle of a transaction; here nobody reads it, because there are
no switch points inside a transaction. For the same reason transactions have nothing to collide
on, and no version check at commit is needed.

Waiting for a condition is given by `AsyncStm.Retry`. A transaction calls it when it is too early
to go on. `Retry` rolls the transaction back, and `Atomically` puts the task to sleep until one of
the variables the transaction has read changes; then `Do` is executed again. Every `Get` adds the
variable to the read set, so the condition may cover any number of variables of different objects:

```
MODULE ObxStm1;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST price = 250;

	TYPE
		Buy = POINTER TO RECORD (AsyncStm.Tx) tries: INTEGER END;
		Buyer = POINTER TO RECORD (Tasks.Task) END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;
		Opener = POINTER TO RECORD (Tasks.Task) END;

	VAR
		money: AsyncStm.IntVar;	(* счёт покупателя *)
		open: AsyncStm.BoolVar;	(* магазин открыт *)

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (t: Buy) Do;
	BEGIN
		INC(t.tries);	(* обычное поле: откат его не трогает *)
		IF (money.Get() < price) OR ~open.Get() THEN AsyncStm.Retry END;
		money.Set(money.Get() - price)
	END Do;

	PROCEDURE (b: Buyer) Run;
		VAR t: Buy;
	BEGIN
		NEW(t); t.tries := 0;
		AsyncStm.Atomically(t);
		Say("bought, money = ", money.Get());
		Say("transaction ran times: ", t.tries)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10);
		money.Set(money.Get() + 200);
		Say("salary paid, money = ", money.Get())
	END Run;

	PROCEDURE (o: Opener) Run;
	BEGIN
		Tasks.Sleep(20);
		open.Set(TRUE);
		Console.WriteStr("shop opened"); Console.WriteLn
	END Run;

	PROCEDURE Do*;
		VAR b: Buyer; p: Payroll; o: Opener;
	BEGIN
		money := AsyncStm.NewInt(100); open := AsyncStm.NewBool(FALSE);
		NEW(b); NEW(p); NEW(o);
		Tasks.Start(b); Tasks.Start(p); Tasks.Start(o);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxStm1.
```

The buyer wakes up twice: after the salary (300, the shop is closed — `Retry` again) and after the
opening. The field `tries` shows that `Do` ran three times; it is an ordinary field, and rollback
does not touch it. A write outside a transaction, like `money.Set` in the task `Payroll` here, is an
ordinary write to the variable that wakes the transactions waiting for it. Forbidding it would gain
nothing: between switch points such a write is indivisible anyway.

Two ready transactions compose into one without opening them. A call of `Atomically` inside a
transaction is part of the outer transaction: a transfer `Move(a, b)` and a transfer `Move(b, c)`
called from one `Do` execute as one whole, and a trap in the second rolls back the first as well.
`AsyncStm.OrElse(x, y)` executes `x`; if `x` called `Retry`, `OrElse` rolls back the writes of `x`
and executes `y`. If both called `Retry`, the whole transaction waits — until a variable read in
either branch changes. The example "pay with the first card, otherwise with the second" is
`ObxStm2`, page `Obx/Docu/Stm2.md`.

The first violation: a switch point inside a transaction. If `Do` calls `Tasks.Yield`,
`Tasks.Sleep`, waits in a channel, enters a busy monitor or `AWAIT`, another task would see the
writes of an unfinished transaction. For the time of `Do`, `Atomically` opens the atomic section
`Tasks.BeginAtomic`; a wait inside it traps 25 with the text `Tasks: wait inside an atomic section`,
a call of `Tasks.Step` traps 22. The writes are rolled back, trap 25 goes to the caller. A yield
point `^` inside a transaction does not switch, as inside a monitor. Entering a free monitor is not
a switch point, and it is allowed.

The second violation: a `Retry` with nothing to wait for. A transaction that has read no variable
would wait forever; `Atomically` traps 21 with the text `AsyncStm: Retry with nothing read`. Only a
task can wait: `Retry` in a transaction on the main stack traps 22, `Retry` outside a transaction
traps 20. If all tasks wait in `Retry` and there are no sleep deadlines, the scheduler detects the
deadlock the same way as for monitors: it prints `Tasks: deadlock` and raises `Tasks.Deadlocked()`.

The third violation: an action that cannot be rolled back. Rollback restores only transactional
variables. Console output, a file write, a send to a channel or a mailbox, `Unpark`, ordinary fields
of objects stay done, and with `Retry` they repeat on every execution of `Do`. The rule: inside a
transaction read and write only transactional variables, and do an external action after
`Atomically`, from the result that `Do` left in the fields of the object. The compiler does not
check this; a waiting channel operation traps 25, a non-waiting one does not.

An own variable is an extension of `AsyncStm.Var`. Its `Get` calls `AsyncStm.Read(v)` before the
read, its `Set` calls `AsyncStm.Write(v)` before the write; the methods `Save` and `Load` copy the
value into the record `AsyncStm.Saved` (fields `i`, `x`, `p`) and back. A record value is put into
`p` as a copy allocated in `Save`.

Cost. A transfer "two reads, two writes" as a transaction costs about 80 ns on AMD64, the same
transfer through `IntVar` without a transaction about 20 ns, by an exclusive procedure over two
fields about 65 ns, by direct writes to fields less than 1 ns (one thread, a million operations, a
loaded machine; `docs/measurements.md`, section 11). A transaction is dearer than a monitor by
`Kernel.Try` and the log; it pays off by rollback and by waiting on several objects, not by speed.
A waiting task is registered only at the variables it has read and is removed from them on wake-up
or cancellation; the log lives until the end of the transaction. A trap with a number greater than
128 (a run-time error) reaches the caller as trap 128: `HALT` accepts numbers 0–128.

Limits. A transaction is indivisible inside one BlackBox process, because it relies on one thread
without preemption. Between BlackBox processes, between computons and between actors on different
machines there is none: there a transfer is made by exchanging messages by the scheme "withdraw,
deposit, give back on refusal", and the programmer writes the give-back.

## 9. Non-Blocking I/O

A server on BlackBox serves a hundred connections, one task per connection. A task reads a request
from a socket. If it calls an ordinary blocking `read`, the whole thread stops, and with it the main
loop and the other ninety-nine tasks. What is needed is a read that waits for data the way `Recv`
waits for a message: giving the processor to others.

The module `AsyncIO` provides such operations on top of the non-blocking streams of `CommStreams`:

```
AsyncIO.Read(s, buf, beg, len, read)      (* read at least a byte *)
AsyncIO.Write(s, buf, beg, len, written)  (* write at least a byte *)
AsyncIO.Accept(l, s, timeout)             (* accept a connection *)
AsyncIO.WaitFd(fd, events)                (* wait until a descriptor is ready *)
```

`Read` tries to read. If there is no data, the task records the descriptor of the stream in the
registry `Poller` and falls asleep in `Tasks.Park`. The main loop checks standard input and all
recorded descriptors with one call of `poll`; readiness wakes the task, and it tries again. `Read`
returns as soon as at least a byte has been read (`read > 0`); `read = 0` means end of stream or a
broken connection. `Write` works the same way. A registration is one-shot: after readiness the record
is removed, and if one byte was not enough, the task registers again.

`Accept` with a negative `timeout` waits for a connection without a deadline. With a non-negative one
it waits no longer than `timeout` ms and returns `FALSE` if no connection came; the task waits for
readiness in `Tasks.Sleep` for the rest of the term.

Readiness for reading is learned from a descriptor, and the descriptor is given by the stream method
`Handle`. By default it returns −1, and only `CommTCP` for Linux overrides it. Without a descriptor
the wait degenerates into `Tasks.Sleep(1)` between attempts: the main loop does not stop, but the
task polls the stream every millisecond. So on other platforms `AsyncIO` works but wastes processor
time.

`WaitFd` from the main stack traps with code 20. The registry holds up to 4096 waits; on overflow the
waiter gets readiness at once and repeats its attempt. The registry is built on `poll` rather than
`select`, because `fd_set` is limited to 1024 descriptors. `CommTCP` for Linux works over IPv4 with
an accept queue of 128 connections.

## 10. Worker Threads

Not every system call can avoid blocking. Resolving a host name (`getaddrinfo`), reading a file from a
slow disk, calling an external library stop the thread, and they have no non-blocking variant. Such a
call is moved to a separate OS thread — a *worker thread* — and its completion is awaited the way a
message is awaited.

The pool `AsyncWork` keeps up to four worker threads. `Execute(proc, arg)` gives the procedure `proc`
with the argument `arg` to a free thread, puts the task to sleep, and returns the status when the
thread finishes. The task waits; the other tasks and the main loop work. The ready-made jobs are
`ReadFileJob`, `WriteFileJob` and `ResolveJob`; their argument is the address of a `FileReq` or
`AddrReq` record.

```
MODULE ObxWork0;
	IMPORT S := SYSTEM, AsyncWork, Tasks, Services, AsyncIO, Console, Strings;

	TYPE Asker = POINTER TO RECORD (Tasks.Task) END;

	VAR req: AsyncWork.AddrReq; status: INTEGER; done: BOOLEAN;

	PROCEDURE (a: Asker) Run;
	BEGIN
		req.host := "localhost"; req.count := 0; req.status := -1;
		status := AsyncWork.Execute(S.ADR(AsyncWork.ResolveJob), S.ADR(req));
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR a: Asker; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(a); done := FALSE; Tasks.Start(a);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxWork0: status="); Strings.IntToString(req.status, s); Console.WriteStr(s);
		Console.WriteLn
	END Do;

END ObxWork0.
```

The command prints:

```
ObxWork0: status=0
```

While the worker thread resolves the name, the task waits in `Tasks.ParkExternal`: the wake-up will
come from the worker thread through `Poller`, not from another task. The scheduler does not take this
wait for a deadlock, and no report is printed.

The record `req` is a global variable of the module, not a heap object, and this is mandatory. A
worker thread runs code outside the scheduler and outside the garbage collector. It must not touch the
heap: call `NEW`, `Kernel.Try`, the log, framework procedures. The heap is not protected by a lock:
allocation in a worker thread corrupts the free-block lists, and the collector may free an object the
thread is still writing to. The compiler does not check this, nor does the library; a violation shows
up as memory corruption at a random moment.

`Execute` is a switch point; outside a task it traps with code 21. For jobs whose result is not needed
at once there are `Submit`, `Done`, `Result` and `Release`: they do not wait, but after `Submit` the
slot stays busy until `Release` frees it. There are 64 slots; without `Release` they run out, and
later `Submit` and `Execute` return −1.

The pool is installed on the first `Execute` or `Submit`: `AsyncWork` looks for the module
`AsyncWorkLin`, then `AsyncWorkWin`. The pool exists on Linux AMD64 and Windows x64. On Linux
readiness arrives as a byte in an OS pipe, which `Poller` sees. On Windows readiness is picked up by a
`Services` action that polls a counter every 5 ms; blocking console input stops such an action, and
with it the delivery of readiness.

Job statuses: `ReadFileJob` returns −30 if the read gave no byte; `WriteFileJob` −20 on a short
write; `ResolveJob` −1 on an error. A path is up to 255 bytes, a file buffer 4096 bytes, a host name
up to 127 bytes. A job makes one read or write call. `ResolveJob` counts the addresses but does not
return them. The test `Async_devWorkers` (`tools/test.sh Async_devWorkers`) gives a worker thread a
300 ms job; the task waits for it about 301 ms, and the main loop manages 286 steps meanwhile.

## 11. Computons

A computation splits into independent pieces, and the machine has many cores. BlackBox tasks run on
one thread, and worker threads must not touch the heap. To compute Component Pascal code with `NEW`
on several cores, it is run in separate BlackBox processes — each with its own heap and its own
collector.

A *computon* is an object that is sent to such a *worker* process, executed there and returned. Its
type extends `AsyncComputon.Computon` — a `Stores.Store` record with the abstract method `Do`. The
type implements `Externalize` and `Internalize`, because the object travels in serialized form.

### 11.1. Request and Reply

`AsyncComputon.Start(n)` starts `n` workers, from 1 to 16. `Push(c, rank, tag)` serializes the
computon `c` and sends it to the worker number `rank`; `tag` is the request number by which the reply
is later collected. `Pull(rank, tag)` waits for the reply and returns the computon after `Do`. `Stop`
shuts the workers down.

```
MODULE ObxComputon;
	IMPORT AsyncComputon, Stores, Tasks, Services, AsyncIO, Console, Strings;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR done: BOOLEAN; d: ARRAY 16 OF CHAR;

	PROCEDURE (c: Square) Do*;
	BEGIN
		c.y := c.x * c.x
	END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(c.x); wr.WriteInt(c.y)
	END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(c.x); rd.ReadInt(c.y)
	END Internalize;

	PROCEDURE (cl: Client) Run;
		VAR c: Square; r: AsyncComputon.Computon;
	BEGIN
		NEW(c); c.x := 12; c.y := 0;
		AsyncComputon.Push(c, 0, 1);
		r := AsyncComputon.Pull(0, 1);
		IF r # NIL THEN
			WITH r: Square DO Strings.IntToString(r.y, d) END
		ELSE
			Strings.IntToString(AsyncComputon.LastError(), d)
		END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR cl: Client;
	BEGIN
		AsyncComputon.Start(1);
		NEW(cl); done := FALSE; Tasks.Start(cl);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxComputon: "); Console.WriteStr(d); Console.WriteLn;
		AsyncComputon.Stop
	END Do;

END ObxComputon.
```

The command prints `ObxComputon: 144`. The worker received the computon with `x = 12`, called `Do`
and returned it with `y = 144`.

A reply is matched to a request by `tag` only. `rank` in `Pull` serves only the message to the error
handler; two simultaneous requests with the same `tag` get mixed up. Each `Push` also gets its own
frame number on the wire. So a late reply to a request abandoned on timeout does not go to a new
request with the same `tag`.

`Pull` waits no longer than 10 s (`PullTimeout` takes its own term) and waits in `Tasks.Sleep(10)`,
so the task sleeps with a deadline and the scheduler does not declare a deadlock. Outside a task
`Pull` traps with code 21. On timeout it returns `NIL`, and `LastError()` returns −4. `Try` does not
wait: it returns `TRUE` if the reply is there, together with the reply itself or `NIL` with an error
code.

`AltPull(rank, tag)` gives a signal for selection. With it a task waits for a computon reply together
with channel messages and a timeout. The selection does not take the reply itself: after `AltPull`
wins, `Pull` or `Try` collects it.

`LastError` codes: −1 — no such request, empty serialization or no free request slot (there are
128); −2 — no live workers left; −3 — sending failed; −4 — the term expired; −5 — the reply could not
be parsed; −6 — the result does not serialize; 1 — a trap in `Do`; 20 — the module is busy on
unloading. `Push` with a negative `tag` traps with code 20 (negative numbers are reserved), with
`rank` outside `0 .. Size() - 1` with code 21.

### 11.2. Death of a Worker

A worker is a separate process, and it may die: a trap in kernel code, lack of memory, `kill`. The
controller sees the death as end of file in the reply pipe. It repeats all unfinished requests of the
dead worker on the next live worker. A repeat is possible after every further death; there is no
attempt counter. If no live workers remain, the request completes with code −2, and the handler
`Hook.Failed`, if installed, learns of it.

This gives "at least once" semantics. A worker may have computed `Do` and died before replying; then
`Do` runs once more on another. So `Do` must have no effects outside its object — writing files,
sending messages over the network — or those effects must survive a repeat.

A trap in `Do` itself does not kill the worker: the worker catches it and replies with code 1. `Stop`
kills the workers with `SIGKILL` and closes the pipes; it completes hanging requests with code −2,
and the `Pull`, `Try` and selection waiting for them see them completed. A repeat after a death in the
test `Async_devComputons` takes 415 ms against 11 ms for the computation without a death
(`docs/measurements.md`, section 5).

`Unload(module)` asks the workers to unload a module's code so that the next `Push` loads the new
one. A module that is imported or has a frame on the stack cannot be unloaded: `Unload` returns
`FALSE`, and `LastError()` returns 20.

### 11.3. Transports

The transport is chosen before `Start` by `SetTransport` or the external variable `ASYNC_TRANSPORT`:

- `pipe` (the default) — a worker is started as a copy of the current process (`/proc/self/exe`)
  with three OS pipes: commands, requests, replies. In the console host it is the same
  `blackbox-console`.
- `tcp` — the controller listens on `127.0.0.1` on a free port, the worker connects to the address
  from the parameter `--ASYNC_TCP=…`.
- `mpi` — the external transport `AsyncMpi` (section 11.4).

A frame consists of a 12-byte header (length, frame number, kind or code) and a body. The reply body
over `pipe` and `tcp` is limited so that a result cannot exceed about 59,988 bytes.

### 11.4. MPI

`AsyncMpi.Install()` loads the wrapper `libbbmpi.so` over MPICH and installs the transport. After
`SetTransport("mpi")` the call `Start(n)` expects a world of exactly `n+1` ranks: rank 0 is the
controller, ranks 1..n run `AsyncMpi.WorkerLoop`. A world of another size gives `Size() = 0`.

MPI does not report the death of a rank (mpiexec usually kills the whole world), so there is no
repeat here. `Unload` over MPI is not supported and returns `FALSE` with code −1; `WorkerPid` returns
0, `WorkerDead` returns `TRUE`.

### 11.5. The RobustParallel Facade

The module `RobustParallel` of the subsystem `Robust` repeats the definition from the report by
I. Denisov (2017, p. 19), so that programs written for that definition run on top of `AsyncComputon`:

```
CONST tag* = 1;
TYPE Computon* = AsyncComputon.Computon;
     Hook* = AsyncComputon.Hook;
VAR rank-, size-: INTEGER;
PROCEDURE LogMsg* (a: ARRAY OF CHAR);
PROCEDURE Push* (store: Computon; rank, tag: INTEGER);
PROCEDURE Pull* (rank, tag: INTEGER): Computon;
PROCEDURE Try* (rank, tag: INTEGER): Computon;
PROCEDURE SetHook* (h: Hook);
PROCEDURE Wait*;
PROCEDURE Stop*;
PROCEDURE Unload* (module: ARRAY OF CHAR);
```

Ranks here are as in MPI: 0 is the controller, 1..size−1 are workers; the facade shifts the rank by
−1 before calling `AsyncComputon`. A rank outside `1 .. size - 1` traps with code 20. The number of
workers is set by the external variable `ROBUST_SIZE`: with `n > 1` the facade starts `n − 1` workers
on the `pipe` transport (or the one set by `ASYNC_TRANSPORT`); the MPI world has nothing to do with
it. In a worker process (`ASYNC_IN`, `ASYNC_OUT` or `ASYNC_TCP` is set) the facade does not start a
pool. `Wait` does nothing: waiting is expressed in `Pull` and `Try`. `Unload` returns no result and
prints a refusal to the console.

## 12. Cost and Limits

The library is built for AMD64, the module `AsyncStm` also for i386. Component Pascal code is run by one OS thread; worker threads
and computon processes do not touch the shared heap. So an operation on a channel between two switch
points is indivisible without any lock: a channel takes its monitor only if some task already holds
it.

Only tasks wait. From the main stack one may send to a mailbox, fill a single-assignment variable,
call `Push`, `Try` and `Unload` of computons, and also `Recv` and `Send` if the other side is already
ready. Everything else — selection, `AsyncIO`, `Execute`, `Pull` — requires a task and traps outside
one.

Cancelling a task while it waits traps it with code 127. The offers of its selection are removed on
the next access to the channel; unfinished computon requests stay occupied until `Pull`, `Try` or
`Stop`.

A blocking channel operation and a selection allocate no memory: wait records are taken from free
lists. The measured numbers and their conditions are collected in `docs/measurements.md`, sections 2
and 5.

## 13. Implementation

This section describes how the behaviour of the previous sections is obtained; it does not define
behaviour.

This section does not define behaviour; it describes how the behaviour is obtained.

### 13.1. Channel and Selection

`AsyncSelect.Core` is an extensible record with a kind, a capacity, a message queue and the heads of
two offer queues (`waitS`, `waitR`). An offer (`Offer`) stores a common context `Ctx`, the index of
the alternative, the operation kind and the message.

Offers and contexts come from the free lists `freeOffers` and `freeCtx` and return there after the
operation. A free-list record lies in no channel queue. Selection without a timeout does not
allocate. The message queue of `Buffer` and `Mailbox` is a ring in an array; for `Buffer` its length
is the capacity, for `Mailbox` the ring doubles when full.

An operation on a channel between two switch points runs without switches: there is one thread and
no preemption. So `Enter` takes the channel monitor only when someone holds it
(`Monitors.IsLocked`). Then the operation waits for the monitor release; otherwise exclusion holds
without the monitor.

Two procedures perform an operation on a channel. `Put` gives the message to a waiting receiver or
puts it into the ring and checks the protocol. `Take` takes a message from the ring or from a
waiting sender. After a `Take` from the ring, `CompleteSend` performs waiting sends while there is
room. Both procedures skip offers with the caller's decision: an offer is not performed against an
offer with the same `Ctx`.

`Select` walks alternatives in index order. For a send or a receive it takes an offer and calls
`Register`. `Register` performs the operation through `Put` or `Take` or puts the offer into
`waitS` or `waitR`. The timeout with the smallest `ms` schedules a `Services` action. The task
sleeps in `Tasks.Park` until `Ctx.decided` becomes true.

The decision is made by the side that performed the offer first. It writes `ctx.decided` and
`ctx.winner`, takes or gives the message and wakes the task with `Tasks.Unpark`. The channel
monitor is held at that moment only if someone else held it. `Wake` wakes only when
`ctx.ready = TRUE`. During registration the task may wait for the monitor of another channel, and
`Unpark` would move it to `Ready` without the monitor. After waking, the owner withdraws the offers
(`Withdraw`). Withdrawal is idempotent, so the winner is withdrawn by the same call.

Direct `Send` and `Recv` of all three kinds are `QueueSend` and `QueueRecv`. The operation runs at
once through `Put` or `Take`; otherwise the offer joins `waitS` or `waitR`, and the task sleeps in
`Tasks.Park`. The other side performs the offer and wakes the task without handing over the
monitor. A rendezvous is a channel of capacity 0: a send without a receiver always waits in
`waitS`. Dead offers are removed when a queue is walked. An offer is dead when its task is in
`Finished` or `Cancelled`.

### 13.2. I/O

`Poller` stores only a pointer to the platform hook. The descriptor registry and `poll` live in
`LinConsole`: an array of 4096 entries, stdin as the first element, one-shot registration. Ready
entries are removed before `Waiter.Ready` is called, so that a repeated registration does not
disturb the walk.

### 13.3. Worker Threads

`AsyncWork` is the facade; `AsyncWorkLin` and `AsyncWorkWin` are the delegates. Linux: up to four
pthreads, a static slot array under a mutex and a condition variable, a ring queue of 256 jobs, a
byte in a pipe as the readiness signal. Windows: up to four `CreateThread` threads, slots under a
`CRITICAL_SECTION`, a queue event; readiness is collected by a `Services` action that polls `done`
every 5 ms.

Synchronization primitives live in the heap, not in global module records: global records do not get
guaranteed alignment, and futex and `CRITICAL_SECTION` require it.

### 13.4. Computons

`AsyncComputon` keeps up to 16 executors and up to 128 requests. A request (`Req`) stores a `tag`, a
rank, a buffer, a task and a `Select` context. `Push` serializes the object through `Stores` into a
temporary file and sends a frame. The response is read by the main loop: on Linux through `Poller`,
on Windows by a `Services` action; `Pull` waits in `Tasks.Sleep(10)` with a deadline.

`MarkDead` closes the dead executor's channel and retries its unfinished requests on a live one.
`Resend` chooses the next live executor; if none is live, the request gets code `-2`. `Unload` sends
kind `kindUnload` and waits for responses; the executor calls `Kernel.UnloadMod` and clears the
pointer to the unloaded module object, so that the next collection does not dereference its method
table.

The MPI transport (`AsyncMpi`) loads `libbbmpi.so` through `LinDl` and binds seven functions. The
controller checks response readiness with `MPI_Iprobe` from a `Services` action; the `WorkerLoop`
executor receives frames on the stdin command and answers through MPI.

### 13.5. Transactions

`AsyncStm` keeps the state of the current transaction in module variables: there is one
transaction, because there are no switches inside it. The transaction number `rser` and the level
number `wser` come from one counter. A variable remembers by which transaction it was read
(`v.rser`) and by which level it was written (`v.wser`); a matching number spares the search in the
read set and a second log entry.

`Atomically` opens `Tasks.BeginAtomic` and executes `Do` under `Kernel.Try`. A normal end is the
commit: the log is cleared, and the tasks waiting for the written variables are woken. A trap is
rollback of the log from the end to the start and passing the number on through `HALT` with the
same number (`HALT` accepts only a constant, so the number is chosen by a `CASE` statement).
`Retry` is a trap too, but a quiet one: number 126 under `Kernel.SetQuiet`, without a kernel report;
`Atomically` recognises it by `Kernel.err`.

`OrElse` opens a new level: it remembers the log length and takes a new level number, so the first
write in the branch puts into the log the value at the start of the branch. After a `Retry` of the
branch the log is rolled back to the mark, and what the branch has read stays in the set — the wait
covers both branches.

A `Retry` wait is a record `Wait` (task, wake flag, array of read variables) entered into the list
of waiters of each variable read. A write to the variable outside a transaction, or a commit, wakes
everybody in its list and clears the list. The task sleeps in `Tasks.Park`; a wake-up without the
flag is a foreign `Unpark`, it is given back to the task after the wait, as in `AsyncSelect`. On
waking, the task removes itself from the lists of the other variables. `Wait` is a trap cleaner
(`Kernel.PushTrapCleaner`), so cancellation in `Park` also removes the records, and the variable
does not hold a cancelled task.

## 14. Interface

This section lists the module exports in the form of BlackBox documentation: a `DEFINITION` block,
then a description of each procedure. A "Pre" line names a condition and the trap number on its
violation. A condition marked "not checked" is not checked by the code; its violation gives an
undefined result or a `NIL` dereference trap. A "Post" line names the state after return.

### 14.1. AsyncSelect

```
DEFINITION AsyncSelect;

	IMPORT AsyncProto;

	CONST
		Rendezvous = 0; Bounded = 1; Mailbox = 2;

	TYPE
		Msg = POINTER TO ABSTRACT RECORD END;

		Ctx = POINTER TO RECORD END;

		Core = POINTER TO EXTENSIBLE RECORD
			count-: INTEGER;
			protoState-: INTEGER
		END;

		Signal = POINTER TO ABSTRACT RECORD
			(s: Signal) Arm (ctx: Ctx; idx: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(s: Signal) Disarm, NEW, ABSTRACT
		END;

		Op = RECORD
			got-: Msg
		END;

	PROCEDURE InitCore (c: Core; kind, capacity: INTEGER);
	PROCEDURE SetProtocol (c: Core; p: AsyncProto.Protocol);
	PROCEDURE QueueSend (c: Core; m: Msg);
	PROCEDURE QueueRecv (c: Core; OUT m: Msg);
	PROCEDURE AltSend (core: Core; msg: Msg; OUT op: Op);
	PROCEDURE AltRecv (core: Core; OUT op: Op);
	PROCEDURE AltTimeout (ms: LONGINT; OUT op: Op);
	PROCEDURE AltSignal (sig: Signal; OUT op: Op);
	PROCEDURE Select (VAR ops: ARRAY OF Op): INTEGER;
	PROCEDURE SignalReady (ctx: Ctx; idx: INTEGER);

END AsyncSelect.
```

`Msg` is the base message type. `Core` is the channel core: `count` is the number of messages in
the ring, `protoState` is the protocol state (0 without a protocol). `Op` is a `Select` alternative;
`got` is the message of the winning receive. `Ctx` is the decision of one `Select` or one wait.

**PROCEDURE InitCore (c: Core; kind, capacity: INTEGER)**
Sets the kind and capacity of a new channel core.
Pre: `c` has just been created by `NEW` — not checked; `kind` is `Rendezvous`, `Bounded` or
`Mailbox` — trap 20; `capacity > 0` for `Bounded`, otherwise `capacity = 0` — trap 21.
Post: `c` is an empty channel of kind `kind` without a protocol.

**PROCEDURE SetProtocol (c: Core; p: AsyncProto.Protocol)**
Attaches a protocol to a channel or removes it.
Pre: `c # NIL` — not checked.
Post: every send into `c` is checked by protocol `p` at the moment it takes place; `protoState` is
the start state of `p`. With `p = NIL` there is no protocol, `protoState = 0`.

**PROCEDURE QueueSend (c: Core; m: Msg)**
Sends `m` into the channel; waits if there is no receiver or no room.
Pre: `c # NIL` — not checked; waiting only from a task — trap 20 in `Tasks.Park`.
Post: `m` is given to a receiver or lies in the ring. The protocol rejected `m` — trap 21, `m` is
not sent.

**PROCEDURE QueueRecv (c: Core; OUT m: Msg)**
Receives a message; waits if there are no messages and no waiting senders.
Pre: `c # NIL` — not checked; waiting only from a task — trap 20 in `Tasks.Park`.
Post: `m` is the first message of the ring or the message of the first waiting sender.

**PROCEDURE AltSend (core: Core; msg: Msg; OUT op: Op)**
Builds a send alternative.
Pre: `core # NIL` — trap 23 in `Select`.
Post: `op` is a send of `msg` into `core`.

**PROCEDURE AltRecv (core: Core; OUT op: Op)**
Builds a receive alternative.
Pre: `core # NIL` — trap 23 in `Select`.
Post: `op` is a receive from `core`.

**PROCEDURE AltTimeout (ms: LONGINT; OUT op: Op)**
Builds a timeout alternative.
Post: `op` is a timeout of `ms` ms from the start of the wait; `ms <= 0` fires at once.

**PROCEDURE AltSignal (sig: Signal; OUT op: Op)**
Builds an external-signal alternative.
Pre: `sig # NIL` — trap 24 in `Select`.
Post: `op` is a wait for signal `sig`.

**PROCEDURE Select (VAR ops: ARRAY OF Op): INTEGER**
Waits until exactly one alternative is performed and returns its index.
Pre: call from a task — trap 20; `LEN(ops) <= 32` — trap 21; the kind of every alternative is
valid — trap 22; a send or a receive has a non-`NIL` channel — trap 23; a signal alternative has a
non-`NIL` signal — trap 24.
Post: exactly one alternative `i` is performed, the result is `i`. For a receive `ops[i].got` is the
message. Losing offers are withdrawn, signals are disarmed. The protocol rejected a send — trap 21
after all offers are withdrawn.

**PROCEDURE SignalReady (ctx: Ctx; idx: INTEGER)**
Reports readiness of an external signal.
Pre: called by the signal owner; `ctx` and `idx` come from `Arm`; `Disarm` is not yet called — not
checked.
Post: if there was no decision, alternative `idx` won; the waiting task is woken.

**PROCEDURE (s: Signal) Arm (ctx: Ctx; idx: INTEGER): BOOLEAN**
Abstract method; `Select` calls it during registration.
Post: `TRUE` — the signal is ready. `FALSE` — the owner later calls `SignalReady(ctx, idx)` at most
once, unless `Disarm` is called first.

**PROCEDURE (s: Signal) Disarm**
Abstract method; `Select` calls it after the decision.
Post: the signal owner no longer uses `ctx` from `Arm` and does not call `SignalReady` with it.

### 14.2. AsyncChan

```
DEFINITION AsyncChan;

	IMPORT AsyncSelect, AsyncProto;

	TYPE
		Chan = POINTER TO RECORD (AsyncSelect.Core)
			(c: Chan) Send (m: AsyncSelect.Msg), NEW;
			(c: Chan) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

		Buffer = POINTER TO RECORD (AsyncSelect.Core)
			(c: Buffer) Send (m: AsyncSelect.Msg), NEW;
			(c: Buffer) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

		Mailbox = POINTER TO RECORD (AsyncSelect.Core)
			(c: Mailbox) Send (m: AsyncSelect.Msg), NEW;
			(c: Mailbox) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

	PROCEDURE NewChan (): Chan;
	PROCEDURE NewBuffer (capacity: INTEGER): Buffer;
	PROCEDURE NewMailbox (): Mailbox;
	PROCEDURE SetProtocol (c: AsyncSelect.Core; p: AsyncProto.Protocol);
	PROCEDURE SendAlt (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op);
	PROCEDURE RecvAlt (c: AsyncSelect.Core; OUT op: AsyncSelect.Op);
	PROCEDURE TimeoutAlt (ms: LONGINT; OUT op: AsyncSelect.Op);
	PROCEDURE Select (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER;

END AsyncChan.
```

**PROCEDURE NewChan (): Chan**
Post: a new empty rendezvous without a protocol.

**PROCEDURE NewBuffer (capacity: INTEGER): Buffer**
Pre: `capacity > 0` — trap 20.
Post: a new empty buffer for `capacity` messages without a protocol.

**PROCEDURE NewMailbox (): Mailbox**
Post: a new empty unbounded mailbox without a protocol.

**PROCEDURE (c: Chan) Send (m: AsyncSelect.Msg)**
**PROCEDURE (c: Buffer) Send (m: AsyncSelect.Msg)**
**PROCEDURE (c: Mailbox) Send (m: AsyncSelect.Msg)**
Send `m`; the same as `AsyncSelect.QueueSend(c, m)`.
Pre: waiting only from a task — trap 20 in `Tasks.Park`. `Mailbox.Send` waits only if another task
holds the channel monitor.
Post: rendezvous — the receiver took `m`; buffer and mailbox — `m` is the last in the queue. The
protocol rejected `m` — trap 21, `m` is not sent.

**PROCEDURE (c: Chan) Recv (OUT m: AsyncSelect.Msg)**
**PROCEDURE (c: Buffer) Recv (OUT m: AsyncSelect.Msg)**
**PROCEDURE (c: Mailbox) Recv (OUT m: AsyncSelect.Msg)**
Receive a message; the same as `AsyncSelect.QueueRecv(c, m)`.
Pre: waiting only from a task — trap 20 in `Tasks.Park`.
Post: `m` is the first message of the queue or of the first waiting sender.

**PROCEDURE SetProtocol (c: AsyncSelect.Core; p: AsyncProto.Protocol)**
The same as `AsyncSelect.SetProtocol`.

**PROCEDURE SendAlt (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op)**
**PROCEDURE RecvAlt (c: AsyncSelect.Core; OUT op: AsyncSelect.Op)**
**PROCEDURE TimeoutAlt (ms: LONGINT; OUT op: AsyncSelect.Op)**
**PROCEDURE Select (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER**
The same as `AsyncSelect.AltSend`, `AltRecv`, `AltTimeout` and `Select` (section 14.1).

### 14.3. AsyncOnce

```
DEFINITION AsyncOnce;

	IMPORT AsyncSelect;

	TYPE
		Once = POINTER TO RECORD
			done-: BOOLEAN;
			(o: Once) FillInt (x: INTEGER), NEW;
			(o: Once) ReadInt (): INTEGER, NEW;
			(o: Once) FillMsg (m: AsyncSelect.Msg), NEW;
			(o: Once) ReadMsg (OUT m: AsyncSelect.Msg), NEW
		END;

	PROCEDURE New (): Once;

END AsyncOnce.
```

All four methods are exclusive procedures (`[exclusive]`). The field `done` is exported read-only.

**PROCEDURE New (): Once**
Post: an unfilled variable, `done = FALSE`.

**PROCEDURE (o: Once) FillInt (x: INTEGER)**
Pre: `~o.done` — trap 21.
Post: `o.done`; the value is `x`.

**PROCEDURE (o: Once) ReadInt (): INTEGER**
Waits for filling in `ACTIVE.AWAIT(o.done)`.
Pre: waiting only from a task — monitor trap 24 if the variable is not filled.
Post: the result is the integer from `FillInt`; after `FillMsg` it is 0.

**PROCEDURE (o: Once) FillMsg (m: AsyncSelect.Msg)**
Pre: `~o.done` — trap 22.
Post: `o.done`; the value is `m`.

**PROCEDURE (o: Once) ReadMsg (OUT m: AsyncSelect.Msg)**
Waits for filling in `ACTIVE.AWAIT(o.done)`.
Pre: waiting only from a task — monitor trap 24 if the variable is not filled.
Post: `m` is the message from `FillMsg`; after `FillInt` it is `NIL`.

### 14.4. AsyncProto

```
DEFINITION AsyncProto;

	TYPE
		Message = ARRAY 256 OF CHAR;
		Protocol = POINTER TO RECORD END;

	PROCEDURE New (IN desc: ARRAY OF CHAR): Protocol;
	PROCEDURE LastError (OUT msg: Message);
	PROCEDURE Start (p: Protocol): INTEGER;
	PROCEDURE StateName (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR);
	PROCEDURE IsFinal (p: Protocol; state: INTEGER): BOOLEAN;
	PROCEDURE Step (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN);
	PROCEDURE LastViolation (p: Protocol; OUT msg: Message);

END AsyncProto.
```

**PROCEDURE New (IN desc: ARRAY OF CHAR): Protocol**
Builds a state machine from a description (section 6).
Post: the machine, or `NIL` on a description error; the error text is in `LastError`.

**PROCEDURE LastError (OUT msg: Message)**
Post: `msg` is the text of the last parse error.

**PROCEDURE Start (p: Protocol): INTEGER**
Post: the index of the start state; 0 when `p = NIL`.

**PROCEDURE StateName (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR)**
Post: `name` is the state name; `""` if `p = NIL` or the index does not exist.

**PROCEDURE IsFinal (p: Protocol; state: INTEGER): BOOLEAN**
Pre: `p # NIL` — trap 20.
Post: `TRUE` if `state` has no outgoing transitions.

**PROCEDURE Step (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN)**
Checks the next message with label `label`.
Post: `ok` — `state` follows the transition `label`. `~ok` — `state` is unchanged, the violation
text is in `LastViolation`. With `p = NIL` — `ok`.

**PROCEDURE LastViolation (p: Protocol; OUT msg: Message)**
Post: `msg` is the text of the last violation; `""` when `p = NIL`.

### 14.5. AsyncIO

```
DEFINITION AsyncIO;

	IMPORT CommStreams;

	PROCEDURE WaitFd (fd: INTEGER; events: SET);
	PROCEDURE Poll (timeout: LONGINT);
	PROCEDURE Read (s: CommStreams.Stream; VAR x: ARRAY OF BYTE; beg, len: INTEGER;
		OUT read: INTEGER);
	PROCEDURE Write (s: CommStreams.Stream; IN x: ARRAY OF BYTE; beg, len: INTEGER;
		OUT written: INTEGER);
	PROCEDURE Accept (l: CommStreams.Listener; OUT s: CommStreams.Stream;
		timeout: LONGINT): BOOLEAN;

END AsyncIO.
```

**PROCEDURE WaitFd (fd: INTEGER; events: SET)**
Waits for descriptor readiness; the registration is one-shot.
Pre: call from a task — trap 20.
Post: `fd` is ready for `events`; with `fd < 0` one `Tasks.Sleep(1)` has passed.

**PROCEDURE Poll (timeout: LONGINT)**
Runs the `Poller` waits for a test pump.
Post: ready waiters are woken within at most `timeout` ms.

**PROCEDURE Read (s; VAR x; beg, len: INTEGER; OUT read: INTEGER)**
Pre: waiting only from a task — trap 20 in `WaitFd` or `Tasks.Sleep`.
Post: `read > 0` — `read` bytes are read; `read = 0` — end or disconnection.

**PROCEDURE Write (s; IN x; beg, len: INTEGER; OUT written: INTEGER)**
Pre: waiting only from a task — trap 20 in `WaitFd` or `Tasks.Sleep`.
Post: `written > 0` — `written` bytes are written; `written = 0` — disconnection.

**PROCEDURE Accept (l; OUT s; timeout: LONGINT): BOOLEAN**
Pre: waiting only from a task — trap 20 in `WaitFd` or `Tasks.Sleep`.
Post: `TRUE` — `s` is the accepted connection; `FALSE` — `timeout` ms expired, `s = NIL`. With
`timeout < 0` there is no deadline.

### 14.6. AsyncWork

```
DEFINITION AsyncWork;

	IMPORT SYSTEM;

	CONST
		maxPath = 256; maxBuf = 4096; maxHost = 128;

	TYPE
		Proc = PROCEDURE [ccall] (arg: SYSTEM.ADDRESS);

		FileReq = RECORD [untagged]
			path: ARRAY maxPath OF SHORTCHAR;
			buf: ARRAY maxBuf OF SHORTCHAR;
			len, status: INTEGER
		END;

		AddrReq = RECORD [untagged]
			host: ARRAY maxHost OF SHORTCHAR;
			count, status: INTEGER
		END;

		FileReqPtr = POINTER [untagged] TO FileReq;
		AddrReqPtr = POINTER [untagged] TO AddrReq;

		Impl = POINTER TO ABSTRACT RECORD
			(i: Impl) Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER, NEW, ABSTRACT;
			(i: Impl) Done (id: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(i: Impl) Result (id: INTEGER): INTEGER, NEW, ABSTRACT;
			(i: Impl) Release (id: INTEGER), NEW, ABSTRACT;
			(i: Impl) Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER, NEW, ABSTRACT;
			(i: Impl) Workers (): INTEGER, NEW, ABSTRACT;
			(i: Impl) Scan, NEW, ABSTRACT;
			(i: Impl) ReadFileJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT;
			(i: Impl) WriteFileJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT;
			(i: Impl) ResolveJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT
		END;

	PROCEDURE Install (i: Impl);
	PROCEDURE Active (): BOOLEAN;
	PROCEDURE Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE Done (id: INTEGER): BOOLEAN;
	PROCEDURE Result (id: INTEGER): INTEGER;
	PROCEDURE Release (id: INTEGER);
	PROCEDURE Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE Workers (): INTEGER;
	PROCEDURE Scan;
	PROCEDURE [ccall] ReadFileJob (arg: SYSTEM.ADDRESS);
	PROCEDURE [ccall] WriteFileJob (arg: SYSTEM.ADDRESS);
	PROCEDURE [ccall] ResolveJob (arg: SYSTEM.ADDRESS);

END AsyncWork.
```

`Impl` is the platform delegate; `AsyncWorkLin` and `AsyncWorkWin` install it. The delegate trap
numbers are given for Linux and Windows; they coincide.

**PROCEDURE Install (i: Impl)**
Installs the delegate. The delegate's own `Install` calls it.
Post: the pool is served by `i`.

**PROCEDURE Active (): BOOLEAN**
Post: `TRUE` if a delegate is installed.

**PROCEDURE Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER**
Queues a job; installs the delegate if needed.
Pre: a delegate is found — trap 22.
Pre: the pool has a worker thread (`Workers() > 0`) — trap 23.
Post: the slot number; −1 — no free slot or the queue is full.

**PROCEDURE Done (id: INTEGER): BOOLEAN**
Pre: a delegate is installed — trap 21; `0 <= id <` the number of slots — trap 20 in the delegate.
Post: `TRUE` if job `id` is done.

**PROCEDURE Result (id: INTEGER): INTEGER**
Pre: the same as for `Done`.
Post: the status of job `id`.

**PROCEDURE Release (id: INTEGER)**
Pre: the same as for `Done`.
Post: slot `id` is free.

**PROCEDURE Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER**
Runs a job in a worker thread and waits for it; a switch point.
Pre: a delegate is found — trap 22; the pool has a worker thread — trap 23; call from a task — trap
21 in the delegate.
Post: the job status, the slot is freed; −1 — no free slot.

**PROCEDURE Workers (): INTEGER**
Post: the number of created worker threads; 0 without a delegate.

**PROCEDURE Scan**
Scans completed jobs; the main loop calls it.
Post: the tasks whose jobs are done are woken.

**PROCEDURE ReadFileJob (arg: SYSTEM.ADDRESS)**
**PROCEDURE WriteFileJob (arg: SYSTEM.ADDRESS)**
**PROCEDURE ResolveJob (arg: SYSTEM.ADDRESS)**
Jobs for a worker thread; `arg` is the address of a `FileReq` or an `AddrReq`.
Pre: a delegate is installed — trap 21.
Post: the job status is in the record field `status` (codes in section 10).

### 14.7. AsyncComputon

```
DEFINITION AsyncComputon;

	IMPORT Stores, AsyncSelect;

	TYPE
		Computon = POINTER TO ABSTRACT RECORD (Stores.Store)
			(c: Computon) Do, NEW, ABSTRACT
		END;

		Hook = POINTER TO ABSTRACT RECORD
			(h: Hook) Failed (rank, tag, err: INTEGER), NEW, ABSTRACT
		END;

		Transport = POINTER TO ABSTRACT RECORD
			(t: Transport) Start (n: INTEGER; OUT size: INTEGER), NEW, ABSTRACT;
			(t: Transport) SendFrame (rank, tag, kind: INTEGER;
				buf: POINTER TO ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(t: Transport) Poll, NEW, ABSTRACT;
			(t: Transport) Stop, NEW, ABSTRACT;
			(t: Transport) Alive (rank: INTEGER): BOOLEAN, NEW, ABSTRACT
		END;

	PROCEDURE Serialize (c: Computon; OUT buf: POINTER TO ARRAY OF SHORTCHAR; OUT len: INTEGER);
	PROCEDURE Deserialize (VAR b: ARRAY OF SHORTCHAR; len: INTEGER): Computon;
	PROCEDURE SetTransport (IN name: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE SetTransportObject (t: Transport);
	PROCEDURE AltPull (rank, tag: INTEGER): AsyncSelect.Signal;
	PROCEDURE Push (c: Computon; rank, tag: INTEGER);
	PROCEDURE PullTimeout (rank, tag: INTEGER; ms: LONGINT): Computon;
	PROCEDURE Pull (rank, tag: INTEGER): Computon;
	PROCEDURE Try (rank, tag: INTEGER; OUT c: Computon): BOOLEAN;
	PROCEDURE Wait;
	PROCEDURE LastError (): INTEGER;
	PROCEDURE SetHook (h: Hook);
	PROCEDURE LogMsg (IN msg: ARRAY OF CHAR);
	PROCEDURE Unload (IN module: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE Size (): INTEGER;
	PROCEDURE Rank (): INTEGER;
	PROCEDURE WorkerPid (rank: INTEGER): INTEGER;
	PROCEDURE WorkerDead (rank: INTEGER): BOOLEAN;
	PROCEDURE Stop;
	PROCEDURE Start (n: INTEGER);
	PROCEDURE MarkDead (idx: INTEGER);
	PROCEDURE DeliverFrame (rank, tag, status: INTEGER; VAR b: ARRAY OF SHORTCHAR; len: INTEGER);
	PROCEDURE RunDo (c: Computon; OUT err: INTEGER);
	PROCEDURE WorkerLoop;

END AsyncComputon.
```

The `LastError` codes are listed in section 11.1. `Transport`, `SetTransportObject`, `MarkDead`,
`DeliverFrame`, `RunDo`, `Serialize` and `Deserialize` serve the transports and the executor.

**PROCEDURE Start (n: INTEGER)**
Stops the previous pool and starts `n` executors.
Pre: `1 <= n <= 16` — trap 20; for the `mpi` transport an external transport is installed — trap 21.
Post: `Size()` is the number of started executors.

**PROCEDURE Stop**
Post: the executors are killed, the channels are closed, `Size() = 0`; unfinished requests are
finished with code `-2`.

**PROCEDURE SetTransport (IN name: ARRAY OF CHAR): BOOLEAN**
Post: `TRUE` — transport `"pipe"` (also `""`), `"tcp"` or `"mpi"` is chosen; `FALSE` — unknown name.

**PROCEDURE Push (c: Computon; rank, tag: INTEGER)**
Serializes `c` and sends it to executor `rank`.
Pre: `tag >= 0` — trap 20 (negative numbers are internal).
Pre: `0 <= rank < Size()` — trap 21.
Post: request `tag` is queued; on error `LastError = -1`; on a failed send — a retry on a live one.

**PROCEDURE PullTimeout (rank, tag: INTEGER; ms: LONGINT): Computon**
**PROCEDURE Pull (rank, tag: INTEGER): Computon**
Wait for the result of request `tag`; `Pull` uses `ms = 10000`.
Pre: call from a task — trap 21.
Post: the result, or `NIL` with a code in `LastError`; the request is freed.

**PROCEDURE Try (rank, tag: INTEGER; OUT c: Computon): BOOLEAN**
Post: `TRUE` — the request is done and freed, `c` is the result or `NIL` with a code in
`LastError`; `FALSE` — no result yet, `c = NIL`.

**PROCEDURE AltPull (rank, tag: INTEGER): AsyncSelect.Signal**
Post: a signal for `Select` that is ready when request `tag` is done.

**PROCEDURE Unload (IN module: ARRAY OF CHAR): BOOLEAN**
Post: `TRUE` — all live executors dropped the module, or the pool is not started; `FALSE` — a
refusal, the code is in `LastError`.

**PROCEDURE Size (): INTEGER**, **Rank (): INTEGER**, **WorkerPid (rank): INTEGER**,
**WorkerDead (rank): BOOLEAN**, **LastError (): INTEGER**
Post: the number of executors; 0; the executor pid or −1; the death flag (`TRUE` out of range); the
code of the last error.

**PROCEDURE SetHook (h: Hook)**, **LogMsg (IN msg)**, **Wait**
Install the error handler, write a line to the console; `Wait` does nothing.

### 14.8. AsyncMpi

```
DEFINITION AsyncMpi;

	PROCEDURE Install (): BOOLEAN;
	PROCEDURE Available (): BOOLEAN;
	PROCEDURE Rank (): INTEGER;
	PROCEDURE WorldSize (): INTEGER;
	PROCEDURE WorkerLoop;

END AsyncMpi.
```

**PROCEDURE Install (): BOOLEAN**
Post: `TRUE` — `libbbmpi.so` is bound, the transport is installed in `AsyncComputon`.

**PROCEDURE Available (): BOOLEAN**
Post: `TRUE` — the MPICH library and the wrapper are found; `MPI_Init` is not called.

**PROCEDURE Rank (): INTEGER**, **WorldSize (): INTEGER**
Post: the MPI rank and world size; before initialization −1 and 0.

**PROCEDURE WorkerLoop**
Entry point of an executor process of rank greater than 0.

### 14.9. AsyncStm

```
DEFINITION AsyncStm;

	TYPE
		Saved = RECORD
			i: LONGINT;
			x: REAL;
			p: ANYPTR
		END;

		Var = POINTER TO ABSTRACT RECORD
			(v: Var) Save- (OUT s: Saved), NEW, ABSTRACT;
			(v: Var) Load- (IN s: Saved), NEW, ABSTRACT
		END;

		IntVar = POINTER TO RECORD (Var)
			(v: IntVar) Get (): INTEGER, NEW;
			(v: IntVar) Set (x: INTEGER), NEW
		END;

		BoolVar = POINTER TO RECORD (Var)
			(v: BoolVar) Get (): BOOLEAN, NEW;
			(v: BoolVar) Set (x: BOOLEAN), NEW
		END;

		RealVar = POINTER TO RECORD (Var)
			(v: RealVar) Get (): REAL, NEW;
			(v: RealVar) Set (x: REAL), NEW
		END;

		RefVar = POINTER TO RECORD (Var)
			(v: RefVar) Get (): ANYPTR, NEW;
			(v: RefVar) Set (x: ANYPTR), NEW
		END;

		Tx = POINTER TO ABSTRACT RECORD
			(t: Tx) Do-, NEW, ABSTRACT
		END;

	PROCEDURE Atomically (t: Tx);
	PROCEDURE Retry;
	PROCEDURE OrElse (a, b: Tx);
	PROCEDURE Read (v: Var);
	PROCEDURE Write (v: Var);
	PROCEDURE NewInt (x: INTEGER): IntVar;
	PROCEDURE NewBool (x: BOOLEAN): BoolVar;
	PROCEDURE NewReal (x: REAL): RealVar;
	PROCEDURE NewRef (x: ANYPTR): RefVar;

END AsyncStm.
```

The methods `Save`, `Load` and `Do` are exported for implementation only: an extension writes them,
`AsyncStm` itself calls them. The module is also built for i386.

**TYPE Tx**
ABSTRACT
The body of a transaction; the data are in the fields of the extension.

**PROCEDURE (t: Tx) Do**
NEW, ABSTRACT
The actions of the transaction. Executed from `Atomically` or `OrElse`.

**PROCEDURE Atomically (t: Tx)**
Executes `t.Do` indivisibly; inside a transaction, as its part.

```
Pre: t # NIL — 20
Pre: Retry only after reading a variable — 21
Pre: Retry only in a task — 22
Pre: no switch point in t.Do — 25 (Tasks), no Tasks.Step — 22 (Tasks)
Post: the writes of t.Do are visible; on a trap in t.Do the writes are rolled back, the trap is
	passed on with the same number (a number greater than 128 as 128)
```

**PROCEDURE Retry**
Rolls the transaction back; `Atomically` waits until a variable read changes and executes the
transaction again.

```
Pre: a transaction is running — 20
Post: does not return
```

**PROCEDURE OrElse (a, b: Tx)**
Executes `a`; if `a` called `Retry`, rolls back its writes and executes `b`. Outside a transaction,
as a separate transaction.

```
Pre: a # NIL — 20
Pre: b # NIL — 21
Post: a or b executed; a Retry in b waits until a variable read by a or b changes
```

**PROCEDURE Read (v: Var)**
Called from `Get` of an extension of `Var` before the value is read.

```
Pre: v # NIL — 20
Post: inside a transaction v is in the read set
```

**PROCEDURE Write (v: Var)**
Called from `Set` of an extension of `Var` before the value is changed.

```
Pre: v # NIL — 20
Post: inside a transaction the old value of v is in the undo log; outside a transaction the tasks
	waiting for v in Retry are woken
```

**PROCEDURE (v: Var) Save (OUT s: Saved)**, **Load (IN s: Saved)**
NEW, ABSTRACT
A copy of the value into the log and back. A record value is put into `s.p` as a copy.

**PROCEDURE NewInt (x: INTEGER): IntVar**, **NewBool**, **NewReal**, **NewRef**
Post: a variable with the value `x`.

**PROCEDURE (v: IntVar) Get (): INTEGER**, **Set (x: INTEGER)** and the same for `BoolVar`, `RealVar`,
`RefVar`
`Get` is `Read(v)` and the value; `Set` is `Write(v)` and the store of the value.
