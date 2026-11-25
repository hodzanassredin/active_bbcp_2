# Обзор на примерах: ObxObjCompose

Композиция мониторов: объект зовёт другой из своей `Exclusive`, и оба ждут.

Пассажир входит в `Path` и под её монитором зовёт `Gate.Do`. Ворота закрыты, поэтому ворота
ждут; монитор `Path` остаётся взят. Затем пассажир ждёт уже саму `Path` — из процедуры,
вложенной на три обычных вызова; это отпускает `Path`. Второй пассажир, ждавший входа в `Path`,
получает её по очереди. Модуль не использует расширений языка. Разбор —
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

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/ObjCompose.cp`; только amd64). В окне
BlackBox команду выполняет коммандер:

```
   ObxObjCompose.Do
```

В консоли:

```sh
echo 'ObxObjCompose.Do' | tools/run64.sh
```

В журнале появится:

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

## Что произошло

1. Два пассажира зовут `Path.Do(PassOp)`; первый входит в `Path`, второй ждёт в очереди входа.
2. Под монитором `Path` процедура `WaitGate` зовёт `Gate.Do(EnterOp)`. Ворота закрыты, поэтому
   ворота ждут; `Gate` отпущены, `Path` остаётся взятой. Открывальщик спит 10 мс и открывает
   ворота.
3. Выход из `Gate.Do` будит первого пассажира; он снова входит в ворота, перепроверяет условие и
   возвращается в `Path`. `Path` снова у него.
4. Затем `Level1` зовёт `Level2`, `Level2` — `Level3`, а `Level3` зовёт `Path.Wait`. Это отпускает
   `Path`; второй пассажир входит, тоже ждёт ворота (они уже открыты), а затем `Path.ready`.
5. Открывальщик спит ещё раз и зовёт `Path.Do(GoOp)`, который ставит `ready` и выходит. Выход
   будит обоих ждущих; один за другим они берут `Path`, проходят и уходят. Последняя строка
   сообщает, что монитор свободен.

В этом примере мы увидели, что взятый монитор одного объекта не мешает войти в другой объект или
ждать в нём, а ожидание на глубине обычных вызовов отпускает ровно монитор названного объекта.
