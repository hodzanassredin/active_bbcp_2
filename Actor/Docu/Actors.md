# Actors

The `Actor` subsystem builds a program from objects that exchange messages and share no state. This
chapter describes actors of one process, their execution by a shared pool of tasks, and the
supervision of children by their parent. Tasks, monitors and switch points are defined in
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
of a node name and an actor name. The node is always the own one: there is no transport between
processes. Sending, watching and stopping go through abstract methods of the address, so the address
of another process takes the same place (section 9).

## 2. The First Actor

A counter accepts `Inc` and answers `Get` with its current value. The module below (the example
`ObxActor0`, page `Obx/Docu/Actor0.md`) creates it, sends a thousand `Inc` from a client task, asks
for the total and stops everything.

```
MODULE ObxActor0;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Get = POINTER TO RECORD (ActorCore.Msg) END;
		Num = POINTER TO RECORD (ActorCore.Msg) n: INTEGER END;
		Counter = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR counter: ActorCore.Ref;

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
		VAR i: INTEGER; inc: Inc; get: Get; r: ActorCore.Msg; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(inc); NEW(get);
		FOR i := 1 TO 1000 DO ActorCore.Tell(counter, inc) END;
		IF ActorCore.Ask(counter, get, 1000, r) = ActorCore.ok THEN
			Strings.IntToString(r(Num).n, s); Console.WriteStr("counter: "); Console.WriteStr(s)
		ELSE Console.WriteStr("counter: no answer")
		END;
		Console.WriteLn;
		ActorRun.Shutdown
	END Run;

	PROCEDURE Do*;
		VAR t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		counter := ActorCore.Spawn(NIL, ActorCore.NewProps(New), "counter");
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor0.
```

The command `ObxActor0.Do` prints `counter: 1000`. An actor is described by a `Props` record: the
instance factory, the mailbox limit, the own-task flag and the strategy for supervising children.
`Spawn` creates an instance with the factory, sets up a mailbox and returns the address; the first
message the handler receives is `Started`. `Tell` puts a message into the mailbox and returns at
once. `Ask` sends a message with a temporary reply address and waits for the first letter to that
address for at most the given number of milliseconds; the handler answers with `c.Respond`, which
addresses the letter to the sender of the current message. `Ask` waits, so the client is a task: on
the main stack `Ask` traps with code 20.

A message is an extension of `AsyncSelect.Msg` (called `ActorCore.Msg` in the subsystem) and does
not change after sending. The counter above sends the same object `inc` a thousand times; this is
allowed exactly because the object is immutable. If the client changed a field of a sent message,
the handler would see the value at the time of processing, and the order of sends would mean
nothing.

At the end the client calls `ActorRun.Shutdown` (section 3), and the command runs the main loop while
there are tasks. Without the common shutdown the executors would wait for work forever, and the
command loop would never end.

## 3. Executors

An actor has no task of its own. Mailboxes with work are served by an *executor*, a task of the
`ActorRun` pool that takes a mailbox from the shared queue, calls `Dispatcher.Process` and returns
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

A free executor waits in `Tasks.ParkExternal`, and `Tell` wakes it with `Tasks.Unpark`. `Unpark`
outside a task, from a command or from a `Services` action, moves the scheduler step to the earliest
deadline by itself, so a letter from the main stack starts to be processed in the next step of the
main loop. `ParkExternal` is a wait for an event from outside: a letter may come from the main
stack, so a free pool does not count as a deadlock (`TestIdlePoolIsNoDeadlock`). Consequence: while
the pool is alive, the scheduler does not report even a complete deadlock of the other tasks.

An actor cannot ask itself with `Ask`: the question would land in its own mailbox, the mailbox is
held by the waiting handler, and no answer would ever come. Therefore `Ask` from the handler of an
actor to that same actor traps with code 23, and the failure goes to the supervisor. The test
`TestAskSelfTraps` checks that the root stops such an actor at once, without waiting for the
question's deadline.

The common shutdown is `ActorRun.Shutdown`. It sends `Stop` to every actor without a parent, waits
until all actors have stopped (children before parents, section 5), then ends the pool tasks: each
executor leaves its loop by itself, without cancellation, so the log shows neither `Tasks: cancelled`
nor `Tasks: deadlock`. The own tasks of actors end together with their actors. If an actor has not
stopped within 5 s (its handler waits without end), `Shutdown` cancels the pool tasks like `Stop`, and
that actor remains. In a task `Shutdown` waits with `Tasks.Sleep`, on the main stack it runs the main
loop; from an executor task it traps with code 20: the executor would wait for itself. The test
`TestShutdownLeavesNoTasks` checks that no task remains afterwards and the scheduler saw no
deadlock; the examples `ObxActor0`–`ObxActor2` check a clean log.

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
| `dead` | the question became a dead letter, or the receiver stopped without answering |

The answer `dead` comes without waiting for the deadline: in the test
`TestStoppedActorGivesDeadLetters` a question with a 1000 ms limit to a stopped actor returns in
less than 500 ms. While the question is open, the reply address watches the receiver, as `Watch`
does; a receiver that took the question and stopped without `Respond` therefore also gives `dead`
at once (`TestAskDeadWhenTargetStopsWithoutReply`: an actor silent on a question with a 2000 ms limit
is stopped after 50 ms, and `Ask` returns in less than 1000 ms). An answer arriving after the
timeout is lost.

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
receives `Terminated` with the field `who = r` once. Another `Watch` of the same `r` adds no
subscription: two calls give one `Terminated` (`TestWatchTwiceGivesOneTerminated`). A subscription to
an already stopped actor answers `Terminated` at once (`TestWatchStoppedGetsTerminatedAtOnce`).
`c.Unwatch(r)` removes the subscription; a `Terminated` sent before `Unwatch` but not yet handled does
not reach the handler either (`TestUnwatchGivesNoTerminated`). Stopping a watcher removes all its
subscriptions, so the watcher list of `r` does not grow with departed watchers.

