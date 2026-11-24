[Contents](Tut-Active-TOC.md) · [Chapter 2 →](Tut-Active-2.md)

# 1. Why it is needed

## 1.1. A window that does not answer

An accountant presses the button "Recalculate interest on all accounts". There are a hundred
thousand accounts, each takes a little time, and the total is half a minute. If the button command
simply loops over all accounts, the BlackBox window does not answer for that half a minute: it is
not redrawn, it takes no clicks, and the "Cancel" button cannot be pressed. The accountant thinks
the program has hung.

The reason is how BlackBox serves a window. It has one *main loop*: it takes the next event from
the operating system (a click, a key press, a request to redraw the window), calls its handler,
waits until the handler returns, and takes the next one. One OS thread does all of this. While the
handler computes interest, the next event is not reached.

There is one way out: split the long work into pieces and return control to the main loop between
the pieces. BlackBox has had the *action* (`Services.Action`) for this from the start. This branch
also has the *task* (`Tasks.Task`). Both solve the same trouble but are written differently, and
this chapter is about the difference.

## 1.2. An action

An action is an object with a method `Do`. It is put into the queue by a call
`Services.DoLater(a, time)`, and the main loop, when its turn comes, calls `a.Do` — once. If the
work is not finished, `Do` puts itself into the queue again. Between the calls the main loop has
time to handle the window events.

Three properties follow. An action has no stack of its own: `Do` is an ordinary procedure, it
starts from the first line and must return. It cannot wait in the middle of the work: if `Do`
stops, the whole main loop stops. And the place where the work stopped is kept by the programmer —
in the fields of the object. The loop "over all accounts" turns into "process account number
`a.i`, increase `a.i`, and if it is not the end, put yourself into the queue again".

## 1.3. A task

A task is an object with a method `Run`, which is written as an ordinary procedure: a loop over the
accounts, the work inside. A task has a stack of its own, so it can stop in the middle of the loop
and continue later from the same place: its local variables and its position in the code are on
its stack. To stop means to call `Tasks.Yield` ("let the others go"), `Tasks.Sleep` ("sleep so
many milliseconds") or another wait from the following chapters. A task is started by a call
`Tasks.Start(t)`.

Tasks are executed by the *scheduler* — itself an action of `Services`, which the main loop calls
like all the others. In one call the scheduler lets the ready tasks work until a quantum of 5 ms
ends, and returns control. So the tasks and the actions live in one main loop, on one thread, and
the window stays alive while the tasks work. The details are in section 5 of the guide
`Docu/Tasks.md`.

## 1.4. One example in two forms

A statement for three accounts: print the balance of each, but not all at once — one account at a
time, giving up control between them. The module `ObxBank0` does it twice, with an action and with
a task:

```
MODULE ObxBank0;
	IMPORT Services, Tasks, Console, Strings;

	CONST n = 3;	(* число счетов *)

	TYPE
		Statement = POINTER TO RECORD (Services.Action) i: INTEGER END;
		StatementTask = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: ARRAY n OF INTEGER; done: BOOLEAN;

	PROCEDURE Line (IN who: ARRAY OF CHAR; i: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(who); Console.WriteStr(": account ");
		Strings.IntToString(i, s); Console.WriteStr(s);
		Console.WriteStr(" balance "); Strings.IntToString(balance[i], s); Console.WriteStr(s);
		Console.WriteLn
	END Line;

	(* Действие: один счёт за вызов, место в работе хранит поле i *)
	PROCEDURE (a: Statement) Do;
	BEGIN
		Line("action", a.i);
		INC(a.i);
		IF a.i < n THEN Services.DoLater(a, Services.now) ELSE done := TRUE END
	END Do;

	(* Задача: обычный цикл, место в работе лежит на её стеке *)
	PROCEDURE (t: StatementTask) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO Line("task", i); Tasks.Yield END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR a: Statement; t: StatementTask;
	BEGIN
		balance[0] := 100; balance[1] := 250; balance[2] := 40;
		NEW(a); a.i := 0; done := FALSE; Services.DoLater(a, Services.now);
		WHILE ~done DO Services.actionHook.Step END;
		NEW(t); done := FALSE; Tasks.Start(t);
		WHILE ~done DO Services.actionHook.Step END
	END Do;

END ObxBank0.
```

The command prints:

```
action: account 0 balance 100
action: account 1 balance 250
action: account 2 balance 40
task: account 0 balance 100
task: account 1 balance 250
task: account 2 balance 40
```

The result is the same, the texts are different. The action `Statement` needed the field `i`: it
holds which account is next, and `Do` each time starts from the beginning, looks at the field and
decides what to do. The task `StatementTask` is written with a `FOR` loop; the variable `i` is
local and survives `Tasks.Yield`, because it lies on the stack of the task.

The command `Do` executes outside the main loop — from the console or a commander — and therefore
turns the loop itself with a call `Services.actionHook.Step`: one such call is one pass of the main
loop over the ready actions. In the second loop the same call also executes the task, because the
scheduler is an action too. In a BlackBox window this is not needed: the main loop turns there by
itself.

On three accounts the difference is small. It grows with the algorithm. Suppose the statement has
to be printed by branches, inside a branch by clients, and for a client by accounts, and after each
account to wait until the printer is free. The task stays three nested loops with a wait inside.
The action turns into an automaton: fields "current branch", "current client", "current account",
"what we are waiting for", and a `Do` that branches on them. The programmer does by hand the work
that the stack does for a task.

## 1.5. What happens if you wait in an action

An action cannot wait, and this rule is checked. If `Do` calls `Tasks.Sleep`, the program traps
with number 21 and the text `Tasks: wait inside an action handler or domain broadcast`. The
BlackBox framework relies on an event handler or an action being executed to the end before the
next event begins; a handler stopped in the middle would leave the framework in an unfinished
state. An action that trapped in `Do` is not put back into the queue by the main loop — otherwise
the broken action would trap on every pass.

A task, on the contrary, must not touch windows and documents directly from its `Run` if that
needs waiting in the middle of the framework's work. The task does its part, queues an action that
will update the window, and goes on waiting on its own.

## 1.6. What to take when

| Need | Take | Why |
|---|---|---|
| Short work "later", one call | action | it needs no stack of its own; it is cheaper than a task |
| Repeat something on a timer, no waits inside | action | `DoLater` with a time is a timer |
| Work in several steps with a wait between them | task | the algorithm is written straight, the state is on the stack |
| Many simultaneous conversations (clients, requests) | tasks, one per conversation | each conversation has its own "where I stopped" |
| Update a window or a document | action (from a task — queue it) | the framework relies on indivisible handlers |
| A long computation without waits | task with yield points, or another thread | chapter 7 |

A rule of thumb: if the description of the work has the word "wait" in it, you need a task. If the
work is done in one short call, an action is enough.

In the next chapter we write the first real task: several clerks move money between accounts, and
a manager waits until they finish.
