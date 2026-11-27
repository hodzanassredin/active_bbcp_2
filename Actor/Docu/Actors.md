# Actors

The `Actor` subsystem builds a program from objects that exchange messages and share no state. This
chapter describes actors of one process, their execution by a shared pool of tasks, and the
supervision of children by their parent. Tasks, monitors and switch points are defined in
`Docu/CP-Lang.md` and `Docu/Tasks.md`; messages and channels in `Docu/Async.md`. Like
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
of a node name and an actor name. Sending, watching and stopping go through abstract methods of the
address, so the address of an actor on another node takes the same place (sections 9 and 10).

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
		Client = POINTER TO RECORD (Tasks.Job) END;

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

	PROCEDURE (t: Client) Body- (VAR ctx: Tasks.Ctx);
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
	END Body;

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
the main stack `Ask` traps with code 20. `Ask` needs no `Tasks.Ctx`, so the client is written as a
`Tasks.Task` with `Run`; this is the lower level. Application code that enters `Tasks.Object` is
written with `Tasks.Job` and `Body-`.

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
`Props.capacity` (stashed letters count toward the limit; an `ActorCore.Urgent` letter bypasses it), is a `Respond` to a message without a sender, or remains in the mailbox when the
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

### Routers

A service often has several equivalent workers: three accounts of one currency, four readers of one
queue. The client wants to send one letter to "one of them" without knowing how many there are. A
*router* is an actor that owns a set of routes and forwards each letter to one route, or to all of
them, keeping the original sender. The sender of the forwarded letter sees the same `Sender` as if
it had written to the route itself.

`ActorRouter.NewGroup(refs, kind, seed)` takes ready addresses; `ActorRouter.NewPool(factory, n,
kind, seed, strategy)` creates `n` children with the factory and keeps their addresses private. In a
pool the router is the parent, so a trap in a child becomes a failure of the router and its
`Props.strategy` decides the fate of the child. A restart keeps the address of the child, so the
route does not change. A stopped route — by a `Stop` directive or by `StopActor` — is removed from
the set when the router receives `Terminated`; a pool with no routes drops letters as dead. The
command `ActorRouter.SetGroup(router, refs)` replaces the set, `SetCount(router, n)` grows or
shrinks a pool, and `SetKind(router, kind)` changes the rule; all three are ordinary letters and
take effect in mailbox order.

The rule chooses the route:

| Kind | Route |
|---|---|
| `roundRobin` | the next one in turn |
| `random` | from a generator seeded with `seed`; the same seed and the same calls give the same sequence |
| `broadcast` | every route |
| `consistentHash` | the route with the greatest `ActorHash.Weight(name, "router", key)`; a letter without a key uses the empty key |

The weight is the same rendezvous hash that places a virtual actor key (`ActorGrain`): for the same
route name and key both choose the same place. When a route leaves, only the keys it owned move. A
letter for consistent hashing extends `ActorRouter.Keyed` and fills its `key` field; the extension
is a plain record, so routing a letter allocates nothing. The router's set is an array allocated at
creation; forwarding only puts the letter into the mailbox of the route.

The example `ObxActor10` sends six letters round-robin, one by broadcast and four with one key by
consistent hashing. Tests: `TestRoundRobinCyclesOverGroup`, `TestBroadcastSendsToEveryRoute`,
`TestRandomIsDeterministicFromSeed`, `TestConsistentHashKeepsKeyOnRoute`,
`TestPoolRestartsFailedRoute`, `TestPoolDropsStoppedRoute`, `TestSetGroupChangesRoutes`.

### Event stream

Several actors often want the same news: a new order, a stopped account, a failed transfer.
Sending it to each of them by hand means the sender knows every recipient, and a new recipient
means a change in the sender. An *event stream* turns it around: actors subscribe to a stream, and
the publisher writes once to the stream. `ActorEvent.New()` creates a stream actor; `Subscribe(s,
r, filter)` adds subscriber `r` with an optional predicate `Filter`, `Unsubscribe(s, r)` removes
it, and `Publish(s, m)` sends the letter to the stream. The stream forwards the letter to every
subscriber whose filter is absent or whose `Match` returns `TRUE`, keeping the letter's sender.

`Publish` allocates nothing: it is a `Tell` to the stream, and the stream only puts the letter into
the mailboxes of the subscribers. `Subscribe`, `Unsubscribe` and the `DeadLetter` event allocate.
The stream watches its subscribers, so a stopped subscriber leaves the list when the stream receives
`Terminated`; there is no need to unsubscribe it.

`ActorEvent.SetDefault(s)` makes the stream the receiver of dead letters: every letter that does not
reach its addressee is published to `s` as `ActorEvent.DeadLetter` with fields `m` (the original
letter), `to` (the addressee; `NIL` — none) and `sender`. A dead letter about a `DeadLetter` is not
published, and a dead letter produced while the receiver is being called is not counted, so a
stopped stream does not close a circle. `SetDefault(NIL)` removes the receiver.

The example `ObxActor11` publishes orders and information to two subscribers, one with a filter,
and shows a dead letter as an event. Tests: `TestPublishReachesSubscribers`,
`TestFilterSelectsLetters`, `TestUnsubscribeStopsDelivery`, `TestStoppedSubscriberIsRemoved`,
`TestDeadLetterPublished`, `TestDeadLetterIsNotRepublished`, `TestDeadLetterAboutDeadLetterIsNotPublished`.

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

A handler that returns to its previous behaviour uses a stack. `c.BecomeStacked(b)` puts the
current behavior aside and installs `b`; `c.Unbecome` drops the current behavior and restores the
last put-aside one. With an empty stack `Unbecome` returns `Actor.Receive`, as does `Become(NIL)`.
`Become` replaces the current behavior and leaves the stack alone; a restart and a stop clear both
(`TestRestartDropsBehStack`). The stack belongs to one instance and is not shared. A push allocates
one node, so a handler that calls `BecomeStacked` on every letter grows the stack; a plain
replacement uses `Become`. `TestBecomeStackedUnbecomeRestores` pushes two behaviours and pops them
back to `Actor.Receive`.

