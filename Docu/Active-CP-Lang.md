Active Component Pascal Language Report

A supplement to the Component Pascal Language Report (`Docu/CP-Lang.md`).
November 2026


Contents

1. Introduction
2. Syntax
3. Vocabulary
4. Tasks
5. Exclusive Procedures
6. Awaiting a Condition
7. Yield Points
8. Abnormal Termination
9. Implementation Limits
Appendix A. Syntax Summary
Appendix B. Differences from Active Oberon
Appendix C. Example
Appendix D. Implementation Scheme


## 1. Introduction

Active Component Pascal is Component Pascal with two additions: the exclusive procedure and the
statement that awaits a condition. Together they provide the monitor in the form introduced by
Hoare and Brinch Hansen and adopted by Active Oberon: shared data are modified only under
protection, and a task with nothing to do waits until a condition over that data becomes true.

The language has received no new reserved word. An exclusive procedure is marked by a system flag,
and the await statement is a procedure of the pseudo-module `ACTIVE`. A program that does not
import `ACTIVE` remains a Component Pascal program: its text, its object code and its symbol file
do not change.

Tasks in this report are *tasks*. A task is not a language construct but an object of the library
module `Tasks`; the report describes tasks only as far as the meaning of the new constructs depends
on them.

This report defines the language. It is not a tutorial and not a description of the library.
Everything not stated here is defined by the Component Pascal Language Report; section numbers
without qualification refer to the present text.


## 2. Syntax

The syntax is written in extended Backus–Naur form, as in the original report. One rule is changed:

```
ProcDecl  =  PROCEDURE [SysFlag] [Receiver] IdentDef [FormalPars] MethAttributes
             [";" DeclSeq [BEGIN StatementSeq] END ident].
SysFlag   =  "[" ident "]".
```

The system flag is a platform facility (the appendix on module `SYSTEM`). This report defines one
of its values, `exclusive`.

The await statement is syntactically a procedure call:

```
ACTIVE.AWAIT(Expr)
```


## 3. Vocabulary

`ACTIVE` is the name of a pseudo-module. It is imported by an ordinary `IMPORT ACTIVE` clause but
does not exist as a file: its only object, the procedure `AWAIT`, is known to the compiler. In this
it resembles the module `SYSTEM`.

`exclusive` is the identifier of a system flag. It has meaning only in a module that imports
`ACTIVE`; in other modules a flag with this name is an error.

The import of `ACTIVE` is a mark: a module whose text contains this word is intended to run under
several tasks.


## 4. Tasks

A task is the sequential execution of a program on its own stack. A task is created and started by
the library module `Tasks`; one task executes at any moment.

A running task yields the processor to another task only at a *switch point*. There are three kinds
of switch point:

1. a call to a procedure that suspends the task by its purpose (`Tasks.Yield`, `Tasks.Sleep`,
   receiving from a channel and the like);
2. an await statement whose condition is false (section 6);
3. a yield point, if the module was compiled with the corresponding key (section 7).

Between two switch points a task executes indivisibly with respect to the other tasks. It follows
that a sequence of statements without switch points needs no protection.

A program also executes outside tasks: a module body, a command, an environment event handler. Such
code behaves like a task that never waits; an await statement with a false condition in it is an
abnormal termination (section 8).


## 5. Exclusive Procedures

### 5.1. Declaration

A procedure declared with the flag `exclusive` is called an *exclusive procedure*.

```
PROCEDURE [exclusive] (b: Buffer) Put* (x: INTEGER), NEW;
PROCEDURE [exclusive] SetLevel* (x: INTEGER);
```

An exclusive procedure may be a procedure bound to a type (a method) or a procedure declared at
module level. A procedure declared inside another procedure cannot be exclusive. The flag
`exclusive` does not combine with other system flags.

An abstract method may carry the flag, but it has no code. For a procedure with an empty body the
compiler emits no entry and no exit: a call of such a procedure does not enter the monitor and does
not wait for it.

### 5.2. Monitor

A *monitor* is associated with every exclusive procedure; it belongs to at most one task at any
moment.

