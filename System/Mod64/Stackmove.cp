MODULE Stackmove;
(** Проба переноса стека задачи (копируемый стек, как в Go), только Linux amd64.

	Пролог процедуры, собранной с ключом "%", сравнивает SP с низом стека задачи и при
	нехватке исполняет трап 137. Обработчик трапа LinKernel (сигнальный стек, переменная
	окружения BB_STACKMOVE) зовёт Kernel.stackGrow = Grow. Grow берёт у Tasks стек вдвое
	больше, копирует занятую часть [SP, hi), правит слова, которые указывают внутрь старого
	стека, и регистры прерванного кода, и исполнение продолжается за трапом на новом стеке.

	Два способа найти слова для правки:
	- exact — по картам кадров компилятора (конец кода модуля, DevCPCamd64.EmitFrameMaps):
	  цепочка сохранённых FP, слоты-адреса параметров, статическая связь, контексты Kernel.Try
	  (sigjmp_buf, SP и FP в нём зашифрованы glibc и расшифровываются ключом fs:[30H]),
	  регистры RSP, RBP, RBX;
	- loose — любое выровненное слово и любой регистр, значение которых попадает в [lo, hi)
	  старого стека, плюс те же контексты Try.
	Оба способа считаются на каждом переносе, правится по выбранному; расхождение копится в
	счётчиках (loose без exact и exact без loose).

	Закрепление (pin): перенос отказывает и остаётся трап 137, если на стеке есть кадр без
	модуля (код C), цепочка FP рвётся, или (правило модулей) кадр принадлежит модулю, который
	импортирует SYSTEM или собран без ключа, кроме модулей самого переноса (trusted). *)

	IMPORT S := SYSTEM, Kernel, Tasks, Console;

	CONST
		off* = 0; exact* = 1; loose* = 2;
		(* виды слотов для мутаций (mask) *)
		kFp* = 0; kPar* = 1; kArr* = 2; kSl* = 3; kRecv* = 4; kTry* = 5; kRbx* = 6;
		(* gregs ucontext x86-64 *)
		rRbp = 10; rRbx = 11; rRsp = 15; rRip = 16; nRegs = 16;
		noSysBit = 11; chkBit = 12;
		magic = 50414D53H;
		reserve = 1024;
		maxPin = 48;

	TYPE
		PinCount = RECORD name: Kernel.Utf8Name; n: INTEGER END;

	VAR
		mode*: INTEGER;
		mask*: SET;	(** виды слотов, которые exact не правит (мутации) *)
		moduleRule*: BOOLEAN;	(** закреплять по правилу модулей (иначе — только кадры C) *)
		poison*: BOOLEAN;	(** залить старый стек неканоническим адресом: устаревшее чтение даст трап *)
		compare*: BOOLEAN;	(** считать оба способа и расхождение (дороже) *)
		cycAlloc*, cycTotal*, cycWalk*: LONGINT;
		moves*, pins*, pinsC*, bytesCopied*: INTEGER;
		looseOnly*, exactOnly*, both*, mangled*, looseOnlyRegs*, unaligned*: INTEGER;
		pinTab: ARRAY maxPin OF PinCount; nPin: INTEGER;
		lastLooseOnly*: ARRAY 8 OF RECORD mod*: Kernel.Utf8Name; pcOff*, fpOff*, val*: INTEGER END;
		nLast*: INTEGER;
		lastLo*, lastHi*: S.ADDRESS;
		dbgMod*: Kernel.Utf8Name; postBreak*: S.ADDRESS; postDepth*: INTEGER;
		dbgFp*, dbgLo*, dbgHi*, dbgRet*, prevLo*, prevHi*: S.ADDRESS;	(** границы последнего нового стека *)
		tryOldOff: INTEGER; tryOldKnown: BOOLEAN;
		pos: POINTER TO ARRAY OF S.ADDRESS; nPos: INTEGER;	(* exact: адреса слов (на старом стеке) *)

	PROCEDURE [code] Guard (): S.ADDRESS
		064H, 048H, 08BH, 004H, 025H, 030H, 000H, 000H, 000H;	(* mov rax, fs:[30H] — pointer_guard glibc *)

	PROCEDURE [code] Tsc (): LONGINT
		00FH, 031H, 048H, 0C1H, 0E2H, 020H, 048H, 009H, 0D0H;	(* rdtsc; shl rdx,32; or rax,rdx *)

	PROCEDURE Xor (x, y: S.ADDRESS): S.ADDRESS;
		VAR a, b: SET; r: S.ADDRESS;
	BEGIN
		S.GET(S.ADR(x), a); S.GET(S.ADR(y), b); S.PUT(S.ADR(r), a / b);
		S.GET(S.ADR(x) + 4, a); S.GET(S.ADR(y) + 4, b); S.PUT(S.ADR(r) + 4, a / b);
		RETURN r
	END Xor;

	PROCEDURE Rol (x: S.ADDRESS; n: INTEGER): S.ADDRESS;
	BEGIN
		RETURN S.LSH(x, n) + S.LSH(x, n - 64)	(* части не пересекаются: сумма = OR *)
	END Rol;

	PROCEDURE Demangle (v: S.ADDRESS): S.ADDRESS;
	BEGIN
		RETURN Xor(Rol(v, 64 - 17), Guard())
	END Demangle;

	PROCEDURE Mangle (v: S.ADDRESS): S.ADDRESS;
	BEGIN
		RETURN Rol(Xor(v, Guard()), 17)
	END Mangle;

	PROCEDURE ModOf (pc: S.ADDRESS): Kernel.Module;
		VAR m: Kernel.Module;
	BEGIN
		m := Kernel.modList;
		WHILE (m # NIL) & ((pc < m.code) OR (pc >= m.code + m.csize)) DO m := m.next END;
		RETURN m
	END ModOf;

	PROCEDURE Trusted (m: Kernel.Module): BOOLEAN;
	BEGIN
		RETURN (m.name = "Kernel") OR (m.name = "LinKernel") OR (m.name = "Tasks") OR (m.name = "Stackmove")
	END Trusted;

	PROCEDURE CountPin (IN name: Kernel.Utf8Name);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < nPin) & (pinTab[i].name # name) DO INC(i) END;
		IF i = nPin THEN
			IF nPin = maxPin THEN RETURN END;
			pinTab[i].name := name; pinTab[i].n := 0; INC(nPin)
		END;
		INC(pinTab[i].n)
	END CountPin;

	PROCEDURE PinReport* (VAR s: ARRAY OF CHAR);
	(** Пост: s — «модуль=число …» по закреплениям. *)
		VAR i, j, k, n: INTEGER; d: ARRAY 16 OF CHAR;
	BEGIN
		s := ""; k := 0;
		FOR i := 0 TO nPin - 1 DO
			j := 0;
			WHILE (pinTab[i].name[j] # 0X) & (k < LEN(s) - 20) DO s[k] := pinTab[i].name[j]; INC(k); INC(j) END;
			s[k] := "="; INC(k);
			n := pinTab[i].n; j := 0;
			REPEAT d[j] := CHR(ORD("0") + n MOD 10); n := n DIV 10; INC(j) UNTIL n = 0;
			WHILE j > 0 DO DEC(j); s[k] := d[j]; INC(k) END;
			s[k] := " "; INC(k)
		END;
		s[k] := 0X
	END PinReport;

	PROCEDURE Reset*;
	BEGIN
		cycAlloc := 0; cycTotal := 0; cycWalk := 0; moves := 0; pins := 0; pinsC := 0; bytesCopied := 0; looseOnly := 0; exactOnly := 0; both := 0;
		mangled := 0; looseOnlyRegs := 0; unaligned := 0; nPin := 0; nLast := 0
	END Reset;

	(* --- карты кадров ---------------------------------------------------- *)

	PROCEDURE MapOf (m: Kernel.Module; pc: S.ADDRESS; OUT ent: S.ADDRESS): BOOLEAN;
	(* Пост: TRUE — ent: адрес записи карты процедуры, в которой лежит pc. *)
		VAR end, a, tab, best: S.ADDRESS; mg, len, e, n, i: INTEGER; off: INTEGER; s: SHORTINT;
	BEGIN
		ent := 0;
		IF ~(chkBit IN m.opts) THEN RETURN FALSE END;
		end := m.code + m.csize; a := end - 4; i := 0;
		LOOP
			S.GET(a, mg);
			IF mg = magic THEN EXIT END;
			DEC(a); INC(i);
			IF i > 16 THEN RETURN FALSE END
		END;
		S.GET(a - 4, len); tab := a - 4 - len; off := SHORT(pc - m.code);
		a := tab; best := 0;
		WHILE a < tab + len DO
			S.GET(a, e); S.GET(a + 4, s); n := s;
			IF e <= off THEN best := a ELSE a := tab + len END;
			IF a # tab + len THEN INC(a, 6 + 2 * n) END
		END;
		ent := best;
		RETURN best # 0
	END MapOf;

	PROCEDURE AddPos (a: S.ADDRESS);
		VAR n: POINTER TO ARRAY OF S.ADDRESS; i: INTEGER;
	BEGIN
		IF pos = NIL THEN NEW(pos, 256) END;
		IF nPos = LEN(pos^) THEN
			NEW(n, 2 * nPos); FOR i := 0 TO nPos - 1 DO n[i] := pos[i] END; pos := n
		END;
		pos[nPos] := a; INC(nPos)
	END AddPos;

	PROCEDURE FindTryOld;
	(* Смещение локальной oldContext процедуры LinKernel.Try от её FP — из описаний отладчика. *)
		VAR m: Kernel.Module; ref: S.ADDRESS; adr: INTEGER; name: Kernel.Utf8Name;
			mode, form: SHORTCHAR; desc: Kernel.Type;
	BEGIN
		tryOldKnown := TRUE; tryOldOff := 0;
		m := Kernel.ThisMod("LinKernel"); IF m = NIL THEN RETURN END;
		ref := m.refs;
		Kernel.GetRefProc(ref, adr, name);
		WHILE (adr # 0) & (name # "Try") DO Kernel.GetRefProc(ref, adr, name) END;
		IF adr = 0 THEN RETURN END;
		Kernel.GetRefVar(ref, mode, form, desc, adr, name);
		WHILE (mode # 0X) & (name # "oldContext") DO Kernel.GetRefVar(ref, mode, form, desc, adr, name) END;
		IF mode # 0X THEN tryOldOff := adr END
	END FindTryOld;

	(* --- обход -------------------------------------------------------------- *)

	PROCEDURE Pinned (sp, fp, pc, hi: S.ADDRESS): BOOLEAN;
	(* Пост: TRUE — на стеке есть кадр, который запрещает перенос; посчитан по модулю. *)
		VAR m: Kernel.Module; ret: S.ADDRESS; name: Kernel.Utf8Name;
	BEGIN
		ret := pc;
		LOOP
			m := ModOf(ret);
			IF m = NIL THEN INC(pinsC); name := "<C>"; CountPin(name); RETURN TRUE END;
			IF moduleRule & ~Trusted(m) & ~((noSysBit IN m.opts) & (chkBit IN m.opts)) THEN
				CountPin(m.name); RETURN TRUE
			END;
			IF fp = 0 THEN RETURN FALSE END;
			IF (fp < sp) OR (fp >= hi) THEN
				name := "<chain>"; CountPin(name); dbgFp := fp; dbgLo := sp; dbgHi := hi; dbgRet := ret - m.code; dbgMod := m.name; RETURN TRUE
			END;
			S.GET(fp + 8, ret); S.GET(fp, fp);
			IF fp = 0 THEN RETURN FALSE END
		END
	END Pinned;

	PROCEDURE CollectExact (fp, pc, lo, hi: S.ADDRESS);
	(* exact: адреса слов на старом стеке, которые по картам содержат адрес стека. *)
		VAR m: Kernel.Module; ent, ret: S.ADDRESS; n, i, k: INTEGER; s: SHORTINT; ctx, tfp, a: S.ADDRESS;
	BEGIN
		nPos := 0; ret := pc;
		WHILE (fp >= lo) & (fp < hi) DO
			IF ~(kFp IN mask) THEN AddPos(fp) END;
			m := ModOf(ret);
			IF (m # NIL) & MapOf(m, ret, ent) THEN
				S.GET(ent + 4, s); n := s;
				FOR i := 0 TO n - 1 DO
					S.GET(ent + 6 + 2 * i, s); k := s MOD 8;
					IF ~((k = 0) & (kPar IN mask) OR (k = 1) & (kArr IN mask) OR (k = 2) & (kSl IN mask)
						OR (k = 3) & (kRecv IN mask)) THEN
						AddPos(fp + (s - k))
					END
				END
			END;
			S.GET(fp + 8, ret); S.GET(fp, fp)
		END;
		(* контексты Kernel.Try: oldContext в кадре LinKernel.Try *)
		IF ~tryOldKnown THEN FindTryOld END;
		IF (tryOldOff # 0) & ~(kTry IN mask) THEN
			ctx := Kernel.platform.GetTryContext();
			WHILE (ctx >= lo) & (ctx < hi) DO
				S.GET(ctx + 8, tfp); tfp := Demangle(tfp);	(* __jmpbuf[1] = rbp вызывающего sigsetjmp *)
				IF (tfp < lo) OR (tfp >= hi) THEN ctx := 0
				ELSE a := tfp + tryOldOff; AddPos(a); S.GET(a, ctx)
				END
			END
		END
	END CollectExact;

	PROCEDURE InExact (a: S.ADDRESS): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < nPos) & (pos[i] # a) DO INC(i) END;
		RETURN i < nPos
	END InExact;

	PROCEDURE NoteLooseOnly (a, fp, pc, val: S.ADDRESS);
	(* Запомнить слово, которое правит только loose: модуль и смещение кода кадра-владельца. *)
		VAR ret, f: S.ADDRESS; m: Kernel.Module;
	BEGIN
		IF nLast >= LEN(lastLooseOnly) THEN RETURN END;
		ret := pc; f := fp;
		WHILE (f # 0) & (f < a) DO S.GET(f + 8, ret); S.GET(f, f) END;	(* слово ниже f — кадр процедуры с ret *)
		m := ModOf(ret);
		IF m # NIL THEN lastLooseOnly[nLast].mod := m.name; lastLooseOnly[nLast].pcOff := SHORT(ret - m.code)
		ELSE lastLooseOnly[nLast].mod := "<C>"; lastLooseOnly[nLast].pcOff := 0
		END;
		lastLooseOnly[nLast].fpOff := SHORT(f - a); lastLooseOnly[nLast].val := SHORT(val - a);
		INC(nLast)
	END NoteLooseOnly;

	PROCEDURE FixTry (lo, hi, delta: S.ADDRESS);
	(* Цепочка контекстов Try на новом стеке: зашифрованные RBP и RSP sigjmp_buf и глобал контекста. *)
		VAR ctx, v, a: S.ADDRESS; k: INTEGER;
	BEGIN
		ctx := Kernel.platform.GetTryContext();
		IF (ctx >= lo) & (ctx < hi) THEN Kernel.platform.SetTryContext(ctx + delta) END;
		WHILE (ctx >= lo) & (ctx < hi) DO
			a := ctx + delta;
			FOR k := 1 TO 6 BY 5 DO	(* __jmpbuf[1] = rbp, [6] = rsp *)
				S.GET(a + 8 * k, v); v := Demangle(v);
				IF (v >= lo) & (v < hi) THEN S.PUT(a + 8 * k, Mangle(v + delta)); INC(mangled) END
			END;
			S.GET(a + 8, v); v := Demangle(v);	(* уже новый rbp *)
			IF (tryOldOff # 0) & (v >= lo + delta) & (v < hi + delta) THEN
				S.GET(v + tryOldOff, ctx); ctx := ctx - delta	(* oldContext уже поправлен: в старых координатах *)
			ELSE ctx := 0
			END
		END
	END FixTry;

	PROCEDURE Grow (regs: S.ADDRESS): BOOLEAN;
		VAR sp, fp, pc, lo, hi, nlo, nhi, olo, ohi, delta, a, w, used: S.ADDRESS;
			size, frame, newSize, oldSize, i: INTEGER; s: Kernel.Stack; inL, inE: BOOLEAN; c0, c1: LONGINT;
	BEGIN
		IF mode = off THEN RETURN FALSE END;
		c0 := Tsc();
		S.GET(regs + 8 * rRsp, sp); S.GET(regs + 8 * rRbp, fp); S.GET(regs + 8 * rRip, pc);
		s := Kernel.StackOf(sp);
		IF (s = NIL) OR (Kernel.stkLo = 0) OR (s.lo # Kernel.stkLo) THEN RETURN FALSE END;
		lo := s.lo; hi := s.hi;
		IF Pinned(sp, fp, pc, hi) THEN INC(pins); RETURN FALSE END;
		S.GET(pc - 6, frame);	(* cmp r11, imm32: запас + кадр *)
		used := hi - sp; size := SHORT(hi - lo); newSize := 2 * size;
		WHILE newSize - used < frame + reserve + 4096 DO newSize := 2 * newSize END;
		c1 := Tsc(); cycWalk := cycWalk + c1 - c0;
		IF ~Tasks.ReplaceStack(newSize, olo, ohi, nlo, nhi, oldSize) THEN RETURN FALSE END;
		cycAlloc := cycAlloc + Tsc() - c1;
		ASSERT((olo = lo) & (ohi = hi), 100);
		delta := nhi - hi;
		IF compare OR (mode = exact) THEN CollectExact(fp, pc, lo, hi) END;
		S.MOVE(sp, sp + delta, used); INC(bytesCopied, SHORT(used));
		IF compare THEN
			(* шаг 4: компилятор выравнивает кадры на 4, слова кадров не обязаны быть кратны 8 от sp *)
			a := sp;
			WHILE a <= hi - 8 DO
				S.GET(a, w);
				inL := (w >= lo) & (w < hi);
				inE := InExact(a);
				IF inL & inE THEN INC(both)
				ELSIF inL THEN INC(looseOnly); NoteLooseOnly(a, fp, pc, w)
				ELSIF inE THEN INC(exactOnly)
				END;
				IF inL & ((a - sp) MOD 8 # 0) THEN INC(unaligned) END;
				IF (mode = loose) & inL OR (mode = exact) & inE & inL THEN S.PUT(a + delta, w + delta) END;
				INC(a, 4)
			END
		ELSIF mode = loose THEN
			a := sp + delta;
			WHILE a <= nhi - 8 DO
				S.GET(a, w); IF (w >= lo) & (w < hi) THEN S.PUT(a, w + delta) END; INC(a, 4)
			END
		ELSE
			FOR i := 0 TO nPos - 1 DO
				a := pos[i] + delta; S.GET(a, w); IF (w >= lo) & (w < hi) THEN S.PUT(a, w + delta) END
			END
		END;
		(* регистры *)
		FOR i := 0 TO nRegs - 1 DO
			S.GET(regs + 8 * i, w);
			IF (w >= lo) & (w < hi) THEN
				IF (i = rRsp) OR (i = rRbp) OR (i = rRbx) & ~(kRbx IN mask) THEN S.PUT(regs + 8 * i, w + delta)
				ELSE INC(looseOnlyRegs); IF mode = loose THEN S.PUT(regs + 8 * i, w + delta) END
				END
			END
		END;
		IF ~(kTry IN mask) OR (mode = loose) THEN FixTry(lo, hi, delta) END;
		(* отладка: цепочка на новом стеке сразу после правки *)
		a := fp + delta; w := 0;
		WHILE (a # 0) & (a >= sp + delta) & (a < nhi) DO S.GET(a, a); INC(w) END;
		IF a # 0 THEN postBreak := a - lo; postDepth := SHORT(w) END;
		IF poison THEN
			a := lo; w := -2252579084297487122;	(* 0xE0BADBADE0BADBAD... неканонический *)
			WHILE a < hi DO S.PUT(a, w); INC(a, 8) END
		END;
		Tasks.ReleaseStack(oldSize, lo, hi);
		prevLo := lo; prevHi := hi; lastLo := nlo; lastHi := nhi;
		INC(moves);
		RETURN TRUE
	END Grow;

	PROCEDURE Int (x: INTEGER);
		VAR d: ARRAY 16 OF CHAR; j: INTEGER;
	BEGIN
		j := 0; REPEAT d[j] := CHR(ORD("0") + x MOD 10); x := x DIV 10; INC(j) UNTIL x = 0;
		WHILE j > 0 DO DEC(j); Console.WriteChar(d[j]) END
	END Int;

	PROCEDURE Report*;
	(** Печать счётчиков одной строкой STACKMOVE. *)
		VAR s: ARRAY 1024 OF CHAR;
	BEGIN
		Console.WriteStr("STACKMOVE mode="); Int(mode); Console.WriteStr(" moves="); Int(moves);
		Console.WriteStr(" pins="); Int(pins); Console.WriteStr(" pinsC="); Int(pinsC);
		Console.WriteStr(" bytes="); Int(bytesCopied); Console.WriteStr(" both="); Int(both);
		Console.WriteStr(" looseOnly="); Int(looseOnly); Console.WriteStr(" exactOnly="); Int(exactOnly);
		Console.WriteStr(" regsLooseOnly="); Int(looseOnlyRegs); Console.WriteStr(" try="); Int(mangled);
		Console.WriteStr(" unaligned4="); Int(unaligned);
		PinReport(s); Console.WriteStr(" pinned: "); Console.WriteStr(s); Console.WriteLn
	END Report;

	PROCEDURE^ SetMode* (m: INTEGER);

	PROCEDURE ProbeLoose*;
	(** Прогон тестов с переносом: нестрогий способ, без правила модулей (закрепляют только C). *)
	BEGIN moduleRule := FALSE; SetMode(loose) END ProbeLoose;

	PROCEDURE ProbeExact*;
	(** Точный способ с правилом модулей. *)
	BEGIN moduleRule := TRUE; SetMode(exact) END ProbeExact;

	PROCEDURE SetMode* (m: INTEGER);
	(** off, exact или loose; не off — Kernel.stackGrow = Grow. *)
	BEGIN
		mode := m;
		IF m = off THEN Kernel.stackGrow := NIL ELSE Kernel.stackGrow := Grow END
	END SetMode;

	PROCEDURE Exact*;
	BEGIN SetMode(exact) END Exact;

	PROCEDURE Loose*;
	BEGIN SetMode(loose) END Loose;

	PROCEDURE Off*;
	BEGIN SetMode(off) END Off;

BEGIN
	moduleRule := TRUE; compare := TRUE
CLOSE
	IF mode # off THEN Report END;
	Kernel.stackGrow := NIL
END Stackmove.
