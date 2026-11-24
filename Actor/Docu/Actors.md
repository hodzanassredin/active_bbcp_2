# Actors

The `Actor` subsystem builds a program from objects that exchange messages and share no state. This
chapter describes the first stage: actors of one process, their execution by a shared pool of tasks,
and the supervision of children by their parent. Tasks, monitors and switch points are defined in
`Docu/Active-CP-Lang.md` and `Docu/Tasks.md`; messages and channels in `Docu/Async.md`. Like
`Async`, the subsystem is built for AMD64 only.

## 1. An Account Everybody Touches

Take a bank account from which several tasks withdraw money. If the balance lives in a shared
variable, each task checks it and decreases it, and between the check and the decrease another
task manages to do the same: the balance goes negative. A monitor closes this hole and opens
another one: a task that traps inside an exclusive procedure leaves the account in whatever state
the trap found it, and the next task keeps working with it.

An actor solves both problems with one rule. An *actor* is an object with its own state and a
handler `Receive`; only its handler changes the state of the actor, and other parts of the program
send it messages. Messages wait for processing in the *mailbox*, the queue of the actor. The handler
receives them one at a time, so nobody intervenes between the check of the balance and its
decrease. A trap in the handler is caught and becomes a message to the *supervisor*, the parent of
the actor, which decides what to do with the failed child: continue, restart it with a clean state,
or stop it.

The sender knows the receiver by its *address*, a value of type `ActorCore.Ref`. An address consists
of a node name and an actor name. At the first stage the node is always the own one, but sending
already goes through an abstract method of the address, and the address of another process takes
the same place (section 9).

## 2. The First Actor

A counter accepts `Inc` and answers `Get` with its current value. The module below creates it, sends
a thousand `Inc` from a client task and asks for the total.

```
MODULE ActorDocCounter;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Get = POINTER TO RECORD (ActorCore.Msg) END;
		Num = POINTER TO RECORD (ActorCore.Msg) n: INTEGER END;
		Counter = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR counter: ActorCore.Ref; answer: INTEGER; done: BOOLEAN;

	PROCEDURE (a: Counter) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR r: Num;
	BEGIN
		WITH m: Inc DO INC(a.n)
		| m: Get DO NEW(r); r.n := a.n; c.Respond(r)
		ELSE	(* Started и прочие сообщения жизненного цикла *)
		END
	END Receive;

	PROCEDURE New (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); RETURN a
	END New;

	PROCEDURE (t: Client) Run;
		VAR i: INTEGER; inc: Inc; get: Get; r: ActorCore.Msg;
	BEGIN
		NEW(inc); NEW(get);
		FOR i := 1 TO 1000 DO ActorCore.Tell(counter, inc) END;
		IF ActorCore.Ask(counter, get, 1000, r) = ActorCore.ok THEN answer := r(Num).n ELSE answer := -1 END;
		done := TRUE
	END Run;

	PROCEDURE Run*;
		VAR t: Client; s: ARRAY 16 OF CHAR;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		counter := ActorCore.Spawn(NIL, ActorCore.NewProps(New), "counter");
		done := FALSE; NEW(t); Tasks.Start(t);
		WHILE ~done DO Services.actionHook.Step END;
		Strings.IntToString(answer, s);
		Console.WriteStr("ActorDocCounter: "); Console.WriteStr(s); Console.WriteLn;
		ActorCore.StopActor(counter)
	END Run;

END ActorDocCounter.
```

The command `ActorDocCounter.Run` prints `ActorDocCounter: 1000`. An actor is described by a `Props`
record: the instance factory, the mailbox limit, the own-task flag and the strategy for supervising
children. `Spawn` creates an instance with the factory, sets up a mailbox and returns the address;
the first message the handler receives is `Started`. `Tell` puts a message into the mailbox and
returns at once. `Ask` sends a message with a temporary reply address and waits for the first letter
to that address for at most the given number of milliseconds; the handler answers with
`c.Respond`, which addresses the letter to the sender of the current message. `Ask` waits, so the
client is a task: on the main stack `Ask` traps with code 20.

A message is an extension of `AsyncSelect.Msg` (called `ActorCore.Msg` in the subsystem) and does
not change after sending. The counter above sends the same object `inc` a thousand times; this is
allowed exactly because the object is immutable. If the client changed a field of a sent message,
the handler would see the value at the time of processing, and the order of sends would mean
nothing.

## 3. Executors

