[← Chapter 8](Tut-Active-8.md) · [Contents](Tut-Active-TOC.md) · [Appendix B →](Tut-Active-B.md)

# Appendix A. Map of choices

A row of the table is a situation the programmer is in. On the right: what to take, where the
tutorial tells about it and which example to look at. The details of each facility are in the guides
`Docu/Tasks.md` and `Docu/Async.md`.

| I need to | Take | Chapter | Example |
|---|---|---|---|
| do a short job later or on a timer | action `Services.Action`, `DoLater` | 1 | `ObxBank0` |
| write a job with waiting in the middle | task `Tasks.Task`, `Tasks.Start` | 1, 2 | `ObxBank0`, `ObxTasks0` |
| wait for another task to finish | `Tasks.Join` | 2 | `ObxBank1`, `ObxTasks1` |
| wait for some time | `Tasks.Sleep` | 2 | `ObxBank1` |
| stop a task | `Tasks.Cancel` | 2 | `ObxBank1` |
| make the failure of one task cancel its neighbours | group: `NewGroup`, `StartIn` | 2 | `ObxTasks3` |
| change shared data with no waiting inside | nothing: between switch points it is indivisible anyway | 2 | `ObxBank1`, `ObxTasks0` |
| change shared data with waiting inside | exclusive procedure `[exclusive]` | 3 | `ObxBank2` |
| wait for a condition over the data of a monitor | `ACTIVE.AWAIT` | 3 | `ObxBank2`, `ObxTasks2` |
| understand why everything has stopped | deadlock log, `Tasks.Deadlocked`, `Tasks.SetName` | 3 | `ObxBank3`, `ObxTasks4` |
| change several objects "all or nothing" | transaction `AsyncStm.Atomically` | 4 | `ObxBank4`, `ObxStm0` |
| wait for a condition over several objects | `AsyncStm.Retry` | 4 | `ObxBank4`, `ObxStm1` |
| do one thing, and if that is impossible, another | `AsyncStm.OrElse` | 4 | `ObxBank4`, `ObxStm2` |
| give the data to one task and send requests from the others | mailbox `AsyncChan.NewMailbox` | 5 | `ObxBank5`, `ObxChan2` |
| hand over from hand to hand, the sender waits for the receiver | rendezvous `AsyncChan.NewChan` | 5 | `ObxBank5`, `ObxChan0` |
| keep a fast sender from running ahead | buffer `AsyncChan.NewBuffer` | 5 | `ObxChan1` |
| check the order of a conversation over a channel | protocol `AsyncProto`, `AsyncChan.SetProtocol` | 5 | `ObxProto0` |
| wait for several events at once | selection `AsyncChan.Select` with `RecvAlt`, `SendAlt` | 6 | `ObxBank6` |
| wait no longer than a given time | `AsyncChan.TimeoutAlt` in a selection | 6 | `ObxBank6`, `ObxSelect0` |
| get one result later | single-assignment variable `AsyncOnce` | 6 | `ObxBank6`, `ObxOnce` |
| keep a long loop from freezing the window | compiler key `^` (yield points) | 7 | — |
| make a blocking call of the OS or of a C library | worker thread `AsyncWork.Execute` | 7 | `ObxWork0` |
| read and write sockets without stopping the rest | `AsyncIO` | 7 | — |
| compute on several cores with a heap and objects | computons `AsyncComputon` | 7 | `ObxComputon` |
| wait for the result of a computon together with channels | `AsyncComputon.AltPull` in a selection | 6, 7 | — |
| repeat a request over a network without withdrawing twice | operation number, idempotent receiver | 8 | — |
| transfer between machines | saga: steps with compensations | 8 | — |

Three questions that narrow the choice faster than the table:

1. Is there the word "wait" in the job? If not, an action or a plain procedure is enough. If yes, a task.
2. Do several tasks touch the data? If the data can be given to one task and requests sent to it,
   do so (chapter 5). If not, a monitor or a transaction (chapters 3 and 4).
3. Is one thread enough? If only responsiveness is in the way, a yield point. If the power of several
   cores is needed, computons; if a blocking call is in the way, a worker thread (chapter 7).