New behaviours are full handlers: a letter that the current behaviour does not handle is lost, not
passed to the behaviour below it. The behaviours in the test handle the push and pop letters
themselves for this reason.

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
		Client = POINTER TO RECORD (Tasks.Job) END;

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

	PROCEDURE (t: Client) Body- (VAR ctx: Tasks.Ctx);
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
	END Body;

	PROCEDURE Do*;
		VAR p: ActorCore.Props; t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		starts := 0; last := 0; worker := NIL;
		p := ActorCore.NewProps(NewParent);
		p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		ASSERT(ActorCore.Spawn(NIL, p, "parent") # NIL, 100);
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

A supervisor sometimes keeps children that depend on one another: a reader and a writer of one
buffer, an account and its transfer log. Restarting only the failed child leaves the others with a
state that no longer matches. `ActorSup.NewAllForOne` restarts every child of the supervisor after
a failure, and `ActorSup.NewRestForOne` restarts the failed child and those started after it, while
the children started earlier keep working. The strategy counts the failures of all children in one
window, not the failures of each child separately: the first failure opens the window and the
failure numbered `max + 1` stops the group. The directives are the same as for `OneForOne`; for
`Restart` the strategy answers `RestartAll` or `RestartRest`, and after the limit `StopAll` or
`StopRest`. A `Stop` directive stops the group at once, `Resume` resumes only the failed child, and
`Escalate` passes the failure upward as before. The test `TestAllForOneRestartsAllChildren` fails
one of three children and sees `Started` six times; `TestRestForOneRestartsRestOnly` fails the
middle child and sees `Restarting` only on it and on the child started after it
(`TestAllForOneTooManyStopsAll`, `TestRestForOneTooManyStopsRest`, `TestAllForOneWindowResets`).

The order of children matters for `RestForOne`. The list of children keeps the newest first: a
child spawned later stands closer to the head. "Started after" therefore means the children from
the head up to and including the failed one; the older children stand farther and are not touched.
`ActorCore` performs the group directive by sending `Restart` or `Stop` to each child in that
range, so each child goes through its usual life cycle and reports `Restarting` or `Stopping` to
its own handler.

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

Waiting: the journals in memory, in a file and in `Kv` answer at once, so with them `Persist` is
synchronous and has no switch points: while it writes the executor is busy. A slow store delays the
whole pool; an actor for which this matters is created with `Props.dedicated` (section 3).

A journal may also answer later: `RaftJournal` confirms a write only when a majority of Raft nodes has
committed it, and waiting for that inside a handler is not possible — under `ActorSim` the same task
also drives the Raft nodes. So `Persist` hands the event to the journal with
`ActorJournal.Journal.Submit` and does not wait. If the journal answered at once, the event is applied
and `Persisted(c, e)` is called before `Persist` returns; if it answers later, `Persist` returns at
once, and applying and `Persisted` happen when the answer arrives as a message. Until then the actor
handles no new messages: each one that arrives is stashed (`ActorCore.Context.Stash`) and after the
answer goes back to the head of the mailbox in its old order. So the handler of the next command
always sees the state with every written event. Reply to a command in `Persisted`: there the state
already includes the event and `c.Sender()` is the sender of the command. Code after `Persist` sees
the new state only with a journal that answers at once:

```
PROCEDURE (a: Counter) Handle (c: ActorCore.Context; m: ActorCore.Msg);
	VAR e: Added;
BEGIN
	IF m IS Inc THEN NEW(e); a.Persist(c, e, 0) END	(* the reply is in Persisted *)
END Handle;

PROCEDURE (a: Counter) Persisted (c: ActorCore.Context; e: ActorPersist.Event);
	VAR r: Value;
BEGIN
	NEW(r); r.v := a.v; c.Respond(r)	(* the event is written and applied *)
END Persisted;
```

Recovery takes the same path (`ActorJournal.Journal.Replay`): messages before `ReplayComplete` are
stashed too. The journal's answer arrives in the mailbox as a service letter `ActorCore.Urgent`,
bypassing `Props.capacity`: a full mailbox does not lose it. Stashed letters count toward the
mailbox limit, so while the actor waits for an answer it holds at most `capacity` letters, and the
extra ones become dead at once (`Actor_devBounded`).

**Freshness before a letter.** An activation of a virtual actor (`fresh`, section 11) answers only
from a state that is not older than the confirmed journal. Before handling a letter it asks the
journal for the last number of its stream (`SubmitLast`); if the stream is ahead, it reads the
missing events (`Replay`) while the letter waits in the stash. With `RaftJournal` that question goes
through `ReadIndex`, so an activation cut off from the journal majority stops with `storeTrap`
instead of answering a stale state. The price is one journal request per letter. The rule holds for
every answer, including the answer to a repeated command, so a client never sees less than the last
confirmed write of the key.

Event versions. `Stores.Writer.WriteStore` writes the frame: the type name, then the fields from
`Externalize`. The type reads its version number with `ReadVersion(min, max, v)` and parses the
fields by `v`. A new field is a new version: `max` grows, old frames are read with the old layout,
and the new field gets its default. A frame from a future version and a frame of an unknown type are
`unknown` (`TestVersionRule`). An event type must not be renamed or removed while journals hold its
frames.

**Cost.** Each change is the encoding of a frame through `Stores` and a write to the journal;
recovery without a snapshot goes through all events of the stream (measurements in section 12). The
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
| writing an event (section 12) | 4.7–4.9 µs | 6.4–7.0 µs | 1.6–1.7 ms (two `Sync`) |

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
`StopActor` calls `Stop`; none of them knows where the receiver is. A transport between nodes
extends `Ref` with its own type (`ActorNet`, section 10), and `ActorCore.InitRef` fills in its node
and name.

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

## 10. Actors on Different Nodes

A shop and a bank run in different BlackBox instances. The shop needs a balance; the bank needs a
foreign process not to debit an account by guessing its name. A node is an instance with a root
actor; the node's name is the name of that root, and the node's actors are its descendants
(`"bank/acct"`). Nodes are joined by a transport `ActorNet.Transport`: "send a frame to a node".
There are two implementations: `ActorNetMem`, a network in memory with a fault plan for tests and
examples, and `ActorNetTcp`, TCP on `CommTCP` and `AsyncIO`, the same modules the computon transport
uses. Several nodes may live in one process: letters between them still go through the transport,
are encoded and decoded, and so behave as between processes.

```
net := ActorNetMem.NewAuto();
shop := ActorNet.New("shop", net, 1234); ActorNetMem.Add(net, shop);
bank := ActorNet.New("bank", net, 5678); ActorNetMem.Add(net, bank);
teller := ActorCore.Spawn(NIL, ActorCore.NewProps(NewTeller), "bank");
ActorNet.Export(bank, teller, cap);	(* "bank|bank|5f0c…" *)
r := ActorNet.Import(shop, cap);
res := ActorCore.Ask(r, q, 1000, reply)
```

**Capability address.** A remote address is a node, a name and a mark: the first 16 bytes of a hash
of the owner node's secret and the name. Only the owner hands out the mark: in the sender address of
a frame, in an address inside a letter (`ActorNet.WriteRef`) and explicitly (`Export`). A frame with
a wrong mark is dropped and counted in `refused`; guessing the name is not enough, because the mark
of another name does not fit (`TestForgedAddressRefused`). The mark is computed, not stored, so a
node restarted with the same secret accepts the old addresses. Frames are not encrypted: the mark
protects against guessing, not against eavesdropping.

**Letter.** The sender writes `Tell(r, m)` or `Ask(r, m, …)` as locally. A letter that goes to another
node extends `ActorNet.Msg` and implements `Write` and `Read` over `Stores.Writer` and `Reader`; the
type is found by module and type name among loaded modules only: a frame loads no modules, and a
letter of an unloaded type is refused. The receiver loads the modules of its letters itself (for
example, by importing them). A letter of another type sent to a remote
address traps with `ActorNet.notNetMsg`.

**At-most-once delivery.** A frame carries the sender node's name, its incarnation number and a
sequence number; the receiver remembers a window of recent numbers and drops a repeat. A network
duplicate is not executed twice (`TestDuplicatesExecuteOnce`), and a lost frame is not repeated:
`Ask` then ends with a time-out (`TestAskTimeout`). Repeating a command is the application's job: the
command carries an operation number, the receiver records the outcome as an event and answers a
repeat of the same number with the same outcome without executing it again.
`TestRetryAfterLostReply` loses the reply, repeats the command and checks that the journal holds
one event.

**Watching and node loss.** `c.Watch(r)` of a remote address sends a request to the owner node; the
owner watches the actor itself and on stop sends an "ended" frame. If a node is gone (the transport
reported `ActorNet.Lost`: in memory `ActorNetMem.Crash`, on TCP a broken connection), every watcher
of an address on that node receives `Terminated` (`TestNodeLost`, `TestTcpLost`). The field `reason` of
`Terminated` tells the causes apart: `byStop` — the actor was stopped, `byFailure` — stopped by
supervision after a failure, `byNodeLoss` — the node was lost (`TestReasonLocal`,
`TestReasonRemoteStop`, `TestReasonRemoteFailure`, `TestReasonNodeLoss`). Code that does not read the
field need not parse it.

**Processes.** A node may live in a process of its own: several `blackbox-console` processes, each an
`ActorNetTcp` node, exchange the same frames as nodes of one process. The address of a node is a
`host:port` string; the addresses of all nodes lie in the file `nodes` of the run directory, one line
`name address`, and a node receives its own role and the directory in external variables. A capability
written by `Export` reaches a peer as a file too (`<role>.cap`), so a node learns the addresses of the
others before it sends the first frame. The script `tools/test-cluster.sh` runs this arrangement on
loopback; the same files and fixed ports serve two machines.

A process restart is a new incarnation of the node. `ActorNet.New` always numbers it 1, while the
repeat window of a peer remembers the frame numbers of the old incarnation; frames of the new process
would be dropped as duplicates. Therefore a node that lives in a process keeps its launch counter in
a file and calls `ActorNet.SetIncarnation` with the next number before the first frame. The secret of
the node stays the same, so the capabilities handed out before the restart remain valid. A killed
process is noticed by the peer as a broken connection: the reader of `ActorNetTcp` reports
`ActorNet.Lost`, and the watchers receive `Terminated` with `byNodeLoss`. The scenario checks the
window directly: a node sends a burst of frames, is killed, starts again, and its first frame must
reach the peer.

The scenario also runs the saga of `ObxActor7` and the virtual actors of `ObxActor8` in three
processes. The sagas and the virtual actors use the same messages and journals as the examples; the
virtual actors take the shared journal from `RaftJournal`, so the state of a key follows it to the
node that takes the key over after the owner is killed.

**Saga.** A money transfer between accounts on nodes A and B is led by a coordinator on node C, a
persistent actor. The steps: withdraw from A, deposit to B, and if the deposit is refused, refund to
A. The coordinator first records each step as an event and then sends the command; on a tick it
repeats the command of the current step, and the accounts answer a repeat with the old outcome. A
crash of any node at any point does not change the sum: after a restart the coordinator reads its
journal and continues from the recorded step, and so does an account. `ObxActor7` shows a transfer
with the coordinator crashing in the middle.

**Testing from a seed.** The network `ActorNetMem.New(seed)` loses, delays, duplicates and reorders
frames by the plan `Plan`, and draws its lots from the seed. The executor `ActorSim` processes
mailboxes in one task in arrival order, without real time. Together they make a run a function of
the seed: `TestSagaSeeds` runs the saga on 200 seeds with node crashes, checks that all transfers
ended and the sum is conserved, and runs every seed a second time, comparing the step and frame
counts and the balances. A failure prints the seed; `Actor_devNet.Rerun` repeats the last one.

An exhaustive sweep complements the random crashes. A point of the saga is a letter processed by a
mailbox (receiving a command or a reply, writing the journal, sending) or a delivered frame; a run
without network faults has 187 of them. `TestSagaEveryPoint` crashes each of the three nodes at every
third point, with and without losing all frames in flight, brings it up five steps later and runs
the saga to the end; the full sweep (1122 runs) is part of `Actor_devNet.Long`. A
long run on 3000 seeds is the command `Actor_devNet.Long`.

```sh
echo 'Actor_devNet.Long' | tools/run64.sh
```

There is no `Ask` and no idle time-out in this run: they live in real time, and a step is repeated
on the driver's tick.

Examples: `ObxActor6`, two nodes in one process, `Ask`, refusal of a forged address and node loss;
`ObxActor7`, a saga with a node crash.

## 11. Virtual Actors

A client needs account 42 by its number, not by its place: it has no reason to know which node the
account lives on now, and the node may go away. A virtual actor is addressed by a kind and a key,
`("acct", "42")`. It comes up on one of the nodes at the first letter, takes its state from the
journal, goes away after idling, and comes up on another node when its node is lost.

```
ca := ActorGrain.New(a, j); ActorGrain.Register(ca, "acct", NewAccount);
cb := ActorGrain.New(b, j); ActorGrain.Register(cb, "acct", NewAccount);
ActorGrain.HostCap(ca, cap); ActorGrain.Member(cb, cap);
ActorGrain.HostCap(cb, cap); ActorGrain.Member(ca, cap);
acct := ActorGrain.Ref(cb, "acct", "42");
c.Tell(acct, deposit)
```

**Cluster and host.** `ActorGrain.New` creates a host on the node, a root actor named after the
node, and a cluster with a member list. Members are given explicitly: the capability of one node's
host (`HostCap`) is handed to another (`Member`). The journal `j` is shared by all nodes: the nodes
share storage, like a common database, not memory.

**Membership from the service.** The list of members need not be assembled by hand. On a cluster
whose nodes are in the Raft membership service (the `Raft` guide, "Membership service"),
`RaftGrainMembership` joins it with the host's capability, renews the lease and reads the list; each
answer is passed to `ActorGrain.ApplyMembers`. That call adds a new member, revives a member that has
returned, and marks a member that is absent from the list dead; a key stored in the list is kept in
the member record (`Key`). Placement is computed from the live members on every send, so a change of
the list changes the owners without a separate rebalancing step. A member whose lease is not renewed
becomes dead for the others exactly as a node lost through `Terminated` does; the keys then move to
the remaining members.

Membership by `Member` remains for a fixed cluster and for tests.

**Placement.** The owner of a key is the live member with the largest weight
`hash(member, kind, key)`. This is rendezvous hashing, as in protoactor-go: when a member leaves,
only its keys move. The owner is computed on every send. A letter to a key's address goes to the
owner's host in an envelope; the host finds the activation (the child `kind.key`) or brings it up and
passes the letter on with the original sender, so `Respond` answers the client directly
(`TestActivateOnFirstMessage`).

**State.** An activation extends `ActorGrain.Actor`, a persistent actor (section 8). Its journal
stream is `kind.key` without the node name (the field `stream`), so the same state comes up on
another node. A repeated command is guarded, as in the saga, by an operation number
(`TestRetryIdempotent`).

**Passivation.** The host counts idle ticks of each activation: `ActorGrain.Tick(cl)`; after `idle`
ticks without letters the activation saves a snapshot and stops. The next letter brings it up from
the snapshot (`TestPassivateAndReactivate`). A letter that reaches the host after the passivation
request but before the activation ends is not handed to the leaving activation: the host holds it
and after `Terminated` passes it to the new one in order (`TestLetterDuringPassivation`). Letters that
reach an activation between a journal-conflict trap and its stop are not foreseen by the host: they
become dead letters, and the client repeats them with the same operation number. A tick is a letter, not a timer: in a deterministic run
the driver gives it, on a live node a `Services` action.

**Node loss.** The host watches the hosts of the other members. `Terminated` with reason
`byNodeLoss` makes the member dead, and its keys are placed among the live ones
(`TestMoveOnNodeLoss`); a returning member announces `Rejoin`. A letter on its way to the lost node is
lost; the client repeats it with the same operation number.

**One activation without consensus.** If the network splits, each node may consider itself the owner
and bring up its own activation of a key. Before writing, an activation of a virtual actor compares
its state with the last event of the stream (`SubmitLast`) and catches up (`Replay`), so two copies
write one after the other instead of past each other. A write that still arrives with an old number
gets `conflict`: it is not applied and the activation stops (`ActorPersist.conflictTrap`), and the
next letter brings it up with the other's event already in (`TestSplitBrainOneWriter`). A replicated
log is the `Raft` subsystem (`Raft/Docu/Raft.md`, decision record 0025); an `ActorPersist` journal on
top of it is `RaftJournal` (example `ObxRaft1`). `Raft_devGrainJournal` runs virtual actors on
`RaftJournal` under `ActorSim`: a journal cluster of three nodes, a membership service of three
nodes, network splits, crashes of nodes of both clusters and changes of the membership list. It
checks on every step and at the end that every operation is executed once, that the confirmed writes
of a key form one chain (`prev` is the previous number), that no answer is older than the last
confirmed write and that the sum is conserved. The read barrier is decision record 0035.

`TestSeeds` runs 100 seeds: three nodes, five keys, twenty operations; the network loses and
duplicates frames, nodes crash and come back, and the client repeats unacknowledged operations.
Every operation is executed exactly once, and a rerun of a seed matches step by step; 2000 seeds are
`Actor_devGrain.Long`. The same properties on `RaftJournal` are `Raft_devGrainJournal.TestSeeds`
(100 seeds, three keys, four deposits and four reads per key, network splits, crashes of the journal,
the service and the participants, a participant joining and leaving); the long run is
`Raft_devGrainJournal.Long` (2000 seeds).

Example: `ObxActor8`, an account on two nodes: access by number, a node crash, continuation on the
other node.

## 12. Measurements

`Actor_devBench` prints `BENCH` lines of the same form as `Async_devBench`. Measurements of
25.11.2026 on the development machine (Linux x86-64, three runs in a row, `tools/test.sh Actor_devBench`) two actors answering each other
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

Nodes (`Actor_devNet`, network in memory, `ActorSim`): a saga run of 8 transfers with 10 % loss,
10 % duplicates, delays of up to 3 steps, reordering and node crashes takes about 3.5 ms per seed
(`TestSagaSeeds`: 200 seeds, each twice, 1.4 s). The long run `Actor_devNet.Long` on 3000 seeds takes
4.5 s (1.5 ms per seed). The list of created nodes is not kept: it would grow together with their
networks and frames, and the collector would work longer and longer (`TestSagaRunsFreeMemory`).

Virtual actors (`Actor_devGrain.Bench`, one node, journal in memory, 2000 keys, three runs): the
first letter to a key with activation and an event write costs 200–209 µs, a letter to an active
activation 39–44 µs, passivation with a snapshot 47–51 µs per activation, activation from a snapshot
189–193 µs, activation without a snapshot from 20 events 0.99–1.18 ms. Activation without a snapshot
grows with the stream length; the passivation snapshot makes the activation cost constant. A call
finds the activation in a hash table: at 1000 keys a letter costs 4 µs and at 100000 keys 5–10 µs,
while a scan of the list cost 1.1–1.5 ms at 100000 (`docs/measurements.md`, section 15).

## 13. Limitations

- One OS thread: actors run in turn, and computation in one actor delays the others until the next
  switch point of the executor (an executor yields every 64 mailboxes).
- Persistence only for `ActorPersist.Actor`; a `Kv` write costs two `Sync` per batch; a batch is
  formed only from tasks writing at the same time (section 8).
- Nodes: frames are neither signed nor encrypted, and the "from" field is not authenticated: the
  holder of one valid capability can send under another name and move that name's repeat window;
  channel protection (TLS, signing frames with a secret of the node pair) is not implemented.
  At-most-once delivery; the TCP transport is Linux only;
  there is no consensus (Raft): when the network splits, two activations of a key are possible and
  only one can write (section 11). The host finds an activation by scanning a list, so a call grows
  more expensive with the number of active keys.
- One strategy, `OneForOne`; "all for one" is not implemented.
- While the pool is alive, the scheduler reports no deadlock of other tasks (section 3).
- `Ask` to itself from the handler traps with code 23.

## 14. Interface

Trap numbers: 20–26 — violated precondition (the number is given at the procedure), 100 — violated
module invariant, 127 — the task was cancelled.

### 14.1. ActorCore

```
DEFINITION ActorCore;

	IMPORT Stores, AsyncSelect;

	CONST
		Resume = 0; Restart = 1; Stop = 2; Escalate = 3;
		RestartAll = 4; StopAll = 5; RestartRest = 6; StopRest = 7;
		ok = 0; timedOut = 1; dead = 2;
		byStop = 0; byFailure = 1; byNodeLoss = 2;
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

		Urgent = POINTER TO ABSTRACT RECORD (Msg) END;
		DeadHook = POINTER TO ABSTRACT RECORD
			(h: DeadHook) Dead (m: Msg; sender, to: Ref), NEW, ABSTRACT
		END;
		Started = POINTER TO RECORD (Msg) END;
		Stopping = POINTER TO RECORD (Msg) END;
		Stopped = POINTER TO RECORD (Msg) END;
		Restarting = POINTER TO RECORD (Msg) END;
		ReceiveTimeout = POINTER TO RECORD (Msg) END;
		Terminated = POINTER TO RECORD (Msg)
			who-: Ref;
			reason-: INTEGER
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
			(c: Context) Become (b: Behavior), NEW;
			(c: Context) BecomeStacked (b: Behavior), NEW;
			(c: Context) Unbecome, NEW;
			(c: Context) Stash, NEW;
			(c: Context) Unstash, NEW
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
	PROCEDURE Terminate (w, who: Ref; reason: INTEGER);
	PROCEDURE SetDispatcher (d: Dispatcher);
	PROCEDURE SetDir (d: Directory);
	PROCEDURE SetDeadHook (h: DeadHook);

END ActorCore.
```

`Context` is the mailbox of an actor, the context of its handler and the local address. The module
takes no monitors: operations on a mailbox between two switch points are indivisible. The methods of
`Dispatcher` other than `Schedule` serve the executor module (`ActorRun`) and work only for the
executor installed by `SetDispatcher`: for any other `Dispatcher` they trap with code 20.

**PROCEDURE (r: Ref) Post (m: Msg; sender: Ref)**
Pre: `m # NIL` — trap 20.
Post: `m` is in the receiver's mailbox or counted as a dead letter; the sender does not wait. A
mailbox is full when its letters together with the stashed ones number at least `capacity > 0`; an
`Urgent` letter bypasses the limit.

**TYPE Urgent**
A service letter: it enters the mailbox bypassing `Props.capacity`. It answers an actor that cannot
leave its waiting without it (the journal's answer to a persistent actor).

**TYPE DeadHook**
The receiver of dead letters. `ActorCore` calls `Dead` for every letter that did not reach its
addressee; `to` is the addressee, `NIL` when there is none. A trap inside `Dead` is caught and
does not become a failure of the actor that sent the dead letter; a dead letter produced during the
call is not counted again, so a receiver that posts to a stopped actor does not close a circle.

**PROCEDURE SetDeadHook (h: DeadHook)**
Post: every dead letter from this moment is passed to `h.Dead`; `h = NIL` — no receiver.

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
Post: a directive; a value outside `Resume..StopRest` is executed as `Stop`, and so is `Resume` for a
child without an instance; `RestartAll` and `StopAll` apply to every child of the parent,
`RestartRest` and `StopRest` to the failed child and those started after it; a trap is a failure of
the parent, the decision is put aside until the parent is resumed.

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

**PROCEDURE (c: Context) BecomeStacked (b: Behavior)**
Pre: called from the handler of `c` — trap 20.
Post: the current behavior is put aside; the following letters are handled by `b.Receive`; a
restart and a stop clear the stack.

**PROCEDURE (c: Context) Unbecome**
Pre: called from the handler of `c` — trap 20.
Post: the current behavior is dropped, the last put-aside one is restored; an empty stack returns
`Actor.Receive`.

**PROCEDURE (c: Context) Stash**
Put aside the message being handled: the handler cannot take it now.
Pre: called from the handler of `c` handling a user message (not a life-cycle step) — trap 20.
Post: the message and its sender are stashed; they still count toward the mailbox limit
`capacity`. A restart puts the stashed messages back at the head
of the mailbox, a stop makes them dead letters.

**PROCEDURE (c: Context) Unstash**
Pre: called from the handler of `c` — trap 20.
Post: the stashed messages are at the head of the mailbox in `Stash` order, with their senders.

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

**PROCEDURE Terminate (w, who: Ref; reason: INTEGER)**
For transport modules: tell the watcher `w` that the address `who`, which is not a local mailbox (a
remote address), has ended.
Pre: `w # NIL` — trap 20; `who # NIL` — 21; `reason` is `byStop`, `byFailure` or `byNodeLoss` — 22.
Post: `w` will receive `Terminated` with the fields `who` and `reason`.

**PROCEDURE SetDispatcher (d: Dispatcher)**
For executor modules.
Pre: `d # NIL` — trap 20.
Post: `dispatcher = d`; the methods of `Dispatcher` other than `Schedule` work only for `d`.

**PROCEDURE SetDir (d: Directory)**
Pre: `d # NIL` — trap 20.
Post: `dir = d`; the first installed directory stays in `stdDir`.

### 14.2. ActorSup

```
DEFINITION ActorSup;

	IMPORT ActorCore;

	PROCEDURE NewOneForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy;
	PROCEDURE NewAllForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy;
	PROCEDURE NewRestForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy;

END ActorSup.
```

**PROCEDURE NewOneForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy**
Pre: `directive` is `Resume`, `Restart`, `Stop` or `Escalate` — trap 20; `max >= 0` — trap 21;
`withinMs > 0` — trap 22.
Post: the strategy answers a child's failure with directive `directive`; for `Restart` it answers
`Stop` if this is failure number `max + 1` within `withinMs` ms from the first failure of the window.

**PROCEDURE NewAllForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy**
Pre: as `NewOneForOne` — traps 20, 21, 22.
Post: for `Restart` it answers `RestartAll` while the failures of all children fit into `max` in a
window of `withinMs` ms, otherwise `StopAll`; for `Stop` it answers `StopAll`; `Resume` and
`Escalate` are answered unchanged. The window counts the failures of all children together.

**PROCEDURE NewRestForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy**
Pre: as `NewOneForOne` — traps 20, 21, 22.
Post: as `NewAllForOne`, but the group is the failed child and the children started after it;
`RestartRest` and `StopRest` name its directives.

### 14.3. ActorRun

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

### 14.4. ActorJournal

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
			(j: Journal) Close, NEW, EMPTY;
			(j: Journal) Submit (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: Meta;
				IN body: ARRAY OF BYTE; len: INTEGER; r: Request), NEW, EXTENSIBLE;
			(j: Journal) SubmitSnapshot (IN stream: ARRAY OF CHAR; seq: LONGINT;
				IN data: ARRAY OF BYTE; len: INTEGER; r: Request), NEW, EXTENSIBLE;
			(j: Journal) SubmitLast (IN stream: ARRAY OF CHAR; r: Request), NEW, EXTENSIBLE;
			(j: Journal) Replay (IN stream: ARRAY OF CHAR; r: Request), NEW, EXTENSIBLE
		END;

		Request = POINTER TO ABSTRACT RECORD (Sink)
			(r: Request) Event (IN m: Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN, EXTENSIBLE;
			(r: Request) Snapshot (seq: LONGINT; IN data: ARRAY OF BYTE; len: INTEGER), NEW, EMPTY;
			(r: Request) Last (seq: LONGINT), NEW, EMPTY;
			(r: Request) Done (res: INTEGER), NEW, ABSTRACT
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

`Submit`, `SubmitSnapshot`, `SubmitLast` and `Replay` are the same `Append`, `SaveSnapshot`, `Last`
and reading of the snapshot with its events, but the result goes to the request `r` by `Done(res)` —
exactly once, inside the call or later, from another context; `Replay` gives the snapshot to
`r.Snapshot` and the events to `r.Event` before `Done`, `SubmitLast` gives the number to `r.Last`. By
default they call the synchronous methods and answer at once, so a synchronous journal does not
redefine them; a journal that answers later (`RaftJournal.Client`) does. Pre: `r # NIL` — trap 23
(`Replay`: 20).

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

### 14.5. ActorCodec

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

### 14.6. ActorPersist

```
DEFINITION ActorPersist;

	IMPORT Stores, ActorCore, ActorJournal;

	CONST
		conflictTrap = 70; storeTrap = 71; decodeTrap = 72;
		mutNoStash = 0; mutNoFresh = 1; mutKeepOnConflict = 2; mutNoCatchUp = 3;

	TYPE
		Event = POINTER TO ABSTRACT RECORD (Stores.Store)
			meta-: ActorJournal.Meta
		END;

		ReplayComplete = POINTER TO RECORD (ActorCore.Msg) END;

		Actor = POINTER TO ABSTRACT RECORD (ActorCore.Actor)
			seq-: LONGINT;
			fresh: BOOLEAN;
			snapEvery: INTEGER;
			journal: ActorJournal.Journal;
			stream: ActorCore.Name;
			(a: Actor) Apply (e: Event), NEW, ABSTRACT;
			(a: Actor) Handle (c: ActorCore.Context; m: ActorCore.Msg), NEW, ABSTRACT;
			(a: Actor) Persisted (c: ActorCore.Context; e: Event), NEW, EMPTY;
			(a: Actor) Busy (): BOOLEAN, NEW;
			(a: Actor) Snapshot (): Stores.Store, NEW, EXTENSIBLE;
			(a: Actor) Restore (s: Stores.Store), NEW, EMPTY;
			(a: Actor) Receive (c: ActorCore.Context; m: ActorCore.Msg), EXTENSIBLE;
			(a: Actor) SaveSnapshot (c: ActorCore.Context), NEW;
			(a: Actor) Persist (c: ActorCore.Context; e: Event; cause: LONGINT), NEW
		END;

		Visitor = POINTER TO ABSTRACT RECORD
			(v: Visitor) Visit (e: Event), NEW, ABSTRACT
		END;

	VAR
		mutations: SET;

	PROCEDURE Read (j: ActorJournal.Journal; IN stream: ARRAY OF CHAR; from: LONGINT; v: Visitor;
		OUT last: LONGINT; OUT res: INTEGER);

END ActorPersist.
```

The stream of an actor is `stream` or, when it is empty, the mailbox name (`c.name`); virtual actors
set `stream` so that the stream does not depend on the node (section 11). `journal = NIL` means `ActorJournal.dir`. `seq` is the
number of the last applied event. `snapEvery > 0` takes a snapshot after each event whose number is
a multiple of `snapEvery`; 0 only on `SaveSnapshot`. `fresh = TRUE` makes the actor compare `seq`
with the last number of its stream before every user message (`SubmitLast`) and read the missing
events; the reply thus cannot be older than the last confirmed write. Virtual actors set it.

**PROCEDURE (a: Actor) Apply (e: Event)**
Post: the state of `a` is changed by event `e`. No messages, no writes, no traps.

**PROCEDURE (a: Actor) Handle (c: ActorCore.Context; m: ActorCore.Msg)**
The command handler. It gets `ReplayComplete` first and does not get `Started`.

**PROCEDURE (a: Actor) Persisted (c: ActorCore.Context; e: Event)**
After a write: `e` is written and applied, `seq = e.meta.seq`; `c.Sender()` is the sender of the
message whose handler wrote `e`. Reply to the command here; `Persist` may be called.

**PROCEDURE (a: Actor) Busy (): BOOLEAN**
Post: recovery or a write without the journal's answer is in progress — user messages are stashed.

`mutations` is for tests only: `mutNoStash` handles a message arriving during a write at once,
`mutNoFresh` skips the freshness check, `mutNoCatchUp` sees the stream ahead but does not read the
missing events, `mutKeepOnConflict` applies and confirms an event whose number did not match.

**PROCEDURE (a: Actor) Snapshot (): Stores.Store**
Post: the object whose frame is the snapshot of the state. By default the actor itself (its
`Externalize`).

**PROCEDURE (a: Actor) Restore (s: Stores.Store)**
Post: the state of `a` is taken from `s`, the object read from the snapshot. Called during recovery
when `seq` already equals the snapshot number.

**PROCEDURE (a: Actor) Receive (c: ActorCore.Context; m: ActorCore.Msg)**
On `Started`: the journal's `Replay` — the snapshot of the stream, if any, goes to `Restore`, then the
events after it go to `Apply`, then `Handle(c, ReplayComplete)`. A journal error traps `storeTrap`,
an unreadable frame traps `decodeTrap`. The journal's answer applies the event and calls `Persisted`.
While `Busy`, user messages are stashed (`Stash`) and come back after the answer; `Stopping`,
`Stopped`, `Restarting` go to `Handle` at once. Other messages go to `Handle`.

**PROCEDURE (a: Actor) SaveSnapshot (c: ActorCore.Context)**
Pre: called from the handler of `a` (not checked); `a.seq > 0`, trap 20.
Post: the snapshot of the state after event `seq` is handed to the journal (`SubmitSnapshot`) after
the waiting writes; a journal failure traps `storeTrap`.

**PROCEDURE (a: Actor) Persist (c: ActorCore.Context; e: Event; cause: LONGINT)**
Pre: called from the handler of `a` (not checked); `e # NIL`, trap 20.
Post: `e` is handed to the journal (`Submit`) with number `seq + 1` plus the number of events still
waiting for an answer, with the sender of the command as author (the actor itself when there is no
sender), cause `cause` and the write time. On the journal's answer: `e.meta` is filled, `Apply(e)`
runs, `seq` grows, `Persisted(c, e)` is called; a snapshot is taken if `seq` is a multiple of
`snapEvery`. If the journal answered at once, all this happens before the return. Another writer
appended to the stream: trap `conflictTrap`; a store error or an unknown result: `storeTrap`; in both
cases `e` is not applied. No switch points.

**PROCEDURE (v: Visitor) Visit (e: Event)**
Post: event `e` (its metadata in `e.meta`) is taken into account.

**PROCEDURE Read (j; IN stream; from: LONGINT; v: Visitor; OUT last: LONGINT; OUT res: INTEGER)**
Reads a stream for the read side (CQRS).
Pre: `j # NIL`, trap 20; `v # NIL`, 21.
Post: the events numbered `>= from` are parsed and given to `v.Visit` in order; `res = ok`, `last`
is the number of the last one given (`from - 1` if there are no new ones); otherwise `res` is a
journal error or `corrupt` (a frame cannot be read), and `last` is the number of the last good one.

### 14.7. ActorFileJournal

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

### 14.8. ActorKvJournal

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

### 14.9. ActorNet

```
DEFINITION ActorNet;

	IMPORT Stores, ActorCore;

	CONST
		notNetMsg = 30;

	TYPE
		Msg = POINTER TO ABSTRACT RECORD (ActorCore.Msg)
			(m: Msg) Write- (VAR wr: Stores.Writer), NEW, EMPTY;
			(m: Msg) Read- (VAR rd: Stores.Reader), NEW, EMPTY
		END;

		Node = POINTER TO LIMITED RECORD
			name-: ActorCore.Name;
			up-: BOOLEAN;
			incarnation-: LONGINT;
			sent-, received-, duplicates-, refused-: LONGINT
		END;

		Transport = POINTER TO ABSTRACT RECORD
			(t: Transport) Send (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER), NEW, ABSTRACT
		END;

	PROCEDURE New (IN name: ARRAY OF CHAR; t: Transport; secret: LONGINT): Node;
	PROCEDURE Down (n: Node);
	PROCEDURE Up (n: Node);
	PROCEDURE SetIncarnation (n: Node; inc: LONGINT);
	PROCEDURE Export (n: Node; r: ActorCore.Ref; OUT cap: ARRAY OF CHAR);
	PROCEDURE Import (n: Node; IN cap: ARRAY OF CHAR): ActorCore.Ref;
	PROCEDURE WriteRef (VAR wr: Stores.Writer; r: ActorCore.Ref);
	PROCEDURE ReadRef (VAR rd: Stores.Reader; OUT r: ActorCore.Ref);
	PROCEDURE WriteMsg (VAR wr: Stores.Writer; m: Msg);
	PROCEDURE ReadMsg (VAR rd: Stores.Reader; OUT m: Msg);
	PROCEDURE Deliver (n: Node; IN frame: ARRAY OF BYTE; len: INTEGER);
	PROCEDURE Lost (n: Node; IN node: ARRAY OF CHAR);

END ActorNet.
```

**TYPE Msg** — a letter that may go to another node. `Write` writes the fields, `Read` reads them in
the same order; addresses in fields go through `WriteRef` and `ReadRef`. The letter type must be
found by name (`Kernel.ThisType`) on the receiving node; a letter of an unknown type is refused
(`refused`).

**TYPE Node** — a node: `name` is the name of the root actor, `up` tells the node is up,
`incarnation` grows on every `Up`; frame counters: sent, received, dropped as a repeat, refused
(wrong mark, broken frame, unknown type).

**TYPE Transport** — a frame transport. `Send` hands the frame `frame[0 .. len - 1]` from node `from`
to node `to`; the frame may be lost. The transport passes a received frame to `Deliver` of the target
node and reports a lost link to a node with `Lost`.

**PROCEDURE New (IN name: ARRAY OF CHAR; t: Transport; secret: LONGINT): Node**
Pre: `name # ""`, without `"/"`, shorter than `ActorCore.nameLen` — trap 20; `t # NIL` — 21.
Post: the node is up, incarnation 1. The marks of the node's addresses depend on `secret`. The user
creates the node's root actor: `Spawn(NIL, …, name)`.

**PROCEDURE Down (n: Node)**
Post: the node is down: frames from and to it are lost, its memory of frames and watches is cleared,
its root actor is stopped. Journals in storage are not touched.

**PROCEDURE Up (n: Node)**
Pre: the node is down and its root actor stopped — trap 20.
Post: the node is up with a new incarnation; the user creates the root actor again.

**PROCEDURE SetIncarnation (n: Node; inc: LONGINT)**
For a node restarted in a new process. The peer's repeat window remembers the frame numbers of the
old incarnation, so the frames of a process that starts again at number 1 would be dropped as
duplicates; the owner stores the counter itself and raises it on every start of the process.
Pre: `n # NIL` — trap 20; `inc >= n.incarnation` — 21.
Post: `n.incarnation = inc` and the frame counter `n.seq = 0`; the marks of the addresses do not
change, because they depend on the secret only.

**PROCEDURE Export (n: Node; r: ActorCore.Ref; OUT cap: ARRAY OF CHAR)**
Pre: `r` is a local address — trap 20.
Post: `cap` is a capability `"node|name|mark"` to `r`, to hand to another node outside letters.

**PROCEDURE Import (n: Node; IN cap: ARRAY OF CHAR): ActorCore.Ref**
Post: the address of the capability for letters from node `n`; `NIL` — the capability did not
parse. The owner node checks the mark on delivery.

**PROCEDURE WriteRef (VAR wr: Stores.Writer; r: ActorCore.Ref)**
**PROCEDURE ReadRef (VAR rd: Stores.Reader; OUT r: ActorCore.Ref)**
Pre: called from `Msg.Write` and `Msg.Read` — trap 20 in `WriteRef`.
Post: the address is written as a capability of the node encoding the frame, and read back: an own
address with a right mark is the local actor, with a wrong one `NIL`, a foreign one a remote address.

**PROCEDURE WriteMsg (VAR wr: Stores.Writer; m: Msg)**
**PROCEDURE ReadMsg (VAR rd: Stores.Reader; OUT m: Msg)**
A letter inside a letter (an envelope field): `WriteMsg` writes the module and type name and the
fields of `m` (or a `NIL` mark), `ReadMsg` reads it back; the type is looked up among loaded modules
only, and if not found `m = NIL` and `rd.cancelled`. The letter is best made the last field.

**PROCEDURE Deliver (n: Node; IN frame: ARRAY OF BYTE; len: INTEGER)**
For a transport. Post: a repeat, a foreign mark and a broken frame are dropped and counted; otherwise
the letter is in the target's mailbox.

**PROCEDURE Lost (n: Node; IN node: ARRAY OF CHAR)**
For a transport. Post: every watcher of an address on `node` received `Terminated`; watches requested
by `node` are removed.

### 14.10. ActorNetMem

```
DEFINITION ActorNetMem;

	IMPORT ActorNet;

	TYPE
		Plan = RECORD
			loss, dup: INTEGER;
			delay: INTEGER;
			reorder: BOOLEAN
		END;

		Net = POINTER TO LIMITED RECORD (ActorNet.Transport)
			plan: Plan;
			now-: LONGINT;
			lost-, duplicated-, delivered-: LONGINT
		END;

	PROCEDURE New (seed: LONGINT): Net;
	PROCEDURE NewAuto (): Net;
	PROCEDURE Add (net: Net; n: ActorNet.Node);
	PROCEDURE Pump (net: Net): BOOLEAN;
	PROCEDURE Pending (net: Net): INTEGER;
	PROCEDURE Rand (net: Net; k: INTEGER): INTEGER;
	PROCEDURE Drop (net: Net);
	PROCEDURE Crash (net: Net; IN name: ARRAY OF CHAR);

END ActorNetMem.
```

A network in memory. **Plan**: `loss` and `dup` are the shares of lost and duplicated frames in
thousandths, `delay` is the largest delay of a frame in network steps, `reorder` picks a ready frame
by lot. **Net**: `now` counts network steps; counters of lost, duplicated and delivered frames.

**PROCEDURE New (seed: LONGINT): Net** — a network with an empty plan; `Pump` delivers the frames.
The same `seed` and the same actions give the same sequence of deliveries.
**PROCEDURE NewAuto (): Net** — a network without faults; the main loop (a `Services` action)
delivers the frames in sending order.
**PROCEDURE Add (net: Net; n: ActorNet.Node)** — Pre: no node with the same name — trap 20. Post:
frames to `n.name` reach `n`.
**PROCEDURE Pump (net: Net): BOOLEAN** — a network step: `now` grows by 1, at most one ready frame
is delivered. Post: `TRUE` — frames are still in flight.
**PROCEDURE Pending (net: Net): INTEGER** — frames in flight.
**PROCEDURE Rand (net: Net; k: INTEGER): INTEGER** — Pre: `k > 0` — trap 20. Post: a number in
`[0, k)` from the network's generator; for a test driver, so that its decisions also come from the
seed.
**PROCEDURE Drop (net: Net)** — all frames in flight are lost.
**PROCEDURE Crash (net: Net; IN name: ARRAY OF CHAR)** — Pre: the node is in the network — trap 20.
Post: `ActorNet.Down` of the node, frames to it are lost, the other nodes received `Lost(name)`.

### 14.11. ActorSim

```
DEFINITION ActorSim;

	VAR
		steps-: LONGINT;

	PROCEDURE Install;
	PROCEDURE Step (): BOOLEAN;
	PROCEDURE Drain (max: INTEGER): BOOLEAN;
	PROCEDURE Idle (): BOOLEAN;

END ActorSim.
```

An executor for deterministic testing: mailboxes with work stand in one queue in arrival order; it
has no tasks of its own, a driver task calls `Step`.

**PROCEDURE Install** — Pre: the `ActorRun` pool is stopped (not checked). Post: this executor
serves the mailboxes, the queue is empty, `steps = 0`.
**PROCEDURE Step (): BOOLEAN** — Pre: `Install`, called from a task — trap 20. Post: `FALSE` — there
was no work; `TRUE` — the first mailbox processed all system letters and at most one user letter.
**PROCEDURE Drain (max: INTEGER): BOOLEAN** — `Step` while there is work, at most `max` times.
Post: `TRUE` — no work left.
**PROCEDURE Idle (): BOOLEAN** — Post: no mailbox has work.

### 14.12. ActorNetTcp

```
DEFINITION ActorNetTcp;

	IMPORT ActorNet;

	TYPE
		Net = POINTER TO LIMITED RECORD (ActorNet.Transport)
			node-: ActorNet.Node;
			sentFrames-, readFrames-: LONGINT
		END;

	PROCEDURE New (IN listen: ARRAY OF CHAR; OUT net: Net; OUT res: INTEGER);
	PROCEDURE Attach (net: Net; n: ActorNet.Node);
	PROCEDURE Address (net: Net; OUT adr: ARRAY OF CHAR);
	PROCEDURE Peer (net: Net; IN name, adr: ARRAY OF CHAR);
	PROCEDURE Close (net: Net);

END ActorNetTcp.
```

A TCP transport, Linux only. A frame to a node goes over an outgoing connection into the queue of a
writer task; `Send` does not wait for the network. On the wire: the length (4 bytes) and the frame;
the first message of a connection is the sender node's name. A broken connection gives
`ActorNet.Lost`; frames queued on a broken connection are lost. The main loop must poll descriptors
(`AsyncIO`).

**PROCEDURE New (IN listen: ARRAY OF CHAR; OUT net: Net; OUT res: INTEGER)** — Post:
`res = CommStreams.done` — `net` listens on `listen` (`"127.0.0.1:0"` — a free port); otherwise
`net = NIL`.
**PROCEDURE Attach (net: Net; n: ActorNet.Node)** — Pre: no node yet — trap 20. Post: received
frames go to node `n` (created on `net`).
**PROCEDURE Address (net: Net; OUT adr: ARRAY OF CHAR)** — Post: the address `net` listens on.
**PROCEDURE Peer (net: Net; IN name, adr: ARRAY OF CHAR)** — Post: frames to node `name` go to `adr`.
**PROCEDURE Close (net: Net)** — Post: the listener and connections are closed, the tasks end; other
nodes see the break.

### 14.13. ActorGrain

```
DEFINITION ActorGrain;

	IMPORT ActorCore, ActorJournal, ActorPersist, ActorNet;

	CONST
		maxMembers = 32;
		capLen = 256;
		notNetMsg = 31;
		mutNoApply = 0;
		mutKeepAlive = 1;
		mutNoRejoin = 2;

	TYPE
		Actor = POINTER TO ABSTRACT RECORD (ActorPersist.Actor)
			kind-, key-: ActorCore.Name;
			(a: Actor) Receive (c: ActorCore.Context; m: ActorCore.Msg)
		END;

		Cluster = POINTER TO LIMITED RECORD
			node-: ActorNet.Node;
			journal-: ActorJournal.Journal;
			idle: INTEGER;
			activations-, passivations-: LONGINT
		END;

		MemberInfo = RECORD
			id: INTEGER;
			cap: ARRAY capLen OF CHAR;
			key: ActorJournal.Bytes;
			alive: BOOLEAN
		END;

	PROCEDURE New (n: ActorNet.Node; j: ActorJournal.Journal): Cluster;
	PROCEDURE HostRef (cl: Cluster): ActorCore.Ref;
	PROCEDURE HostCap (cl: Cluster; OUT cap: ARRAY OF CHAR);
	PROCEDURE Member (cl: Cluster; IN cap: ARRAY OF CHAR);
	PROCEDURE ApplyMembers (cl: Cluster; IN list: ARRAY OF MemberInfo; n: INTEGER);
	PROCEDURE Key (cl: Cluster; IN name: ARRAY OF CHAR): ActorJournal.Bytes;
	PROCEDURE Rejoin (cl: Cluster; IN name: ARRAY OF CHAR);
	PROCEDURE Leave (cl: Cluster; IN name: ARRAY OF CHAR);
	PROCEDURE Alive (cl: Cluster; IN name: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE Members (cl: Cluster): INTEGER;
	PROCEDURE AliveCount (cl: Cluster): INTEGER;
	PROCEDURE MemberName (cl: Cluster; i: INTEGER; OUT name: ActorCore.Name);
	PROCEDURE Register (cl: Cluster; IN kind: ARRAY OF CHAR; f: ActorCore.Factory);
	PROCEDURE Ref (cl: Cluster; IN kind, key: ARRAY OF CHAR): ActorCore.Ref;
	PROCEDURE Owner (cl: Cluster; IN kind, key: ARRAY OF CHAR; OUT node: ARRAY OF CHAR);
	PROCEDURE Tick (cl: Cluster);

END ActorGrain.
```

**TYPE Actor** — an activation. `kind` and `key` are set before `Started`; the journal stream is
`kind.key`, the journal is the cluster's. `Receive` intercepts the passivation letter (a snapshot if
there are events, then a stop) and hands the rest to `ActorPersist`.

**TYPE Cluster** — the node's cluster: `idle` is the number of idle ticks before passivation (0 — no
passivation); counters of activations and passivations.

**PROCEDURE New (n: ActorNet.Node; j: ActorJournal.Journal): Cluster** — Pre: `n # NIL` — 20;
`j # NIL` — 21; no root actor named after the node — 22. Post: the host is created, the node itself
is the only member.
**PROCEDURE HostRef (cl: Cluster): ActorCore.Ref** — Post: the address of the node's host.
**PROCEDURE HostCap (cl: Cluster; OUT cap: ARRAY OF CHAR)** — Post: the host's capability for
`Member` on other nodes.
**PROCEDURE ApplyMembers (cl: Cluster; IN list: ARRAY OF MemberInfo; n: INTEGER)** — Pre: each
`list[i].cap` parses with `ActorNet.Import`; not checked. Post: the members of `cl` are the entries of
`list[0 .. n-1]` with `alive = TRUE`; a new entry is added, a returning one is alive again, an absent
one is marked dead; `id` and `key` are kept in the member record. Placement is computed from the live
members on the next send.
**PROCEDURE Key (cl: Cluster; IN name: ARRAY OF CHAR): ActorJournal.Bytes** — Post: the open key of
the member from the last `ApplyMembers`; `NIL` if there is no such member. The key is not checked.
**PROCEDURE Members (cl: Cluster): INTEGER** — Post: the number of members including the node itself.
**PROCEDURE AliveCount (cl: Cluster): INTEGER** — Post: the number of live members including the node.
**PROCEDURE MemberName (cl: Cluster; i: INTEGER; OUT name: ActorCore.Name)** — Post: the name of
member `i` (0 is the node itself); empty outside the list.
**PROCEDURE Member (cl: Cluster; IN cap: ARRAY OF CHAR)** — Pre: the capability parses — 20; fewer
than `maxMembers` members — 21. Post: the member is alive, its host is watched.
**PROCEDURE Rejoin (cl: Cluster; IN name: ARRAY OF CHAR)** — Pre: the member exists — 20. Post: the
member is alive and watched again.
**PROCEDURE Leave (cl: Cluster; IN name: ARRAY OF CHAR)** — Pre: the member exists and is not the
node itself — 20. Post: the member is considered dead (a decision from outside).
**PROCEDURE Alive (cl: Cluster; IN name: ARRAY OF CHAR): BOOLEAN** — Post: the member is alive in
`cl`'s view.
**PROCEDURE Register (cl: Cluster; IN kind: ARRAY OF CHAR; f: ActorCore.Factory)** — Pre:
`kind # ""`, without `.` and `/` — 20; `f # NIL` — 21. Post: `f` creates activations of the kind; if
`f` returns something other than an `Actor`, no activation comes up.
**PROCEDURE Ref (cl: Cluster; IN kind, key: ARRAY OF CHAR): ActorCore.Ref** — Pre: `key # ""`,
without `/` — 20. Post: `Tell` and `Ask` to the address go to the activation at the current owner; a
letter that is not an `ActorNet.Msg` traps with `notNetMsg`; `Watch` has no effect.
**PROCEDURE Owner (cl: Cluster; IN kind, key: ARRAY OF CHAR; OUT node: ARRAY OF CHAR)** — Post: the
owner node among the live members, in `cl`'s view.
**PROCEDURE Tick (cl: Cluster)** — Post: activations of the node without letters for `idle` ticks in
a row are passivated.

### 14.14. ActorRouter

```
DEFINITION ActorRouter;

	IMPORT ActorCore;

	CONST
		roundRobin = 0; random = 1; broadcast = 2; consistentHash = 3;
		maxRoutes = 64;

	TYPE
		Keyed = POINTER TO EXTENSIBLE RECORD (ActorCore.Msg)
			key: ActorCore.Name
		END;

	PROCEDURE NewPool (factory: ActorCore.Factory; n: INTEGER; kind: INTEGER;
		seed: LONGINT; strategy: ActorCore.Strategy): ActorCore.Ref;
	PROCEDURE NewGroup (IN refs: ARRAY OF ActorCore.Ref; kind: INTEGER; seed: LONGINT): ActorCore.Ref;
	PROCEDURE SetGroup (r: ActorCore.Ref; IN refs: ARRAY OF ActorCore.Ref);
	PROCEDURE SetCount (r: ActorCore.Ref; n: INTEGER);
	PROCEDURE SetKind (r: ActorCore.Ref; kind: INTEGER);

END ActorRouter.
```

**TYPE Keyed** — a letter with a key for `consistentHash`; the handler of the extension fills `key`.

**PROCEDURE NewPool (factory: ActorCore.Factory; n: INTEGER; kind: INTEGER; seed: LONGINT; strategy: ActorCore.Strategy): ActorCore.Ref**
Pre: `factory # NIL` — trap 20; `0 < n <= maxRoutes` — trap 21; `kind` is one of the four — trap 22.
Post: a root router with `n` children made by `factory`; a child's failure is decided by `strategy`.

**PROCEDURE NewGroup (IN refs: ARRAY OF ActorCore.Ref; kind: INTEGER; seed: LONGINT): ActorCore.Ref**
Pre: `0 < LEN(refs) <= maxRoutes` — trap 20; every address is not `NIL` — trap 22; `kind` is one of
the four — trap 23. Post: a root router with the given routes.

**PROCEDURE SetGroup (r: ActorCore.Ref; IN refs: ARRAY OF ActorCore.Ref)**
Pre: `r # NIL` — trap 20; `0 < LEN(refs) <= maxRoutes` — trap 21; every address is not `NIL` — trap
22. Post: the router receives a letter that replaces its routes in mailbox order.

**PROCEDURE SetCount (r: ActorCore.Ref; n: INTEGER)**
Pre: `r # NIL` — trap 20. Post: a pool router receives a letter that makes `n` routes; a group
router ignores it.

**PROCEDURE SetKind (r: ActorCore.Ref; kind: INTEGER)**
Pre: `r # NIL` — trap 20. Post: the router receives a letter that changes the rule; an invalid rule
changes nothing.

### 14.15. ActorHash

```
DEFINITION ActorHash;

	PROCEDURE Weight (IN member, kind, key: ARRAY OF CHAR): LONGINT;

END ActorHash.
```

**PROCEDURE Weight (IN member, kind, key: ARRAY OF CHAR): LONGINT** — Post: the weight of the place
`member` for the key `key` of kind `kind`; the same input gives the same weight. The owner is the
live place with the greatest weight; on equal weights the lesser name wins.

### 14.16. ActorEvent

```
DEFINITION ActorEvent;

	IMPORT ActorCore;

	TYPE
		Filter = POINTER TO ABSTRACT RECORD
			(f: Filter) Match (m: ActorCore.Msg): BOOLEAN, NEW, ABSTRACT
		END;

		DeadLetter = POINTER TO RECORD (ActorCore.Msg)
			m-: ActorCore.Msg;
			to-, sender-: ActorCore.Ref
		END;

	PROCEDURE New (): ActorCore.Ref;
	PROCEDURE Subscribe (s, r: ActorCore.Ref; f: Filter);
	PROCEDURE Unsubscribe (s, r: ActorCore.Ref);
	PROCEDURE Publish (s: ActorCore.Ref; m: ActorCore.Msg);
	PROCEDURE SetDefault (s: ActorCore.Ref);
	PROCEDURE Default (): ActorCore.Ref;

END ActorEvent.
```

**TYPE Filter** — the subscription predicate; `Match` answers whether the letter goes to the
subscriber.

**TYPE DeadLetter** — the event of a dead letter: `m` is the original letter, `to` the addressee
(`NIL` — none), `sender` the sender of the original letter.

**PROCEDURE New (): ActorCore.Ref** — Post: a new stream actor without subscribers; it is not the
receiver of dead letters.

**PROCEDURE Subscribe (s, r: ActorCore.Ref; f: Filter)** — Pre: `s # NIL` — trap 20; `r # NIL` —
trap 21. Post: the stream receives a letter: `r` is subscribed with `f` (`NIL` — all letters); a
repeated subscription of the same `r` replaces the predicate.

**PROCEDURE Unsubscribe (s, r: ActorCore.Ref)** — Pre: `s # NIL` — trap 20; `r # NIL` — trap 21.
Post: the stream receives a letter: `r` is not subscribed.

**PROCEDURE Publish (s: ActorCore.Ref; m: ActorCore.Msg)** — Pre: `s # NIL` — trap 20; `m # NIL` —
trap 21. Post: `m` is sent to the stream; subscribers get it in the order of the stream's mailbox,
with the sender of the letter to the stream. A stopped stream makes `m` a dead letter.

**PROCEDURE SetDefault (s: ActorCore.Ref)** — Post: stream `s` is the receiver of dead letters;
`s = NIL` — there is none.

**PROCEDURE Default (): ActorCore.Ref** — Post: the stream set as the receiver of dead letters;
`NIL` — none.