An actor has no task of its own. Mailboxes with work are served by an *executor*, a task of the
`ActorRun` pool that takes a mailbox from the shared queue, calls `ActorCore.Process` and returns
the mailbox to the queue if letters remain in it. `ActorRun.Start(n)` creates n executors; in one
call of `Process` an actor handles all system messages and up to a hundred user messages. A
thousand actors therefore occupy a thousand mailboxes and as many stacks as there are executors in
the pool.

The handler of an actor may wait: sleep, receive from a channel, ask another actor with `Ask`.
While it waits, one rule holds: one handler per actor at a time. The mailbox of a waiting actor is
marked busy, and the next letter to this actor is given neither to this executor nor to another
one. The price of the rule is a busy executor. Suppose the pool has four executors and four actors
fall asleep in `Receive` for a second at once: a fifth actor that received a letter waits a second,
although its work takes a microsecond. The test `TestBusyActorGetsNoSecondMessage` checks both sides
of the rule: two `Nap` letters to one actor are handled one after another, and a letter to a second
actor is handled during the first nap.

An actor that needs to wait all the time gets a task of its own: `Props.dedicated := TRUE`. Only
that task serves its mailbox, and the pool executors stay free; in the test
`TestDedicatedLeavesPoolFree` a pool of one executor answers another actor while the actor with its
own task sleeps.

A free executor waits in `Tasks.Park`, and `Tell` wakes it with `Tasks.Unpark`. If `Tell` is made
outside a task, from a command or from a `Services` action, the task scheduler may already be set
for the deadline of the earliest sleeping task, and the woken executor would wait for that deadline.
Therefore the first executor of the pool sleeps 10 ms at a time instead of `Park`: a letter sent
from the main stack starts to be processed within 10 ms. A letter sent from a task has no delay.

An actor cannot ask itself with `Ask`: the question lands in its own mailbox, and the mailbox is
held by the waiting handler. `Ask` returns `timedOut` when the time runs out, and no answer comes.

## 4. Delivery

Delivery follows the rule "at most once". A letter that cannot be processed is neither returned to
the sender nor repeated; it becomes a *dead letter*, and the counter `ActorCore.deadLetters` grows by
one. A letter becomes dead when it goes to a stopped actor, exceeds the mailbox limit
`Props.capacity`, is a `Respond` to a message without a sender, or remains in the mailbox when the
actor stops. The sender neither traps nor waits.

A mailbox without a limit grows while memory lasts. With the limit `capacity = 5`, twenty letters
sent in a row before the first processing give five processed and fifteen dead ones
(`TestCapacityOverflowGoesToDeadLetters`).

Letters from one sender to one receiver arrive in the order of sending: the mailbox is one queue,
and every address puts a letter at its end. The counter test sends a thousand numbered `Inc` and
checks that the numbers follow each other. Letters from different senders interleave in the order
in which the senders got control.

`Ask` returns one of three results:

| Result | Condition |
|---|---|
| `ok` | a letter arrived at the reply address; it is in `reply` |
| `timedOut` | no answer within the given time |
| `dead` | the question became a dead letter |

The answer `dead` comes without waiting for the deadline: in the test
`TestStoppedActorGivesDeadLetters` a question with a 1000 ms limit to a stopped actor returns in
less than 500 ms. An answer arriving after the timeout is lost.

## 5. Life Cycle

An actor passes the states "running", "restarting", "stopping" and "stopped". The handler learns
about each transition by a message:

| Message | When |
|---|---|
| `Started` | first after creation and first after a restart |
| `Restarting` | to the old instance before a restart |
| `Stopping` | at the beginning of stopping |
| `Stopped` | last, after all children have stopped |
| `Terminated` | to a watcher when the watched actor has stopped |
| `ReceiveTimeout` | after idling longer than `SetReceiveTimeout` |

Besides the mailbox of user letters an actor has a system queue: stop, watch, failure of a child,
departure of a child, directives of the supervisor. Before each user letter the executor works
through the whole system queue. Therefore `StopActor` stops an actor even with a thousand letters in
its mailbox: all of them become dead.

Stopping goes from top to bottom and completes from bottom to top. The parent receives `Stopping`,
sends a stop to its children and waits for each of them to leave; the parent receives `Stopped` only
when no children remain. If the order were reversed, the parent's handler would release in `Stopped`
resources that the children still use in their `Stopping`. The test
`TestStopParentStopsChildrenFirst` checks from a log that `Stopping` and `Stopped` of both children
are recorded before `Stopped` of the parent.