`c.SetReceiveTimeout(ms)` asks to be told about idleness. After `ms` milliseconds without user
letters the handler receives `ReceiveTimeout` once; the next count starts with the next letter. In
the test `TestReceiveTimeoutOnceAfterIdle`, with a limit of 100 ms, ten letters 20 ms apart give no
`ReceiveTimeout`, then 250 ms of silence give exactly one, and another 250 ms of silence give none.
Life-cycle messages and `ReceiveTimeout` itself do not restart the count.

The receive timeout neither overtakes letters nor gets lost. While the handler runs or letters wait
in the mailbox, the actor is not idle, and `ReceiveTimeout` is not given: the deadline is checked
again later (`TestTimeoutDoesNotOvertakeMail`: a letter waiting behind a long handler is handled
before the timeout). A suspended actor (section 6) gets no timeout, and after `Resume` the count
starts again (`TestTimeoutNotLostWhileSuspended`).

`c.Become(b)` replaces the handler: the following letters go to `b.Receive(a, c, m)`, where `a` is
the actor instance. `Become(NIL)` returns the instance's `Receive`, and so does a restart
(`TestRestartDropsBecome`). `Become` and `SetReceiveTimeout` change how the actor's own letters are
handled, so only its handler calls them; a call from anywhere else traps with code 20
(`TestHandlerOnlyCallsTrapOutside`). A restart removes the timeout too
(`TestRestartDropsReceiveTimeout`).

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

A restart without a journal loses the state. The module below (the example `ObxActor1`) counts
letters, traps on the third one and restarts: after the restart the count starts again.

```
MODULE ObxActor1;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorSup, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Boom = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Parent = POINTER TO RECORD (ActorCore.Actor) END;
		Client = POINTER TO RECORD (Tasks.Task) END;

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

	PROCEDURE (t: Client) Run;
		VAR inc: Inc; boom: Boom; s: ARRAY 16 OF CHAR;
	BEGIN
		WHILE worker = NIL DO Tasks.Sleep(1) END;
		NEW(inc); NEW(boom);
		ActorCore.Tell(worker, inc); ActorCore.Tell(worker, inc);
		ActorCore.Tell(worker, boom);
		ActorCore.Tell(worker, inc);
		Tasks.Sleep(100);
		Strings.IntToString(starts, s); Console.WriteStr("worker: Started "); Console.WriteStr(s);
		Strings.IntToString(last, s); Console.WriteStr(", count after restart "); Console.WriteStr(s);
		Console.WriteLn;
		ActorRun.Shutdown
	END Run;

	PROCEDURE Do*;
		VAR p: ActorCore.Props; t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		starts := 0; last := 0; worker := NIL;
		p := ActorCore.NewProps(NewParent);
		p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		IF ActorCore.Spawn(NIL, p, "parent") = NIL THEN END;
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor1.
```

The command `ObxActor1.Do` prints `worker: Started 2, count after restart 1`: the two `Inc` letters
before the trap disappeared with the old instance. A restart first stops the children of the child
and only then creates the new instance (`TestRestartStopsGrandchildrenFirst`: `Stopped` of the
grandchild is recorded before `Started` of the new instance). The `OneForOne` restart window moves:
a failure after the end of the window gives `Restart` again, not `Stop` (`TestRestartWindowResets`).

The example `ObxActor2` shows why the accounts use `Resume`. The bank supervises accounts and
transfers; every hundredth transfer carries a zero amount, and the account traps on its
precondition. With `Resume` the account balance is kept, the transfer gets no answer and is counted
as refused on `ReceiveTimeout`, and at the end the sum of the balances equals the initial one. With
`Restart` the new account instance would start from the initial balance, and the total amount of
money would change by all operations that went through this account.

The factory may fail on a restart as well. Then the child has no instance, and its mailbox is
suspended again with a failure reported to the parent. `Resume` for such a child would mean handling
letters with a `NIL` instance: every letter traps, and the circle of failures never ends. Therefore
`Resume` for a child without an instance is executed as `Stop`
(`TestFactoryFailAfterRestartStopsOnResume`). A factory that returned `NIL` fails with number 26.

`Escalate` passes the failure upward: the parent is suspended and fails before its own parent. If
the parent is then resumed, its suspended children are resumed with it; if it is restarted or
stopped, the children stop. A failure that reaches the root is decided by the root with one
directive: it stops its child, that is, the top-level actor through which the failure passed, and
prints the line `Actor: root stops <name> after trap <number>` to `Console`. The stop takes the
whole subtree with it. The root does not restart: it has no factory it could trust more than the
factory of the failed actor, and nobody could break an endless restart loop at the top level. The
test `TestEscalateReachesRootWhichStops` checks that a watcher of a top-level actor with strategy
`Escalate` receives `Terminated`, and the child receives `Stopped`.

A suspended parent makes no decisions about its children. While its own parent decides its fate, the
failure of another child is put aside and decided after the resume; if the parent is restarted or
stopped, the children stop with it, and the put-aside decision is not needed. Otherwise the strategy
would restart children of a parent that is about to be stopped. The test
`TestSuspendedParentDefersDecisions` checks that during 100 ms, while the parent's fate is decided,
the second failed child is not restarted, and after the resume it is.

A trap in the handler of `Restarting`, `Stopping` or `Stopped` does not count as a failure: the
actor is leaving anyway, and supervision cannot add anything. Such a trap is noted by the line
`Actor: trap in lifecycle of <name>`. A trap inside `Strategy.Decide` counts as a failure of the
parent; the decision about the child is put aside until the parent is resumed.

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

Cancellation interrupts only a user handler. A step of the life cycle or of supervision is carried
to its end, otherwise the actor would get stuck halfway: a stop without `Stopped` and without leaving
the directory, a parent never told that its child left, a child suspended forever without a
decision. An interrupted handler of `Started`, `Stopping`, `Stopped` or `Restarting` counts as done,
and the transition goes on in the same task (`TestCancelInStoppingFinishesStop`). An interrupted
factory or `Decide` is repeated: its system letter goes back to the head of the queue, and the next
executor calls it again (`TestCancelInFactoryRetries`, `TestCancelInDecideKeepsFailure`). Then the
task is interrupted with trap 127.

