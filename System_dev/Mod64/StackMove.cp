MODULE System_devStackMove;
(** Проба переноса стека задачи (Stackmove, docs/stack-copy-study.md). Задачи из
	System_devStackMoveCode стартуют со стеком 4 КБ и уходят в рекурсию на десятки КБ:
	каждая переживает несколько переносов. Probe печатает таблицу «сценарий × способ»,
	Mut<k> — то же в точном способе без правки слотов вида k (мутации). *)

	IMPORT S := SYSTEM, Kernel, Tasks, Stackmove, C := System_devStackMoveCode,
		Sys := System_devStackMoveSys, System_devClock, Console, UnitAssert;

	CONST
		small = 4096;
		nScen = 10;

	VAR
		lastFp: S.ADDRESS;

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN Console.WriteStr(s) END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR; i: INTEGER; neg: BOOLEAN;
	BEGIN
		neg := x < 0; IF neg THEN x := -x END;
		i := 0; REPEAT s[i] := CHR(ORD("0") + x MOD 10); x := x DIV 10; INC(i) UNTIL x = 0;
		IF neg THEN W("-") END;
		WHILE i > 0 DO DEC(i); Console.WriteChar(s[i]) END
	END N;

	PROCEDURE Pump;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 < 10000) DO Tasks.Step END
	END Pump;

	PROCEDURE New (i: INTEGER; OUT name: ARRAY OF CHAR): C.Probe;
		VAR v: C.VarT; n: C.NestT; a: C.ArrT; va: C.ValArrT; r: C.RecvT; e: C.ExprT; g: C.ArgT;
			gc: C.GcT; tr: C.TrapT; n2: C.Nest2T;
	BEGIN
		CASE i OF
		| 0: NEW(v); name := "var-param"; RETURN v
		| 1: NEW(n); name := "nested"; RETURN n
		| 2: NEW(a); name := "open-array"; RETURN a
		| 3: NEW(va); name := "value-array"; RETURN va
		| 4: NEW(r); name := "receiver"; RETURN r
		| 5: NEW(e); name := "mid-expr"; RETURN e
		| 6: NEW(g); name := "call-in-arg"; RETURN g
		| 7: NEW(gc); name := "gc-after"; RETURN gc
		| 8: NEW(tr); name := "trap-after"; RETURN tr
		| 9: NEW(n2); name := "nested-slot"; RETURN n2
		END
	END New;

	PROCEDURE RunOne (i, mode: INTEGER; mask: SET): BOOLEAN;
	(* Пост: TRUE — сценарий i верен в способе mode, перенос был. *)
		VAR p: C.Probe; name: ARRAY 32 OF CHAR; ok: BOOLEAN; pr: ARRAY 256 OF CHAR;
	BEGIN
		Stackmove.Reset; Stackmove.mask := mask; Stackmove.poison := TRUE; Stackmove.SetMode(mode);
		p := New(i, name);
		Tasks.SetStackSize(p, small); Tasks.Start(p);
		Pump;
		Stackmove.Off; Stackmove.mask := {};
		IF i = 8 THEN
			ok := Tasks.Failed(p) & (Kernel.err = 42)
				& (Kernel.sp >= Stackmove.lastLo) & (Kernel.sp < Stackmove.lastHi)
		ELSE ok := ~Tasks.Failed(p) & p.ok
		END;
		W("SCEN "); W(name); W(" mode="); N(mode); W(" ok="); IF ok THEN W("1") ELSE W("0") END;
		W(" got="); N(p.got); W(" want="); N(p.want);
		W(" moves="); N(Stackmove.moves); W(" pins="); N(Stackmove.pins);
		W(" both="); N(Stackmove.both); W(" looseOnly="); N(Stackmove.looseOnly);
		W(" exactOnly="); N(Stackmove.exactOnly); W(" regsLooseOnly="); N(Stackmove.looseOnlyRegs);
		W(" try="); N(Stackmove.mangled); W(" unaligned4="); N(Stackmove.unaligned);
		W(" failed="); IF Tasks.Failed(p) THEN N(Kernel.err) ELSE W("-") END;
		Stackmove.PinReport(pr); W(" pinned: "); W(pr);
		IF Stackmove.dbgFp # 0 THEN
			W(" fp-sp="); N(Stackmove.dbgFp - Stackmove.dbgLo); W(" hi-sp="); N(Stackmove.dbgHi - Stackmove.dbgLo);
			W(" fp-prevLo="); N(Stackmove.dbgFp - Stackmove.prevLo); W(" prevHi-prevLo="); N(Stackmove.prevHi - Stackmove.prevLo);
			W(" postBreak="); N(Stackmove.postBreak); W(" at="); N(Stackmove.postDepth);
			W(" ret="); W(Stackmove.dbgMod$); W("+"); N(Stackmove.dbgRet); Stackmove.dbgFp := 0
		END;
		Console.WriteLn;
		RETURN ok & (Stackmove.moves > 0)
	END RunOne;

	PROCEDURE Detail (i: INTEGER);
		VAR k: INTEGER;
	BEGIN
		FOR k := 0 TO Stackmove.nLast - 1 DO
			W("  looseOnly mod="); W(Stackmove.lastLooseOnly[k].mod$);
			W(" pc="); N(Stackmove.lastLooseOnly[k].pcOff);
			W(" fpMinusWord="); N(Stackmove.lastLooseOnly[k].fpOff);
			W(" valMinusWord="); N(Stackmove.lastLooseOnly[k].val); Console.WriteLn
		END
	END Detail;

	PROCEDURE Probe*;
		VAR i: INTEGER; ok: BOOLEAN;
	BEGIN
		FOR i := 0 TO nScen - 1 DO
			ok := RunOne(i, Stackmove.loose, {}); Detail(i);
			ok := RunOne(i, Stackmove.exact, {})
		END
	END Probe;

	PROCEDURE Mut (k: INTEGER);
		VAR i: INTEGER; ok: BOOLEAN;
	BEGIN
		W("MUT kind="); N(k); Console.WriteLn;
		FOR i := 0 TO nScen - 1 DO ok := RunOne(i, Stackmove.exact, {k}) END
	END Mut;

	PROCEDURE Mut0*; BEGIN Mut(Stackmove.kFp) END Mut0;
	PROCEDURE Mut1*; BEGIN Mut(Stackmove.kPar) END Mut1;
	PROCEDURE Mut2*; BEGIN Mut(Stackmove.kArr) END Mut2;
	PROCEDURE Mut3*; BEGIN Mut(Stackmove.kSl) END Mut3;
	PROCEDURE Mut4*; BEGIN Mut(Stackmove.kRecv) END Mut4;
	PROCEDURE Mut5*; BEGIN Mut(Stackmove.kTry) END Mut5;
	PROCEDURE Mut6*; BEGIN Mut(Stackmove.kRbx) END Mut6;

	(* --- тесты ------------------------------------------------------------- *)

	PROCEDURE Check (i, mode: INTEGER; IN msg: ARRAY OF CHAR);
	BEGIN
		UnitAssert.True(RunOne(i, mode, {}), msg)
	END Check;

	PROCEDURE TestProcVarParam*;
	BEGIN Check(0, Stackmove.exact, "var param exact"); Check(0, Stackmove.loose, "var param loose") END TestProcVarParam;

	PROCEDURE TestProcNested*;
	BEGIN Check(1, Stackmove.exact, "nested exact"); Check(1, Stackmove.loose, "nested loose") END TestProcNested;

	PROCEDURE TestProcOpenArray*;
	BEGIN Check(2, Stackmove.exact, "open array exact"); Check(2, Stackmove.loose, "open array loose") END TestProcOpenArray;

	PROCEDURE TestProcValueArray*;
	BEGIN Check(3, Stackmove.exact, "value array exact"); Check(3, Stackmove.loose, "value array loose") END TestProcValueArray;

	PROCEDURE TestProcReceiver*;
	BEGIN Check(4, Stackmove.exact, "receiver exact"); Check(4, Stackmove.loose, "receiver loose") END TestProcReceiver;

	PROCEDURE TestProcMidExpression*;
	BEGIN Check(5, Stackmove.exact, "mid-expression exact"); Check(5, Stackmove.loose, "mid-expression loose") END TestProcMidExpression;

	PROCEDURE TestProcCallInArgument*;
	(* Адрес VAR-аргумента лежит временным словом на стеке во время вызова Deep: карта на
	   процедуру его не знает. Точный способ здесь обязан закрепить стек или ошибиться;
	   проверяется нестрогий. *)
	BEGIN Check(6, Stackmove.loose, "call in argument loose") END TestProcCallInArgument;

	PROCEDURE TestProcNestedSlot*;
	BEGIN Check(9, Stackmove.exact, "nested slot exact"); Check(9, Stackmove.loose, "nested slot loose") END TestProcNestedSlot;

	PROCEDURE TestProcGcAfterMove*;
	BEGIN Check(7, Stackmove.exact, "gc exact"); Check(7, Stackmove.loose, "gc loose") END TestProcGcAfterMove;

	PROCEDURE TestProcTrapAfterMove*;
		VAR fp, a: S.ADDRESS; n: INTEGER;
	BEGIN
		Check(8, Stackmove.exact, "trap exact");
		(* цепочка кадров трапа — на новом стеке и доходит до его верха *)
		fp := Kernel.fp; n := 0;
		WHILE (fp >= Stackmove.lastLo) & (fp < Stackmove.lastHi) DO INC(n); S.GET(fp, a); fp := a END;
		UnitAssert.True(n > C.depth, "trap frame chain on the new stack")
	END TestProcTrapAfterMove;

	PROCEDURE TestProcSleeperUntouched*;
	(* Спящая задача держит адрес своей локальной; другая в это время переносит стек. *)
		VAR s: C.SleepT; v: C.VarT; t0: LONGINT;
	BEGIN
		C.stop := FALSE;
		NEW(s); Tasks.SetStackSize(s, small); Tasks.Start(s);
		NEW(v); Tasks.SetStackSize(v, small);
		Stackmove.Reset; Stackmove.SetMode(Stackmove.exact);
		Tasks.Start(v);
		t0 := Kernel.Time();
		WHILE (Tasks.State(v) # Tasks.Finished) & (Kernel.Time() - t0 < 5000) DO Tasks.Step END;
		C.stop := TRUE; Pump; Stackmove.Off;
		UnitAssert.True(Stackmove.moves > 0, "mover moved");
		UnitAssert.True(v.ok & ~Tasks.Failed(v), "mover result");
		UnitAssert.True(s.ok & ~Tasks.Failed(s), "sleeper result")
	END TestProcSleeperUntouched;

	PROCEDURE TestProcSystemModulePinned*;
	(* Рекурсия через модуль с SYSTEM: стек закреплён, трап 137, переноса нет. *)
		VAR t: Sys.SysT;
	BEGIN
		Stackmove.Reset; Stackmove.SetMode(Stackmove.exact);
		NEW(t); Tasks.SetStackSize(t, small); Tasks.Start(t);
		Pump; Stackmove.Off;
		UnitAssert.True(Tasks.Failed(t), "pinned task failed");
		UnitAssert.EqualInt(137, Kernel.err, "trap 137");
		UnitAssert.EqualInt(0, Stackmove.moves, "no move");
		UnitAssert.True(Stackmove.pins > 0, "pin counted")
	END TestProcSystemModulePinned;

	(* --- замеры ------------------------------------------------------------ *)

	PROCEDURE Cost*;
	(** Цена переноса от глубины: задача с начальным стеком s уходит на глубину d кадров
		по 100 байт; время задачи минус время той же задачи со стеком, где переноса нет. *)
		VAR d, k, rep, m: INTEGER; t0, tm, tb: LONGINT; v: C.VarT;
	BEGIN
		Stackmove.compare := FALSE;
		FOR m := 1 TO 2 DO
			d := 4096;
			WHILE d <= 256 * 1024 DO
				tm := 0; tb := 0; rep := 200;
				FOR k := 1 TO rep DO
					Stackmove.SetMode(m);
					NEW(v); Tasks.SetStackSize(v, d); Tasks.Start(v);
					Stackmove.Reset; t0 := System_devClock.now(); Pump; tm := tm + System_devClock.now() - t0;
					Stackmove.Off;
					NEW(v); Tasks.SetStackSize(v, 1024 * 1024); Tasks.Start(v);
					t0 := System_devClock.now(); Pump; tb := tb + System_devClock.now() - t0
				END;
				W("BENCH move mode="); N(m); W(" start="); N(d); W(" movesPerTask="); N(Stackmove.moves);
				W(" bytesPerTask="); N(Stackmove.bytesCopied);
				W(" cycTotalPerMove="); N(Stackmove.cycTotal DIV MAX(1, Stackmove.moves));
				W(" cycAllocPerMove="); N(Stackmove.cycAlloc DIV MAX(1, Stackmove.moves));
				W(" cycWalkPerMove="); N(Stackmove.cycWalk DIV MAX(1, Stackmove.moves));
				W(" nsPerTaskExtra="); N((tm - tb) DIV rep); Console.WriteLn;
				d := d * 4
			END
		END;
		Stackmove.compare := TRUE
	END Cost;

END System_devStackMove.
