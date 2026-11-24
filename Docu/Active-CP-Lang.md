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
Appendix B. Differences from Component Pascal
Appendix C. Differences from Active Oberon
Appendix D. Example
Appendix E. Implementation Scheme


## 1. Introduction

Active Component Pascal is Component Pascal with two additions: the exclusive procedure and the
statement that awaits a condition. Together they provide the monitor in the form introduced by
Hoare and Brinch Hansen and adopted by Active Oberon: shared data are modified only under
protection, and a task with nothing to do waits until a condition over that data becomes true.

The language adds no reserved words. A system flag marks an exclusive procedure; the await statement
is a procedure of the pseudo-module `ACTIVE`. A program that does not import `ACTIVE` remains a
Component Pascal program: its text, object code and symbol file are the same.

This report calls processes tasks. A task is not a language construct but an object of the library
module `Tasks`. The report describes tasks only as far as the meaning of the new constructs depends
on them.

The report defines the language. It is neither a tutorial nor a library description. Everything not
stated here is defined by the Component Pascal Language Report. Section numbers without a qualifier
refer to the present text.

The words "The compiler does not check this" mark a rule that the compiler does not check. Such a
rule is a convention: a program that breaks it compiles, but this report does not define its
behaviour.


## 2. Syntax

The syntax is written in Extended Backus-Naur Form, as in the original report. The report changes
one rule and adds one:

```
ProcDecl        =  PROCEDURE [SysFlag] [Receiver] IdentDef [FormalPars] MethAttributes
                   [";" DeclSeq [BEGIN StatementSeq] END ident].
SysFlag         =  "[" (ident | number) "]".
AwaitStatement  =  Qualident "(" Expression ")".
```

`AwaitStatement` is a procedure call whose `Qualident` denotes `AWAIT` of the pseudo-module `ACTIVE`
(`ACTIVE.AWAIT`, or `A.AWAIT` with the import `A := ACTIVE`). It stands where a statement is allowed.

A system flag is a platform facility (the appendix on module `SYSTEM`). The present report defines
one of its values — `exclusive`; its numeric form is `32`.


## 3. Vocabulary

`ACTIVE` is the name of a pseudo-module. A module imports it with an ordinary `IMPORT ACTIVE` clause.
There is no module file `ACTIVE`: the compiler knows its only object, the procedure `AWAIT`. In this
`ACTIVE` resembles the module `SYSTEM`.

`exclusive` is the identifier of a system flag. It is allowed only in a module that imports
`ACTIVE`.

Importing `ACTIVE` serves as a mark: a module whose text contains this word is meant for execution
by several tasks.


## 4. Tasks

A *task* is the sequential execution of a program on its own stack. The library module `Tasks`
creates and starts a task; one task runs at any moment.

A running task yields the processor to another only at a *switch point*. There are three kinds of
switch point:

1. a call to a procedure whose purpose is to suspend the task (`Tasks.Yield`, `Tasks.Sleep`,
   receiving from a channel and the like);
2. entering a monitor owned by another task (section 5.3), and an await statement whose condition is
   false (section 6);
3. a yield point in a module compiled with the yield-point key (section 7).

Between two switch points a task executes indivisibly with respect to the other tasks. A sequence of
statements without switch points therefore needs no protection.

A program also executes outside tasks: a module body, a command, an environment event handler. Such
code behaves like a task that never waits. An await statement with a false condition in it is an
abnormal termination (section 8).


## 5. Exclusive Procedures

### 5.1. Syntax

A procedure declared with the flag `exclusive` is called *exclusive*.

```
PROCEDURE [exclusive] (b: Buffer) Put* (x: INTEGER), NEW;
PROCEDURE [exclusive] SetLevel* (x: INTEGER);
```

### 5.2. Monitor

Each exclusive procedure has a *monitor* — an object that belongs to at most one task at any moment.

- The monitor of a method is the record denoted by the receiver. Different records of one type are
  different monitors.
- The monitor of a module-level procedure is a hidden variable of the module. All exclusive
  procedures of one module share one monitor.

Any record can be a monitor; its type does not declare this.

### 5.3. Entry and Exit

A call to an exclusive procedure executes as follows:

1. The task *enters* the monitor. If the monitor is free, the task becomes its owner. If another task
   owns the monitor, the caller is suspended until the monitor is handed to it (section 6.3).
2. The procedure body executes.
3. The task *exits* the monitor. Exit happens on any completion of the body: reaching the end, a
   `RETURN` statement, abnormal termination (section 8). A function value is computed before the
   exit.

Hence the basic property: bodies of exclusive procedures of one monitor do not interleave. A
statement about the monitor's data that holds on exit from each such procedure and is not violated
outside them holds on entry to each of them. Such a statement is called the *monitor invariant*.