The same rule holds for an actor with its own task: cancelling that task is neither a failure nor a
stop. The mailbox is taken off the task, and on the next work `ActorRun` gives the actor a new task
(`TestCancelledDedicatedTaskStillAnswers`: after cancelling the task that waited inside `Receive`,
the actor answers the next question). Only a stop (`StopActor`) stops an actor: cancelling a task is
a tool of the executor, not of the actor's owner.

## 8. State That Survives a Restart

`ActorCore.Actor` extends `Stores.Store`, but the core of the subsystem does not call the store
methods: messages are not serialized, and `Restart` makes a new instance with the factory (section 6,
`ObxActor1`). State that must survive a trap, a restart and a process crash is kept by the module
`ActorPersist`.

**Reason.** A restart cures a failure by throwing the broken state away. An account whose balance is
thrown away together with the error cannot be cured that way, and `Resume` (`ObxActor2`) keeps the
state but does not save it from a process crash.

**Reasoning.** Saving the whole state after each message is expensive and loses the cause of the
change. So what is saved is not the state but an *event*, a record of what has already happened
("30 withdrawn by operation 1"). The state is a fold of the events by one pure procedure `Apply`.
Then:

- recovery is the same fold: read the events and apply them in order;
- the event journal is a ready history of changes: who, when and for what cause;
- to avoid folding a million events, a *snapshot* is saved every `snapEvery` events: the frame of the
  state after event number `seq`; recovery takes the snapshot and applies only the tail.

The command handler of a persistent actor takes three steps: it checks the command against the
current state, calls `Persist(c, e, cause)` and does not touch the state any more. `Persist` encodes
the event, appends it to the stream named after the actor, expecting number `seq`, and only after a
successful write calls `Apply(e)`. Recovery runs while `Started` is handled, both at creation and
after a restart: the snapshot, then the events after it, then the handler gets `ReplayComplete`. The
store is synchronous, and recovery has no switch points; messages that arrive meanwhile wait in the
mailbox and come after `ReplayComplete` (`TestCommandsWaitForReplay`).

Each event has metadata that the journal keeps next to the frame and returns without parsing the
body: the number `seq`, the write time `time` (UTC, `YYYYMMDDhhmmss`), the cause `cause`, a number the
handler passed (an operation number, a command number), and the author `author`, the sender of the
command, or the actor itself when there is no sender. The operation number also serves retries: if
the operation is already in the state, the handler writes no event, and a repeated command does not
withdraw twice. The table of done operations is part of the state; the same `Apply` restores it.
The table remembers the last N numbers (16 in `ObxActor3`): a new number pushes out the oldest one.
A retry usually comes soon after the first attempt, and a table without a bound would grow together
with the snapshot. Cost: a repeat of an operation older than the last N withdraws again
(`TestOldOperationsForgotten`); N is chosen larger than the number of operations one client can have
"in flight".

**Example.** The account from `ObxActor3` (a journal in a `Kv` base):

```
	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO INC(a.balance, e.sum)
		| e: Withdrawn DO DEC(a.balance, e.sum); a.ops[a.nOps MOD 16] := e.op; INC(a.nOps)
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR d: Deposited; w: Withdrawn; b: Balance;
	BEGIN
		WITH m: ActorPersist.ReplayComplete DO INC(starts)
		| m: Deposit DO NEW(d); d.sum := m.sum; a.Persist(c, d, 0)
		| m: Withdraw DO
			IF ~a.Done(m.op) THEN NEW(w); w.sum := m.sum; w.op := m.op; a.Persist(c, w, m.op) END
		| m: Crash DO HALT(42)
		| m: GetBalance DO NEW(b); b.n := a.balance; c.Respond(b)
		ELSE
		END
	END Handle;
```

After a trap, after a restart and after reopening the base the balance is the same, and a repeat of
operation 1 withdraws nothing. `ObxActor4` shows snapshots and a history with author and cause;
`ObxActor5` shows a report of account turnover that reads the same streams apart from the accounts
(CQRS) and catches up with the journal by the number of the last event read.

**Violation.** A handler that changes the state itself, bypassing `Apply`, loses the change on a
restart: recovery does not repeat it. An `Apply` that sends messages or writes to the journal
repeats them on every recovery. A trap between the write and `Apply` does not lose the event: it is
already in the journal and is applied on recovery (`TestTrapAfterPersistKeepsEvent`); a trap before
`Persist` leaves no trace (`TestTrapBeforePersistLosesEvent`).

Failure rules:

| Case | What happens |
|---|---|
| another writer appended to the stream (the number does not match) | `Append` writes nothing and answers `conflict`; `Persist` traps `conflictTrap` (70), the event is not applied; on `Restart` the actor reads the stream again with the foreign event (`TestConflictFailsActor`) |
| a store error on write or read | trap `storeTrap` (71); the supervisor decides: retry (`Restart`) or stop |
| a frame cannot be read (type not found, version out of range) | the codec answers `unknown` and does not trap; recovery traps `decodeTrap` (72), the reader `ActorPersist.Read` gets `res = corrupt` (`TestUnknownEventType`) |

Waiting: `Persist` is synchronous and has no switch points, so while it writes the executor is busy.
A slow store delays the whole pool; an actor for which this matters is created with
`Props.dedicated` (section 3).

Event versions. `Stores.Writer.WriteStore` writes the frame: the type name, then the fields from
`Externalize`. The type reads its version number with `ReadVersion(min, max, v)` and parses the
fields by `v`. A new field is a new version: `max` grows, old frames are read with the old layout,
and the new field gets its default. A frame from a future version and a frame of an unknown type are
`unknown` (`TestVersionRule`). An event type must not be renamed or removed while journals hold its
frames.

**Cost.** Each change is the encoding of a frame through `Stores` and a write to the journal;
recovery without a snapshot goes through all events of the stream (measurements in section 10). The
journal grows without bound: events before a snapshot are not deleted, because the audit needs the
history. The file journal (`ActorFileJournal`) is a temporary store until an ordered key-value
store: one process per file, `Flush` without `fsync` (a power failure can take the last records; on
open they are dropped as a torn tail), the stream index in memory, and opening reads the whole file.