`c.Watch(r)` subscribes actor `c` to the stop of `r`: when `r` has stopped, the handler of `c`
receives `Terminated` with the field `who = r`. A subscription to an already stopped actor answers
`Terminated` at once.

`c.SetReceiveTimeout(ms)` asks to be told about idleness. After `ms` milliseconds without user
letters the handler receives `ReceiveTimeout` once; the next count starts with the next letter. In
the test `TestReceiveTimeoutOnceAfterIdle`, with a limit of 100 ms, ten letters 20 ms apart give no
`ReceiveTimeout`, then 250 ms of silence give exactly one, and another 250 ms of silence give none.
Life-cycle messages and `ReceiveTimeout` itself do not restart the count.

`c.Become(b)` replaces the handler: the following letters go to `b.Receive(a, c, m)`, where `a` is
the actor instance. `Become(NIL)` returns the instance's `Receive`, and so does a restart.

## 6. Supervision

Every actor created by `Spawn(c, ...)` is a child of actor `c`; one created by `Spawn(NIL, ...)` is a
child of the root. The handler runs under `Kernel.Try`, and a trap in it does not bring down the
executor: the mailbox is suspended, and the parent receives a system message about the failure with
the trap number. The parent asks the strategy from its `Props.strategy` and gets a *directive*, the
decision about the fate of the failed child:

| Directive | Action |
|---|---|
| `Resume` | the mailbox is unsuspended, the state is kept, the offending letter is skipped |
| `Restart` | the children of the child are stopped, then the factory creates a new instance, which receives `Started`; letters in the mailbox are kept, the offending one is skipped |
| `Stop` | the child is stopped, its letters become dead |
| `Escalate` | the parent itself fails with the same trap number; the child waits for its fate |

Without a strategy the directive is `Stop`. `ActorSup.NewOneForOne(d, max, ms)` answers with
directive `d` and applies it only to the failed child. For `Restart` it counts restarts in a window:
the first failure opens the window, a failure after the end of the window opens a new one, and the
failure numbered `max + 1` in a window gives `Stop`. Without a limit, a child that traps on every
letter would restart forever and occupy an executor. The test
`TestTooManyRestartsStopsAndNotifiesWatcher` with `max = 2` sends three trapping letters: the child
receives `Started` three times, then stops, and the watcher receives `Terminated`.

A restart without a journal loses the state. The module below counts letters, traps on the third
one and restarts: after the restart the count starts again.

```
MODULE ActorDocRestart;
	IMPORT Kernel, Services, Console, Strings, ActorCore, ActorSup, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Boom = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Parent = POINTER TO RECORD (ActorCore.Actor) END;

	VAR worker: ActorCore.Ref; starts, last: INTEGER;

	PROCEDURE (a: Worker) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: ActorCore.Started DO INC(starts)
		| m: Inc DO INC(a.n); last := a.n
		| m: Boom DO HALT(42)
		ELSE
		END
	END Receive;

	PROCEDURE NewWorker (): ActorCore.Actor;
		VAR a: Worker;
	BEGIN
		NEW(a); RETURN a
	END NewWorker;

	PROCEDURE (a: Parent) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF m IS ActorCore.Started THEN
			worker := ActorCore.Spawn(c, ActorCore.NewProps(NewWorker), "worker")
		END
	END Receive;

	PROCEDURE NewParent (): ActorCore.Actor;
		VAR a: Parent;
	BEGIN
		NEW(a); RETURN a
	END NewParent;

	PROCEDURE Run*;
		VAR p: ActorCore.Props; parent: ActorCore.Ref; inc: Inc; boom: Boom; t0: LONGINT;
			s: ARRAY 16 OF CHAR;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		starts := 0; last := 0; worker := NIL;
		p := ActorCore.NewProps(NewParent);
		p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		parent := ActorCore.Spawn(NIL, p, "parent");
		WHILE worker = NIL DO Services.actionHook.Step END;
		NEW(inc); NEW(boom);
		ActorCore.Tell(worker, inc); ActorCore.Tell(worker, inc);
		ActorCore.Tell(worker, boom);
		ActorCore.Tell(worker, inc);
		t0 := Kernel.Time();
		WHILE Kernel.Time() - t0 < 100 DO Services.actionHook.Step END;
		Strings.IntToString(starts, s); Console.WriteStr("ActorDocRestart: Started "); Console.WriteStr(s);
		Strings.IntToString(last, s); Console.WriteStr(", счёт после перезапуска "); Console.WriteStr(s);
		Console.WriteLn;
		ActorCore.StopActor(parent)
	END Run;

END ActorDocRestart.
```