### 5.4. Re-entry

A task that owns a monitor may enter it again: call another exclusive procedure of the same monitor
or the same one recursively. Such an entry does not suspend it. The monitor is released when the
number of exits equals the number of entries.

### 5.5. Nested Monitors

A task may own several monitors. It exits them in the reverse order of entry; the nesting of calls
ensures this order.

Two tasks, each owning one monitor and waiting to enter the other, never continue. The language does
not prevent this. A program avoids deadlock by entering monitors in one and the same order. The
compiler does not check this.

### 5.6. Restrictions

1. The flag `exclusive` is allowed only in a module that imports `ACTIVE`.
2. A procedure bound to a type (a method) and a module-level procedure can be exclusive. A procedure
   declared inside another procedure cannot.
3. The flag `exclusive` does not combine with other system flags.
4. An abstract method may carry the flag, but it has no code. A procedure with an empty body neither
   enters nor exits the monitor: a call to it does not wait for the monitor.
5. The flag belongs to the procedure, not to the data. The monitor does not protect a record field
   changed outside the record's exclusive procedures. Exporting a field for writing from another
   module voids the guarantee of section 5.3. The compiler does not check this.
6. The flag is not part of the interface: the caller does not care whether a procedure is exclusive.
   A method overriding an exclusive method does not become exclusive; it needs its own flag.

Breaking rules 1–3 is a compilation error (appendix B).


## 6. Awaiting a Condition

### 6.1. Syntax

```
ACTIVE.AWAIT(c)
```

`c` is an expression of type `BOOLEAN`, called the *condition*.

### 6.2. Meaning

If `c` is true, the statement does nothing. If `c` is false, the task exits the monitor of its
procedure and is suspended. It continues after the statement when two things hold together: `c` is
true, and the task owns the monitor again.

Consequently the condition is true after the await statement. A re-check in a loop, as with Hoare
condition variables, is not needed.

```
PROCEDURE [exclusive] (b: Buffer) Get* (OUT x: INTEGER), NEW;
BEGIN
	ACTIVE.AWAIT(b.n > 0);
	x := b.a[b.out]; b.out := (b.out + 1) MOD LEN(b.a); DEC(b.n)
END Get;
```

Awaiting is a switch point (section 4). While the task waits, others own the monitor. Therefore the
monitor invariant holds before each await statement, as before exit from the procedure. The compiler
does not check this.

The condition is evaluated not only by the waiting task but also by the task that exits the monitor
(section 6.3). The waiting task's local variables and parameters are accessible to the condition and
keep the values they had when the wait began.

`ACTIVE.AWAIT(TRUE)` is an empty statement. `ACTIVE.AWAIT(FALSE)` suspends the task until it is
cancelled (section 8).

### 6.3. Handing Over the Monitor

When the owner exits the monitor or starts waiting, the monitor is handed over as follows:

1. The conditions of the waiting tasks are evaluated in the order in which the tasks started
   waiting. The first task whose condition is true receives the monitor; it becomes ready to run.
2. If there is no such task, the task suspended at entry earliest receives the monitor.
3. If there is none either, the monitor becomes free.

A waiting task with a true condition takes precedence over entering tasks. Between the moment the
condition is found true and the continuation of the waiting task no other task owns the monitor.
Therefore the condition cannot become false.

Awaiting releases only the monitor of the procedure that contains the statement. The task keeps its
other monitors for the whole wait. If the task entered the monitor several times (section 5.4),
awaiting releases it completely, and on continuation the entry count is restored.

### 6.4. Fairness

The language guarantees the order of section 6.3 and nothing beyond it. A task whose condition is true
only temporarily, while another task owns the monitor, may wait forever.

### 6.5. Restrictions

1. The statement is allowed only directly in the body of an exclusive procedure. It is forbidden in a
   procedure nested in it and in a procedure it calls.
2. There is exactly one argument. It is not the name of a type or of a procedure, and its type is
   `BOOLEAN`.
3. The condition contains no procedure calls and no assignments. It only reads.
4. The condition depends only on data protected by the monitor and on the local variables and
   parameters of its procedure. The compiler does not check this. A condition over unrelated data may
   become true unnoticed: the waiting task continues only at the next exit of some task from the
   monitor.

Breaking rules 1–3 is a compilation error (appendix B).


## 7. Yield Points

A task executing a long loop without switch points does not let the others run. A module compiled
with the yield-point key (section 9.2) gets a *yield point* at the end of each iteration of the
statements `WHILE`, `REPEAT`, `LOOP` and `FOR`. At a yield point the task may yield the processor.

Rules:

1. A task that owns at least one monitor does not switch at a yield point. Inside an exclusive
   procedure only the await statement gives up the monitor.