The journal follows the pattern of `Files.Directory`: the abstract `ActorJournal.Journal`, the
variables `dir` and `stdDir` and `SetDir`. The codec (`ActorCodec`) is replaceable the same way. There
are three journal implementations:

| | memory (`NewMem`) | append-only file (`ActorFileJournal`) | `Kv` base (`ActorKvJournal`) |
|---|---|---|---|
| survives a process crash | no | yes, except records that did not reach the medium | yes |
| durability of a write | — | `Flush` without `fsync` | commit: pages, `Sync`, meta, `Sync` |
| transactions | — | no; a torn tail is dropped on open | yes: `Append` is one write transaction |
| processes | one | one per file | one per base |
| opening | — | reads the whole file, index in memory | reads the meta; tree lookup |
| writing an event (section 10) | 4.7–4.9 µs | 6.4–7.0 µs | 1.6–1.7 ms (two `Sync`) |

The append-only file stays as a simple implementation for examples and tests without a base; where
an event must survive a power failure, the journal is `ActorKvJournal`.

The `Kv` journal keeps an event under the key `"e" + stream + 0X + number` (8 bytes, most significant
first) and the snapshot under `"s" + stream`. A cursor finds the last number of a stream: `Seek` past
the last possible key of the stream and a step back; a separate counter key is not needed, it would
repeat what the keys already hold. `KvDb.Update` and `View` have no switch points inside, and `Sync`
holds the executor until the write reaches the medium: for actors on the shared pool the whole pool
waits. `Begin(TRUE)` of another task waits in `Tasks.Park` while a write transaction is open; as long
as journal transactions do not switch, no other task finds one open. An actor that writes often gets
its own task (`Props.dedicated`).

Two `Sync` per event limit the journal to about 600 events per second. So `Append` from a task does
not commit by itself but joins the queue of the journal. The first task in an empty queue becomes
the leader: it yields once, so that other writers can join the queue, and commits the whole queue in
one transaction. Each request checks its expected number in that transaction: a conflict in one
stream does not hinder the others. The other tasks wait in `Tasks.Park` and get `ok` only after
their batch is committed; a failure in the middle of the commit leaves the base without the whole
batch (`TestKvGroupTorn`). A batch is formed from tasks that write at the same time: eight actors on
a pool of four executors give batches of at most four, actors with `Props.dedicated` as many as there
are actors. `Append` outside a task commits by itself.

## 9. Address and Directory

An address is an abstract record `Ref` with the fields `node` and `name` and four abstract methods:
`Post(m, sender)` delivers a letter, `AddWatcher(w)` and `RemoveWatcher(w)` subscribe and unsubscribe
a watcher, `Stop` stops the receiver. A local address is the mailbox itself (`Context` extends
`Ref`). `Tell` calls `Post`, `c.Watch` and `c.Unwatch` call `AddWatcher` and `RemoveWatcher`,
`StopActor` calls `Stop`; none of them knows where the receiver is. A transport between processes
would extend `Ref` with its own type, and `ActorCore.InitRef` fills in its node and name.

The reply address of `Ask` is a `Ref` too. While `Ask` waits, the address stands in a registry under
the name `"$ask/number"`, and the directory finds it by that name like an actor; after `Ask` returns, and after
the waiting task is cancelled, the name leaves the directory (`TestCancelledAskLeavesNoReplyAddress`).

An address is looked up by name in the directory `ActorCore.dir`, following the pattern of
`Files.dir`: the abstract type `Directory` with the method `Lookup(node, name)`, the variables `dir`
and `stdDir` and the procedure `SetDir`. The standard directory knows only its own node
(`ActorCore.node = "local"`) and answers `NIL` for another one.

The name of an actor is the string from `Spawn`: `"counter"` for a top-level actor,
`"parent/worker"` for a child, `"$7"` for an actor without a name. Two actors with one name trap in
`Spawn` with code 24. A stopped actor leaves the directory, and its name becomes free.

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

Persistence (`TestSlowPersist`, one executor, 100 000 events with 4 bytes of payload, three runs):
`Persist` to a memory journal takes 4.7–4.9 µs per event (about 200 000 events per second), to a
file 6.4–7.0 µs (about 150 000 per second; `Flush` after each write, no `fsync`). The `Kv` journal
in a disk file (`TestSlowPersist`, 2000 events) takes 1.6–1.7 ms per event (about 600 per second),
the cost of two `Sync` per commit; recovery of 2000 events without a snapshot takes 7 ms. Group
commit (`TestSlowKvGroupCommit`, 2048 events, three runs) changes nothing for one writer, 1.8–4.7 ms
per event; with 8 writer tasks it gives 0.25–1.4 ms, with 64, 0.44–0.52 ms; without it all three
cases cost 1.8–4.0 ms. The spread comes from the disk: its `Sync` time is not constant. Recovery of
100 000 events without a snapshot takes 0.38–0.51 s from memory and 0.38–0.41 s from the file; with
a snapshot after the last event, 6–12 µs. Lines `BENCH persist-*` and `recover-*`.

## 11. Limitations

- One OS thread: actors run in turn, and computation in one actor delays the others until the next
  switch point of the executor (an executor yields every 64 mailboxes).
- Persistence only for `ActorPersist.Actor`; a `Kv` write costs two `Sync` per batch; a batch is
  formed only from tasks writing at the same time (section 8).
- No transport: a `Ref` of another node can be described, but nobody delivers to it.
- One strategy, `OneForOne`; "all for one" is not implemented.
- While the pool is alive, the scheduler reports no deadlock of other tasks (section 3).
- `Ask` to itself from the handler traps with code 23.

## 12. Interface