The command `ActorDocRestart.Run` prints `ActorDocRestart: Started 2, счёт после перезапуска 1`
("count after restart 1"): the two `Inc` letters before the trap disappeared with the old instance.
The example `ActorDemoBank` (command `ActorDemoBank.Run`) shows why the accounts use `Resume`. The
bank supervises accounts and transfers; every hundredth transfer carries a zero amount, and the
account traps on its precondition. With `Resume` the account balance is kept, the transfer gets no
answer and is counted as refused on `ReceiveTimeout`, and at the end the sum of the balances equals
the initial one. With `Restart` the new account instance would start from the initial balance, and
the total amount of money would change by all operations that went through this account.

`Escalate` passes the failure upward: the parent is suspended and fails before its own parent. If
the parent is then resumed, its suspended children are resumed with it; if it is restarted or
stopped, the children stop. A failure that reaches the root is decided by the root with one
directive: it stops its child, that is, the top-level actor through which the failure passed, and
prints the line `Actor: root stops <name> after trap <number>` to `Console`. The stop takes the
whole subtree with it. The root does not restart: it has no factory it could trust more than the
factory of the failed actor, and nobody could break an endless restart loop at the top level. The
test `TestEscalateReachesRootWhichStops` checks that a watcher of a top-level actor with strategy
`Escalate` receives `Terminated`, and the child receives `Stopped`.

A trap in the handler of `Restarting`, `Stopping` or `Stopped` does not count as a failure: the
actor is leaving anyway, and supervision cannot add anything. Such a trap is noted by the line
`Actor: trap in lifecycle of <name>`. A trap inside `Strategy.Decide` counts as a failure of the
parent.

## 7. Cancelling an Executor

An executor task can be cancelled: `ActorRun.Stop` cancels the whole pool, and a cancelled task is
interrupted by trap 127 at its next switch point. If that point is inside `Receive`, trap 127 passes
through the `Kernel.Try` of the handler. Cancelling the executor does not mean a failure of the
actor: the actor is not at fault, it was interrupted from outside. Therefore trap 127 is not passed
to the supervisor. The letter being processed is lost under the "at most once" rule, the mailbox is
taken off the executor and, if letters remain in it, put back into the pool queue, and the executor
ends with the same trap 127. After the next `ActorRun.Start` the actor continues with its previous
state and the next letter (`TestCancelledExecutorIsNotFailure`: `Started` received once, the
interrupted letter not completed, the next one processed).

If trap 127 counted as a failure, stopping the pool at program exit would turn into an avalanche of
failures, restarts and messages to supervisors that nobody is left to execute.

## 8. The Actor and the Store

`ActorCore.Actor` extends `Stores.Store`. The state of an instance can therefore be externalized and
internalized with the methods `Externalize` and `Internalize`, like the state of any BlackBox model;
the second stage (event journal and snapshots) will not have to change the base type. The extension
causes no interference: `Stores.Store` has no abstract methods, the fields `dlink`, `era`, `id` are
hidden, and the domain (`Domain`) of an actor instance is empty until the actor sets one up. The
factory creates an instance with an ordinary `NEW`.

## 9. Address and Directory

An address is an abstract record `Ref` with the fields `node` and `name` and one method
`Post(m, sender)`. A local address is the mailbox itself (`Context` extends `Ref`). `Tell` calls
`Post` and does not know where the receiver is. The transport module of a future stage will extend
`Ref` with its own type whose `Post` packs the letter and sends it to another process, and
`ActorCore.InitRef` will fill in the node and the name.

An address is looked up by name in the directory `ActorCore.dir`, following the pattern of
`Files.dir`: the abstract type `Directory` with the method `Lookup(node, name)`, the variables `dir`
and `stdDir` and the procedure `SetDir`. The standard directory knows only its own node
(`ActorCore.node = "local"`) and answers `NIL` for another one. A transport will install its own
directory, which serves foreign nodes itself and passes the own node to `stdDir`.

The name of an actor is the string from `Spawn`: `"counter"` for a top-level actor,
`"parent/worker"` for a child, `"$7"` for an actor without a name. Two actors with one name trap in
`Spawn` with code 24. A stopped actor leaves the directory, and its name becomes free.