2. Code executing outside a task does not switch at a yield point.
3. The key is a property of the module, not of its interface. The caller does not know whether the
   called procedure has yield points.

In a module compiled with the key the indivisibility of section 4 does not hold: between two
iterations of a loop another task may change shared data. Such a module protects shared data with a
monitor.

Modules of the run-time environment are compiled without the key (section 9.2). Therefore a call
from a task to an environment procedure is indivisible.


## 8. Abnormal Termination

Abnormal termination (a trap) in a task stops the execution of its procedures up to the nearest place
where it is caught. If there is no such place, the task finishes. The other tasks continue.

On abnormal termination of an exclusive procedure the task exits its monitor, as on normal
completion. The monitor invariant may be violated; the program restores it. The compiler does not
check this.

Abnormal termination is caused by:

- an await statement with a false condition outside a task;
- an error while evaluating a condition. If the exiting task evaluated the condition, not it but the
  waiting task that owns the condition terminates abnormally;
- cancellation of a task. A cancelled task terminates abnormally at the nearest switch point,
  including an await statement.

Deadlock does not cause abnormal termination. A task whose condition nobody makes true waits. When all
tasks wait, the environment reports this and keeps working.

Monitor trap numbers (module `Monitors`; before the trap the environment writes the line
"Kernel: <text>" to the console):

| Trap | Where | Cause |
|---:|---|---|
| 20 | entry | the object is `NIL` |
| 21 | entry | a task owns the monitor, and the call executes outside a task |
| 22 | exit, await | the caller does not own the monitor |
| 23 | exit | monitors are released not in the reverse order of entry |
| 24 | await | awaiting outside a task |
| 25 | await | the condition is `NIL` |
| 60 | await | the condition trapped while another task evaluated it; the original number is in the console |
| 100 | abnormal termination | the lock chain is shorter than its depth (an environment invariant is broken) |
| 127 | switch point | the task is cancelled (module `Tasks`) |

A program built only from the constructs of this report does not cause traps 22, 23 and 25: direct
calls to `Kernel.Lock`, `Kernel.Unlock` and `Kernel.Await` cause them.


## 9. Implementation Limits

### 9.1. Platform

1. The constructs are implemented for x86 and AMD64 processors. Importing `ACTIVE` for another target
   gives error 151 (`incorrect symbol file`); there is no separate message.
2. One operating-system thread executes all tasks. Two statements of a program never execute
   simultaneously.
3. A module whose code is in a frame of some stack is not unloaded. A call to an exclusive procedure
   leaves its frame on the stack. Therefore a module whose monitor is owned or awaited is not unloaded
   either; there is no separate check of the monitor table on unloading.
4. An empty body and an abstract method do not take the monitor (section 5.6, rule 4). This is
   observable only by the absence of waiting when the monitor is occupied.

### 9.2. Compiler Key

The yield-point key is the character `^` after the module name in the compile command:

```
DevCompiler.CompileThis ObxLoop^
```

The compiler's option string accepts the same character. The key is not part of the module text or of
its symbol file.

The compiler rejects the key for a module of the run-time environment (error 225). The compiler
decides by the name whether a module belongs to the environment. A module belongs to it if its
subsystem — the part of the name before the first inner capital letter — is one of `System`, `Std`,
`Text`, `Form`, `Host`, `Dev`, `Lin`, `Win`, or if it is in the explicit list of modules of the
subsystem `System`. A user module with such a prefix does not get the key; an environment module with
another name gets it.


## Appendix A. Syntax Summary

```
ProcDecl        =  PROCEDURE [SysFlag] [Receiver] IdentDef [FormalPars] MethAttributes
                   [";" DeclSeq [BEGIN StatementSeq] END ident].
SysFlag         =  "[" (ident | number) "]".
AwaitStatement  =  Qualident "(" Expression ")".
```

Context restrictions:

| Construct | Condition of validity |
|---|---|
| `[exclusive]`, `[32]` | the module imports `ACTIVE`; the procedure is declared at module level or bound to a type; there are no other system flags |
| `ACTIVE.AWAIT(c)` | stands directly in the body of an exclusive procedure; exactly one argument; `c` of type `BOOLEAN`, without calls and assignments |
| key `^` | the module name does not belong to the run-time environment (section 9.2) |


## Appendix B. Differences from Component Pascal

This appendix lists everything in which Active Component Pascal differs from Component Pascal.

**New language entities.**

| Entity | Kind | Section |
|---|---|---|
| `ACTIVE` | pseudo-module, imported like `SYSTEM` | 3 |
| `ACTIVE.AWAIT(c)` | statement that awaits a condition | 6 |
| `[exclusive]` (`[32]`) | system flag of a procedure | 5 |
| monitor | the receiver record or a hidden module variable | 5.2 |