Trap numbers: 20–26 — violated precondition (the number is given at the procedure), 100 — violated
module invariant, 127 — the task was cancelled.

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
			(r: Ref) Post (m: Msg; sender: Ref), NEW, ABSTRACT;
			(r: Ref) AddWatcher (w: Ref), NEW, ABSTRACT;
			(r: Ref) RemoveWatcher (w: Ref), NEW, ABSTRACT;
			(r: Ref) Stop, NEW, ABSTRACT
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
			(d: Dispatcher) Schedule (c: Context), NEW, ABSTRACT;
			(d: Dispatcher) Process (c: Context; max: INTEGER): BOOLEAN, NEW;
			(d: Dispatcher) Pending (c: Context): BOOLEAN, NEW;
			(d: Dispatcher) Stopped (c: Context): BOOLEAN, NEW;
			(d: Dispatcher) Host (c: Context): ANYPTR, NEW;
			(d: Dispatcher) SetHost (c: Context; h: ANYPTR), NEW;
			(d: Dispatcher) Live (): INTEGER, NEW;
			(d: Dispatcher) StopRoots, NEW
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
			dedicated-: BOOLEAN;
			(c: Context) Post (m: Msg; sender: Ref);
			(c: Context) AddWatcher (w: Ref);
			(c: Context) RemoveWatcher (w: Ref);
			(c: Context) Stop;
			(c: Context) Tell (to: Ref; m: Msg), NEW;
			(c: Context) Sender (): Ref, NEW;
			(c: Context) Respond (m: Msg), NEW;
			(c: Context) Parent (): Ref, NEW;
			(c: Context) Watch (r: Ref), NEW;
			(c: Context) Unwatch (r: Ref), NEW;
			(c: Context) SetReceiveTimeout (ms: LONGINT), NEW;
			(c: Context) Become (b: Behavior), NEW
		END;

	VAR
		dispatcher-: Dispatcher;
		dir-, stdDir-: Directory;
		node-: Name;
		deadLetters-: LONGINT;

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

`Context` is the mailbox of an actor, the context of its handler and the local address. The module
takes no monitors: operations on a mailbox between two switch points are indivisible. The methods of
`Dispatcher` other than `Schedule` serve the executor module (`ActorRun`) and work only for the
executor installed by `SetDispatcher`: for any other `Dispatcher` they trap with code 20.

**PROCEDURE (r: Ref) Post (m: Msg; sender: Ref)**
Pre: `m # NIL` — trap 20.
Post: `m` is in the receiver's mailbox or counted as a dead letter; the sender does not wait.

**PROCEDURE (r: Ref) AddWatcher (w: Ref)**
Called by `c.Watch` and `Ask`.
Pre: `w # NIL` — trap 20.
Post: after `r` has stopped, watcher `w` receives `Terminated` with `who = r` once; `r` already
stopped — receives it at once; another call with the same `w` adds nothing.

**PROCEDURE (r: Ref) RemoveWatcher (w: Ref)**
Pre: `w # NIL` — trap 20.
Post: `w` does not watch `r`.

**PROCEDURE (r: Ref) Stop**
Post: `r` receives the stop before its user letters; a stopped `r` does not change.

For the reply address of `Ask` the methods `AddWatcher`, `RemoveWatcher` and `Stop` do nothing.

**PROCEDURE (a: Actor) Receive (c: Context; m: Msg)**
Pre: called only by an executor, one call per actor at a time.
Post: the letter is processed; a trap is a failure of the actor, except trap 127 (section 7).

**PROCEDURE (b: Behavior) Receive (a: Actor; c: Context; m: Msg)**
The same as `Actor.Receive`, after `c.Become(b)`; `a` is the actor instance.

**PROCEDURE (s: Strategy) Decide (child: Ref; reason: INTEGER; VAR st: Stats): INTEGER**
Pre: child `child` failed with trap `reason`; `st` is its failure count.
Post: a directive; a value outside `Resume..Escalate` is executed as `Stop`, and so is `Resume` for a
child without an instance; a trap is a failure of the parent, the decision is put aside until the
parent is resumed.

**PROCEDURE (d: Dispatcher) Schedule (c: Context)**
Pre: `c` has work, `c` is not at an executor.
Post: the executor calls `d.Process(c, ...)` while it returns `TRUE`.

**PROCEDURE (d: Dispatcher) Process (c: Context; max: INTEGER): BOOLEAN**
Pre: `d` is the installed executor — trap 20; `c # NIL` — trap 21; `c` is at an executor and not
being processed — trap 22; `max > 0` — trap 23; called from a task — trap 24.
Post: all system messages and at most `max` user messages are processed; `TRUE` — work remains, `c`
is still at the executor; `FALSE` — no work, `c` is taken off the executor. Cancelling the executor
task gives trap 127 after the life-cycle step is carried to its end, the mailbox is taken off the
executor and, if it has work, handed to `Schedule` again (section 7).

**PROCEDURE (d: Dispatcher) Pending (c: Context): BOOLEAN**
Pre: `d` is the installed executor — trap 20.
Post: `c` is at an executor.

**PROCEDURE (d: Dispatcher) Stopped (c: Context): BOOLEAN**
Pre: `d` is the installed executor — trap 20.
Post: `c` is stopped; letters to it are dead.

**PROCEDURE (d: Dispatcher) Host (c: Context): ANYPTR**
Pre: `d` is the installed executor — trap 20.
Post: the executor's data at mailbox `c` (`ActorRun` keeps the actor's own task here); `NIL` — none.

**PROCEDURE (d: Dispatcher) SetHost (c: Context; h: ANYPTR)**
Pre: `d` is the installed executor — trap 20.
Post: `d.Host(c) = h`.

**PROCEDURE (d: Dispatcher) Live (): INTEGER**
Pre: `d` is the installed executor — trap 20.
Post: the number of actors not yet stopped.

**PROCEDURE (d: Dispatcher) StopRoots**
Pre: `d` is the installed executor — trap 20.
Post: every actor without a parent that is not stopped has received a stop.

**PROCEDURE (d: Directory) Lookup (IN node, name: ARRAY OF CHAR): Ref**
Post: the address of the actor or waiting reply address `name` on node `node`; `NIL` — unknown. The
standard directory knows only `node = ActorCore.node`.