At the first stage `Watch` and `StopActor` accept only a local address (trap 20): watching and
stopping over the network will come with the transport.

## 10. Measurements

`Actor_devBench` prints `BENCH` lines of the same form as `Async_devBench`. On the development machine
(Linux x86-64, three runs in a row, `tools/test.sh Actor_devBench`) two actors answering each other
with `Respond` spend 78–82 ns per letter, that is 156–164 ns per round trip, with one executor and
with four; a round trip through two `AsyncChan` rendezvous channels costs 389–483 ns in
`docs/measurements.md`. The difference comes from switches: a round trip between tasks over channels
switches stacks four times, while an executor handles both mailboxes in a row and switches 62 times
per thousand letters with one executor and 39 times with four. The exchange makes no allocations.

Creating 10,000 actors on four executors together with handling `Started` takes 10.9–11.0 µs per
actor, delivering one letter to each takes 2.8–4.3 µs per letter. The profile (`tools/prof.sh`)
gives more than 70 % of the samples to the garbage collector: each mailbox has its own queue rings,
and an address keeps two names of 128 characters.

## 11. Limitations

- One OS thread: actors run in turn, and computation in one actor delays the others until the next
  switch point of the executor (an executor yields every 64 mailboxes).
- No persistence: `Restart` creates an instance with the initial state.
- No transport: a `Ref` of another node can be described, but nobody delivers to it.
- One strategy, `OneForOne`; "all for one" is not implemented.
- A letter from the main stack waits up to 10 ms for an executor (section 3).
- Asking oneself with `Ask` always ends in `timedOut`.

## 12. Interface

Trap numbers: 20–25 — violated precondition (the number is given at the procedure), 60 — the
factory returned `NIL`, 100 — violated module invariant, 127 — the task was cancelled.

### 12.1. ActorCore

```
DEFINITION ActorCore;

	IMPORT Stores, AsyncSelect;

	CONST
		Resume = 0; Restart = 1; Stop = 2; Escalate = 3;
		ok = 0; timedOut = 1; dead = 2;
		nameLen = 128;

	TYPE
		Msg = AsyncSelect.Msg;
		Name = ARRAY 128 OF CHAR;

		Ref = POINTER TO ABSTRACT RECORD
			node-, name-: Name;
			(r: Ref) Post (m: Msg; sender: Ref), NEW, ABSTRACT
		END;

		Actor = POINTER TO ABSTRACT RECORD (Stores.Store)
			(a: Actor) Receive (c: Context; m: Msg), NEW, ABSTRACT
		END;

		Behavior = POINTER TO ABSTRACT RECORD
			(b: Behavior) Receive (a: Actor; c: Context; m: Msg), NEW, ABSTRACT
		END;

		Stats = RECORD
			failures: INTEGER;
			since: LONGINT
		END;

		Strategy = POINTER TO ABSTRACT RECORD
			(s: Strategy) Decide (child: Ref; reason: INTEGER; VAR st: Stats): INTEGER, NEW, ABSTRACT
		END;

		Factory = PROCEDURE (): Actor;

		Props = POINTER TO RECORD
			new: Factory;
			capacity: INTEGER;
			dedicated: BOOLEAN;
			strategy: Strategy
		END;

		Dispatcher = POINTER TO ABSTRACT RECORD
			(d: Dispatcher) Schedule (c: Context), NEW, ABSTRACT
		END;

		Directory = POINTER TO ABSTRACT RECORD
			(d: Directory) Lookup (IN node, name: ARRAY OF CHAR): Ref, NEW, ABSTRACT
		END;

		Started = POINTER TO RECORD (Msg) END;
		Stopping = POINTER TO RECORD (Msg) END;
		Stopped = POINTER TO RECORD (Msg) END;
		Restarting = POINTER TO RECORD (Msg) END;
		ReceiveTimeout = POINTER TO RECORD (Msg) END;
		Terminated = POINTER TO RECORD (Msg)
			who-: Ref
		END;

		Context = POINTER TO LIMITED RECORD (Ref)
			host: ANYPTR;
			dedicated-: BOOLEAN;
			(c: Context) Post (m: Msg; sender: Ref);
			(c: Context) Tell (to: Ref; m: Msg), NEW;
			(c: Context) Sender (): Ref, NEW;
			(c: Context) Respond (m: Msg), NEW;
			(c: Context) Parent (): Ref, NEW;
			(c: Context) Watch (r: Ref), NEW;
			(c: Context) SetReceiveTimeout (ms: LONGINT), NEW;
			(c: Context) Become (b: Behavior), NEW;
			(c: Context) Pending (): BOOLEAN, NEW;
			(c: Context) IsStopped (): BOOLEAN, NEW
		END;

	VAR
		dispatcher-: Dispatcher;
		dir-, stdDir-: Directory;
		node-: Name;
		deadLetters-: LONGINT;

	PROCEDURE Process (c: Context; max: INTEGER): BOOLEAN;
	PROCEDURE NewProps (f: Factory): Props;
	PROCEDURE Spawn (parent: Context; p: Props; IN name: ARRAY OF CHAR): Ref;
	PROCEDURE Tell (to: Ref; m: Msg);
	PROCEDURE StopActor (r: Ref);
	PROCEDURE Ask (to: Ref; m: Msg; ms: LONGINT; OUT reply: Msg): INTEGER;
	PROCEDURE InitRef (r: Ref; IN node, name: ARRAY OF CHAR);
	PROCEDURE SetDispatcher (d: Dispatcher);
	PROCEDURE SetDir (d: Directory);

END ActorCore.
```

