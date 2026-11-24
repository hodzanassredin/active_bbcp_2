MODULE System_devMonField;
(** Тесты быстрого пути [exclusive]: скрытое поле владельца "@mon".

	Вход в свободный монитор пишет токен задачи в поле объекта и кладёт
	взятие на стек ядра; выход обнуляет поле. Всё остальное — повторный
	вход, соперник, AWAIT, трап, отмена, ручной Kernel.Lock — идёт через
	таблицу Monitors. Тесты проверяют, что оба пути видят одного
	владельца. *)

	IMPORT ACTIVE, Kernel, Monitors, Tasks, UnitAssert, System_devMonBase;

	CONST
		deep = 70;	(* глубже стека взятий ядра (64) *)

	TYPE
		Ext = POINTER TO RECORD (System_devMonBase.Counter) END;
		PlainExt = POINTER TO RECORD (System_devMonBase.Plain) inside, overlaps: INTEGER END;

		(* Расширение объявляет исключающий метод раньше базы: обе пользуются
		   полем базы. *)
		Root = POINTER TO EXTENSIBLE RECORD inside, overlaps, n: INTEGER END;
		Leaf = POINTER TO RECORD (Root) END;

		Empty = POINTER TO RECORD END;	(* запись без полей *)

		Buf = POINTER TO RECORD n, got: INTEGER END;
		Node = POINTER TO RECORD next: Node; depth: INTEGER END;

		Runner = POINTER TO RECORD (Tasks.Task) kind: INTEGER END;
		Bare = POINTER TO RECORD x: INTEGER END;	(* без исключающих методов *)

	VAR
		ext: Ext; pext: PlainExt; leaf: Leaf; empty: Empty; buf: Buf;
		chainHead: Node;
		innerDepth, emptyHits: INTEGER;
		holdFlag, entered, lockedInside: BOOLEAN;
		modCount: INTEGER;

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE NilCrash;
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1
	END NilCrash;

	(* --- методы ----------------------------------------------------------- *)

	PROCEDURE [exclusive] (e: Ext) Own, NEW;
	BEGIN
		INC(e.inside);
		IF e.inside > 1 THEN INC(e.overlaps) END;
		Tasks.Yield;
		DEC(e.inside); INC(e.n)
	END Own;

	PROCEDURE [exclusive] (p: PlainExt) Touch, NEW;
	BEGIN
		INC(p.inside);
		IF p.inside > 1 THEN INC(p.overlaps) END;
		Tasks.Yield;
		DEC(p.inside); INC(p.n)
	END Touch;

	PROCEDURE [exclusive] (l: Leaf) LeafM, NEW;
	BEGIN
		INC(l.inside);
		IF l.inside > 1 THEN INC(l.overlaps) END;
		Tasks.Yield;
		DEC(l.inside); INC(l.n)
	END LeafM;

	PROCEDURE [exclusive] (r: Root) RootM, NEW;
	BEGIN
		INC(r.inside);
		IF r.inside > 1 THEN INC(r.overlaps) END;
		Tasks.Yield;
		DEC(r.inside); INC(r.n)
	END RootM;

	PROCEDURE [exclusive] (e: Empty) Hit, NEW;
	BEGIN
		INC(emptyHits);
		lockedInside := Monitors.IsLocked(e)
	END Hit;

	PROCEDURE [exclusive] (e: Empty) Outer, NEW;
	BEGIN
		e.Hit;	(* повторный вход того же монитора *)
		innerDepth := Monitors.LockDepth()
	END Outer;

	PROCEDURE [exclusive] (b: Buf) Get (): INTEGER, NEW;
	BEGIN
		ACTIVE.AWAIT(b.n > 0);
		DEC(b.n); INC(b.got);
		RETURN b.got
	END Get;

	PROCEDURE [exclusive] (b: Buf) Put, NEW;
	BEGIN
		INC(b.n)
	END Put;

	PROCEDURE [exclusive] (b: Buf) Hold, NEW;
	BEGIN
		WHILE holdFlag DO Tasks.Yield END
	END Hold;

	PROCEDURE [exclusive] (b: Buf) Enter, NEW;
	BEGIN
		entered := TRUE
	END Enter;

	PROCEDURE [exclusive] (b: Buf) Crash, NEW;
	BEGIN
		NilCrash
	END Crash;

	PROCEDURE [exclusive] (n: Node) Nest, NEW;
	BEGIN
		IF n.next # NIL THEN n.next.Nest ELSE innerDepth := Monitors.LockDepth() END
	END Nest;

	PROCEDURE [exclusive] ModProc (k: INTEGER);
	BEGIN
		INC(modCount);
		IF k > 0 THEN ModProc(k - 1) END	(* повторный вход монитора модуля *)
	END ModProc;

	(* --- задачи ----------------------------------------------------------- *)

	PROCEDURE (t: Runner) Run;
		VAR i, x: INTEGER;
	BEGIN
		CASE t.kind OF
		| 0: FOR i := 1 TO 10 DO ext.Base; ext.Own END
		| 1: FOR i := 1 TO 10 DO pext.Touch END
		| 2: FOR i := 1 TO 10 DO leaf.LeafM; leaf.RootM END
		| 3: FOR i := 1 TO 5 DO x := buf.Get() END
		| 4: FOR i := 1 TO 5 DO buf.Put; Tasks.Yield END
		| 5: buf.Hold
		| 6: buf.Enter
		| 7: buf.Crash
		| 8: Kernel.Lock(buf); WHILE holdFlag DO Tasks.Yield END; Kernel.Unlock(buf)
		END
	END Run;

	PROCEDURE Start (kind: INTEGER): Runner;
		VAR t: Runner;
	BEGIN
		NEW(t); t.kind := kind; Tasks.Start(t); RETURN t
	END Start;

	(* --- тесты ------------------------------------------------------------ *)

	PROCEDURE TestProcExtensionOtherModule*;
	(** Исключающие методы базы (другой модуль) и расширения на одном объекте
		исключают друг друга: у них одно поле владельца. *)
		VAR a, b: Runner;
	BEGIN
		NEW(ext);
		a := Start(0); b := Start(0);
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.EqualInt(40, ext.n, "all calls done");
		UnitAssert.EqualInt(0, ext.overlaps, "never two inside")
	END TestProcExtensionOtherModule;

	PROCEDURE TestProcFieldInExtension*;
	(** База без исключающих методов: поле заводит расширение. *)
		VAR a, b: Runner;
	BEGIN
		NEW(pext);
		a := Start(1); b := Start(1);
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.EqualInt(20, pext.n, "all calls done");
		UnitAssert.EqualInt(0, pext.overlaps, "never two inside")
	END TestProcFieldInExtension;

	PROCEDURE TestProcLeafBeforeRoot*;
	(** Метод расширения объявлен раньше метода базы: оба берут поле базы. *)
		VAR a, b: Runner;
	BEGIN
		NEW(leaf);
		a := Start(2); b := Start(2);
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.EqualInt(40, leaf.n, "all calls done");
		UnitAssert.EqualInt(0, leaf.overlaps, "never two inside")
	END TestProcLeafBeforeRoot;

	PROCEDURE TestProcEmptyRecordReentry*;
	(** Запись без полей получает поле; повторный вход идёт медленным путём,
		после выхода монитор свободен и снова берётся быстро. *)
	BEGIN
		NEW(empty); emptyHits := 0;
		empty.Outer;
		UnitAssert.EqualInt(1, emptyHits, "inner ran");
		UnitAssert.True(lockedInside, "locked inside");
		UnitAssert.EqualInt(1, innerDepth, "depth after inner exit");
		UnitAssert.True(~Monitors.IsLocked(empty), "free after exit");
		empty.Hit;
		UnitAssert.EqualInt(2, emptyHits, "fast entry again");
		UnitAssert.EqualInt(0, Monitors.LockDepth(), "nothing held")
	END TestProcEmptyRecordReentry;

	PROCEDURE TestProcAwaitAfterFastEntry*;
	(** AWAIT в методе, вошедшем быстрым путём: ожидание, передача монитора
		и выход работают. *)
		VAR c, p: Runner;
	BEGIN
		NEW(buf);
		c := Start(3); p := Start(4);
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.EqualInt(5, buf.got, "all received");
		UnitAssert.EqualInt(0, buf.n, "buffer empty");
		UnitAssert.True(~Monitors.IsLocked(buf), "free")
	END TestProcAwaitAfterFastEntry;

	PROCEDURE TestProcWaiterAtExit*;
	(** Держатель уступает внутри метода, соперник ждёт входа; выход
		держателя передаёт монитор сопернику. *)
		VAR h, e: Runner; i: INTEGER;
	BEGIN
		NEW(buf); holdFlag := TRUE; entered := FALSE;
		h := Start(5); e := Start(6);
		FOR i := 1 TO 3 DO Tasks.Step END;
		UnitAssert.True(~entered, "enterer waits");
		UnitAssert.True(Monitors.IsLocked(buf), "held");
		UnitAssert.EqualInt(1, Monitors.WaitersCount(), "one enterer queued");
		holdFlag := FALSE;
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.True(entered, "enterer got the monitor");
		UnitAssert.True(~Monitors.IsLocked(buf), "free")
	END TestProcWaiterAtExit;

	PROCEDURE TestProcCancelWaiter*;
	(** Отмена задачи, ждущей входа: она уходит из очереди, монитор остаётся
		у держателя и потом освобождается. *)
		VAR h, e: Runner; i: INTEGER;
	BEGIN
		NEW(buf); holdFlag := TRUE; entered := FALSE;
		h := Start(5); e := Start(6);
		FOR i := 1 TO 3 DO Tasks.Step END;
		Tasks.Cancel(e);
		FOR i := 1 TO 3 DO Tasks.Step END;
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(e), "cancelled");
		UnitAssert.EqualInt(0, Monitors.WaitersCount(), "queue empty");
		UnitAssert.True(Monitors.IsLocked(buf), "still held");
		holdFlag := FALSE;
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.True(~entered, "cancelled never entered");
		UnitAssert.True(~Monitors.IsLocked(buf), "free")
	END TestProcCancelWaiter;

	PROCEDURE TestProcTrapInside*;
	(** Трап внутри метода, вошедшего быстрым путём: монитор освобождается,
		следующий вход проходит. *)
		VAR c, e: Runner;
	BEGIN
		NEW(buf); entered := FALSE;
		c := Start(7);
		UnitAssert.True(Pump(2000), "crashed task finished");
		UnitAssert.True(Tasks.Failed(c), "trap");
		UnitAssert.True(~Monitors.IsLocked(buf), "free after trap");
		e := Start(6);
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.True(entered, "entered after trap")
	END TestProcTrapInside;

	PROCEDURE TestProcManualLockExcludes*;
	(** Kernel.Lock на объекте с полем владельца: исключающий метод ждёт
		ручного Unlock. *)
		VAR m, e: Runner; i: INTEGER;
	BEGIN
		NEW(buf); holdFlag := TRUE; entered := FALSE;
		m := Start(8); e := Start(6);
		FOR i := 1 TO 3 DO Tasks.Step END;
		UnitAssert.True(~entered, "method waits for manual lock");
		holdFlag := FALSE;
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.True(entered, "entered after unlock");
		UnitAssert.True(~Monitors.IsLocked(buf), "free")
	END TestProcManualLockExcludes;

	PROCEDURE TestProcDeeperThanStack*;
	(** Вложенные взятия глубже стека взятий ядра: остальные идут медленным
		путём, LockDepth считает все, после выхода всё свободно. *)
		VAR i: INTEGER; n: Node;
	BEGIN
		chainHead := NIL;
		FOR i := 1 TO deep DO NEW(n); n.next := chainHead; chainHead := n END;
		innerDepth := 0;
		chainHead.Nest;
		UnitAssert.EqualInt(deep, innerDepth, "depth at the bottom");
		UnitAssert.EqualInt(0, Monitors.LockDepth(), "nothing held after");
		n := chainHead;
		WHILE n # NIL DO
			UnitAssert.True(~Monitors.IsLocked(n), "free"); n := n.next
		END
	END TestProcDeeperThanStack;

	PROCEDURE TestProcModuleMonitorReentry*;
	(** Исключающая процедура модуля: быстрый путь по скрытой переменной,
		повторный вход — медленным путём. *)
	BEGIN
		modCount := 0;
		ModProc(3);
		UnitAssert.EqualInt(4, modCount, "all levels ran");
		UnitAssert.EqualInt(0, Monitors.LockDepth(), "nothing held")
	END TestProcModuleMonitorReentry;

	PROCEDURE TestIsLockedShortcut*;
	(** IsLocked без поиска в таблице, когда ничего не взято: ответ тот же,
		что с поиском, в каждом углу. *)
		VAR a, b: Bare;
	BEGIN
		NEW(a); NEW(b);
		UnitAssert.True(~Monitors.IsLocked(a), "nothing held");
		Kernel.Lock(a);
		UnitAssert.True(Monitors.IsLocked(a), "held through the table");
		UnitAssert.True(~Monitors.IsLocked(b), "other object free");
		Kernel.Unlock(a);
		UnitAssert.True(~Monitors.IsLocked(a), "free again (hot record left)");
		Kernel.Lock(a); Kernel.Lock(a);
		UnitAssert.True(Monitors.IsLocked(a), "re-entered hot record");
		Kernel.Unlock(a); Kernel.Unlock(a);
		NEW(empty);
		empty.Hit;
		UnitAssert.True(lockedInside, "held on the fast-path stack");
		UnitAssert.True(~Monitors.IsLocked(empty), "free after exit")
	END TestIsLockedShortcut;

END System_devMonField.