**Compiler key.** The character `^` after the module name turns on yield points (sections 7 and 9.2).
Without the key the module's object code is the same as for Component Pascal.

**Compilation errors.**

| Number | Condition |
|---:|---|
| 120 | the condition of `ACTIVE.AWAIT` is not of type `BOOLEAN` |
| 126 | the argument of `ACTIVE.AWAIT` is the name of a type or of a procedure |
| 151 | import of `ACTIVE` for a target other than x86 and AMD64 |
| 225 | `[exclusive]` in a module that does not import `ACTIVE` |
| 225 | `[exclusive]` together with another system flag |
| 225 | `[exclusive]` on a procedure nested in another procedure |
| 225 | `ACTIVE.AWAIT` outside the body of an exclusive procedure |
| 225 | a procedure call or an assignment in the condition of `ACTIVE.AWAIT` |
| 225 | the key `^` for a module of the run-time environment |

The numbers 120, 126, 151 and 225 are general Component Pascal numbers (`Dev/Rsrc/Errors.odc`): 120 —
"type of expression following IF, WHILE, UNTIL or ASSERT is not BOOLEAN", 126 — "this expression
cannot be a type or a procedure", 151 — "incorrect symbol file", 225 — "illegal sys flag". There are
no separate messages for the extension. The set `Dev_dev/Reject` (`tools/check-reject.sh`) checks each
case.

**Hidden entities.** The compiler generates entities that are absent from the module text and from
its symbol file:

- a hidden module variable — the monitor of module-level procedures (section 5.2);
- a hidden condition procedure for each `ACTIVE.AWAIT`, nested in the exclusive procedure;
- a hidden local variable holding the frame address of the waiting procedure;
- the hidden import area `"$$"` through which kernel calls go (appendix E).

**Convention rules.** The compiler does not check: the order of entering monitors (section 5.5), the
protection of monitor fields (section 5.6, rule 5), the invariant before awaiting (section 6.2), the
dependence of the condition on monitor data only (section 6.5, rule 4), the restoration of the
invariant after a trap (section 8).


## Appendix C. Differences from Active Oberon

| Active Oberon | Active Component Pascal |
|---|---|
| block `BEGIN {EXCLUSIVE} … END` | procedure with the flag `[exclusive]`; there is no exclusive block inside a procedure |
| `AWAIT(c)` | `ACTIVE.AWAIT(c)`; the condition has no procedure calls and no assignments |
| active object: body `BEGIN {ACTIVE}` | none; the module `Tasks` creates a task |
| re-entering one's own monitor is an error | allowed |
| an error in an exclusive block leaves the monitor occupied | the monitor is released |
| shared access `{SHARED}` | none |
| several processors | one thread |


## Appendix D. Example

The bounded buffer is the classic problem on which a monitor is tested. The producer puts numbers, the
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

The invariant is written in the comment on the fields. It holds after `NEW`, each procedure preserves
it, and the fields are not exported. By section 5.3 it holds at every entry. The condition of each await
statement depends only on the monitor's fields, as section 6.5 requires.


## Appendix E. Implementation Scheme

This appendix does not define the language. It describes the translation of the constructs, so that
the reader can judge their cost.

An exclusive procedure with a non-empty body gets a call `Kernel.Lock(m)` at the start and
`Kernel.Unlock(m)` before return, where `m` is the receiver or the hidden module variable. All
`RETURN` statements lead to one exit point. An abstract method and a procedure with an empty body have
no such calls. The first entry for a given object creates a monitor record. Uncontended entry and exit
take records from free lists and allocate no memory; awaiting and entering an occupied monitor create
queue records.

The await statement translates as:

```
@frm := SYSTEM.GETREG(BP);
IF ~c THEN Kernel.Await(P, @frm, m) END
```

`P` is a hidden procedure nested in the exclusive one, with the body `RETURN c`. It receives the frame
address of the waiting procedure; through it the condition reads that procedure's local variables.
The frame exists on the waiting task's stack for the whole wait. The kernel evaluates the condition
again in `Kernel.Await` through `Kernel.CallCond`, passing the frame in register `BX`. Both
restrictions of section 6.5 follow: another task evaluates the condition, so it has no calls; the frame
belongs to one procedure, so the statement stands in its body and not in a nested one.

Kernel calls go through the hidden area `"$$"`; the loader binds it to `Kernel`. Therefore a module
that does not use the extension gets no extra import.

A monitor is not a record field. The environment keeps information about occupied monitors in a table
keyed by record address; a monitor record exists while the monitor is owned or awaited. A free monitor
occupies no table record: the record returns to a free list.

A yield point is a call to `Kernel.Poll`. It checks a flag that the scheduler raises every 5 ms and
returns if no switch is needed.