**PROCEDURE (c: Context) Tell (to: Ref; m: Msg)**
Pre: `to # NIL` — trap 20; `m # NIL` — trap 21.
Post: `m` is sent to `to`, the sender is `c`.

**PROCEDURE (c: Context) Sender (): Ref**
Post: the sender of the letter being processed; `NIL` — no sender.

**PROCEDURE (c: Context) Respond (m: Msg)**
Pre: `m # NIL` — trap 20.
Post: `m` is sent to the sender of the letter being processed; without a sender it is a dead letter.

**PROCEDURE (c: Context) Parent (): Ref**
Post: the address of the parent; `NIL` — `c` has no parent.

**PROCEDURE (c: Context) Watch (r: Ref)**
Pre: `r # NIL` — trap 20.
Post: after `r` has stopped, the handler of `c` receives `Terminated` with `who = r` once; another
`Watch` of the same `r` adds nothing; `r` already stopped — receives it at once. Stopping `c` removes
its subscriptions.

**PROCEDURE (c: Context) Unwatch (r: Ref)**
Pre: `r # NIL` — trap 20.
Post: `c` does not receive `Terminated` from `r`, even one sent before `Unwatch`.

**PROCEDURE (c: Context) SetReceiveTimeout (ms: LONGINT)**
Pre: called from the handler of `c` — trap 20.
Post: `ms > 0` — after `ms` ms without user letters the handler receives `ReceiveTimeout` once, the
next count starts with the next letter; the timeout does not overtake waiting letters and is counted
again after `Resume`; `ms <= 0` — no timeout. A restart removes the timeout.

**PROCEDURE (c: Context) Become (b: Behavior)**
Pre: called from the handler of `c` — trap 20.
Post: the following letters are handled by `b.Receive`; `b = NIL` and a restart return
`Actor.Receive`.

**PROCEDURE NewProps (f: Factory): Props**
Pre: `f # NIL` — trap 20.
Post: a description with factory `f`, an unlimited mailbox, a pool executor, no strategy.

**PROCEDURE Spawn (parent: Context; p: Props; IN name: ARRAY OF CHAR): Ref**
Pre: `p # NIL` and `p.new # NIL` — trap 20; `p.capacity >= 0` — trap 21; an executor is installed —
trap 22; `parent = NIL` or `parent` is running — trap 23; the name is free — trap 24; the full name
is shorter than `nameLen` — trap 25; the factory does not return `NIL` — trap 26. A trap of the
factory is a trap of the caller.
Post: a new actor, a child of `parent` (`NIL` — of the root); the name is `name`, for a child
"parent name/name", an empty `name` gives `"$number"`; the handler receives `Started` first.

**PROCEDURE Tell (to: Ref; m: Msg)**
Pre: `to # NIL` — trap 20; `m # NIL` — trap 21.
Post: `m` is sent to `to` without a sender; no waiting.

**PROCEDURE StopActor (r: Ref)**
Pre: `r # NIL` — trap 20.
Post: `r.Stop`.

**PROCEDURE Ask (to: Ref; m: Msg; ms: LONGINT; OUT reply: Msg): INTEGER**
Waits for an answer.
Pre: called from a task — trap 20; `to # NIL` — trap 21; `m # NIL` — trap 22; not called from the
handler of `to` itself — trap 23; cancelling the task — trap 127.
Post: `ok` — `reply` is the first letter to the reply address; `timedOut` — no answer within `ms` ms;
`dead` — the question became a dead letter or `to` stopped without answering. Except for `ok`,
`reply = NIL`.

**PROCEDURE InitRef (r: Ref; IN node, name: ARRAY OF CHAR)**
For transport modules.
Pre: `r # NIL` — trap 20; `node` and `name` shorter than `nameLen` — trap 21.
Post: `r.node = node`, `r.name = name`.

**PROCEDURE SetDispatcher (d: Dispatcher)**
For executor modules.
Pre: `d # NIL` — trap 20.
Post: `dispatcher = d`; the methods of `Dispatcher` other than `Schedule` work only for `d`.

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
Post: the strategy answers a child's failure with directive `directive`; for `Restart` it answers
`Stop` if this is failure number `max + 1` within `withinMs` ms from the first failure of the window.

### 12.3. ActorRun

```
DEFINITION ActorRun;

	VAR
		n-: INTEGER;

	PROCEDURE Start (k: INTEGER);
	PROCEDURE Stop;
	PROCEDURE Shutdown;

END ActorRun.
```

`n` is the number of executors in the pool; 0 — the pool is stopped.

**PROCEDURE Start (k: INTEGER)**
Pre: `0 < k <= 64` — trap 20; the pool is stopped — trap 21.
Post: a pool of `k` executors is installed in `ActorCore`; mailboxes that waited in the queue are
served.

**PROCEDURE Stop**
Post: the pool tasks are cancelled (trap 127 at their switch point); mailboxes with work wait for the
next `Start`. The own tasks of actors are not affected.

**PROCEDURE Shutdown**
The common shutdown.
Pre: not called from an executor task — trap 20.
Post: every actor without a parent has received a stop, all actors are stopped (children before
parents); the pool tasks ended by themselves, without cancellation; the own tasks of actors end
together with the actors; `n = 0`. An actor that has not stopped within 5 s remains, and the pool
tasks are then cancelled.

### 12.4. ActorJournal

