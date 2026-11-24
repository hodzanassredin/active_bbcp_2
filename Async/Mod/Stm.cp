MODULE AsyncStm;
(** Транзакционная память для задач одной нити.

	Транзакция — объект Tx с методом Do. Atomically исполняет Do так, что
	другие задачи видят либо все его записи, либо ни одной: при трапе
	записи откатываются, трап уходит вызывающему с тем же номером. Retry
	откатывает транзакцию и ждёт, пока изменится одна из прочитанных ею
	переменных; затем Do исполняется заново. OrElse(a, b) исполняет b,
	если a сделала Retry. Вложенный Atomically — часть внешней транзакции.

	Столкновений нет: задачу никто не прерывает, а внутри транзакции
	точки переключения запрещены (Tasks.BeginAtomic, трап 25). Поэтому
	запись идёт прямо в переменную, а журнал отката хранит прежнее
	значение; чтение стоит одного сравнения.

	Переменная — расширение Var. Её Get зовёт Read, Set — Write до
	изменения значения; Save и Load копируют значение в Saved и обратно.

	Инвариант модуля: ~active => nLog = 0 & nReads = 0 & ~retrying.
	Инвариант журнала: active => для каждой v с v.rser = rser v есть в
	reads[0..nReads); для каждой v, изменённой после отметки mark уровня с
	номером s, в log[mark..nLog) есть запись её значения на момент mark,
	и v.wser = s ⇒ такая запись есть. *)

	IMPORT S := SYSTEM, Kernel, Tasks, Console;

	CONST
		retryTrap = 126;	(* тихий трап Retry; до вызывающего не доходит *)
		maxHalt = 128;	(* старший номер, который передаётся дальше как есть *)
		logMin = 16;

	TYPE
		(** Копия значения переменной для журнала отката. *)
		Saved* = RECORD
			i*: LONGINT;
			x*: REAL;
			p*: ANYPTR
		END;

		(** Транзакционная переменная. *)
		Var* = POINTER TO ABSTRACT RECORD
			waiters: Waiter;	(* задачи, ждущие её изменения в Retry *)
			rser, wser: LONGINT	(* номер транзакции и уровня, отметивших чтение и запись *)
		END;

		IntVar* = POINTER TO RECORD (Var) x: INTEGER END;
		BoolVar* = POINTER TO RECORD (Var) x: BOOLEAN END;
		RealVar* = POINTER TO RECORD (Var) x: REAL END;
		RefVar* = POINTER TO RECORD (Var) x: ANYPTR END;

		(** Тело транзакции. *)
		Tx* = POINTER TO ABSTRACT RECORD END;

		(* Ожидание одной задачи в Retry. Инвариант: ~woken => запись Waiter
		   с w = этой есть у каждой vars[i], i < LEN(vars). *)
		Wait = POINTER TO RECORD (Kernel.TrapCleaner)
			task: Tasks.Task;
			woken: BOOLEAN;
			vars: POINTER TO ARRAY OF Var
		END;

		Waiter = POINTER TO RECORD next: Waiter; w: Wait END;

		Entry = RECORD v: Var; s: Saved END;

		Pair = POINTER TO RECORD (Tx) a, b: Tx END;

	VAR
		active, retrying: BOOLEAN;
		serial, rser, wser: LONGINT;
		reads: POINTER TO ARRAY OF Var; nReads: INTEGER;
		log: POINTER TO ARRAY OF Entry; nLog: INTEGER;

	PROCEDURE (v: Var) Save- (OUT s: Saved), NEW, ABSTRACT;
	PROCEDURE (v: Var) Load- (IN s: Saved), NEW, ABSTRACT;
	PROCEDURE (t: Tx) Do-, NEW, ABSTRACT;

	(* --- пробуждение ------------------------------------------------------- *)

	PROCEDURE Wake (v: Var);
		VAR r: Waiter;
	BEGIN
		r := v.waiters; v.waiters := NIL;
		WHILE r # NIL DO	(* инвариант: задачи списка до r разбужены *)
			IF ~r.w.woken THEN r.w.woken := TRUE; Tasks.Unpark(r.w.task) END;
			r := r.next
		END
	END Wake;

	PROCEDURE Unregister (w: Wait);
	(* Пост: ни у одной w.vars[i] нет записи Waiter с w. *)
		VAR i: INTEGER; p, r: Waiter;
	BEGIN
		FOR i := 0 TO LEN(w.vars^) - 1 DO
			p := NIL; r := w.vars[i].waiters;
			WHILE r # NIL DO	(* инвариант: до r записей с w нет *)
				IF r.w = w THEN
					IF p = NIL THEN w.vars[i].waiters := r.next ELSE p.next := r.next END
				ELSE p := r
				END;
				r := r.next
			END
		END
	END Unregister;

	PROCEDURE (w: Wait) Cleanup;
	BEGIN
		Unregister(w)
	END Cleanup;

	(* --- журнал ----------------------------------------------------------- *)

	PROCEDURE Read* (v: Var);
	(** Отметить чтение v; зовёт Get расширения Var.
		Пред: v # NIL — 20.
		Пост: в транзакции v в множестве прочитанных. *)
		VAR new: POINTER TO ARRAY OF Var; i: INTEGER;
	BEGIN
		ASSERT(v # NIL, 20);
		IF active & (v.rser # rser) THEN
			v.rser := rser;
			IF nReads = LEN(reads^) THEN
				NEW(new, 2 * nReads); FOR i := 0 TO nReads - 1 DO new[i] := reads[i] END; reads := new
			END;
			reads[nReads] := v; INC(nReads)
		END
	END Read;

	PROCEDURE Write* (v: Var);
	(** Отметить запись v; зовёт Set расширения Var до изменения значения.
		Пред: v # NIL — 20.
		Пост: в транзакции прежнее значение v в журнале отката; вне
		транзакции задачи, ждущие v в Retry, разбужены. *)
		VAR new: POINTER TO ARRAY OF Entry; i: INTEGER;
	BEGIN
		ASSERT(v # NIL, 20);
		IF ~active THEN Wake(v)
		ELSIF v.wser # wser THEN
			v.wser := wser;
			IF nLog = LEN(log^) THEN
				NEW(new, 2 * nLog); FOR i := 0 TO nLog - 1 DO new[i] := log[i] END; log := new
			END;
			log[nLog].v := v; v.Save(log[nLog].s); INC(nLog)
		END
	END Write;

	PROCEDURE Undo (mark: INTEGER);
	(* Пост: переменные из log[mark..nLog) получили значения на момент mark, nLog = mark. *)
	BEGIN
		WHILE nLog > mark DO	(* инвариант: записи после nLog применены *)
			DEC(nLog);
			log[nLog].v.Load(log[nLog].s); log[nLog].v.wser := 0;
			log[nLog].v := NIL; log[nLog].s.p := NIL
		END
	END Undo;

	PROCEDURE Clear;
	(* Пост: журнал и множество прочитанных пусты, ссылок на переменные нет. *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nLog - 1 DO log[i].v := NIL; log[i].s.p := NIL END;
		FOR i := 0 TO nReads - 1 DO reads[i] := NIL END;
		nLog := 0; nReads := 0
	END Clear;

	(* --- исполнение -------------------------------------------------------- *)

	PROCEDURE Body (a, b, c: S.ADDRESS);
		VAR t: Tx;
	BEGIN
		t := S.VAL(Tx, b); t.Do; S.PUT(a, TRUE)
	END Body;

	PROCEDURE Run (t: Tx): BOOLEAN;
	(* Пост: TRUE — t.Do завершился; FALSE — трап (Kernel.err) или Retry (retrying). *)
		VAR ok: BOOLEAN;
	BEGIN
		ok := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(Body)), S.ADR(ok), S.VAL(S.ADDRESS, t), 0);
		RETURN ok
	END Run;

	PROCEDURE Pass (code: INTEGER);
	(* Передать трап дальше с тем же номером: HALT принимает только константу 0..128. *)
	BEGIN
		IF (code < 0) OR (code > maxHalt) THEN
			Console.WriteStr("AsyncStm: trap passed on as 128"); Console.WriteLn; HALT(128)
		END;
		CASE code OF
		| 0: HALT(0) | 1: HALT(1) | 2: HALT(2) | 3: HALT(3) | 4: HALT(4) | 5: HALT(5) | 6: HALT(6) | 7: HALT(7)
		| 8: HALT(8) | 9: HALT(9) | 10: HALT(10) | 11: HALT(11) | 12: HALT(12) | 13: HALT(13) | 14: HALT(14) | 15: HALT(15)
		| 16: HALT(16) | 17: HALT(17) | 18: HALT(18) | 19: HALT(19) | 20: HALT(20) | 21: HALT(21) | 22: HALT(22) | 23: HALT(23)
		| 24: HALT(24) | 25: HALT(25) | 26: HALT(26) | 27: HALT(27) | 28: HALT(28) | 29: HALT(29) | 30: HALT(30) | 31: HALT(31)
		| 32: HALT(32) | 33: HALT(33) | 34: HALT(34) | 35: HALT(35) | 36: HALT(36) | 37: HALT(37) | 38: HALT(38) | 39: HALT(39)
		| 40: HALT(40) | 41: HALT(41) | 42: HALT(42) | 43: HALT(43) | 44: HALT(44) | 45: HALT(45) | 46: HALT(46) | 47: HALT(47)
		| 48: HALT(48) | 49: HALT(49) | 50: HALT(50) | 51: HALT(51) | 52: HALT(52) | 53: HALT(53) | 54: HALT(54) | 55: HALT(55)
		| 56: HALT(56) | 57: HALT(57) | 58: HALT(58) | 59: HALT(59) | 60: HALT(60) | 61: HALT(61) | 62: HALT(62) | 63: HALT(63)
		| 64: HALT(64) | 65: HALT(65) | 66: HALT(66) | 67: HALT(67) | 68: HALT(68) | 69: HALT(69) | 70: HALT(70) | 71: HALT(71)
		| 72: HALT(72) | 73: HALT(73) | 74: HALT(74) | 75: HALT(75) | 76: HALT(76) | 77: HALT(77) | 78: HALT(78) | 79: HALT(79)
		| 80: HALT(80) | 81: HALT(81) | 82: HALT(82) | 83: HALT(83) | 84: HALT(84) | 85: HALT(85) | 86: HALT(86) | 87: HALT(87)
		| 88: HALT(88) | 89: HALT(89) | 90: HALT(90) | 91: HALT(91) | 92: HALT(92) | 93: HALT(93) | 94: HALT(94) | 95: HALT(95)
		| 96: HALT(96) | 97: HALT(97) | 98: HALT(98) | 99: HALT(99) | 100: HALT(100) | 101: HALT(101) | 102: HALT(102) | 103: HALT(103)
		| 104: HALT(104) | 105: HALT(105) | 106: HALT(106) | 107: HALT(107) | 108: HALT(108) | 109: HALT(109) | 110: HALT(110) | 111: HALT(111)
		| 112: HALT(112) | 113: HALT(113) | 114: HALT(114) | 115: HALT(115) | 116: HALT(116) | 117: HALT(117) | 118: HALT(118) | 119: HALT(119)
		| 120: HALT(120) | 121: HALT(121) | 122: HALT(122) | 123: HALT(123) | 124: HALT(124) | 125: HALT(125) | 126: HALT(126) | 127: HALT(127)
		| 128: HALT(128)
		END
	END Pass;

	PROCEDURE Await;
	(* Пред: транзакция откатана, reads — её прочитанные.
	   Пост: одна из прочитанных изменилась; журнал пуст. *)
		VAR w: Wait; r: Waiter; i: INTEGER; foreign: BOOLEAN;
	BEGIN
		IF nReads = 0 THEN Console.WriteStr("AsyncStm: Retry with nothing read"); Console.WriteLn; Clear END;
		ASSERT(nReads > 0, 21);
		IF ~Tasks.InTask() THEN Console.WriteStr("AsyncStm: Retry outside a task"); Console.WriteLn; Clear END;
		ASSERT(Tasks.InTask(), 22);
		NEW(w); w.task := Tasks.Current(); w.woken := FALSE; NEW(w.vars, nReads);
		FOR i := 0 TO nReads - 1 DO
			w.vars[i] := reads[i]; NEW(r); r.w := w; r.next := reads[i].waiters; reads[i].waiters := r
		END;
		Clear;
		Kernel.PushTrapCleaner(w);	(* отмена в Park снимает записи с переменных *)
		foreign := FALSE;
		WHILE ~w.woken DO	(* пробуждение без изменения — чужой Unpark *)
			Tasks.Park;
			IF ~w.woken THEN foreign := TRUE END
		END;
		Kernel.PopTrapCleaner(w);
		Unregister(w);
		IF foreign THEN Tasks.Unpark(w.task) END
	END Await;

	PROCEDURE Atomically* (t: Tx);
	(** Исполнить t.Do неделимо. Внутри транзакции t.Do исполняется как её
		часть.
		Пред: t # NIL — 20.
		Пред: Retry только при прочитанных переменных — 21, не на главном
		стеке — 22.
		Пред: в t.Do нет точек переключения — 25 (Tasks).
		Пост: записи t.Do видны все; при трапе в t.Do записи откатаны, трап
		передан с тем же номером (номер больше 128 — как 128). *)
		VAR ok, retried: BOOLEAN; code: INTEGER;
	BEGIN
		ASSERT(t # NIL, 20);
		IF active THEN t.Do
		ELSE
			LOOP
				ASSERT((nLog = 0) & (nReads = 0) & ~retrying, 100);
				INC(serial); rser := serial; wser := serial;
				active := TRUE; Tasks.BeginAtomic;
				ok := Run(t);
				Tasks.EndAtomic; active := FALSE;
				retried := retrying;	(* Retry узнаётся по признаку, а не по номеру трапа *)
				IF retrying THEN Kernel.SetQuiet(FALSE); retrying := FALSE END;
				IF ok THEN
					FOR code := 0 TO nLog - 1 DO Wake(log[code].v) END;
					Clear; EXIT
				END;
				Undo(0);
				IF ~retried THEN code := Kernel.err; Clear; Pass(code) END;
				Await
			END
		END
	END Atomically;

	PROCEDURE Retry*;
	(** Откатить транзакцию и ждать изменения прочитанных ею переменных.
		Пред: идёт транзакция — 20.
		Пост: не возвращается: Atomically исполняет транзакцию заново. *)
	BEGIN
		ASSERT(active, 20);
		retrying := TRUE; Kernel.SetQuiet(TRUE); HALT(retryTrap)
	END Retry;

	PROCEDURE^ OrElse* (a, b: Tx);

	PROCEDURE (p: Pair) Do;
	BEGIN
		OrElse(p.a, p.b)
	END Do;

	PROCEDURE OrElse* (a, b: Tx);
	(** Исполнить a; если a сделала Retry — откатить её записи и исполнить b.
		Вне транзакции — отдельной транзакцией.
		Пред: a # NIL — 20, b # NIL — 21.
		Пост: исполнена a или b; Retry в b ждёт изменения переменных,
		прочитанных a или b. *)
		VAR p: Pair; mark: INTEGER;
	BEGIN
		ASSERT(a # NIL, 20); ASSERT(b # NIL, 21);
		IF ~active THEN NEW(p); p.a := a; p.b := b; Atomically(p)
		ELSE
			mark := nLog; INC(serial); wser := serial;
			IF ~Run(a) THEN
				IF ~retrying THEN Pass(Kernel.err) END;
				Kernel.SetQuiet(FALSE); retrying := FALSE;
				Undo(mark); INC(serial); wser := serial;
				b.Do
			END
		END
	END OrElse;

	(* --- переменные -------------------------------------------------------- *)

	PROCEDURE NewInt* (x: INTEGER): IntVar;
		VAR v: IntVar;
	BEGIN
		NEW(v); v.x := x; RETURN v
	END NewInt;

	PROCEDURE (v: IntVar) Get* (): INTEGER, NEW;
	BEGIN
		Read(v); RETURN v.x
	END Get;

	PROCEDURE (v: IntVar) Set* (x: INTEGER), NEW;
	BEGIN
		Write(v); v.x := x
	END Set;

	PROCEDURE (v: IntVar) Save- (OUT s: Saved);
	BEGIN
		s.i := v.x
	END Save;

	PROCEDURE (v: IntVar) Load- (IN s: Saved);
	BEGIN
		v.x := SHORT(s.i)
	END Load;

	PROCEDURE NewBool* (x: BOOLEAN): BoolVar;
		VAR v: BoolVar;
	BEGIN
		NEW(v); v.x := x; RETURN v
	END NewBool;

	PROCEDURE (v: BoolVar) Get* (): BOOLEAN, NEW;
	BEGIN
		Read(v); RETURN v.x
	END Get;

	PROCEDURE (v: BoolVar) Set* (x: BOOLEAN), NEW;
	BEGIN
		Write(v); v.x := x
	END Set;

	PROCEDURE (v: BoolVar) Save- (OUT s: Saved);
	BEGIN
		IF v.x THEN s.i := 1 ELSE s.i := 0 END
	END Save;

	PROCEDURE (v: BoolVar) Load- (IN s: Saved);
	BEGIN
		v.x := s.i # 0
	END Load;

	PROCEDURE NewReal* (x: REAL): RealVar;
		VAR v: RealVar;
	BEGIN
		NEW(v); v.x := x; RETURN v
	END NewReal;

	PROCEDURE (v: RealVar) Get* (): REAL, NEW;
	BEGIN
		Read(v); RETURN v.x
	END Get;

	PROCEDURE (v: RealVar) Set* (x: REAL), NEW;
	BEGIN
		Write(v); v.x := x
	END Set;

	PROCEDURE (v: RealVar) Save- (OUT s: Saved);
	BEGIN
		s.x := v.x
	END Save;

	PROCEDURE (v: RealVar) Load- (IN s: Saved);
	BEGIN
		v.x := s.x
	END Load;

	PROCEDURE NewRef* (x: ANYPTR): RefVar;
		VAR v: RefVar;
	BEGIN
		NEW(v); v.x := x; RETURN v
	END NewRef;

	PROCEDURE (v: RefVar) Get* (): ANYPTR, NEW;
	BEGIN
		Read(v); RETURN v.x
	END Get;

	PROCEDURE (v: RefVar) Set* (x: ANYPTR), NEW;
	BEGIN
		Write(v); v.x := x
	END Set;

	PROCEDURE (v: RefVar) Save- (OUT s: Saved);
	BEGIN
		s.p := v.x
	END Save;

	PROCEDURE (v: RefVar) Load- (IN s: Saved);
	BEGIN
		v.x := s.p
	END Load;

BEGIN
	NEW(reads, logMin); NEW(log, logMin)
END AsyncStm.