`Context` is the mailbox of an actor, the context of its handler and the local address. The field
`host` belongs to the executor (`ActorRun` keeps the own task of the actor there). The module takes
no monitors: operations on a mailbox between two switch points are indivisible.

**PROCEDURE (r: Ref) Post (m: Msg; sender: Ref)**
Pre: `m # NIL` — trap 20 in `Context`.
Post: `m` is in the receiver's mailbox or counted as a dead letter; the sender does not wait.

**PROCEDURE (a: Actor) Receive (c: Context; m: Msg)**
Pre: called only by an executor, one call per actor at a time.
Post: the letter is handled; a trap is a failure of the actor, except trap 127 (section 7).

**PROCEDURE (b: Behavior) Receive (a: Actor; c: Context; m: Msg)**
Same as `Actor.Receive`, after `c.Become(b)`; `a` is the actor instance.

**PROCEDURE (s: Strategy) Decide (child: Ref; reason: INTEGER; VAR st: Stats): INTEGER**
Pre: child `child` failed with trap `reason`; `st` is its failure count.
Post: a directive; a value outside `Resume..Escalate` is executed as `Stop`; a trap is a failure of
the parent.

**PROCEDURE (d: Dispatcher) Schedule (c: Context)**
Pre: `c` has work and is not with an executor.
Post: an executor calls `Process(c, ...)` while it returns `TRUE`.

**PROCEDURE (d: Directory) Lookup (IN node, name: ARRAY OF CHAR): Ref**
Post: the address of actor `name` on node `node`; `NIL` — unknown. The standard directory knows only
`node = ActorCore.node`.

**PROCEDURE (c: Context) Tell (to: Ref; m: Msg)**
Pre: `to # NIL` — trap 20; `m # NIL` — trap 21.
Post: `m` is sent to `to`, the sender is `c`.

**PROCEDURE (c: Context) Sender (): Ref**
Post: the sender of the letter being handled; `NIL` — no sender.

**PROCEDURE (c: Context) Respond (m: Msg)**
Pre: `m # NIL` — trap 20.
Post: `m` is sent to the sender of the letter being handled; without a sender, a dead letter.

**PROCEDURE (c: Context) Parent (): Ref**
Post: the address of the parent; `NIL` — the parent is the root.

**PROCEDURE (c: Context) Watch (r: Ref)**
Pre: `r` is a local address — trap 20.
Post: after `r` stops, the handler of `c` receives `Terminated` with `who = r`, once per call; if `r`
has already stopped, at once.

**PROCEDURE (c: Context) SetReceiveTimeout (ms: LONGINT)**
Post: `ms > 0` — after `ms` ms without user letters the handler receives `ReceiveTimeout` once, the
next count starts with the next letter; `ms <= 0` — no timeout. A restart removes the timeout.

**PROCEDURE (c: Context) Become (b: Behavior)**
Post: the following letters are handled by `b.Receive`; `b = NIL` and a restart return
`Actor.Receive`.

**PROCEDURE (c: Context) Pending (): BOOLEAN**
Post: `c` is with an executor.

**PROCEDURE (c: Context) IsStopped (): BOOLEAN**
Post: `c` has stopped; letters to it are dead.