- The monitor of a method is the record denoted by the receiver. Different records of one type are
  different monitors.
- The monitor of a module-level procedure is a hidden variable of the module. All exclusive
  procedures of one module share one monitor.

Any record can be a monitor; this need not be declared in its type.

### 5.3. Entry and Exit

A call of an exclusive procedure proceeds as follows:

1. The task *enters* the monitor. If the monitor is free, the task becomes its owner. If another
   task owns the monitor, the caller suspends until the monitor is handed over to it (section 6.4).
2. The body of the procedure executes.
3. The task *leaves* the monitor. This happens on any completion of the body: at the end, on a
   `RETURN` statement, on abnormal termination (section 8). A function result is computed before
   the exit.

The main property follows: the bodies of exclusive procedures of one monitor do not interleave. A
statement about the data of the monitor that is true on exit from every such procedure and is not
violated outside them is true on entry to each of them. Such a statement is called a *monitor
invariant*.

### 5.4. Re-entry

A task that owns a monitor may enter it again: call another exclusive procedure of the same monitor,
or the same procedure recursively. The entry does not suspend it. The monitor is released when the
number of exits equals the number of entries.

### 5.5. Nested Monitors

A task may own several monitors. It leaves them in the reverse order of entry; this is ensured by
the nesting of calls.

Two tasks, each owning one monitor and waiting to enter the other, will never continue. The
language does not prevent this. A program avoids deadlock by entering monitors in the same order.

### 5.6. What Is Not Protected

The flag applies to a procedure, not to data. A field of a record modified outside its exclusive
procedures is not protected by the monitor. Exporting a field for writing from the module that
declares the monitor cancels the guarantee of section 5.3.

The flag is not part of the interface: it makes no difference to the caller whether a procedure is
exclusive. A method that overrides an exclusive method does not itself become exclusive; it needs
its own flag.


## 6. Awaiting a Condition

### 6.1. Form

```
ACTIVE.AWAIT(c)
```

`c` is an expression of type `BOOLEAN`, called the *condition*. The statement is allowed only in the
body of an exclusive procedure — not in a procedure nested in it and not in a procedure it calls.

There is exactly one argument. If `c` is a type or procedure name, the compiler reports error 126;
if `c` is not `BOOLEAN`, error 120. A procedure call or an assignment in `c` is error 225.

### 6.2. Meaning

If `c` is true, the statement does nothing. If `c` is false, the task leaves the monitor of its
procedure and suspends. It continues after the statement when both of the following hold: `c` is
true and the task again owns the monitor.

Consequently, after the await statement the condition is true. It need not be re-checked in a loop,
as with Hoare's condition variables.

```
PROCEDURE [exclusive] (b: Buffer) Get* (OUT x: INTEGER), NEW;
BEGIN
	ACTIVE.AWAIT(b.n > 0);
	x := b.a[b.out]; b.out := (b.out + 1) MOD LEN(b.a); DEC(b.n)
END Get;
```

Awaiting is a switch point (section 4). The monitor invariant must be true before every await
statement, as before the exit from the procedure: while the task waits, other tasks own the
monitor.

### 6.3. Restrictions on the Condition

The condition is evaluated not only by the waiting task but also by the task that leaves the
monitor (section 6.4). Therefore it is subject to two restrictions.

1. The condition contains no procedure calls and no assignments. It only reads.
2. The condition depends only on what the monitor protects and on local variables and parameters
   of its procedure. The compiler cannot check this. A condition that depends on outside data may
   become true unnoticed: the waiting task continues only on the next exit of some task from the
   monitor.

Local variables and parameters of the waiting task are visible to the condition and keep the values
they had at the moment of waiting.

`ACTIVE.AWAIT(TRUE)` is an empty statement. `ACTIVE.AWAIT(FALSE)` suspends the task until it is
cancelled (section 8).

### 6.4. Handover of the Monitor

When the owner leaves the monitor or starts waiting, the monitor is handed over as follows:

