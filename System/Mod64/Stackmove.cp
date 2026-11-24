MODULE Stackmove;
(** Растущие стеки задач (копируемый стек, как в Go), только Linux amd64.

	Поставщик стеков Tasks.StackDir: задача стартует с малым стеком из плотной плиты (без
	сторожевых страниц, с запасом под Kernel.stkLo и канарейкой у дна). Пролог процедуры,
	собранной с ключом "%", сравнивает SP с Kernel.stkLo и при нехватке исполняет трап 137.
	Обработчик трапа LinKernel (сигнальный стек) отдаёт его Kernel.Scheduler.grow → Tasks →
	StackDir.Grow этого поставщика: стек вдвое больше от него же (Tasks.ReplaceStack), копия
	занятой части [SP, hi), правка слов, которые указывают внутрь старого стека, и регистров
	прерванного кода; старый стек — в пул (Tasks.ReleaseStack); исполнение продолжается за
	трапом на новом стеке. Выше потолка max — обычный трап 137. Уменьшения стека нет.

	Включается командой UseGrowingStacks (или SetMode); по умолчанию поставщик не стоит, и
	поведение Tasks прежнее.

	Слова для правки (exact) — по картам кадров компилятора (конец кода модуля,
	DevCPCamd64.EmitFrameMaps): цепочка сохранённых FP, слоты-адреса параметров, статическая
	связь [FP-8], контексты Kernel.Try (sigjmp_buf, SP и FP в нём зашифрованы glibc и
	расшифровываются ключом fs:[30H]), регистры RSP, RBP, RBX. Диагностика: loose — любое
	слово и любой регистр со значением в [lo, hi) старого стека; compare — считать оба
	множества на каждом переносе и копить расхождение. Рабочий способ — exact.

	Закрепление (pin): перенос отказывает, и остаётся трап 137, если на стеке есть кадр без
	модуля (код C), цепочка FP рвётся, или (правило модулей) кадр принадлежит модулю, который
	не «собран с % и без SYSTEM» (бит 11 опций) и не в списке доверенных (trustedList). *)

	IMPORT S := SYSTEM, Kernel, Tasks, Console;

	CONST
		off* = 0; exact* = 1; loose* = 2;
		(* виды слотов для мутаций (mask) *)
		kFp* = 0; kPar* = 1; kArr* = 2; kSl* = 3; kRecv* = 4; kTry* = 5; kRbx* = 6;
		(* gregs ucontext x86-64 *)
		rRbp = 10; rRbx = 11; rRsp = 15; rRip = 16; nRegs = 16;
		noSysBit = 11; chkBit = 12;	(* опции модуля: собран с % и без SYSTEM; собран с % *)
		magic = 50414D53H;	(* "SMAP" — метка карт кадров в конце кода *)
		checkReserve = 1024;	(* DevCPCamd64.stackReserve: запас проверки пролога *)
		maxPin = 48;
		maxPos = 16384;	(* слов по картам на один перенос; больше — закрепление *)
		maxMods = 1024;	(* модулей в таблице кода *)
		(* Список доверенных модулей: проверены вручную (docs/stack-copy-study.md, раздел 7) —
		   адреса стека задачи в их кадрах и вне стека либо мертвы на время вызова, где стек
		   может вырасти, либо известны переносу (контексты Try). Только здесь. *)
		trustedList = " Kernel LinKernel Tasks Taskarch ";	(* ядро: собрано без %, карт нет *)
		(* Доверенные с картами: импортируют SYSTEM, но собраны с % (без ключа — закрепляют). *)
		trustedMapped = " ";
		(* Процедуры доверенных модулей, чей кадр всё же закрепляет стек: держат адрес стека
		   через вызов, где стек может вырасти (Kernel.NewObj пишет в VAR-параметр после NewRec,
		   а там сборка с финализаторами; Await — кадр ждущего в параметре). CallCond — [code],
		   встраивается в Monitors, а Monitors не доверен. *)
		pinnedProcs = " Kernel.NewObj Kernel.Await ";
		maxProcPins = 8;
		(* поставщик *)
		growStart* = 4096;	(** стартовый стек UseGrowingStacks *)
		growReserve* = 2048;	(** запас под Kernel.stkLo: код без проверки (ядро, C) *)
		growMax* = 8 * 1024 * 1024;	(** потолок: выше — трап 137 *)
		minLog = 12; maxLog = 26;	(* классы плит: 4 КБ .. 64 МБ *)
		slabBytes = 4 * 1024 * 1024;	(* плита мелких стеков *)
		maxSlabs = 8192;

	TYPE
		PinCount = RECORD name: Kernel.Utf8Name; n: INTEGER END;
		CodeRange = RECORD lo, hi: S.ADDRESS; mod: Kernel.Module; movable: BOOLEAN END;

		(** Поставщик растущих стеков: плотные плиты по классам-степеням двойки. Память не
			выделяется в куче после настройки: NewStack зовётся и из обработчика трапа. *)
		GrowDir* = POINTER TO RECORD (Tasks.StackDir)
			max: INTEGER;
			next, end: ARRAY maxLog + 1 OF S.ADDRESS;	(* невыданные стеки класса: [next, end) *)
			slabLo, slabHi: ARRAY maxSlabs OF S.ADDRESS;
			nSlabs: INTEGER
		END;

	VAR
		mode*: INTEGER;
		mask*: SET;	(** виды слотов, которые exact не правит (мутации) *)
		moduleRule*: BOOLEAN;	(** закреплять по правилу модулей (иначе — только кадры C) *)
		trustRule*: BOOLEAN;	(** список доверенных действует (FALSE — мутация: ядро закрепляет) *)
		testTrusted*: ARRAY 64 OF CHAR;	(** только тесты: "Имя " — модуль сверх списка (с картами) *)
		poison*: BOOLEAN;	(** залить старый стек неканоническим адресом: устаревшее чтение даст трап *)
		compare*: BOOLEAN;	(** считать оба способа и расхождение (дороже) *)
		cycAlloc*, cycTotal*, cycWalk*: LONGINT;
		moves*, pins*, pinsC*, capped*, bytesCopied*: INTEGER;
		looseOnly*, exactOnly*, both*, mangled*, looseOnlyRegs*, unaligned*: INTEGER;
		pinTab: ARRAY maxPin OF PinCount; nPin: INTEGER;
		lastLooseOnly*: ARRAY 8 OF RECORD mod*: Kernel.Utf8Name; pcOff*, fpOff*, val*: INTEGER END;
		nLast*: INTEGER;
		lastLo*, lastHi*: S.ADDRESS;
		dbgMod*: Kernel.Utf8Name; postBreak*: S.ADDRESS; postDepth*: INTEGER;
		dbgFp*, dbgLo*, dbgHi*, dbgRet*, prevLo*, prevHi*: S.ADDRESS;	(** границы последнего нового стека *)
		tryOldOff: INTEGER; tryOldKnown: BOOLEAN;
		pos: ARRAY maxPos OF S.ADDRESS; nPos: INTEGER;	(* exact: адреса слов (на старом стеке) *)
		procLo, procHi: ARRAY maxProcPins OF S.ADDRESS; nProcPins: INTEGER;
		code: ARRAY maxMods OF CodeRange; nCode: INTEGER; codeHead: Kernel.Module; codeRule: INTEGER;
		dir: GrowDir; prevDir: Tasks.StackDir;

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

	PROCEDURE InList (IN l: ARRAY OF CHAR; IN name: ARRAY OF SHORTCHAR): BOOLEAN;
	(* l — имена через пробел, с пробелом в начале и в конце. *)
		VAR n: ARRAY 72 OF CHAR; i, k, j: INTEGER;
	BEGIN
		n := " " + name$ + " "; i := 0;
		WHILE l[i] # 0X DO
			k := 0; j := i;
			WHILE (n[k] # 0X) & (l[j] = n[k]) DO INC(j); INC(k) END;
			IF n[k] = 0X THEN RETURN TRUE END;
			INC(i)
		END;
		RETURN FALSE
	END InList;

	PROCEDURE Trusted* (IN name: ARRAY OF SHORTCHAR): BOOLEAN;
	(** Модуль name в списке доверенных (trustedList, trustedMapped, testTrusted). *)
	BEGIN
		RETURN InList(trustedList, name) OR InList(trustedMapped + testTrusted, name)
	END Trusted;

	PROCEDURE Movable (m: Kernel.Module): BOOLEAN;
	(* Правило модулей: кадры m не мешают переносу. *)
	BEGIN
		RETURN ~moduleRule OR (noSysBit IN m.opts) & (chkBit IN m.opts)
			OR trustRule & (InList(trustedList, m.name) OR (chkBit IN m.opts) & InList(trustedMapped + testTrusted, m.name))
	END Movable;

	PROCEDURE PinProcs;
	(* Диапазоны кода процедур pinnedProcs: от входа до входа следующей по описаниям. *)
		VAR l: ARRAY 128 OF CHAR; mn: ARRAY 64 OF CHAR; pn, name: Kernel.Utf8Name; i, k, adr, next: INTEGER;
			m: Kernel.Module; ref: S.ADDRESS;
	BEGIN
		nProcPins := 0; l := pinnedProcs; i := 1;
		WHILE (l[i] # 0X) & (nProcPins < maxProcPins) DO
			k := 0; WHILE l[i] # "." DO mn[k] := l[i]; INC(k); INC(i) END; mn[k] := 0X; INC(i);
			k := 0; WHILE l[i] # " " DO pn[k] := SHORT(l[i]); INC(k); INC(i) END; pn[k] := 0X; INC(i);
			m := Kernel.ThisMod(mn);
			IF m # NIL THEN
				(* описания процедур — по возрастанию адреса; adr — конец процедуры *)
				ref := m.refs; next := 0; Kernel.GetRefProc(ref, adr, name);
				WHILE (adr # 0) & (name # pn) DO next := adr; Kernel.GetRefProc(ref, adr, name) END;
				IF adr # 0 THEN
					procLo[nProcPins] := m.code + next; procHi[nProcPins] := m.code + adr; INC(nProcPins)
				END
			END
		END
	END PinProcs;

	PROCEDURE InPinnedProc (pc: S.ADDRESS): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < nProcPins) & ((pc < procLo[i]) OR (pc >= procHi[i])) DO INC(i) END;
		RETURN i < nProcPins
	END InPinnedProc;

	PROCEDURE BuildCode;
	(* Таблица кода: диапазоны модулей по возрастанию адреса и признак переносимости. Строится
	   вне обработчика трапа (SetMode) и заново, когда список модулей сменился; без кучи. *)
		VAR m: Kernel.Module; i, j: INTEGER; r: CodeRange;
	BEGIN
		nCode := 0; m := Kernel.modList;
		WHILE (m # NIL) & (nCode < maxMods) DO
			IF m.csize > 0 THEN
				r.lo := m.code; r.hi := m.code + m.csize; r.mod := m; r.movable := Movable(m);
				i := nCode; WHILE (i > 0) & (code[i - 1].lo > r.lo) DO code[i] := code[i - 1]; DEC(i) END;
				code[i] := r; INC(nCode)
			END;
			m := m.next
		END;
		j := 0; IF moduleRule THEN INC(j) END; IF trustRule THEN INC(j, 2) END;
		codeHead := Kernel.modList; codeRule := j;
		PinProcs
	END BuildCode;

	PROCEDURE Find (pc: S.ADDRESS): INTEGER;
	(* Пост: номер диапазона кода с pc; -1 — не код модуля (C). Двоичный поиск. *)
		VAR l, h, k, rule: INTEGER;
	BEGIN
		rule := 0; IF moduleRule THEN INC(rule) END; IF trustRule THEN INC(rule, 2) END;
		IF (codeHead # Kernel.modList) OR (rule # codeRule) THEN BuildCode END;
		l := 0; h := nCode;
		WHILE l < h DO
			k := (l + h) DIV 2;
			IF code[k].hi <= pc THEN l := k + 1 ELSE h := k END
		END;
		IF (l < nCode) & (code[l].lo <= pc) THEN RETURN l ELSE RETURN -1 END
	END Find;

	PROCEDURE ModOf (pc: S.ADDRESS): Kernel.Module;
		VAR k: INTEGER;
	BEGIN
		k := Find(pc);
		IF k < 0 THEN RETURN NIL ELSE RETURN code[k].mod END
	END ModOf;

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
	(* Без кучи: обработчик трапа. Переполнение — nPos > maxPos, перенос отказывает. *)
	BEGIN
		IF nPos < maxPos THEN pos[nPos] := a END;
		INC(nPos)
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
		VAR k: INTEGER; ret: S.ADDRESS; name: Kernel.Utf8Name; m: Kernel.Module;
	BEGIN
		ret := pc;
		LOOP
			k := Find(ret);
			IF k < 0 THEN INC(pinsC); name := "<C>"; CountPin(name); RETURN TRUE END;
			m := code[k].mod;
			IF ~code[k].movable OR (m.refcnt < 0) THEN CountPin(m.name); RETURN TRUE END;
			IF moduleRule & InPinnedProc(ret) THEN name := m.name$ + ".proc"; CountPin(name); RETURN TRUE END;
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

	PROCEDURE Move (d: GrowDir; t: Tasks.Task; regs: S.ADDRESS): BOOLEAN;
	(* Пред: обработчик трапа 137 на сигнальном стеке, t — текущая задача, её стек выдал d.
	   Пост: TRUE — t на новом стеке, регистры regs поправлены; FALSE — стек не тронут. *)
		VAR sp, fp, pc, lo, hi, nlo, nhi, olo, ohi, delta, a, w, used: S.ADDRESS;
			size, frame, newSize, oldSize, i: INTEGER; inL, inE: BOOLEAN; c0, c1: LONGINT;
			name: Kernel.Utf8Name;
	BEGIN
		IF mode = off THEN RETURN FALSE END;
		c0 := Tsc();
		S.GET(regs + 8 * rRsp, sp); S.GET(regs + 8 * rRbp, fp); S.GET(regs + 8 * rRip, pc);
		Tasks.StackBounds(t, lo, hi);
		IF (sp < lo) OR (sp >= hi) OR (Kernel.stkLo # lo + Tasks.StackReserve(d)) THEN RETURN FALSE END;
		IF Pinned(sp, fp, pc, hi) THEN INC(pins); RETURN FALSE END;
		S.GET(pc - 6, frame);	(* cmp r11, imm32: запас + кадр *)
		used := hi - sp; size := SHORT(hi - lo); newSize := 2 * size;
		WHILE newSize - used < frame + Tasks.StackReserve(d) + 4096 DO newSize := 2 * newSize END;
		IF newSize > d.max THEN INC(capped); RETURN FALSE END;
		c1 := Tsc(); cycWalk := cycWalk + c1 - c0;
		IF compare OR (mode = exact) THEN
			CollectExact(fp, pc, lo, hi);
			IF nPos > maxPos THEN INC(pins); name := "<maps>"; CountPin(name); RETURN FALSE END
		END;
		IF ~Tasks.ReplaceStack(t, newSize, olo, ohi, nlo, nhi, oldSize) THEN RETURN FALSE END;
		cycAlloc := cycAlloc + Tsc() - c1;
		ASSERT((olo = lo) & (ohi = hi), 100);
		delta := nhi - hi;
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
		Tasks.ReleaseStack(t, oldSize, lo, hi);
		prevLo := lo; prevHi := hi; lastLo := nlo; lastHi := nhi;
		INC(moves); cycTotal := cycTotal + Tsc() - c0;
		RETURN TRUE
	END Move;

	(* --- поставщик ------------------------------------------------------------ *)

	PROCEDURE ClassOf (size: INTEGER): INTEGER;
	(* Пост: k — наименьший класс с 2^k >= size, k >= minLog; maxLog + 1 — больше наибольшего. *)
		VAR k: INTEGER;
	BEGIN
		k := minLog; WHILE (k <= maxLog) & (S.LSH(1, k) < size) DO INC(k) END;
		RETURN k
	END ClassOf;

	PROCEDURE (d: GrowDir) NewStack- (size: INTEGER; OUT lo, hi: S.ADDRESS): BOOLEAN;
	(* Стек класса 2^k >= size из плиты класса; плита — slabBytes или один стек, если он больше.
	   Без кучи: зовётся и из Move. *)
		VAR k, sz, n: INTEGER; a, b: S.ADDRESS;
	BEGIN
		lo := 0; hi := 0;
		IF size < Tasks.StackReserve(d) + 2048 THEN size := Tasks.StackReserve(d) + 2048 END;
		k := ClassOf(size); IF k > maxLog THEN RETURN FALSE END;
		sz := S.LSH(1, k);
		IF d.next[k] = d.end[k] THEN
			IF d.nSlabs = maxSlabs THEN RETURN FALSE END;
			n := slabBytes DIV sz; IF n < 1 THEN n := 1 END;
			IF ~Kernel.AllocStack(sz * n, a, b) THEN RETURN FALSE END;
			d.slabLo[d.nSlabs] := a; d.slabHi[d.nSlabs] := b; INC(d.nSlabs);
			d.next[k] := a; d.end[k] := a + sz * n
		END;
		lo := d.next[k]; hi := lo + sz; d.next[k] := hi;
		RETURN TRUE
	END NewStack;

	PROCEDURE (d: GrowDir) FreeStack- (lo, hi: S.ADDRESS);
	BEGIN
		HALT(100)	(* keep: Tasks не возвращает стеки плит по одному *)
	END FreeStack;

	PROCEDURE (d: GrowDir) Drain-;
		VAR k: INTEGER;
	BEGIN
		WHILE d.nSlabs > 0 DO DEC(d.nSlabs); Kernel.FreeStack(d.slabLo[d.nSlabs], d.slabHi[d.nSlabs]) END;
		FOR k := 0 TO maxLog DO d.next[k] := 0; d.end[k] := 0 END
	END Drain;

	PROCEDURE (d: GrowDir) Grow- (t: Tasks.Task; regs: S.ADDRESS): BOOLEAN;
	BEGIN
		RETURN Move(d, t, regs)
	END Grow;

	PROCEDURE NewGrowDir* (reserve, max: INTEGER): GrowDir;
	(** Поставщик растущих стеков. Пред: reserve >= 8 (20), max — степень двойки от 4 КБ до
		64 МБ (21). Корзины пула всех размеров роста готовятся здесь: Move не выделяет память. *)
		VAR d: GrowDir; k: INTEGER;
	BEGIN
		ASSERT(reserve >= 8, 20);
		k := ClassOf(max); ASSERT((k <= maxLog) & (S.LSH(1, k) = max), 21);
		NEW(d); Tasks.InitStackDir(d, reserve, TRUE, TRUE); d.max := max;
		FOR k := minLog TO ClassOf(max) DO Tasks.PrepareBin(d, S.LSH(1, k)) END;
		RETURN d
	END NewGrowDir;

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
		Console.WriteStr(" capped="); Int(capped);
		Console.WriteStr(" procPins="); Int(nProcPins);
		Console.WriteStr(" bytes="); Int(bytesCopied); Console.WriteStr(" both="); Int(both);
		Console.WriteStr(" looseOnly="); Int(looseOnly); Console.WriteStr(" exactOnly="); Int(exactOnly);
		Console.WriteStr(" regsLooseOnly="); Int(looseOnlyRegs); Console.WriteStr(" try="); Int(mangled);
		Console.WriteStr(" unaligned4="); Int(unaligned);
		PinReport(s); Console.WriteStr(" pinned: "); Console.WriteStr(s); Console.WriteLn
	END Report;

	PROCEDURE Dir* (): GrowDir;
	(** Поставщик растущих стеков этого модуля (growReserve, growMax). *)
	BEGIN
		IF dir = NIL THEN dir := NewGrowDir(growReserve, growMax) END;
		RETURN dir
	END Dir;

	PROCEDURE SetMode* (m: INTEGER);
	(** off, exact или loose. Не off — новые задачи берут стеки у Dir(), прежний поставщик
		запомнен; off — он возвращён (живые задачи вернут стеки тому, кто их выдал). *)
	BEGIN
		IF (m # off) & (Tasks.StackDirOf() # Dir()) THEN prevDir := Tasks.StackDirOf(); Tasks.SetStackDir(Dir())
		ELSIF (m = off) & (prevDir # NIL) THEN Tasks.SetStackDir(prevDir); prevDir := NIL
		END;
		mode := m;
		BuildCode
	END SetMode;

	PROCEDURE UseGrowingStacks*;
	(** Команда: новые задачи — с растущим стеком: старт growStart из плотной плиты, рост
		вдвое по трапу 137 до growMax, точный способ, правило модулей со списком доверенных. *)
	BEGIN
		moduleRule := TRUE; trustRule := TRUE; compare := FALSE;
		SetMode(exact);
		Tasks.SetDefaultStackSize(growStart)
	END UseGrowingStacks;

	PROCEDURE ProbeExact*;
	(** Прогон тестов: UseGrowingStacks и сверка с нестрогим способом (счёт расхождений). *)
	BEGIN
		UseGrowingStacks; compare := TRUE
	END ProbeExact;

	PROCEDURE ProbeLoose*;
	(** Диагностика: нестрогий способ без правила модулей (закрепляют только кадры C). *)
	BEGIN
		UseGrowingStacks; moduleRule := FALSE; compare := TRUE; SetMode(loose)
	END ProbeLoose;

	PROCEDURE ProbeNoTrust*;
	(** Точный способ, правило модулей без списка доверенных (замер «до списка»). *)
	BEGIN
		UseGrowingStacks; trustRule := FALSE; BuildCode
	END ProbeNoTrust;

	PROCEDURE Exact*;
	BEGIN SetMode(exact) END Exact;

	PROCEDURE Loose*;
	BEGIN SetMode(loose) END Loose;

	PROCEDURE Off*;
	BEGIN SetMode(off) END Off;

BEGIN
	moduleRule := TRUE; trustRule := TRUE; compare := TRUE
CLOSE
	IF (mode # off) OR (moves + pins > 0) THEN Report END;
	IF prevDir # NIL THEN Tasks.SetStackDir(prevDir) END
END Stackmove.
