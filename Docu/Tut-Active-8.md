[← Chapter 7](Tut-Active-7.md) · [Contents](Tut-Active-TOC.md) · [Appendix A →](Tut-Active-A.md)

# 8. Several machines

## 8.1. Accounts in different cities

The bank has grown. The accounts of the Moscow clients lie on one machine, those of the Petersburg
clients on another. A client transfers 100 from Moscow to Petersburg. Everything that chapters 2–4
rested on does not work here.

There is no shared memory: the Petersburg program cannot read a Moscow variable. So there is no
indivisibility between switch points, no monitors, no transactions; they were held up by one thread
of one process. What is left is messages, like the cashier in chapter 5: "withdraw 100", "credit
100".

And a message over a network can be lost. Worse, when no answer has come, the sender does not know
what happened: the request did not arrive, or it arrived and was carried out and the answer was lost,
or the machine is still thinking. Three techniques follow from this, and they are the foundation of
distributed programs.

## 8.2. Repeat with an operation number

There is no answer, so the request is sent again. But if the first request arrived and was carried
out, the repeat will withdraw the money a second time. So every request carries an *operation
number*, which the sender issues and which does not change on a repeat. The receiver remembers the
numbers of the requests it has carried out and answers a repeat with the earlier answer, without
doing anything again:

```
WITH m: Request DO
	IF done.Has(m.op) THEN
		Reply(m, done.Answer(m.op))	(* repeat: give the same answer *)
	ELSE
		ok := Withdraw(m.account, m.sum);
		done.Put(m.op, ok);	(* remember before replying *)
		Reply(m, ok)
	END
END
```

Here `done` is a table of carried-out operations, and `Withdraw` and `Reply` are procedures of the
cashier; this is a sketch, not a module. An operation that can be repeated without harm is called
*idempotent*. A repeat plus an operation number give "exactly once" out of the two things a network
can do: "at most once" and "at least once". The table of numbers is kept where the accounts are, and
just as reliably: if the machine restarts and forgets the numbers, a repeat will withdraw twice
again.

We met this question in chapter 7: the executor of a computon can die without answering, and the
request is repeated on another one. The rule there is the same: `Do` must survive a repeat.

## 8.3. Saga: do it, and if it fails, take it back

A transfer from Moscow to Petersburg is two steps on two machines: withdraw in Moscow, credit in
Petersburg. There is no transaction across the network, and both steps cannot be rolled back at
once. Instead of a rollback one writes a *compensation*: for each step, a step that undoes its
effects. A sequence of steps with compensations is called a *saga*:

1. Moscow: withdraw 100 (operation number 17-1). If it fails, the transfer is rejected and there is
   nothing more to do.
2. Petersburg: credit 100 (number 17-2). If it succeeds, the transfer is finished.
3. If it fails (the account is closed, or seized), Moscow: return 100 (number 17-3).

Between the steps the money is "in transit": it is no longer in Moscow and not yet in Petersburg.
For this time the sum over all accounts is smaller by 100, and the first rule of the bank holds only
with the reservation "when all transfers have finished". This is the price of giving up shared
memory, and it cannot be avoided. Each step is repeated with its operation number until there is an
answer, so the compensation is idempotent too. And a saga has to remember which step it stopped at,
so that after a restart it can finish the transfer. This is the same problem of "where did I stop"
that the stack of a task solved in chapter 1, only now the state lies on disk.

## 8.4. What exists for this

Inside one machine, computons (chapter 7) travel between BlackBox processes. The transport is
chosen: `pipe` is OS channels to child processes, `tcp` is a connection that the manager listens for
on `127.0.0.1`, `mpi` is the external MPI library, which starts ranks on several machines of a
cluster as well. The death of an executor over `pipe` and `tcp` is noticed, and the request is
repeated; MPI does not report the death of a rank, and there is no repeat there. A computon is
"compute and return", with no state between requests; for transfers between accounts on different
machines it is not enough.

For long-lived objects with their own state that communicate only by letters, the subsystem of
*actors* is being built (decision record `docs/adr/0018-actors.md`). An actor is the cashier from
chapter 5 made into a rule: its own data, its own mailbox, letters one at a time. Actors currently
exist in a separate branch and work inside one process; delivery between processes and machines,
keeping state as a journal with snapshots, and sagas between nodes are not done yet. Part III of the
tutorial will be about them.

## 8.5. What not to do

Do not carry the techniques of one machine over to several. A monitor will not protect data on
another machine; an `AsyncStm` transaction will not cover a variable in another process. A transfer
written as "send the withdrawal, send the credit" with no operation numbers and no compensation will
work in tests and lose or double money at the first network failure.

And do not make distributed what fits on one machine. Twelve computons over TCP take 31 ms in the
test even inside one machine, while a transfer by a transaction costs 76–82 ns
(`docs/measurements.md`, sections 5 and 11); over a real network every message costs still more.
Accounts that are often transferred between each other are kept on one machine, and sagas are left
for the rare transfers between machines.