1. The conditions of the waiting tasks are evaluated in the order in which the tasks started
   waiting. The monitor is handed over to the first task whose condition is true; it becomes ready
   to run.
2. If there is no such task, the monitor is handed over to the task that suspended at entry
   earliest.
3. If there is no such task either, the monitor becomes free.

A waiting task with a true condition has priority over entering tasks. Between the moment the
condition is found true and the continuation of the waiting task, no other task owns the monitor,
so the condition cannot become false.

Awaiting releases only the monitor of the procedure in which the statement stands. Other monitors
owned by the task remain with it for the whole time of waiting. If the task entered the monitor
several times (section 5.4), waiting releases it completely, and on continuation the number of
entries is restored.

### 6.5. Fairness

The language guarantees the order of section 6.4 and nothing beyond it. A task whose condition
becomes true only while another task owns the monitor may wait forever.


## 7. Yield Points

A task executing a long loop without switch points does not let the others run. A module can be
compiled with the yield-point key. Then at the end of every repetition of the statements `WHILE`,
`REPEAT`, `LOOP` and `FOR` there is a *yield point*, at which the task may yield the processor.

Rules:

1. A task that owns at least one monitor does not switch at a yield point. Inside an exclusive
   procedure the monitor is given up only by the await statement.
2. Code executing outside a task does not switch at a yield point.
3. The key is a property of the module, not of its interface. The caller does not know whether the
   called procedure contains yield points.

A module compiled with the key loses the indivisibility property of section 4: between any two
repetitions of a loop, shared data may be modified by another task. Such a module protects shared
data with a monitor.

Runtime modules are compiled without the key; the compiler rejects the key for a framework module.
A call of a runtime procedure from a task is indivisible.


## 8. Abnormal Termination

Abnormal termination (a trap) in a task stops the execution of its procedures up to the nearest
place where it is caught; if there is no such place, the task terminates. The other tasks
continue.

On abnormal termination of an exclusive procedure the task leaves its monitor, as on normal
completion. The monitor invariant may be violated; restoring it is the program's business.

Abnormal termination is caused by:

- an await statement with a false condition outside a task;
- an error in evaluating the condition. If the condition was evaluated by a leaving task, it is not
  that task but the waiting task to which the condition belongs that terminates abnormally;
- cancellation of a task. A cancelled task terminates abnormally at the nearest switch point,
  including an await statement.

Deadlock does not cause abnormal termination. A task whose condition nobody will make true waits;
when all tasks wait, the environment reports this and continues operation.


## 9. Implementation Limits

1. The constructs are implemented for the x86 and AMD64 processors. An import of `ACTIVE` for another
   target is an error.
2. One operating-system thread executes all tasks. Two statements of a program never execute
   simultaneously.
3. A module whose code is present in a frame of any stack is not unloaded. Therefore a module whose
   monitor is owned or whose entry is awaited is also not unloaded: a call of an exclusive procedure
   leaves its frame on the stack. There is no separate check of the monitor table on unload.
4. The yield-point key is written as the character `^` after the module name in the compilation
   command.
5. The compiler determines whether a module belongs to the runtime framework by name: the subsystem
   before the first internal capital letter from the list `System`, `Std`, `Text`, `Form`, `Host`,
   `Dev`, `Lin`, `Win`, or an explicit list of modules of the `System` subsystem. A user module with
   such a prefix does not get the key; a framework module with another name does.
6. An empty body and an abstract method do not take the monitor: entry and exit are not generated.
   This is observable only as the absence of waiting when the monitor is occupied.
7. An import of `ACTIVE` for another target gives the general error 151 (`incorrect symbol file`);
   there is no separate message for the extension.


## Appendix A. Syntax Summary

```
ProcDecl  =  PROCEDURE [SysFlag] [Receiver] IdentDef [FormalPars] MethAttributes
             [";" DeclSeq [BEGIN StatementSeq] END ident].
SysFlag   =  "[" ident "]".
```

Context restrictions:

