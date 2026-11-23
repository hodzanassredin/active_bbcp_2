MODULE WinKernel;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, Anton Dmitriev"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT S := SYSTEM, COM, Kernel, Utf, 
		WinApi, WinOle;

	CONST
		CX = 1; SP = 4; FP = 5; ML = 3;	(* register which holds the module list at program start *)
		keyboardInterrupt = 200;	(* error number for keyboard interrupt *)
		monoClusterSize = 1024 * 100000H; (* 1.0 GByte, see note on Stack Size *)
		multiClusterCapacity = 32 * 1024 * 1024;
		
		(* constants for the message boxes *)
		mbClose = -1; mbOk = 0; mbCancel =1;
		mbRetry = 2; mbIgnore = 3; mbYes = 4; mbNo = 5;


	TYPE
		TrapHandlerProc = 
			PROCEDURE (excpRec: WinApi.PtrEXCEPTION_RECORD; estFrame: ExcpFramePtr; context: WinApi.PtrCONTEXT; dispCont: INTEGER): INTEGER;

		ExcpFramePtr* = POINTER TO ExcpFrame;
		ExcpFrame* = EXTENSIBLE RECORD [untagged]
			link*: ExcpFramePtr;
			handler*: TrapHandlerProc
		END;
		ComExcpFramePtr = POINTER TO RECORD (ExcpFrame)
			par: INTEGER
		END;
		(* THE COMPILER ensures every guarded procedure (which seems to be every procedure bound to a COM interface record) has an exception frame on the stack, and upon entry (inside BEGIN) puts it's exception frame in the (system) linked list of SEH frames, rooted in FS:[0]. This is like running every interface procedure inside a Kernel.Try.
		Each such compiler-formed SEH frame references Kernel.InterfaceTrapHandler as the exception handler. The hos OS ensures it is called.
		Kernel.InterfaceTrapHandler (which calls WinKernel.InterfaceTrapHandler) calls the regular TrapHandler, which forms the trap report; but then, instead of returning control to the Try-bottom, TrapHandler removes the interface procedure's parameters from the stack (for which ComExcpFramePtr.par is formed by the entry code prepared by the compiler) and returns WinApi.E_UNEXPECTED to the caller of the interface procedure. TrapHandler never returns control to InterfaceTrapHandler, and therefore never allows the host OS to continue searching for another exception handler. *)
		
		Platform = RECORD (Kernel.Platform) END;

		ErrLog = RECORD
			log: WinApi.HANDLE;
			string: PROCEDURE (IN string: ARRAY OF CHAR);
			ln: PROCEDURE
		END;
		
	VAR
		messageBox: PROCEDURE (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER;
		
	VAR 
		heap: WinApi.HANDLE;
		told, shift: INTEGER;	(* used in Time() *)
		excpPtr: ExcpFramePtr;
		passing: BOOLEAN;
		trapped,
		secondTrap,
		inDll, static, monoClusterMem: BOOLEAN;
		error, pc, sp, fp, val: INTEGER;
		rres: INTEGER;	(* auxiliary global variables used for trap handling *)
		restart: Kernel.Command;
		baseStack, trapTop: INTEGER;
		nestingTrapHandler: TrapHandlerProc;
		interrupted*: BOOLEAN;
		pageSize: INTEGER;
	
		err: ErrLog; 
		platform: RECORD 
			tag: S.ADDRESS;	(* to be filled out by hand! *)
			p: Platform
		END;
		
	(* debug logging functions *)

	PROCEDURE SetErr* (string: PROCEDURE(IN msg: ARRAY OF CHAR); ln: PROCEDURE);
	BEGIN err.string := string; err.ln := ln
	END SetErr;

	PROCEDURE Ln;
		CONST crlf = 0DX + 0AX;
		VAR res_: INTEGER;
	BEGIN
		IF err.ln # NIL THEN err.ln ELSE res_ := WinApi.WriteFile(err.log, S.ADR(crlf), 2, res_, NIL) END
	END Ln;

	PROCEDURE (VAR e: ErrLog) Ln, NEW;
	BEGIN
		IF e.ln # NIL THEN e.ln ELSE Ln END
	END Ln;

	PROCEDURE String (IN str: ARRAY OF CHAR);
		VAR res, i, j, k, z_: INTEGER; ss: ARRAY 2048 OF SHORTCHAR; ch: CHAR;
	BEGIN
		i := 0;
		REPEAT
			k := i; j := 0; REPEAT ch := str[i]; INC(i); INC(j) UNTIL (ch = 0X) OR (j = LEN(ss));
			res := WinApi.WideCharToMultiByte(WinApi.CP_OEMCP, {}, S.VAL(WinApi.PtrWSTR, S.ADR(str[k])), j, ss, LEN(ss) , NIL, NIL);
			ASSERT((res > 0) & (res <= LEN(ss)), 60);
			res := WinApi.WriteFile(err.log, S.ADR(ss[0]), res - 1, z_, NIL)
		UNTIL ch = 0X
	END String;

	PROCEDURE (VAR e: ErrLog) String (IN str: ARRAY OF CHAR), NEW;
	BEGIN 
		IF e.string # NIL THEN e.string(str) ELSE String(str) END
	END String;

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
		CONST n = 8;
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

	(* code procedures for exception handling *)

	PROCEDURE [code] PushFP 055H;
	PROCEDURE [code] PopFP 05DH;
	PROCEDURE [code] PushBX 053H;
	PROCEDURE [code] PopBX 05BH;
	PROCEDURE [code] PushSI 056H;
	PROCEDURE [code] PopSI 05EH;
	PROCEDURE [code] PushDI 057H;
	PROCEDURE [code] PopDI 05FH;
	PROCEDURE [code] LdSP8 08DH, 065H, 0F8H;
	PROCEDURE [code] Return0 (ret: INTEGER) 0C3H;
	PROCEDURE [code] ReturnCX (ret: INTEGER) 05AH, 001H, 0CCH, 0FFH, 0E2H;
		(* POP DX; ADD SP,CX; JP DX *)
	PROCEDURE [code] FPageWord (offs: INTEGER): INTEGER 64H, 8BH, 0H;	(* MOV EAX,FS:[EAX] *)
	PROCEDURE [code] InstallExcp (VAR e: ExcpFrame) 64H, 8BH, 0DH, 0, 0, 0, 0, 89H, 8, 64H, 0A3H, 0, 0, 0, 0;
	(* MOV ECX, [FS:0]; MOV [EAX], ECX; MOV [FS:0], EAX
	FS:0 holds the current TEB (Thread Environment Block), it's first field is the SEH (structured exception handling) Frame pointer, ExcpFramePtr. So essentially this does e.link := CurrentExcpFrame; CurrentExcpFrame := e 
	 *)
	
	PROCEDURE [code] RemoveExcp (VAR e: ExcpFrame) 8BH, 0, 64H, 0A3H, 0, 0, 0, 0;


	(* Memory management *)
	
	PROCEDURE (VAR p: Platform) DeallocateModMem (adr: S.ADDRESS; size: INTEGER; release: BOOLEAN);
		CONST MEM_DECOMMIT = 14; MEM_RELEASE = 15;
		VAR res_: INTEGER; opts: SET;
	BEGIN
		IF release THEN res_ := WinApi.VirtualFree(adr, 0, { MEM_RELEASE })
		ELSE res_ := WinApi.VirtualFree(adr, size, { MEM_DECOMMIT })
		END;
		ASSERT(res_ # 0, 111)
	END DeallocateModMem;
	
	PROCEDURE (VAR p: Platform) PageSize (): INTEGER;
	BEGIN
		RETURN pageSize
	END PageSize;

	PROCEDURE (VAR p: Platform) ProtectMemRead (addr: S.ADDRESS; size: INTEGER): BOOLEAN;
		VAR old: INTEGER;
	BEGIN
		RETURN WinApi.VirtualProtect(addr, size, WinApi.PAGE_READONLY, old) = WinApi.TRUE
	END ProtectMemRead;

	PROCEDURE (VAR p: Platform) ProtectMemExec (addr: S.ADDRESS; size: INTEGER): BOOLEAN;
		VAR old: INTEGER;
	BEGIN
		RETURN WinApi.VirtualProtect(addr, size, WinApi.PAGE_EXECUTE_READ, old) = WinApi.TRUE
	END ProtectMemExec;

	PROCEDURE (VAR p: Platform) AllocateModMem (size: INTEGER; OUT addr: S.ADDRESS; forCode: BOOLEAN);
		CONST HEAP_NO_SERIALIZE = 0; MEM_COMMIT = 12; MEM_RESERVE = 13;
			PAGE_EXECUTE_READWRITE = 6; PAGE_READWRITE = 2;
	BEGIN
		addr := WinApi.VirtualAlloc(0, size, {MEM_COMMIT, MEM_RESERVE}, { PAGE_READWRITE })
	END AllocateModMem;
	
	
	PROCEDURE (VAR p: Platform) Commit (base, committed, capacity, request: INTEGER; OUT granted: INTEGER);
	(** Given that capacity bytes have been reserved at base address, and committed bytes have been committed already, grow the committed committed to at least newSize. Upon return, exactly newSize bytes have been committed at base address *)
		CONST N = 262144;
		VAR adr, s: INTEGER;
	BEGIN
		ASSERT(monoClusterMem, 20);
		ASSERT(request >= committed, 21);
		IF request <= capacity THEN
			request := (request + (N - 1)) DIV N * N;
			adr := WinApi.VirtualAlloc(base, request, {12}, {6});	(* commit; exec, read, write *)
			IF adr = WinApi.NULL THEN granted := committed ELSE granted := request END
		ELSE granted := committed
		END
	END Commit;

	PROCEDURE (VAR p: Platform) AllocateClusterMem (VAR size: INTEGER; OUT base, capacity: INTEGER);
		(* allocate at least size bytes, typically at least 256 kbytes are allocated *)
		VAR s: INTEGER;
	BEGIN
		IF ~monoClusterMem THEN 
			ASSERT(heap # WinApi.NULL, 30);
			INC(size, 16);
			ASSERT(size > 0, 100); base := 0;
			
			IF size < multiClusterCapacity THEN
				base := WinApi.HeapAlloc(heap, {0}, multiClusterCapacity)
			END;
			IF base # 0 THEN size := multiClusterCapacity 
			ELSE base := WinApi.HeapAlloc(heap, {0}, size)
			END;
			IF base # 0 THEN capacity := WinApi.HeapSize(heap, {0}, base) END
			
		ELSE
			base := 0; s := monoClusterSize;
			REPEAT
				base := WinApi.VirtualAlloc(01000000H, s, {13}, {6});	(* reserve; exec, read, write *)
				IF base = 0 THEN
					base := WinApi.VirtualAlloc(0, s, {13}, {6})	(* reserve; exec, read, write *)
				END;
				s := s DIV 2
			UNTIL base # 0;
			(* IF platform.p.Debug() THEN err.String("Cluster size "); err.Hex(s * 2); err.Ln END; *)
			IF base = 0 THEN 
			ELSE
				base := WinApi.VirtualAlloc(base, 1024, {12}, {6});	(* commit; exec, read, write *)
				capacity := s * 2;
				platform.p.Commit(base, 0, capacity, size, size);
				IF size = 0 THEN base := 0 END
			END
		END
		(* post: (c = NIL) OR (c MOD 16 = 0) & (c.size >= size) *)
	END AllocateClusterMem;

	PROCEDURE (VAR p: Platform) DeallocateClusterMem* (base: S.ADDRESS; size: INTEGER);
	(** Deallocate cluster memory [base..base+size); base has to be the same as returned by AllocateClusterMem.
	Pre:
		monoClusterMem, 20
		base was earlier returned by AllocateClusterMem, 21 *)
	BEGIN
		ASSERT(~monoClusterMem, 20);
		ASSERT(0 # WinApi.HeapFree(heap, {0}, base), 21)
	END DeallocateClusterMem;
		

	PROCEDURE (VAR p: Platform) Time (): LONGINT;
		VAR t: INTEGER;
	BEGIN
		t := WinApi.GetTickCount();
		IF t < told THEN INC(shift) END;
		told := t;
		RETURN shift * 100000000L + t
	END Time;

	PROCEDURE (VAR p: Platform) LoadDll (IN name: ARRAY OF CHAR; VAR ok: BOOLEAN);
		VAR h: WinApi.HANDLE;
	BEGIN
		ok := FALSE;
		h := WinApi.LoadLibraryW(name);
		IF h # 0 THEN ok := TRUE END
	END LoadDll;

	PROCEDURE (VAR p: Platform) ThisDllObj (mode, fprint: INTEGER; IN dll, name: ARRAY OF CHAR): INTEGER;
		CONST mProc = 4; 
		VAR ad: WinApi.FARPROC; h: WinApi.HANDLE; res: INTEGER; sname: Kernel.Utf8Name;
	BEGIN
		ad := NIL;
		IF mode = mProc THEN
			h := WinApi.GetModuleHandleW(dll);
			IF h # 0 THEN Utf.StringToUtf8(name, sname, res); ASSERT(res = 0);
				ad := WinApi.GetProcAddress(h, sname)
			END
		END;
		RETURN S.VAL(INTEGER, ad)
	END ThisDllObj;

	PROCEDURE (VAR p: Platform) GetLoadTime (OUT loadTime: ARRAY OF SHORTINT);
		VAR t: WinApi.SYSTEMTIME;
	BEGIN
		WinApi.GetLocalTime(t);
		loadTime[0] := t.wYear;
		loadTime[1] := t.wMonth;
		loadTime[2] := t.wDay;
		loadTime[3] := t.wHour;
		loadTime[4] := t.wMinute;
		loadTime[5] := t.wSecond
	END GetLoadTime;
	
	PROCEDURE (VAR p: Platform) Terminate (exitCode: INTEGER);
	BEGIN
		WinOle.OleUninitialize();
		IF ~inDll THEN excpPtr.handler := nestingTrapHandler END;
		WinApi.ExitProcess(exitCode)
	END Terminate;
	
	PROCEDURE SetMsgBoxProc* (
		p: PROCEDURE (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER);
	BEGIN
		messageBox := p
	END SetMsgBoxProc;
	
	PROCEDURE (VAR p: Platform) FatalError (id: INTEGER; IN str: ARRAY OF CHAR);
	VAR res_: INTEGER; title: ARRAY 16 OF CHAR; 
	BEGIN
		IF id = Kernel.trapMessage THEN
			title := "TRAP REPORT";
			IF messageBox # NIL THEN
				res_ := messageBox(title$, str, {});
			ELSE
				err.String(str)
			END
		ELSE
			title := "Error xy";
			title[6] := CHR(id DIV 10 + ORD("0"));
			title[7] := CHR(id MOD 10 + ORD("0"));
			IF messageBox # NIL THEN
				res_ := messageBox(title$, str, {});
			ELSE
				err.String(" "); err.Ln;
				err.String("****"); err.Ln;
				err.String("* " + title); err.Ln;
				err.String("* " + str); err.Ln;
				err.String("****"); err.Ln
			END;
		END;
		platform.p.Terminate(id)
	END FatalError;
	
	PROCEDURE (VAR p: Platform) Start (code: Kernel.Command);
	BEGIN
		restart := code;
		S.GETREG(SP, baseStack);	(* save base stack *)
		code()
	END Start;

	PROCEDURE (VAR p: Platform) IsReadable (from, to: INTEGER): BOOLEAN;
	(** Check wether memory[from..to) may be read *)
	BEGIN
		RETURN WinApi.IsBadReadPtr(from, to - from) = 0
	END IsReadable;
	
	


	PROCEDURE (VAR p: Platform) GetTrapInfo (VAR opc, osp, ofp: S.ADDRESS; VAR oerr, oval: INTEGER; OUT orecursive: BOOLEAN);
	BEGIN 
		IF passing THEN
			opc := pc; osp := sp; ofp := fp; oerr := error; oval := val; orecursive := secondTrap
		ELSE
			opc := 0
		END;
		passing := FALSE
	END GetTrapInfo;
	
	PROCEDURE Unwind (f: ExcpFramePtr);	(* COMPILER DEPENDENT *)
		CONST Label = 27;	(* offset of Label: from proc start *)
	BEGIN
		PushFP;
		WinApi.RtlUnwind(S.VAL(WinApi.PtrVoid, f), S.ADR(Unwind) + Label, NIL, 0);
		(* Label: *)
		PopFP
	END Unwind;
	
	
	PROCEDURE TrapHandler (excpRec: WinApi.PtrEXCEPTION_RECORD; estFrame: ExcpFramePtr; context: WinApi.PtrCONTEXT; dispCont: INTEGER): INTEGER;
		(* same parameter size as Try *)
		VAR res, sig: INTEGER;
	BEGIN
		sig := 0;  (* registered to log various trap states *)
		IF excpRec^.ExceptionFlags * {1, 2} = {} THEN
			IF (excpRec.ExceptionCode MOD 256 = 4) & ~interrupted THEN	(* wrong trace trap *)
				context.Dr7 := 0;	(* disable all debug traps *)
				LdSP8; PopSI; PopDI; PopFP;	(* COMPILER DEPENDENT *)
				Return0(0)	(* return continueExecution without parameter remove *)
			END;
			Unwind(estFrame);
			IF trapped & (excpRec.ExceptionCode MOD 256 # 1) & (excpRec.ExceptionCode MOD 256 # 253) THEN
				(* what is this branch?
				DefaultTrapViewer(trapped, sig, trapViewer = NIL);*)
				IF ~secondTrap THEN trapped := FALSE; secondTrap := TRUE END
			END;
			error := -(excpRec.ExceptionCode MOD 256);
			pc := context.Eip; sp := context.Esp; fp := context.Ebp; trapTop := fp; (*stack := baseStack;*)
			IF error = -4 THEN error := keyboardInterrupt	(* keyboard interrupt *)
			ELSIF error = -5 THEN
				val := excpRec.ExceptionInformation[1];
				IF val = pc THEN	(* call to undef adr *)
					error := 205; S.GET(sp, pc); INC(sp, 4); DEC(pc)
				ELSIF excpRec.ExceptionInformation[0] = 0 THEN	(* illegal read *)
					error := 203
				ELSE	(* illegal write *)
					error := 204
				END
			ELSIF (error = -29) OR (error = -30) THEN	(* illegal instruction *)
				error := 202; val := 0;
				IF Kernel.IsReadable(excpRec.ExceptionAddress, excpRec.ExceptionAddress + 4) THEN
					S.GET(excpRec.ExceptionAddress, val);
					IF val MOD 100H = 8DH THEN	(* lea reg,reg *)
						IF val DIV 100H MOD 100H = 0F0H THEN error := val DIV 10000H MOD 100H	(* trap *)
						ELSIF val DIV 1000H MOD 10H = 0EH THEN
							error := 128 + val DIV 100H MOD 10H	(* run time error *)
						END
					END
				END
			ELSIF error = -142 THEN DEC(pc); error := 140	(* fpu: div by zero *)
			ELSIF (error = -144) OR (error = -146) THEN DEC(pc); error := 143	;	(* fpu: invalid op *)
				val := context.FloatSave.ControlWord MOD 4096 * 65536 + context.FloatSave.StatusWord MOD 65536
			ELSIF error = -145 THEN DEC(pc); error := 142	(* fpu: overflow *)
			ELSIF error = -147 THEN DEC(pc); error := 141	(* fpu: underflow *)
			ELSIF error = -148 THEN error := 139	(* division by zero *)
			ELSIF error = -149 THEN error := 138	(* integer overflow *)
			ELSIF (error = -1) OR (error = -253) THEN error := 137	(* stack overflow *)
			END;
			
			passing := TRUE; trapped := TRUE;
			Kernel.HandleTrap;
			
			(*INC(trapCount);
			InitFpu;
			IF error # 137 THEN	(* stack overflow handling is delayed *)
				TrapCleanup;
				IF error = 128 THEN	(* do nothing *)
				ELSIF(trapViewer # NIL) & (restart # NIL) & ~trapped & ~guarded THEN
					trapped := TRUE; trapViewer()
				ELSE DefaultTrapViewer(trapped, sig, trapViewer = NIL);
				END
			END;*)
			trapped := FALSE; secondTrap := FALSE;
			IF dispCont = 0 THEN	(* InterfaceTrapHandler *)	(* COMPILER DEPENDENT *)
				RemoveExcp(estFrame^);
				S.PUTREG(CX, estFrame(ComExcpFramePtr).par);
				S.PUTREG(SP, S.VAL(INTEGER, estFrame) + 12);
				(*IF error = 137 THEN	(* retrigger stack overflow *)
					TrapCleanup; DefaultTrapViewer(trapped, sig, trapViewer = NIL);
					ResetStackGuard
				END;*)
				PopSI; PopDI; PopBX; PopFP;
				ReturnCX(WinApi.E_UNEXPECTED)
			ELSIF estFrame # excpPtr THEN	(* Try failed *)	(* COMPILER DEPENDENT *)
				RemoveExcp(estFrame^);
				res := S.VAL(INTEGER, estFrame);
				S.PUTREG(FP, res + (SIZE(ExcpFrame) + 8));	(* restore fp *)
				S.PUTREG(SP, res - 4);	(* restore stack *)
				(*IF error = 137 THEN	(* retrigger stack overflow *)
					TrapCleanup; DefaultTrapViewer(trapped, sig, trapViewer = NIL);
					ResetStackGuard
				END;*)
				PopBX;
				RETURN 0	(* return from Try *)
			ELSIF restart # NIL THEN	(* Start failed *)
				err.String("WinKernel.TrapHandler: Start failed "); err.Ln;
				S.PUTREG(FP, baseStack);	(* restore fp *)
				S.PUTREG(SP, baseStack);	(* restore stack *)
				(*IF error = 137 THEN	(* retrigger stack overflow *)
					TrapCleanup; DefaultTrapViewer(trapped, sig, trapViewer = NIL);
					ResetStackGuard
				END;*)
				restart();
				Kernel.Quit(1)
			ELSE	(* boot process failed *)
				err.String("WinKernel.TrapHandler.Boot process failed, error = "); err.Int(error); err.String(' '); err.Ln;
				Kernel.Quit(1)
			END
			(* never returns *)
		ELSE
			LdSP8; PopSI; PopDI; PopFP;	(* COMPILER DEPENDENT *)
			Return0(1)	(* return continueSearch without parameter remove *)
		END
	END TrapHandler;
	(* TODO: stack overflow handling and reporting *)
	
	PROCEDURE Try (h: Kernel.TryHandler; a, b, c: INTEGER);
		(* COMPILER DEPENDENT *)
		(* same parameter size as TrapHandler *)
		VAR excp: ExcpFrame;	(* no other local variables! *)
	BEGIN
		PushBX;
		excp.handler := TrapHandler;
		InstallExcp(excp);
		IF excpPtr # NIL THEN h(a, b, c)
		ELSE
			excpPtr := S.VAL(ExcpFramePtr, S.ADR(excp));
			h(a, b, c);
			excpPtr := NIL
		END;
		RemoveExcp(excp);
		PopBX
	END Try;
	
	PROCEDURE [code] Below (a, b: S.ADDRESS): BOOLEAN
		03BH, 004H, 024H, 058H, 0FH, 92H, 0C0H; 
		(* CMP EAX, [ESP]; POP EAX; SETC AL *)
	(** Below compares S.ADDRESS bit patterns as unsigned integers *)
	
	PROCEDURE (VAR p: Platform) InActivationStack (sp: S.ADDRESS): BOOLEAN;
	BEGIN	 (* trapTop <= sp < excpPtr *)
		RETURN ((trapTop = sp) OR Below(trapTop, sp)) & Below(sp, S.VAL(INTEGER, Kernel.stack))
	END InActivationStack;
	
	PROCEDURE InterfaceTrapHandler (excpRec, estFrame, context, dispCont: INTEGER): INTEGER;
	(* known to compiler *)
		VAR res: INTEGER;
	BEGIN
		res := TrapHandler(S.VAL(WinApi.PtrEXCEPTION_RECORD, excpRec),
								S.VAL(ExcpFramePtr, estFrame),
								S.VAL(WinApi.PtrCONTEXT, context),
								0);
		(* dispCont = 0 signals to TrapHandler that it's called from InterfaceTrapHandler. 
		TrapHandler should never return here, rather to the caller of the interface procedure! The following is a catch-all for a malfunctioning TrapHandler (i guess), it allows the host OS to continue searching for other trap handlers. *)
		(* LdSP8 removes parameters of TrapHandler *)
		LdSP8; PopSI; PopDI; PopFP;	(* COMPILER DEPENDENT *)
		Return0(1);	(* return continueSearch without parameter remove *)
		IF FALSE THEN RETURN 0 END
	END InterfaceTrapHandler;

	PROCEDURE (VAR p: Platform) Setup (baseStack: S.ADDRESS; opts: SET; OUT id: INTEGER; OUT eoln: Kernel.Eoln; VAR monoCluster: BOOLEAN; OUT gradualCommit: BOOLEAN; VAR physicalMemory: INTEGER; OUT try: PROCEDURE (h: Kernel.TryHandler; a, b, c: INTEGER));
		CONST init = 16; dyn = 17; dll = 24; iptrs = 30;			(* kernel flags in module desc *)
			genericWindows = 10;
		VAR ms: WinApi.MEMORYSTATUS;
	BEGIN
		id := genericWindows;
		inDll := dll IN opts;
		static := init IN opts;
		monoClusterMem := monoCluster;
		IF ~monoClusterMem THEN heap := WinApi.GetProcessHeap() ELSE heap := WinApi.NULL END;
		p.interfaceTrapHandler := InterfaceTrapHandler;
		p.String := String;
		p.Ln := Ln;
		eoln := 0DX+0AX;
		gradualCommit := TRUE;
		ms.dwLength := SIZE(WinApi.MEMORYSTATUS); ms.dwMemoryLoad := -1;
		WinApi.GlobalMemoryStatus(ms);
		physicalMemory := ms.dwTotalPhys;
		try := Try
	END Setup;
	
	PROCEDURE Init;
		VAR excp: ExcpFrame; _: INTEGER; systemInfo: WinApi.SYSTEM_INFO;
	BEGIN
		interrupted := FALSE;
		(* set debug out *)
		err.log := WinApi.GetStdHandle(WinApi.STD_ERROR_HANDLE);
		
		WinApi.GetSystemInfo(systemInfo);
		pageSize := systemInfo.dwPageSize;
		
		platform.tag := S.TYP(Platform);
		Kernel.SetPlatform(S.VAL(S.PTR, S.ADR(platform.p)));
		_ := WinOle.OleInitialize(0);
		IF ~inDll THEN
			InstallExcp(excp);	(* now excp.link points to the previous SEH frame *)
			ASSERT(-1 # S.VAL(INTEGER, excp.link), 99);
			nestingTrapHandler := excp.link.handler;
			excp.link.handler := TrapHandler;
			excpPtr := excp.link;
			RemoveExcp(excp);
		ELSIF FALSE & ~inDll & static THEN 
			excpPtr := S.VAL(ExcpFramePtr, S.ADR(excp));
			IF FALSE & static THEN HALT(126)
			END;
			excpPtr.handler := TrapHandler;	(* init exception handling *)
			InstallExcp(excpPtr^)
		END;
		heap := WinApi.GetProcessHeap();
		told := 0; shift := 0
	END Init;

BEGIN 
	Init
CLOSE
	IF excpPtr # NIL THEN excpPtr.handler := nestingTrapHandler END
END WinKernel.