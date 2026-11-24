# Tutorial: Several Things in One Program

A tutorial for a programmer who writes Component Pascal in BlackBox and wants the program to do
several things at once: serve a window and compute, wait for an answer and not freeze, keep shared
data in order. It explains why each thing is needed and when to take it. How a thing works in every
detail is told by the guides: `Docu/Tasks.md` (tasks), `Docu/Async.md` (the library `Async`) and the
language report `Docu/Active-CP-Lang.md` (monitors, `AWAIT`, yield points).

The running example of the tutorial is a cash desk and accounts. There are several accounts, and
money is moved from one to another. There are two rules: the sum over all accounts does not change,
and an account does not go below zero. The example grows from chapter to chapter; each version of it
is a module `ObxBank0`…`ObxBank6` of the subsystem `Obx`, it can be built and run, and each has a
page in `Obx/Docu/`.

## Contents

Part I. One program, many things

1. [Why it is needed](Tut-Active-1.md) — a window that does not answer; action and task.
2. [The first task](Tut-Active-2.md) — start, wait, put to sleep, cancel; switch points.
3. [Shared data and a monitor](Tut-Active-3.md) — a wait in the middle of a transfer, `AWAIT`, deadlock.
4. [Transactions](Tut-Active-4.md) — a transfer that rolls back; `Retry` and `OrElse`.
5. [Messages instead of shared data](Tut-Active-5.md) — a clerk and a counter window; channels and mailboxes.
6. [Choice](Tut-Active-6.md) — waiting for several events at once; a promise of a result.

Part II. Beyond one thread

7. [A heavy computation and blocking calls](Tut-Active-7.md) — yield point, worker thread, process.
8. [Several machines](Tut-Active-8.md) — messages, a retry with an operation number, a saga.

Part III. Actors — will be written when the actor subsystem enters this branch.

Appendices

- [A. Selection map](Tut-Active-A.md) — "I need … → take …".
- [B. For those who know other systems](Tut-Active-B.md) — Go, Erlang, Active Oberon, Concurrent ML.

The examples are built by `tools/build64.sh`; they are for Linux AMD64 only. The command of each
example is run in a BlackBox window by a commander or from the console by a line of the form

```sh
echo 'ObxBank1.Do' | tools/run64.sh
```
