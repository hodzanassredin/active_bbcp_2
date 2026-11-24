[← Appendix A](Tut-Active-A.md) · [Contents](Tut-Active-TOC.md)

# Appendix B. For those who know other systems

Here are the correspondences for a programmer who has already written in Go, Erlang, Active Oberon
or Concurrent ML. The correspondence is approximate: a similar thing serves the same purpose, but
has its own rules. There is one difference common to all four: BlackBox runs all tasks on one OS
thread, without preemption. So the code between switch points is indivisible without locks, and
parallel computation goes to worker threads and processes (chapter 7).

## B.1. Go

| Go | Here | Remark |
|---|---|---|
| goroutine, `go f()` | task, `Tasks.Start(t)` | like Go with `GOMAXPROCS=1`, but without preemption: a `Yield` or a yield point is needed |
| `chan T` without a buffer | rendezvous `AsyncChan.NewChan` | |
| `make(chan T, n)` | buffer `AsyncChan.NewBuffer(n)` | |
| — | mailbox `AsyncChan.NewMailbox` | sending does not wait; Go has no such channel |
| `select` | `AsyncChan.Select` | of the ready ones the first by number wins; in Go a random one |
| `time.After` in `select` | `AsyncChan.TimeoutAlt` | |
| `close(ch)` | none; the end of a stream is sent as a conventional message, for example `NIL` | `ObxBank5` |
| `sync.Mutex` | exclusive procedure `[exclusive]` | the lock is tied to the procedure and is not taken by hand |
| `sync.Cond` | `ACTIVE.AWAIT(condition)` | the condition is written directly, with no loop `for !cond { Wait() }` |
| `context` with cancellation | `Tasks.Cancel` | cancellation is trap 127 at the nearest wait |
| `errgroup` | group `Tasks.NewGroup`, `StartIn` | the failure of a member cancels the others |
| `sync.WaitGroup` | `Tasks.Join` on each task, or a group | |
| `sync/atomic` | not needed: code between switch points is indivisible | |

A Go programmer will recognize almost everything. The main difference: a goroutine with no waiting
calls will sooner or later be preempted in Go, while here the task holds the thread to the end of the
loop. The other side of the same thing: data races in the Go sense do not exist here. The cost of
the operations is comparable with Go on one thread: an exchange through two rendezvous channels is
2.1 times more expensive, creating a task from the pool costs the same (`docs/measurements.md`,
section 2.2).

## B.2. Erlang

| Erlang | Here | Remark |
|---|---|---|
| process | task | a task shares the address space with the others; isolation is kept by the programmer |
| the mailbox of a process | `AsyncChan.Mailbox`, which the task keeps for itself | a task may have several mailboxes |
| `receive` with patterns | `Recv` takes the first letter; selection by kinds of letters is `WITH` after the receive | there is no selective receive |
| `receive … after` | `Select` with `TimeoutAlt` | |
| `link`, exit propagation | group of tasks | the failure of a member cancels the group |
| supervisor, restart | none in this branch; supervision is in the actors branch | `docs/adr/0018-actors.md` |
| distributed nodes | computons between processes; actors between machines are being built | chapter 8 |
| `gen_server` | a cashier task with a mailbox, the answer through a single-assignment variable | `ObxBank5`, `ObxBank6` |

The cashier of chapter 5 is the Erlang `gen_server`, written by hand. The difference is that Erlang
does not let a process touch foreign data at all, while here this is a prohibition by agreement: the
memory is shared, and a task can read a global variable of a module. This is why the data of the
cashier is made local variables of its `Run`, as in `ObxBank5`: another task cannot reach it there.
The actors subsystem (Part III) will turn this agreement into a rule.

## B.3. Active Oberon

| Active Oberon | Here | Remark |
|---|---|---|
| active object, body `BEGIN {ACTIVE}` | task: a record `Tasks.Task` with the method `Run` | created by the module `Tasks`, not by the language |
| block `BEGIN {EXCLUSIVE} … END` | procedure with the flag `[exclusive]` | there is no exclusive block inside a procedure |
| `AWAIT(c)` | `ACTIVE.AWAIT(c)` | the same scheme: the conditions are rechecked by the one leaving the monitor |
| re-entering one's own monitor is an error | allowed | |
| an error in an exclusive block leaves the monitor busy | the monitor is released | |
| `{SHARED}` | none | |
| several processors | one thread | |

Whoever has written in Active Oberon will feel at home in chapter 3: the monitor and `AWAIT` are
taken from there. The full comparison is in appendix C of the language report
`Docu/Active-CP-Lang.md`. Active Oberon has no channels, selection or transactions; here they are in
the library `Async`.

## B.4. Concurrent ML and Hopac

Concurrent ML (CML, John Reppy) makes an event into a value. `recvEvt ch` is not a receive but a
description of a receive; it can be stored, passed around and combined with other events: `choose
[e1, e2]` is "one of the two", `wrap e f` is "when `e` happens, apply `f` to the result", `guard g`
is "compute the event anew before waiting", `withNack` is "if my branch was not chosen, tell me".
`sync e` waits for an event. Hopac is the same for F#, with the emphasis on speed.

| CML, Hopac | Here | Remark |
|---|---|---|
| `recvEvt`, `sendEvt` | `AsyncChan.RecvAlt`, `SendAlt` | fill in a record `AsyncSelect.Op` and do not return a value |
| `timeOutEvt` | `AsyncChan.TimeoutAlt` | |
| `choose` | array of alternatives | one level; there is no nested selection |
| `sync` | `AsyncChan.Select(ops)` | returns a number, not the result of a handler |
| `wrap` | `CASE` on the number after `Select` | the handler is written after the selection and is not attached to the event |
| `guard` | code before filling in the alternatives | |
| `withNack` | `Signal.Disarm` of one's own event | the selection itself notifies the losing alternatives |
| own event | extension of `AsyncSelect.Signal` | `AltSignal`; this is how `AsyncComputon.AltPull` is built |
| `IVar` | `AsyncOnce.Once` | |
| `Mailbox` | `AsyncChan.Mailbox` | |

Inside, the selection follows the pattern of CML and Hopac: each alternative leaves an *offer* in the
channel, all the offers of one selection share a flag, the winner clears the flag, and the losers are
withdrawn (`docs/adr/0011-channels-on-offers.md`). From outside the difference is that there are no
events as values: an alternative lives only within one call of `Select`, and it cannot be returned
from a procedure and used to build a new event. The price is not paid in memory allocated for each
event, and a selection reads like a `CASE` statement. If a library of events that compose as in CML is
needed, it can be built on top of `AsyncSelect.Signal`; this branch does not have one.