```
DEFINITION ActorJournal;

	CONST
		ok = 0; conflict = 1; absent = 2; failed = 3; corrupt = 4;
		nameLen = 128;

	TYPE
		Bytes = POINTER TO ARRAY OF BYTE;
		Name = ARRAY 128 OF CHAR;

		Meta = RECORD
			seq, time, cause: LONGINT;
			author: Name
		END;

		Sink = POINTER TO ABSTRACT RECORD
			(s: Sink) Event (IN m: Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN, NEW, ABSTRACT
		END;

		Journal = POINTER TO ABSTRACT RECORD
			(j: Journal) Last (IN stream: ARRAY OF CHAR): LONGINT, NEW, ABSTRACT;
			(j: Journal) Append (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: Meta;
				IN body: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
			(j: Journal) Read (IN stream: ARRAY OF CHAR; from: LONGINT; s: Sink; OUT res: INTEGER), NEW, ABSTRACT;
			(j: Journal) SaveSnapshot (IN stream: ARRAY OF CHAR; seq: LONGINT;
				IN data: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
			(j: Journal) LoadSnapshot (IN stream: ARRAY OF CHAR; OUT seq: LONGINT;
				OUT data: Bytes; OUT len, res: INTEGER), NEW, ABSTRACT;
			(j: Journal) Close, NEW, EMPTY
		END;

	VAR
		dir-, stdDir-: Journal;

	PROCEDURE NewMem (): Journal;
	PROCEDURE SetDir (j: Journal);
	PROCEDURE Now (): LONGINT;

END ActorJournal.
```

A journal is a set of streams; a stream is events numbered 1, 2, 3 … without gaps. `res`: `ok`;
`conflict`, the expected number does not match; `absent`, there is no snapshot; `failed`, an
environment error; `corrupt`, the data cannot be read.

`Meta` is the event metadata: the number `seq`, the write time `time` (UTC, `YYYYMMDDhhmmss`), the
cause `cause`, the author `author`. The journal keeps them apart from the body and returns them
without parsing the body.

