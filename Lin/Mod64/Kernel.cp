MODULE LinKernel;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems, A. Shiryaev, A. Dmitriev"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	(* Alexander Shiryaev, 2012.11, 2013.10, 2016.11, 2018.06, 2019.06, 2023.08
		Linux Kernel
		Based on 1.7.1-a1 (build number 710) Windows Kernel
		Some parts taken from OpenBUGS linKernel 

		Most Windows-specific code removed
		Some Windows-specific code commented and marked red
		Windows COM-specific code re-marked from green to gray
		Linux(/OpenBSD,/FreeBSD,/NetBSD)-specific code marked green

		TODO:
			handle stack overflow exceptions
			Quit from TrapHook
	*)

	IMPORT S := SYSTEM, Libc := LinLibc, LibW := LinLibW, Dl := LinDl, Rt := LinRt, Utf,
		Kernel;

	CONST
		clusterSize = 65536 * 4;	(* heap cluster size *)
		monoClusterSize = 1536 * 100000H;
		sigStackSize = MAX(Libc.SIGSTKSZ, 1024 * 1024);	(* 64-бит: 64К не хватало — трап-вьювер (DevDebug) внутри обработчика переполнял alt-стек, nested SIGSEGV убивал процесс *)
		
		(* constants for the message boxes same for *)
		mbClose = -1; mbOk = 0; mbCancel =1;
		mbRetry = 2; mbIgnore = 3; mbYes = 4; mbNo = 5;
		
		trapReturn = 1; (* Return value for sigsetjmp given from siglongjmp *)

	TYPE
		Platform = RECORD (Kernel.Platform) END;
		TryContext = POINTER TO Libc.sigjmp_buf;

		ErrLog = RECORD
			string: PROCEDURE (IN string: ARRAY OF CHAR);
			ln: PROCEDURE
		END;

		
	VAR
		baseStack: S.ADDRESS;	(* received from Kernel thru InitPlatform *)
		err, sig: INTEGER;
		pc, sp, fp, val: S.ADDRESS;	(* 64-бит: адреса ловушки не влезают в INTEGER *)
		argc-: INTEGER;
		argv-: Libc.StrArray;

		restart: Kernel.Command;
		
		sigStack: Libc.PtrVoid;
		pageSize: INTEGER;

		loopContext: Libc.sigjmp_buf; (* trap return context, if no Kernel.Try has been used. *)
		currentTryContext: POINTER TO Libc.sigjmp_buf; (* trap return context, if Kernel.Try has been used. *)
		isReadableContext: Libc.sigjmp_buf; (* for IsReadable *)
		isReadableCheck: BOOLEAN;

		stubPage: S.ADDRESS; stubLeft: INTEGER;	(* dll-трамполины (StubFor), bump-аллокация по 16 байт *)
		
		trap: RECORD
			depth: INTEGER;	(** counts recursive traps *)
			top	(** fp at trap - top of main stack during a trap *)
			: S.ADDRESS;
		END;
		
		monoClusterMem: BOOLEAN;
		
		passing,
		recursive: BOOLEAN;

		messageBox: PROCEDURE (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER;
		log: ErrLog; 
		platform: RECORD
			tag: LONGINT;	(* heap-tag при obj-8, 8 байт (x86-64) *)
			p: Platform
		END;
	
	(* debug logging functions *)

	PROCEDURE SetErr* (string: PROCEDURE(IN msg: ARRAY OF CHAR); ln: PROCEDURE);
	BEGIN log.string := string; log.ln := ln
	END SetErr;
	
	PROCEDURE (VAR e: ErrLog) String (IN str: ARRAY OF CHAR), NEW;
	BEGIN e.string(str)
	END String;
	
	PROCEDURE (VAR e: ErrLog) Ln, NEW; BEGIN e.ln END Ln;
	
	PROCEDURE (VAR e: ErrLog) Int (x: LONGINT), NEW;
		VAR a: ARRAY 25 OF CHAR;
		CONST max = 25;
		VAR b: ARRAY max OF CHAR; i, j: INTEGER; y: LONGINT;
	BEGIN
		IF x < 0 THEN x := -x; a[0] := '-'; j := 1 ELSIF x > 0 THEN j := 0 ELSE j := 1; a[0] := '0' END;
		i := 0;
		WHILE x # 0 DO y := x MOD 10; b[i] := SHORT(CHR(y + ORD('0'))); x := x DIV 10; INC(i) END;
		DEC(i); WHILE (i >= 0) & (j < max) DO a[j] := b[i]; INC(j); DEC(i) END;
		ASSERT(j < max, 20);
		a[j] := 0X; 
		e.String(a)
	END Int;
	
	PROCEDURE (VAR e: ErrLog) Hex (x: LONGINT), NEW;
		VAR i, xx, yy: INTEGER; y: LONGINT; buf: ARRAY 17 OF CHAR;
	BEGIN 
		IF (MIN(INTEGER) < x) & (x < MAX(INTEGER)) THEN 
			i := 7; xx := SHORT(x); WHILE i >= 0 DO
				yy := xx MOD 16; xx := xx DIV 16;
				IF yy > 9 THEN yy := yy + (ORD("A") - ORD("0") - 10) END;
				buf[i] := CHR(yy + ORD("0")); DEC(i)
			END;
			buf[8] := 0X
		ELSE 
			i := 15; WHILE i >= 0 DO
				y := x MOD 16; x := x DIV 16;
				IF y > 9 THEN y := y + (ORD("A") - ORD("0") - 10) END;
				buf[i] := CHR(y + ORD("0")); DEC(i)
			END;
			buf[16] := 0X
		END;
		e.String(buf)
	END Hex;

	(* end of debug logging functions *)

	PROCEDURE (VAR p: Platform) Time (): LONGINT;
		VAR res: INTEGER; tp: Rt.timespec_t;
	BEGIN
		res := Rt.clock_gettime(Rt.CLOCK_MONOTONIC, tp);
		ASSERT(res = 0);
		RETURN tp.tv_sec * 1000 + tp.tv_nsec DIV 1000000
	END Time;

	PROCEDURE (VAR p: Platform) PageSize (): INTEGER;
	BEGIN
		RETURN pageSize
	END PageSize;

	PROCEDURE (VAR p: Platform) ProtectMemRead (addr: S.ADDRESS; size: INTEGER): BOOLEAN;
	BEGIN
		RETURN Libc.mprotect(addr, size, Libc.PROT_READ) = 0
	END ProtectMemRead;

	PROCEDURE (VAR p: Platform) ProtectMemExec (addr: S.ADDRESS; size: INTEGER): BOOLEAN;
	BEGIN
		RETURN Libc.mprotect(addr, size, Libc.PROT_READ + Libc.PROT_EXEC) = 0
	END ProtectMemExec;

	PROCEDURE (VAR p: Platform) AllocateModMem (size: INTEGER; OUT addr: S.ADDRESS; forCode: BOOLEAN);
		CONST protection = Libc.PROT_READ + Libc.PROT_WRITE;
			flags = Libc.MAP_PRIVATE + Libc.MAP_ANON + Libc.MAP_32BIT;	(* addr уходит в INTEGER (Kernel.AllocModMem) — нужно < 4 ГБ *)
	BEGIN
		addr := SHORT(Libc.mmap(0, size, protection, flags, -1, 0));
		IF addr = Libc.MAP_FAILED THEN addr := 0 END
	END AllocateModMem;

	PROCEDURE (VAR p: Platform) AllocateClusterMem* (VAR size: S.ADDRESS; OUT base, capacity: S.ADDRESS);
	(** Allocate at least size bytes of memory for a heap cluster; return base, actually allocated capacity and committed size
	Post: base MOD 16 = 0, 60 *)
		CONST protection = Libc.PROT_READ + Libc.PROT_WRITE; 
			flags = Libc.MAP_PRIVATE + Libc.MAP_ANON;	(* 64-бит: куча может быть выше 4 ГБ — GC ходит полными LONGINT-адресами *)
		VAR s: S.ADDRESS;
	BEGIN
		(* IF platform.p.Debug() THEN log.String("LinKernel.AllocateClusterMem "); log.Int(size) END; *)
		IF monoClusterMem THEN s := MAX(size, monoClusterSize) ELSE s := MAX(size, clusterSize) END;
		base := Libc.mmap(0, s, protection, flags, -1, 0);
		IF (base = Libc.MAP_FAILED) & (s # size) THEN
			s := size;
			base := Libc.mmap(0, s, protection, flags, -1, 0) 
		END;
		IF base = Libc.MAP_FAILED THEN base := 0
		ELSE size := s; capacity := s
		END;
		(* IF platform.p.Debug() THEN log.String(' '); log.Int(size); log.Ln END *)
	END AllocateClusterMem;

	PROCEDURE Erase (adr: S.ADDRESS; words: INTEGER);
		TYPE IntArr = POINTER TO ARRAY [untagged] OF INTEGER;
		VAR p: IntArr; i: INTEGER;
	BEGIN
		p := S.VAL(IntArr, adr);
		FOR i := 0 TO words - 1 DO p[i] := 0 END
	END Erase;

	PROCEDURE (VAR p: Platform) DeallocateModMem (adr: S.ADDRESS; size: INTEGER; release: BOOLEAN);
		VAR res: INTEGER;
	BEGIN
		ASSERT(size MOD 4 = 0, 100);
		IF ~release THEN
			Erase(adr, size DIV 4);
			res := Libc.mprotect(adr, size, Libc.PROT_NONE);
			ASSERT(res = 0, 101)
		END;
		res := Libc.munmap(adr, size);
		ASSERT(res = 0, 102)
	END DeallocateModMem;

	PROCEDURE (VAR p: Platform) DeallocateClusterMem (base: S.ADDRESS; size: INTEGER);
		VAR res: INTEGER;
	BEGIN
		ASSERT(size MOD 4 = 0, 100);
		res := Libc.munmap(base, size);
		ASSERT(res = 0, 102)
	END DeallocateClusterMem;

	PROCEDURE (VAR p: Platform) AllocateStackMem (size: INTEGER; OUT lo, hi: S.ADDRESS);
	(* Стек задачи: mmap + одна сторожевая страница PROT_NONE снизу.
	   Страницы физически выделяются по касанию, поэтому 1000 задач стоят
	   адресного пространства, а не памяти. *)
		CONST protection = Libc.PROT_READ + Libc.PROT_WRITE;
			flags = Libc.MAP_PRIVATE + Libc.MAP_ANON;
		VAR guard, total: S.ADDRESS; base: S.ADDRESS; res: INTEGER;
	BEGIN
		guard := p.PageSize();
		total := ((size + guard - 1) DIV guard) * guard + guard;
		base := Libc.mmap(0, total, protection, flags, -1, 0);
		IF base = Libc.MAP_FAILED THEN lo := 0; hi := 0; RETURN END;
		res := Libc.mprotect(base, guard, Libc.PROT_NONE);
		IF res # 0 THEN
			res := Libc.munmap(base, total);
			lo := 0; hi := 0; RETURN
		END;
		lo := base + guard; hi := base + total
	END AllocateStackMem;

	PROCEDURE (VAR p: Platform) DeallocateStackMem (lo, hi: S.ADDRESS);
		VAR guard, res: INTEGER;
	BEGIN
		IF lo # 0 THEN
			guard := p.PageSize();
			res := Libc.munmap(lo - guard, hi - lo + guard);
			ASSERT(res = 0, 103)
		END
	END DeallocateStackMem;

	PROCEDURE (VAR p: Platform) LoadDll (IN name: ARRAY OF CHAR; VAR ok: BOOLEAN);
		VAR h: Dl.HANDLE; res: INTEGER; s: ARRAY Kernel.nameLen OF SHORTCHAR;
	BEGIN
		ok := FALSE;
		Utf.StringToUtf8(name, s, res); ASSERT(res = 0);
		h := Dl.dlopen(s, Dl.RTLD_LAZY +  Dl.RTLD_GLOBAL);
		IF h # Dl.NULL THEN ok := TRUE END
	END LoadDll;

	PROCEDURE StubFor (target: LONGINT): INTEGER;
		(* трамплин mov rax,imm64; jmp rax в MAP_32BIT-памяти: ripBased disp32 из модулей
		   (< 4 ГБ) не дотягивается до libc (~0x7f...). Как StubFor в bbrun64.c. *)
		CONST stubLen = 16;
		VAR p, p0: S.ADDRESS;
	BEGIN
		IF stubLeft < stubLen THEN
			stubPage := Libc.mmap(0, 4096, Libc.PROT_READ + Libc.PROT_WRITE + Libc.PROT_EXEC,
				Libc.MAP_PRIVATE + Libc.MAP_ANON + Libc.MAP_32BIT, -1, 0);
			ASSERT(stubPage # Libc.MAP_FAILED);
			stubLeft := 4096
		END;
		p0 := stubPage; INC(stubPage, stubLen); DEC(stubLeft, stubLen);
		p := p0;
		S.PUT(p, SHORT(48H)); INC(p); S.PUT(p, SHORT(0B8H)); INC(p);	(* mov rax, imm64 *)
		S.PUT(p, target); INC(p, 8);
		S.PUT(p, SHORT(0FFH)); INC(p); S.PUT(p, SHORT(0E0H));	(* jmp rax *)
		RETURN SHORT(p0)	(* НАЧАЛО стаба! (было p = p0+11 — вызов улетал на байт E0) *)
	END StubFor;

	PROCEDURE (VAR p: Platform) ThisDllObj (mode, _: INTEGER; IN dll, name: ARRAY OF CHAR): INTEGER;
		CONST mVar = 3; mProc = 4; (* sync with Kernel!! *)
		VAR ad: INTEGER; h: Dl.HANDLE; res: INTEGER; s: ARRAY Kernel.nameLen OF SHORTCHAR;
	BEGIN
		ad := 0;
		IF mode IN {mVar, mProc} THEN
			Utf.StringToUtf8(dll, s, res); ASSERT(res = 0);
			h := Dl.dlopen(s, Dl.RTLD_LAZY + Dl.RTLD_GLOBAL);
			IF h # Dl.NULL THEN Utf.StringToUtf8(name, s, res); ASSERT(res = 0);
				ad := StubFor(Dl.dlsym(h, s))
			END
		END;
		RETURN ad
	END ThisDllObj;

	PROCEDURE (VAR p: Platform) Start (cmd: Kernel.Command);	(* extension for Kernel.Start *)
		VAR _: INTEGER;
	BEGIN (* not sure this is correct.. *)
		ASSERT(cmd # NIL, 20);
		S.GETREG(4, baseStack);	(* SP=4; 64-бит: реальная база стека — на уровне главного цикла.
			Значение из Setup (SP на момент SetPlatform) слишком глубокое: фреймы приложения
			оказывались ВЫШЕ baseStack -> InActivationStack = FALSE -> walker резал стек *)
		restart := cmd;
		_ := Libc.sigsetjmp(loopContext, Libc.TRUE);
		IF restart # cmd THEN log.String("LinKernel.Start: restart # cmd"); log.Ln END;
		restart (*cmd()*)
	END Start;

	(* adimetrius, 2021.05 *)

	PROCEDURE Below (a, b: S.ADDRESS): BOOLEAN;
	BEGIN
		(* 64-бит: чистый CP; был x86-32 [code] (CMP EAX,[ESP] ломал стек на amd64).
		   unsigned '<': при одинаковых знаках — обычное сравнение; иначе
		   отрицательный (старший бит) беззнаково больше *)
		IF (a < 0) = (b < 0) THEN RETURN a < b ELSE RETURN b < 0 END
	END Below;
	(** Below is unsigned '<' (less) - to be used with S.ADDRESS *)
	
	PROCEDURE (VAR p: Platform) InActivationStack (pc: S.ADDRESS): BOOLEAN;
		VAR inMainStack, inSignalStack: BOOLEAN; hi: S.ADDRESS;
	BEGIN
		hi := baseStack;	(* 64-бит: без SHORT *)
		inMainStack := ((trap.top = pc) OR Below(trap.top, pc)) & Below(pc, hi);
		inSignalStack := ((sigStack = pc) OR Below(sigStack, pc)) & Below(pc, sigStack + sigStackSize);
		RETURN inMainStack OR inSignalStack OR Kernel.InRegisteredStack(pc)
	END InActivationStack;

	PROCEDURE (VAR p: Platform) GetTryContext (): S.ADDRESS;
	(* Контекст Try текущего стека — для Kernel.SwitchSave/SwitchRestore. *)
	BEGIN
		RETURN S.VAL(S.ADDRESS, currentTryContext)
	END GetTryContext;

	PROCEDURE (VAR p: Platform) SetTryContext (ctx: S.ADDRESS);
	BEGIN
		currentTryContext := S.VAL(TryContext, ctx)
	END SetTryContext;

	(* Alexander Shiryaev, 2012.10: Linux: can be implemented through mincore/madvise *)
	(* This procedure can be called from TrapHandler also *)
	PROCEDURE (VAR p: Platform) IsReadable (from, to: S.ADDRESS): BOOLEAN;
		(* check wether memory between from (incl.) and to (excl.) may be read *)
		VAR res: BOOLEAN; res1: INTEGER;
			x: SHORTCHAR;
			mask, omask: Libc.sigset_t;
	BEGIN
		(* save old sigmask and unblock SIGSEGV *)
			res1 := Libc.sigemptyset(S.ADR(mask));
			ASSERT(res1 = 0, 100);
			res1 := Libc.sigaddset(S.ADR(mask), Libc.SIGSEGV);
			ASSERT(res1 = 0, 101);
			res1 := Libc.sigprocmask(Libc.SIG_UNBLOCK, S.ADR(mask), S.ADR(omask));
			ASSERT(res1 = 0, 102);

		res := FALSE;
		res1 := Libc.sigsetjmp(isReadableContext, Libc.TRUE);
		IF res1 = 0 THEN
			isReadableCheck := TRUE;
			(* read memory *)
			REPEAT
				S.GET(from, x);
				INC(from)
			UNTIL from = to;
			res := TRUE
		ELSE
			ASSERT(res1 = 1, 103)
		END;
		isReadableCheck := FALSE;

		(* restore saved sigmask *)
			res1 := Libc.sigprocmask(Libc.SIG_SETMASK, S.ADR(omask), NIL);
			ASSERT(res1 = 0, 104);

		RETURN res
	END IsReadable;
	
	PROCEDURE Try (h: Kernel.TryHandler; a, b, c: S.ADDRESS);	
		VAR res: INTEGER; context: Libc.sigjmp_buf; oldContext: POINTER TO Libc.sigjmp_buf;
	BEGIN
		oldContext := currentTryContext;
		res := Libc.sigsetjmp(context, Libc.TRUE);
		currentTryContext := S.ADR(context);
		IF res = 0 THEN (* first time around *)
			h(a, b, c);
		ELSIF res = trapReturn THEN  (* after a trap *)
		ELSE
			HALT(100)
		END;
		currentTryContext := oldContext;
	END Try;

	PROCEDURE SigToErr (pc: S.ADDRESS; sig, fpe: INTEGER; VAR err: INTEGER; VAR val: S.ADDRESS);
	(** Translate Linux signal number sig and fpe error code into BB err number and, if applicable - program trap number val. pc is only relevant in case of SIGILL to retrieve trap/runtime error number *)
		VAR limits: Libc.rlimit; x: INTEGER;
	BEGIN
		CASE sig OF
			Libc.SIGINT: 
				err := 200; (* keyboardInterrupt*) (* Interrupt (ANSI). *)
			| Libc.SIGILL: (* Illegal instruction (ANSI). *)
				err := 202; val := 0;
				IF Kernel.IsReadable(pc, pc + 4) THEN
					S.GET(pc, x); val := x;
					IF x MOD 100H = 8DH THEN	(* lea reg,reg *)
						IF x DIV 100H MOD 100H = 0F0H THEN
							err := x DIV 10000H MOD 100H	(* trap *)
						ELSIF x DIV 1000H MOD 10H = 0EH THEN
							err := 128 + x DIV 100H MOD 10H	(* run time error *)
						END
					END
				END
			| Libc.SIGFPE: 
				CASE fpe OF
					0: (* TODO: ?????? *)
						(*
						IF siginfo.si_int = 8 THEN
							err := 139
						ELSIF siginfo.si_int = 0 THEN
							err := 143
						END
						*)
					| Libc.FPE_INTDIV: err := 139 (* Integer divide by zero.  *)
					| Libc.FPE_INTOVF: err := 138 (* Integer overflow.  *)
					| Libc.FPE_FLTDIV: err := 140 (* Floating point divide by zero.  *)
					| Libc.FPE_FLTOVF: err := 142 (* Floating point overflow.  *)
					| Libc.FPE_FLTUND: err := 141 (* Floating point underflow.  *)
					| Libc.FPE_FLTRES: err := 143 (* Floating point inexact result.  *)
					| Libc.FPE_FLTINV: err := 143 (* Floating point invalid operation.  *)
					| Libc.FPE_FLTSUB: err := 134 (* Subscript out of range.  *)
				ELSE
				END
			| Libc.SIGBUS:
				err := 203;
				IF fpe <= 0 THEN val := 0 END (* user-generated signals have no fault address *)
			| Libc.SIGSEGV: (* Segmentation violation (ANSI). *) 
				err := Libc.getrlimit(Libc.RLIMIT_STACK, limits);
				IF sp - (S.VAL(LONGINT, argv) - limits.rlim_cur) < 16384  THEN err := 137 (* stack overflow *)
				ELSE err := 203
				END
		ELSE
		END
	END SigToErr;
	
	PROCEDURE (VAR p: Platform) PrintHeaderInfo;
	BEGIN 
		CASE sig OF	 (* work in progress: more signals can be listed here as need arises *)
		| 4: log.String("SIGILL: TRAP ")
		| 11: log.String("SIGSEGV ")
		| 2: log.String("SIGINT ")
		ELSE IF sig > 0 THEN log.Int(sig) END
		END
	END PrintHeaderInfo;
	
	PROCEDURE (VAR p: Platform) GetTrapInfo (VAR opc, osp, ofp: S.ADDRESS; VAR oerr: INTEGER; VAR oval: S.ADDRESS; OUT orecursive: BOOLEAN);
	BEGIN 
		IF passing THEN opc := pc; osp := sp; ofp := fp; oerr := err; oval := val; orecursive := recursive
		ELSE opc := 0
		END;
		passing := FALSE
	END GetTrapInfo;
	
	PROCEDURE [ccall] HandleTrap (sig: INTEGER; siginfo: Libc.Ptrsiginfo_t; context: Libc.Ptrucontext_t);
		VAR inSignalStack: BOOLEAN; oldpc, oldfp: S.ADDRESS;
	BEGIN
		IF isReadableCheck THEN
			isReadableCheck := FALSE;	(* probe-read faulted: тихо возвращаем FALSE *)
			Libc.siglongjmp(isReadableContext, 1)
		END;

		oldpc := pc;

		(* Linux *)
			sp := context.uc_mcontext.gregs[Libc.REG_ESP];	(* 64-бит: без SHORT *)
			fp := context.uc_mcontext.gregs[Libc.REG_EBP];
			pc := context.uc_mcontext.gregs[Libc.REG_EIP];
			val := siginfo._sifields._sigfault.si_addr;

		INC(trap.depth);
		inSignalStack := ((sigStack = fp) OR Below(sigStack, fp)) & Below(fp, sigStack + sigStackSize);
		oldfp := trap.top;
		IF ~inSignalStack OR (trap.top = -1) THEN trap.top := fp END;
		IF (pc = oldpc) & (trap.depth > 1) THEN
			log.String("Recursive trap at the same PC="); log.Hex(oldpc); 
			log.String(". Aborting immediately."); log.Ln;
			Libc.exit(2)
		ELSE
			
			SigToErr(pc, sig, siginfo.si_code, err, val);
			passing := TRUE;
			Kernel.HandleTrap
		END;
		DEC(trap.depth);
		trap.top := oldfp;
		IF currentTryContext # NIL THEN (* Try failed *)
			Libc.siglongjmp(currentTryContext, trapReturn)
		ELSE
			IF restart # NIL (* can this be replaced with a check of loopContext? *)THEN (* Start failed *)	(* this might belond to universal Kernel *)
				Libc.siglongjmp(loopContext, trapReturn)
			END;
			Kernel.Quit(1); (* FIXME *)
		END;
	END HandleTrap;

	PROCEDURE InitSignalStack;
		VAR sigstk: Libc.stack_t; errno, res: INTEGER;
	BEGIN
		sigstk.ss_sp := sigStack;
		sigstk.ss_size := sigStackSize;
		sigstk.ss_flags := {};
		res := Libc.sigaltstack(sigstk, NIL);
		IF res # 0 THEN
			log.String("ERROR: LinKernel.InitSignalStack: sigaltstack failed!"); log.Ln;
			S.GET(Libc.__errno_location(), errno);
			log.Int(errno); log.Ln;
			Libc.exit(1)
		END
	END InitSignalStack;

	PROCEDURE InstallSignals*;
		(* Restore BlackBox handlers after GTK initialization; leave the signal stack unchanged. *)
		VAR sa, old: Libc.sigaction_t; res: INTEGER;
	BEGIN
		sa.sa_sigaction := HandleTrap;
		res := Libc.sigemptyset(S.ADR(sa.sa_mask));
		sa.sa_flags :=  Libc.SA_SIGINFO + Libc.SA_NODEFER ; (* HandleTrap takes three arguments *)
		(* Do not intercept service signals used by native libraries.
		   Preserve BlackBox interrupt, termination and broken-pipe handling. *)
		IF Libc.sigaction(Libc.SIGINT, sa, NIL) # 0 THEN log.String("LinKernel: sigaction SIGINT failed"); log.Ln END;
		IF Libc.sigaction(Libc.SIGILL, sa, NIL) # 0 THEN log.String("LinKernel: sigaction SIGILL failed"); log.Ln END;
		IF Libc.sigaction(Libc.SIGFPE, sa, NIL) # 0 THEN log.String("LinKernel: sigaction SIGFPE failed"); log.Ln END;
		IF Libc.sigaction(Libc.SIGBUS, sa, NIL) # 0 THEN log.String("LinKernel: sigaction SIGBUS failed"); log.Ln END;
		IF Libc.sigaction(Libc.SIGPIPE, sa, NIL) # 0 THEN log.String("LinKernel: sigaction SIGPIPE failed"); log.Ln END;
		IF Libc.sigaction(Libc.SIGTERM, sa, NIL) # 0 THEN log.String("LinKernel: sigaction SIGTERM failed"); log.Ln END;

		(* SIGSEGV to be executed on an alternative stack *)
		sa.sa_flags := sa.sa_flags + Libc.SA_ONSTACK;
		IF Libc.sigaction(Libc.SIGSEGV, sa, NIL) # 0 THEN
			log.String("LinKernel: failed to install SIGSEGV handler"); log.Ln
		END;

		(* set SIG_IGN for SIGCHLD
		Because SIG_IGN is set for SIGCHILD, BlackBox doesn't have to waitpid() for it's children to terminate after fork()ing. If at least one of these is NOT done (no SIG_IGN and no waitpid()), the child processes become zombie. Cf. HostDialog.RunExternal, http://thinkiii.blogspot.com/2009/12/double-fork-to-avoid-zombie-process.html *)
		sa.sa_sigaction := S.VAL(Libc.sa_sigaction_t, Libc.SIG_IGN);
		sa.sa_flags := Libc.SA_NOCLDWAIT;
		IF Libc.sigaction(Libc.SIGCHLD, sa, old) # 0 THEN
			log.String("LinKernel: failed to install SIGCHLD"); log.Ln
		END
	END InstallSignals;

	PROCEDURE (VAR p: Platform) GetLoadTime (OUT loadTime: ARRAY OF SHORTINT);
		VAR t: Libc.time_t; tm: Libc.tm;
	BEGIN
		t := Libc.time(NIL);
		tm := Libc.localtime(t);
		loadTime[0] := SHORT(tm.tm_year + 1900); (* Linux counts years from 1900 but BlackBox from 0000 *)
		loadTime[1] := SHORT(tm.tm_mon + 1) (* Linux month range 0-11 but BB month range 1-12 *);
		loadTime[2] := SHORT(tm.tm_mday);
		loadTime[3] := SHORT(tm.tm_hour);
		loadTime[4] := SHORT(tm.tm_min);
		loadTime[5] := SHORT(tm.tm_sec); 
		tm := NIL;
	END GetLoadTime;

	PROCEDURE (VAR p: Platform) Terminate (exitCode: INTEGER);
		VAR _: INTEGER;
	BEGIN _ := Libc.fflush(0); Libc.exit(exitCode)
	END Terminate;

	PROCEDURE (VAR p: Platform) Commit (base, committed, capacity, request: S.ADDRESS; OUT granted: S.ADDRESS);
	BEGIN HALT(20 (* should never be called*))
	END Commit;

	PROCEDURE SetMsgBoxProc* (
		p: PROCEDURE (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER);
	BEGIN
		messageBox := p
	END SetMsgBoxProc;

	PROCEDURE ErrLn;
		VAR res_: INTEGER;
	BEGIN
		res_ := Libc.printf0(0AX); res_ := Libc.fflush(0) 
	END ErrLn;

	PROCEDURE ErrString (IN str: ARRAY OF CHAR);
		CONST max = 1024;
		VAR ss: ARRAY max + 2 OF SHORTCHAR; i, j, res_, len: INTEGER;
	BEGIN
		len := LEN(str); ss[max + 1] := 0X;
		i := 0; j := 0;
		WHILE (i < len) & (str[i] # 0X) DO ss[j] := SHORT(str[i]); INC(i);
			IF j = max THEN res_ := Libc.printfs("%s", ss); j := 0 ELSE INC(j) END
		END;
		IF j > 0 THEN ss[j] := 0X; res_ := Libc.printfs("%s", ss) END
	END ErrString;

	PROCEDURE (VAR p: Platform) FatalError (id: INTEGER; IN str: ARRAY OF CHAR);
		VAR res_: INTEGER; title: ARRAY 16 OF CHAR; 
	BEGIN
		IF id = Kernel.trapMessage THEN
			title := "TRAP REPORT";
			IF messageBox # NIL THEN
				res_ := messageBox(title$, str, {mbOk});
			ELSE
				ErrString(str)
			END
		ELSE
			title := "Error xy";
			title[6] := CHR(id DIV 10 + ORD("0"));
			title[7] := CHR(id MOD 10 + ORD("0"));
			IF messageBox # NIL THEN
				res_ := messageBox(title$, str, {mbOk});
			ELSE
				ErrString(" "); ErrLn;
				ErrString("****"); ErrLn;
				ErrString("* " + title); ErrLn;
				ErrString("* " + str); ErrLn;
				ErrString("****"); ErrLn
			END
		END;
		platform.p.Terminate(1)
	END FatalError;

	PROCEDURE (VAR p: Platform) Setup (stackBase: S.ADDRESS; opts: SET; OUT id: INTEGER; OUT eoln: Kernel.Eoln; VAR monoCluster: BOOLEAN; OUT gradualCommit: BOOLEAN; VAR physicalMemory: INTEGER; OUT try: PROCEDURE (h: Kernel.TryHandler; a, b, c: S.ADDRESS));
		CONST linux = 30; (* Dialog.linux *)
	BEGIN
		id := linux;
		p.Try := Try;
		p.String := ErrString;
		p.Ln := ErrLn;
		eoln := 0AX;
		gradualCommit := FALSE;
		baseStack := stackBase;
		(* forced to FALSE, because it's experimental for now *)
		monoCluster := FALSE;
		monoClusterMem := monoCluster;
		IF Kernel.bootInfo # NIL THEN
			argc := Kernel.bootInfo.argc;
			argv := Kernel.bootInfo.argv
		ELSE
			(* A bootstrap without BootInfo cannot provide process arguments. *)
				argc := 0;
				argv := NIL
		END
	END Setup;

	(*
		Reserve space for 80000000H cross border
		This space must not be accessed by GC

		ref.: https://forum.oberoncore.ru/viewtopic.php?f=134&t=6959#p118177
	*)
	PROCEDURE ReserveCrossBorder;
		CONST
			crossBorder = 80000000H;
			protection = Libc.PROT_NONE;
			flags = Libc.MAP_PRIVATE + Libc.MAP_ANON;
		VAR adr: Libc.PtrVoid;
			res: INTEGER;
	BEGIN
		adr := Libc.mmap(crossBorder - pageSize, pageSize, protection, flags, -1, 0);
		IF (adr # Libc.MAP_FAILED) & (adr # crossBorder - pageSize) THEN
			res := Libc.munmap(adr, pageSize)
		END;
		adr := Libc.mmap(crossBorder, pageSize, protection, flags, -1, 0);
		IF (adr # Libc.MAP_FAILED) & (adr # crossBorder) THEN
			res := Libc.munmap(adr, pageSize)
		END
	END ReserveCrossBorder;

	PROCEDURE Init;
		VAR sigStk: Kernel.Stack;
	BEGIN
		log.string := ErrString; log.ln := ErrLn;

		pageSize := SHORT(Libc.sysconf(Libc._SC_PAGESIZE));
		IF pageSize < 0 THEN
			log.String("ERROR: Kernel.Init: pageSize < 0!"); log.Ln;
			Libc.exit(1)
		END;

		ReserveCrossBorder;

		(* for sigaltstack *)
			sigStack := Libc.calloc(1, sigStackSize);
			IF sigStack = Libc.NULL THEN
				log.String("ERROR: Kernel.Init: calloc(1, sigStackSize) failed!"); log.Ln;
				Libc.exit(1)
			END;

		IF LibW.setlocale(LibW.LC_ALL, "") = NIL THEN
			log.String("LinKernel.Init: setlocale failed"); log.Ln
		END;

		isReadableCheck := FALSE;
		InitSignalStack;
		InstallSignals; (* init exception handling *)
		currentTryContext := NIL;
		trap.depth := 0; trap.top := -1; pc := 0;
		platform.tag := S.VAL(LONGINT, S.TYP(Platform));
		Kernel.SetPlatform(S.VAL(S.PTR, S.ADR(platform.p)));
		(* Сигнальный стек — в общем реестре: MarkLocals должен уметь
		   разобрать GC, запущенный в обработчике трапа. *)
		sigStk := Kernel.RegisterSignalStack(sigStack, sigStack + sigStackSize);
		Kernel.SetErr(ErrString, ErrLn);
	END Init;

BEGIN Init
END LinKernel.

Dev2Linker1.LinkElfExe Linux blackbox := Kernel$+ HostFiles HostGnome StdLoader

"Kernel.IsBelow(-1, 0)"			ObxTrap.Do		Kernel.Collect

CrossAnalyzer.Analyze



	(* Uses Linux SYSCALL *)
	PROCEDURE [code] SSpeak (IN msg: ARRAY  OF SHORTCHAR; len: INTEGER)
		5AH,					 	(* POP EDX; length of message *)
		89H, 0C1H, 			(* MOV ECX, EAX; address of message *)
		53H,						(* PUSH EBX *)
		0BBH, 1, 0, 0, 0, 	(* MOV EBX, 1; file descriptor 1 = stdout *)
		0B8H, 4, 0, 0, 0,	(* MOV EAX, 4; syscall function write *)
		0CDH, 80H,			(* INT 80h;	syscall *)
		5BH						(* POP EBX *)
		;
	
	PROCEDURE Speak (IN msg: ARRAY OF SHORTCHAR);
	BEGIN SSpeak(msg, LEN(msg$) + 1)
	END Speak;

	PROCEDURE [code] EmergencyExit 0B8H, 1, 0, 0, 0, 0BBH, 4EH, 0, 0, 0, 0CDH, 80H; 
		(* MOV EAX, 1; MOV EBX, 78; INT 80H *)