| Construct | Condition of admissibility |
|---|---|
| `[exclusive]` | the module imports `ACTIVE`; the procedure is declared at module level or bound to a type; no other system flags; the numeric form `[32]` is accepted |
| `ACTIVE.AWAIT(c)` | stands directly in the body of an exclusive procedure; exactly one argument; `c` is of type `BOOLEAN`, without calls and assignments |
| yield-point key | the module name does not belong to the runtime framework by the rule of section 9, item 5 |


## Appendix B. Differences from Active Oberon

| Active Oberon | Active Component Pascal |
|---|---|
| block `BEGIN {EXCLUSIVE} … END` | procedure with the flag `[exclusive]`; there is no exclusive block inside a procedure |
| `AWAIT(c)` | `ACTIVE.AWAIT(c)`; the condition has no procedure calls or assignments |
| active object: body `BEGIN {ACTIVE}` | none; a task is created by the module `Tasks` |
| re-entry into one's own monitor is an error | allowed |
| an error in an exclusive block leaves the monitor held | the monitor is released |
| shared access `{SHARED}` | none |
| several processors | one thread |


## Appendix C. Example

A bounded buffer is the classical problem on which a monitor is tested. A producer puts numbers, a
consumer takes them; the producer waits while the buffer is full, the consumer while it is empty.

```
MODULE ObxBuffer;
	IMPORT ACTIVE;

	CONST N = 8;

	TYPE
		Buffer* = POINTER TO RECORD
			a: ARRAY N OF INTEGER;
			in, out, n: INTEGER	(* 0 <= n <= N; n elements starting at out *)
		END;

	PROCEDURE [exclusive] (b: Buffer) Put* (x: INTEGER), NEW;
	BEGIN
		ACTIVE.AWAIT(b.n < N);
		b.a[b.in] := x; b.in := (b.in + 1) MOD N; INC(b.n)
	END Put;

	PROCEDURE [exclusive] (b: Buffer) Get* (OUT x: INTEGER), NEW;
	BEGIN
		ACTIVE.AWAIT(b.n > 0);
		x := b.a[b.out]; b.out := (b.out + 1) MOD N; DEC(b.n)
	END Get;

END ObxBuffer.
```

The invariant is written in the comment to the fields. It is true after `NEW`, every procedure
preserves it, and the fields are not exported; by section 5.3 it is true at every entry. The
condition of every await statement depends only on the fields of the monitor, as section 6.3
requires.


## Appendix D. Implementation Scheme

This appendix does not define the language. It describes how the constructs are translated, so that
the reader can estimate their cost.

An exclusive procedure with a non-empty body receives at its beginning a call `Kernel.Lock(m)` and
before its return a call `Kernel.Unlock(m)`, where `m` is the receiver or a hidden variable of the
module. All `RETURN` statements lead to one exit point. An abstract method and a procedure with an
empty body have no such calls. The first entry for a given object creates a monitor record. An
uncontended entry and an exit take records from free lists and allocate no memory; waiting and
entry into an occupied monitor create queue records.

The await statement is translated as follows:

```
@frm := SYSTEM.GETREG(BP);
IF ~c THEN Kernel.Await(P, @frm, m) END
```

`P` is a hidden procedure nested in the exclusive procedure, with the body `RETURN c`. It is passed
the address of the frame of the waiting procedure; through it the condition reads its local
variables. The frame exists on the stack of the waiting task for the whole time of waiting. The
kernel evaluates the condition once more in `Kernel.Await` through `Kernel.CallCond`, passing the
frame in register `BX`. This explains both restrictions: the condition is evaluated by another
task, so it contains no calls; the frame belongs to one procedure, so the statement stands in its
body, not in a nested one.

Calls to the kernel go through the hidden area `"$$"`; the loader binds it to `Kernel`. A module that
does not use the extension therefore gets no extra import.

A monitor is not a field of a record. The environment stores information about occupied monitors in
a table keyed by the address of the record; a monitor record exists while it is owned or awaited. A
free monitor occupies no table entry: the record is returned to the free list.

A yield point is a call to `Kernel.Poll`, which checks the flag and returns if no switch is needed.