**PROCEDURE Process (c: Context; max: INTEGER): BOOLEAN**
Called by an executor.
Pre: `c # NIL` — trap 20; `c` is with an executor and not being processed — trap 21; `max > 0` —
trap 22.
Post: all system messages and at most `max` user letters are handled; `TRUE` — work remains, `c`
stays with the executor; `FALSE` — no work, `c` is taken off the executor. Cancelling the executor
task inside a handler gives trap 127 after the mailbox is taken off the executor and, if it has
work, handed to `Schedule` again.

**PROCEDURE NewProps (f: Factory): Props**
Pre: `f # NIL` — trap 20.
Post: a description with factory `f`, an unlimited mailbox, a pool executor, no strategy.

**PROCEDURE Spawn (parent: Context; p: Props; IN name: ARRAY OF CHAR): Ref**
Pre: `p # NIL` and `p.new # NIL` — trap 20; `p.capacity >= 0` — trap 21; a dispatcher is installed
— trap 22; `parent = NIL` or `parent` is running — trap 23; the name is free — trap 24; the full name
is shorter than `nameLen` — trap 25; the factory does not trap and does not return `NIL` — trap 60.
Post: a new actor, child of `parent` (`NIL` — of the root); its name is `name`, for a child
"parent name/name", for an empty `name` `"$number"`; the handler receives `Started` first.

**PROCEDURE Tell (to: Ref; m: Msg)**
Pre: `to # NIL` — trap 20; `m # NIL` — trap 21.
Post: `m` is sent to `to` without a sender; no waiting.

**PROCEDURE StopActor (r: Ref)**
Pre: `r` is a local address — trap 20.
Post: `r` receives the stop before its user letters; a stopped `r` does not change.

**PROCEDURE Ask (to: Ref; m: Msg; ms: LONGINT; OUT reply: Msg): INTEGER**
Waits for an answer.
Pre: called from a task — trap 20; `to # NIL` — trap 21; `m # NIL` — trap 22; task cancelled —
trap 127.
Post: `ok` — `reply` is the first letter to the reply address; `timedOut` — no answer within `ms`
ms; `dead` — the question became a dead letter. Except for `ok`, `reply = NIL`.

**PROCEDURE InitRef (r: Ref; IN node, name: ARRAY OF CHAR)**
For transport modules.
Pre: `r # NIL` — trap 20; `node` and `name` shorter than `nameLen` — trap 21.
Post: `r.node = node`, `r.name = name`.

**PROCEDURE SetDispatcher (d: Dispatcher)**
Pre: `d # NIL` — trap 20.
Post: `dispatcher = d`.

**PROCEDURE SetDir (d: Directory)**
Pre: `d # NIL` — trap 20.
Post: `dir = d`; the first installed directory stays in `stdDir`.

### 12.2. ActorSup

```
DEFINITION ActorSup;

	IMPORT ActorCore;

	PROCEDURE NewOneForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy;

END ActorSup.
```

**PROCEDURE NewOneForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy**
Pre: `directive` is `Resume`, `Restart`, `Stop` or `Escalate` — trap 20; `max >= 0` — trap 21;
`withinMs > 0` — trap 22.
Post: the strategy answers a failure of a child with directive `directive`; for `Restart`, with
`Stop` if it is failure number `max + 1` within `withinMs` ms of the first failure of the window.

### 12.3. ActorRun

```
DEFINITION ActorRun;

	VAR
		n-: INTEGER;

	PROCEDURE Start (k: INTEGER);
	PROCEDURE Stop;

END ActorRun.
```

`n` is the number of pool executors; 0 — the pool is stopped.

**PROCEDURE Start (k: INTEGER)**
Pre: `0 < k <= 64` — trap 20; the pool is stopped — trap 21.
Post: a pool of `k` executors is installed in `ActorCore`; mailboxes waiting in the queue are served.

**PROCEDURE Stop**
Post: the pool tasks are cancelled (trap 127 at their switch point); mailboxes with work wait for the
next `Start`. Own tasks of actors are not affected.

### 12.4. ActorDemoBank

```
DEFINITION ActorDemoBank;

	PROCEDURE Run;

END ActorDemoBank.
```

**PROCEDURE Run**
Command. Starts a pool of four executors if it is stopped.
Post: a thousand transfers between ten accounts are complete; the numbers of completed and refused
transfers and the check of the sum of balances against the initial one are printed.
