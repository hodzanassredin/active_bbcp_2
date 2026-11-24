[← Chapter 6](Tut-Active-6.md) · [Contents](Tut-Active-TOC.md) · [Chapter 8 →](Tut-Active-8.md)

# 7. Heavy computation and blocking calls

## 7.1. One cashier computes the annual report

At the end of the year the bank reconciles all accounts: it goes through a million operations,
recomputes the balances and compares them with the recorded ones. This is a few seconds of pure
computation. If the reconciliation is done by a task written as an ordinary loop, the other tasks
stand still for all these seconds, and the window does not respond. Nobody interrupts a task
(chapter 2), and this holds even when one would like it to be interrupted.

There is a second trouble, similar to the first. A task reads a file from a slow network disk or
asks the system for the address of a server by name. Such operating system calls are *blocking*: the
thread that made one stands until the answer comes. And the tasks have one thread. While one cashier
waits for the disk, everybody stands.

This chapter offers three means. They differ in where the work runs: in the same thread, in another
thread of the same process, or in another process.

## 7.2. Yield point: the same thread, but with pauses

The cheapest means is to let the long loop yield by itself. A module compiled with the key `^`
gets a *yield point* at the end of each iteration of the loops `WHILE`, `REPEAT`, `LOOP` and `FOR`:

```
DevCompiler.CompileThis ObxAudit^
```

Every 5 ms the scheduler raises a flag, and the nearest yield point does a `Yield`. The text of the
module does not change; the reconciliation stays an ordinary loop, while the window and the other
tasks get control.

The key has a cost, and it must be kept in mind. In a module with the key the indivisibility from
chapter 2 no longer holds: between two iterations of a loop another task can change shared data. The
transfer from `ObxBank1`, compiled with the key, stops being safe if the transfer contains a loop.
This is why a module with the key protects shared data with a monitor. A task that holds a monitor
does not switch at a yield point, since otherwise the monitor would stay busy while other work
runs; code outside a task does not switch either. The modules of the environment (`System`, `Std`,
`Host` and others) do not accept the key: the compiler answers with error 225. For details see
sections 7 and 9.2 of the language report `Docu/Active-CP-Lang.md`.

A yield point makes a program responsive, but not faster: the computation still runs on one core.

## 7.3. Worker thread: a blocking call on the side

A blocking call needs another operating system thread. The pool `AsyncWork` keeps up to four
*worker threads*. `AsyncWork.Execute(proc, arg)` hands the procedure `proc` to a free thread, puts
the task to sleep and wakes it when the thread finishes. The other tasks and the window keep working
all this time. The ready-made jobs are reading and writing a file (`ReadFileJob`, `WriteFileJob`) and
resolving a host name (`ResolveJob`); the example is `ObxWork0`, and see section 10 of the guide
`Docu/Async.md`.

The main rule of a worker thread: it must not touch the BlackBox heap. It must not call `NEW`, must
not write into objects created by `NEW`, and must not call framework procedures or the log. The heap
and the garbage collector are designed for one thread; allocating memory in a worker thread corrupts
the free-block lists, and the collector may free an object that the thread is still writing into.
This is why the argument of a job is a global variable of the module or a record without pointers,
like `AsyncWork.FileReq`. Neither the compiler nor the library checks this, and a violation shows up
as memory corruption at a random moment; such an error is the hardest to find.

From this one can see what a worker thread is good for: calling external code that computes or
waits over simple data, such as a system call or a function of a C library. It will not speed up
computation in Component Pascal with objects. In the test `Async_devWorkers` a job waits 300 ms in a
worker thread, and the main loop manages 286 steps in that time (`docs/measurements.md`, section 3).

Network I/O is not moved into a worker thread: sockets have a non-blocking mode, and for them there
is `AsyncIO`, reading and writing that wait for data the same way `Recv` waits for a message
(section 9 of the guide `Docu/Async.md`).

## 7.4. Computon: another process with its own heap

One wants to split the reconciliation of a million operations into parts and compute them on
several cores, with objects, `NEW` and the whole language. For this several BlackBox processes are
started. Each has its own heap and its own garbage collector, and the rule "do not touch the heap"
is lifted.

A *computon* is an object that is sent to such a process, the *executor*, where its method `Do` is
called and it is sent back. The type of a computon extends `AsyncComputon.Computon` and can write
itself into a stream and read itself back (`Externalize`, `Internalize`), like any BlackBox
document. The order of work:

1. `AsyncComputon.Start(n)` starts `n` executors, from 1 to 16;
2. `Push(c, rank, tag)` sends the computon `c` to the executor number `rank`; `tag` is the number of
   the request;
3. `Pull(rank, tag)` waits for the answer and returns the computon with the result;
4. `Stop` shuts the executors down.

The example is `ObxComputon`: the square of a number is computed in a separate process. For the
reconciliation of accounts a computon would carry a piece of the operations and return the sum of
the discrepancies.

An executor can die: from a kernel trap, from running out of memory, from `kill`. Then the
unfinished requests are repeated on a live executor. This is why `Do` may run twice: the executor
computed, died without answering, and the same computon was computed again. So `Do` must have no
effects outside its own object (no writing to files, no sending messages), or those effects must
survive a repeat. This question, "what happens on a repeat", comes back in chapter 8.

Sending is expensive compared with a call: 12 squares on one executor take 11 ms, a repeat after
the death of an executor takes 415 ms (`docs/measurements.md`, section 5). A computon is given a
piece of work of tens of milliseconds and more, not a single addition. For the details, the
transports (`pipe`, `tcp`, MPI) and the facade `RobustParallel`, see section 11 of the guide
`Docu/Async.md`.

## 7.5. What to choose

| Situation | Means | Limitation |
|---|---|---|
| A long loop in Component Pascal, responsiveness is needed | yield point (key `^`) | indivisibility between iterations is lost; one processor |
| A blocking call of the OS or a C library | worker thread, `AsyncWork.Execute` | no BlackBox heap in the thread |
| Sockets, non-blocking I/O | `AsyncIO` | only what has a non-blocking mode |
| Heavy computation with objects, on several cores | computons | sending is expensive; `Do` may run twice |

If in doubt, start with a yield point: it costs nothing until it interferes with indivisibility. When
real parallelism is needed, the choice between a thread and a process is decided by one thing: whether
the work needs the heap.
