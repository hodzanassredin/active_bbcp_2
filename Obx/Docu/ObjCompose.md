# Overview by Example: ObxObjCompose

Composition of monitors: one object calls another from inside `Exclusive`, and both wait.

A passenger enters `Path` and, under its monitor, calls `Gate.Do`. The gate is closed, so the gate
waits; the monitor of `Path` stays held. Later the passenger waits for `Path` itself, from a
procedure that is three ordinary calls deep; that releases `Path`. The second passenger, which
waits to enter `Path`, gets it in turn. The module uses no language extension. The study is
`docs/object-form-study.md`.

```
MODULE ObxObjCompose;
(** Композиция мониторов и ожидание на глубине вызовов.

	Path внутри своей Exclusive зовёт Gate.Do: монитор Path остаётся
	взят, пока Gate ждёт своего условия. Затем Path и сам ждёт — из
	обычной процедуры, вызванной через три уровня вложенных процедур.
	Второй вызов Path.Do ждёт, пока первый не выйдет. *)

	IMPORT AsyncObject, Tasks, Console;

	TYPE
		Gate = POINTER TO RECORD (AsyncObject.Object)
			open: BOOLEAN
		END;
		OpenOp = RECORD (AsyncObject.Op) END;
		EnterOp = RECORD (AsyncObject.Op) END;

		Path = POINTER TO RECORD (AsyncObject.Object)
			gate: Gate;
			ready, passed: BOOLEAN
		END;
		PassOp = RECORD (AsyncObject.Op) END;
		GoOp = RECORD (AsyncObject.Op) END;

		Passenger = POINTER TO RECORD (Tasks.Task) p: Path; id: INTEGER END;
		Opener = POINTER TO RECORD (Tasks.Task) p: Path END;

	VAR
		done: INTEGER;

	PROCEDURE Say (IN what: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(what); Console.WriteLn
	END Say;

	PROCEDURE (g: Gate) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: OpenOp DO
			g.open := TRUE; Say("gate open")
		| op: EnterOp DO
			WHILE ~g.open DO g.Wait END; Say("gate entered")
		ELSE
			HALT(20)
		END
	END Exclusive;

	(* Ожидание Path на глубине трёх обычных процедур. *)
	PROCEDURE Level3 (p: Path);
	BEGIN
		WHILE ~p.ready DO p.Wait END
	END Level3;

	PROCEDURE Level2 (p: Path);
	BEGIN
		Level3(p)
	END Level2;

	PROCEDURE Level1 (p: Path);
	BEGIN
		Level2(p)
	END Level1;

	PROCEDURE WaitGate (g: Gate);
		VAR op: EnterOp;
	BEGIN
		g.Do(op)
	END WaitGate;

	PROCEDURE (p: Path) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: PassOp DO
			WaitGate(p.gate);	(* Path взят, Gate ждёт *)
			Say("path holds its monitor");
			Level1(p);	(* Path отпускается и снова берётся *)
			p.passed := TRUE; Say("path passed")
		| op: GoOp DO
			p.ready := TRUE; Say("path ready")
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (t: Passenger) Run;
		VAR op: PassOp;
	BEGIN
		t.p.Do(op);
		IF t.p.passed THEN Say("passenger returned") END;
		INC(done)
	END Run;

	PROCEDURE (t: Opener) Run;
		VAR open: OpenOp; go: GoOp;
	BEGIN
		Tasks.Sleep(10); t.p.gate.Do(open);
		Tasks.Sleep(10); t.p.Do(go)
	END Run;

	PROCEDURE ObjectDone (p: Path): BOOLEAN;
	BEGIN
		RETURN ~AsyncObject.Held(p)
	END ObjectDone;

	PROCEDURE Do*;
		VAR p: Path; g: Gate; a, b: Passenger; o: Opener;
	BEGIN
		NEW(g); NEW(p); p.gate := g;
		NEW(a); a.p := p; a.id := 1; NEW(b); b.p := p; b.id := 2;
		NEW(o); o.p := p;
		done := 0;
		Tasks.Start(a); Tasks.Start(b); Tasks.Start(o);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF done = 2 THEN Say("two passengers passed") ELSE Say("incomplete") END;
		IF ObjectDone(p) THEN Say("path monitor free") END
	END Do;

END ObxObjCompose.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/ObjCompose.cp`; it is for
amd64 only). In the BlackBox window a commander executes the command:

```
   ObxObjCompose.Do
```

In the console:

```sh
echo 'ObxObjCompose.Do' | tools/run64.sh
```

The log shows:

```
gate open
gate entered
path holds its monitor
gate entered
path holds its monitor
path ready
path passed
passenger returned
path passed
passenger returned
two passengers passed
path monitor free
```

## What happened

1. Two passengers call `Path.Do(PassOp)`; the first enters `Path`, the second waits in the entry
   queue of `Path`.
2. Under the monitor of `Path`, `WaitGate` calls `Gate.Do(EnterOp)`. The gate is closed, so the
   gate waits; `Gate` is released, `Path` remains held. The opener sleeps for 10 ms and opens the
   gate.
3. The exit from `Gate.Do` wakes the first passenger; it reenters the gate, rechecks the
   condition, and returns to `Path`. It holds `Path` again.
4. Then `Level1` calls `Level2`, `Level2` calls `Level3`, and `Level3` calls `Path.Wait`. This
   releases `Path`; the second passenger enters and also waits for the gate, which is already open,
   and then for `Path.ready`.
5. The opener sleeps again and calls `Path.Do(GoOp)`, which sets `ready` and exits. The exit wakes
   both waiters; one after the other takes `Path`, passes, and leaves. The last line reports that
   the monitor is free.

In this example we saw that a monitor held on one object does not prevent another object from
being entered or waited on, and that a wait deep inside ordinary calls releases exactly the
monitor of the object it names.