**PROCEDURE (s: Sink) Event (IN m: Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN**
The receiver of `Read`. `body[0 .. len-1]` is the event frame. Post: `FALSE` stops reading.

**PROCEDURE (j: Journal) Last (IN stream: ARRAY OF CHAR): LONGINT**
Post: the number of the last event of the stream; 0 for an empty stream.

**PROCEDURE (j: Journal) Append (IN stream; expected: LONGINT; IN m: Meta; IN body; len: INTEGER; OUT res: INTEGER)**
Pre: `stream # ""`, trap 20; `m.seq = expected + 1`, 21; `0 <= len <= LEN(body)`, 22.
Post: `res = ok`, the event is written with number `m.seq`; `res = conflict`,
`Last(stream) # expected`, nothing is written; otherwise `failed`.

**PROCEDURE (j: Journal) Read (IN stream; from: LONGINT; s: Sink; OUT res: INTEGER)**
Pre: `s # NIL`, trap 20. Post: the events numbered `>= from` are given to `s.Event` in ascending
order while `s.Event` answers `TRUE`; `res = ok` or a journal error.

**PROCEDURE (j: Journal) SaveSnapshot (IN stream; seq: LONGINT; IN data; len: INTEGER; OUT res: INTEGER)**
Pre: `0 < seq <= Last(stream)`, trap 20; `0 <= len <= LEN(data)`, 21.
Post: `res = ok`, the snapshot `data[0 .. len-1]` after event `seq` replaced the previous snapshot
of the stream. The events before the snapshot stay.

**PROCEDURE (j: Journal) LoadSnapshot (IN stream; OUT seq: LONGINT; OUT data: Bytes; OUT len, res: INTEGER)**
Post: `res = ok`, the last snapshot of the stream; `seq` is the number of the event after which it
was taken; `res = absent`, there is no snapshot, `seq = 0`.

**PROCEDURE (j: Journal) Close**
Post: the journal resources are released; the journal is not used after `Close`.

**PROCEDURE NewMem (): Journal**
Post: a new empty journal in memory.

**PROCEDURE SetDir (j: Journal)**
Pre: `j # NIL`, trap 20. Post: `dir = j`; the first journal installed stays in `stdDir`. When the
module is loaded, `dir` is a memory journal.

**PROCEDURE Now (): LONGINT**
Post: the current UTC time as `YYYYMMDDhhmmss`.

### 12.5. ActorCodec

```
DEFINITION ActorCodec;

	IMPORT Stores, ActorJournal;

	CONST
		ok = 0; unknown = 1;

	TYPE
		Codec = POINTER TO ABSTRACT RECORD
			(c: Codec) Encode (s: Stores.Store; OUT data: ActorJournal.Bytes; OUT len: INTEGER), NEW, ABSTRACT;
			(c: Codec) Decode (IN data: ARRAY OF BYTE; len: INTEGER; OUT s: Stores.Store;
				OUT res: INTEGER), NEW, ABSTRACT
		END;

	VAR
		dir-, stdDir-: Codec;

	PROCEDURE SetDir (c: Codec);

END ActorCodec.
```

The standard codec writes an object with `Stores.Writer.WriteStore` (the type name, then the fields
from `Externalize`) and reads it with `Stores.Reader.ReadStore`.

**PROCEDURE (c: Codec) Encode (s: Stores.Store; OUT data: ActorJournal.Bytes; OUT len: INTEGER)**
Pre: `s # NIL`, trap 20. Post: `data[0 .. len-1]` is the frame of `s`.

**PROCEDURE (c: Codec) Decode (IN data; len: INTEGER; OUT s: Stores.Store; OUT res: INTEGER)**
Pre: `0 < len <= LEN(data)`, trap 20.
Post: `res = ok`, `s` is a new object from the frame; `res = unknown`, the type is not found or the
version is out of range, `s = NIL`.

**PROCEDURE SetDir (c: Codec)**
Pre: `c # NIL`, trap 20. Post: `dir = c`; the first codec installed stays in `stdDir`.

### 12.6. ActorPersist

```
DEFINITION ActorPersist;

	IMPORT Stores, ActorCore, ActorJournal;

	CONST
		conflictTrap = 70; storeTrap = 71; decodeTrap = 72;

	TYPE
		Event = POINTER TO ABSTRACT RECORD (Stores.Store)
			meta-: ActorJournal.Meta
		END;

		ReplayComplete = POINTER TO RECORD (ActorCore.Msg) END;

		Actor = POINTER TO ABSTRACT RECORD (ActorCore.Actor)
			seq-: LONGINT;
			snapEvery: INTEGER;
			journal: ActorJournal.Journal;
			(a: Actor) Apply (e: Event), NEW, ABSTRACT;
			(a: Actor) Handle (c: ActorCore.Context; m: ActorCore.Msg), NEW, ABSTRACT;
			(a: Actor) Snapshot (): Stores.Store, NEW, EXTENSIBLE;
			(a: Actor) Restore (s: Stores.Store), NEW, EMPTY;
			(a: Actor) Receive (c: ActorCore.Context; m: ActorCore.Msg);
			(a: Actor) SaveSnapshot (c: ActorCore.Context), NEW;
			(a: Actor) Persist (c: ActorCore.Context; e: Event; cause: LONGINT), NEW
		END;

		Visitor = POINTER TO ABSTRACT RECORD
			(v: Visitor) Visit (e: Event), NEW, ABSTRACT
		END;

	PROCEDURE Read (j: ActorJournal.Journal; IN stream: ARRAY OF CHAR; from: LONGINT; v: Visitor;
		OUT last: LONGINT; OUT res: INTEGER);

END ActorPersist.
```

The stream of an actor is its name (`c.name`). `journal = NIL` means `ActorJournal.dir`. `seq` is the
number of the last applied event. `snapEvery > 0` takes a snapshot after each event whose number is
a multiple of `snapEvery`; 0 only on `SaveSnapshot`.

**PROCEDURE (a: Actor) Apply (e: Event)**
Post: the state of `a` is changed by event `e`. No messages, no writes, no traps.

**PROCEDURE (a: Actor) Handle (c: ActorCore.Context; m: ActorCore.Msg)**
The command handler. It gets `ReplayComplete` first and does not get `Started`.

**PROCEDURE (a: Actor) Snapshot (): Stores.Store**
Post: the object whose frame is the snapshot of the state. By default the actor itself (its
`Externalize`).

**PROCEDURE (a: Actor) Restore (s: Stores.Store)**
Post: the state of `a` is taken from `s`, the object read from the snapshot. Called during recovery
when `seq` already equals the snapshot number.

**PROCEDURE (a: Actor) Receive (c: ActorCore.Context; m: ActorCore.Msg)**
On `Started`: the snapshot of the stream, if any, goes to `Restore`, then the events after it go to
`Apply`, then `Handle(c, ReplayComplete)`. A journal error traps `storeTrap`, an unreadable frame
traps `decodeTrap`. Other messages go to `Handle`.

**PROCEDURE (a: Actor) SaveSnapshot (c: ActorCore.Context)**
Pre: called from the handler of `a` (not checked); `a.seq > 0`, trap 20.
Post: the snapshot of the state after event `seq` is saved; an error traps `storeTrap`.

**PROCEDURE (a: Actor) Persist (c: ActorCore.Context; e: Event; cause: LONGINT)**
Pre: called from the handler of `a` (not checked); `e # NIL`, trap 20.
Post: `e` is written to the stream with number `seq + 1`, with the sender of the command as author
(the actor itself when there is no sender), cause `cause` and the write time; then `e.meta` is
filled, `Apply(e)` runs and `seq` grows; a snapshot is taken if `seq` is a multiple of `snapEvery`.
Another writer appended to the stream: trap `conflictTrap`; a store error: `storeTrap`; in both
cases `e` is not applied. No switch points.

**PROCEDURE (v: Visitor) Visit (e: Event)**
Post: event `e` (its metadata in `e.meta`) is taken into account.

**PROCEDURE Read (j; IN stream; from: LONGINT; v: Visitor; OUT last: LONGINT; OUT res: INTEGER)**
Reads a stream for the read side (CQRS).
Pre: `j # NIL`, trap 20; `v # NIL`, 21.
Post: the events numbered `>= from` are parsed and given to `v.Visit` in order; `res = ok`, `last`
is the number of the last one given (`from - 1` if there are no new ones); otherwise `res` is a
journal error or `corrupt` (a frame cannot be read), and `last` is the number of the last good one.

### 12.7. ActorFileJournal

```
DEFINITION ActorFileJournal;

	IMPORT Files, CasStore, ActorJournal;

	PROCEDURE Open (loc: Files.Locator; IN name: Files.Name; snaps: CasStore.Store;
		OUT res: INTEGER): ActorJournal.Journal;

END ActorFileJournal.
```

A journal in one append-only file: a record is the byte `0A5X`, the body length (4 bytes), the body
(stream, `seq`, `time`, `cause`, author, frame) and an FNV-1a checksum (4 bytes). Snapshots are
blocks of `snaps`: the ref `"s"` plus 40 characters of the hash of the stream name points to the
block "`seq` (8 bytes), frame".

**PROCEDURE Open (loc; IN name; snaps: CasStore.Store; OUT res: INTEGER): ActorJournal.Journal**
Pre: `loc # NIL`, trap 20; `name # ""`, 21; `snaps # NIL`, 22.
Post: `res = ActorJournal.ok`, a journal with the events of all whole records of the file (no file
gives a new empty one); the first unfinished record or record with a wrong checksum and everything
after it are dropped, and the next record takes their place. `res = failed`, the file is not
opened, the result is `NIL`. The file stays open exclusively until `Close`.

Limits: `Flush` without `fsync`; one process per file; the stream index in memory (one entry per
event); opening reads the whole file; the checksum catches a tear and accidental damage, not a
deliberate change.

### 12.8. ActorKvJournal

```
DEFINITION ActorKvJournal;

	IMPORT KvDb, ActorJournal;

	PROCEDURE New (db: KvDb.Db): ActorJournal.Journal;

END ActorKvJournal.
```

A journal in a `KvDb` base: an event is the key `"e" + stream (UTF-8) + 0X + seq` (8 bytes, most
significant first) with the value `time`, `cause`, author and frame; the snapshot is the key
`"s" + stream` with the value `seq` and frame. `Append` is one write transaction (`KvDb.Update`)
committed to the medium; on `conflict` nothing is written. Base errors: `KvDb.corrupt` becomes
`ActorJournal.corrupt`, others `failed`.

**PROCEDURE New (db: KvDb.Db): ActorJournal.Journal**
Pre: `db # NIL`, trap 20.
Post: a journal in the open base `db`; `Close` of the journal closes the base.
