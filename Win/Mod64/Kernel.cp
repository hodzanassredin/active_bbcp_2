MODULE WinKernel;
(* Windows x64 Kernel.Platform, platform services and exception recovery.
	License: The 2-Clause BSD License (System/Rsrc/About). *)
	IMPORT S := SYSTEM, Kernel, A := WinApi, N := WinNtdll, O := WinOle, Trace := WinTrace;
	CONST
		contextSize = 1232; contextSp = 152; contextFp = 160; contextPc = 248;
		memCommit = 1000H; memReserve = 2000H; memDecommit = 4000H; memRelease = 8000H;
	TYPE
		Platform = RECORD (Kernel.Platform) END;
		Allocation = POINTER TO RECORD [untagged]
			next: Allocation; base, size, remaining, execbase, execsize: S.ADDRESS;
			live: ARRAY 1 OF BYTE
		END;
		Stub = POINTER TO StubRec;
		StubRec = RECORD [untagged] next: Stub; proc: S.ADDRESS; addr: INTEGER END;
		Frame = RECORD [align8]
			prev, report, context: S.ADDRESS; resumed: INTEGER;
			storage: ARRAY 1248 OF BYTE
		END;
		FramePtr = POINTER TO Frame;
		Trap = RECORD [align8] pc, sp, fp, value: S.ADDRESS; error, recursive: INTEGER END;
		MemoryInfo = RECORD [align8]
			base, allocationBase: S.ADDRESS; allocationProtect, alignment1: INTEGER;
			size: S.ADDRESS; state, protect, typ, alignment2: INTEGER
		END;
		Unwind = POINTER TO RECORD
			next: Unwind; code, table: S.ADDRESS; size: INTEGER
		END;
		Call3 = PROCEDURE (a,b,c: S.ADDRESS);
	VAR
		unwindTables: Unwind;
		allocations: Allocation; stubs: Stub;
		handler, current, heap, nextHeapAddress, maxApplicationAddress: S.ADDRESS; owner, pageSize: INTEGER;
		oleInitialized, reporting: BOOLEAN; trap: Trap;

		platform: RECORD tag: S.ADDRESS; p: Platform END;
		baseStack, trapTop: S.ADDRESS;
		messageBox: PROCEDURE (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER;
		logString: PROCEDURE (IN msg: ARRAY OF CHAR);
		logLn: PROCEDURE;
		interrupted*: BOOLEAN;

	PROCEDURE [code] StackBase (): S.ADDRESS 65H, 48H, 8BH, 04H, 25H, 08H, 00H, 00H, 00H;
	PROCEDURE [code] AlignStack 48H, 83H, 0E4H, 0F0H;
	PROCEDURE WkStackBase* (): S.ADDRESS;
	BEGIN RETURN StackBase() END WkStackBase;
	PROCEDURE WkPageSize* (): INTEGER;
		VAR info: ARRAY 48 OF BYTE;
	BEGIN
		IF pageSize = 0 THEN A.GetSystemInfo(S.ADR(info)); S.GET(S.ADR(info)+4, pageSize); S.GET(S.ADR(info)+16, maxApplicationAddress) END;
		RETURN pageSize
	END WkPageSize;
	PROCEDURE QueryMemory (address: S.ADDRESS; VAR info: MemoryInfo): BOOLEAN;
		VAR buffer: ARRAY 64 OF BYTE; aligned, result: S.ADDRESS; error: INTEGER;
	BEGIN
		(* CP frames may be four-byte aligned. Give the NT syscall a native-aligned
		   output buffer, then copy back without imposing alignment on the caller. *)
		aligned := (S.ADR(buffer)+15) DIV 16 * 16;
		result := A.VirtualQuery(address, aligned, SIZE(MemoryInfo));
		IF result < SIZE(MemoryInfo) THEN
			error := A.GetLastError(); Trace.Value("VirtualQuery failed buffer", aligned);
			Trace.Value("VirtualQuery error", error); RETURN FALSE
		END;
		S.MOVE(aligned, S.ADR(info), SIZE(MemoryInfo)); RETURN TRUE
	END QueryMemory;

	PROCEDURE WkReadable* (from, to: S.ADDRESS): INTEGER;
		VAR m: MemoryInfo; end: S.ADDRESS; p: INTEGER;
	BEGIN
		IF (from < 0) OR (to < from) THEN RETURN 0 END;
		WHILE from < to DO
			IF ~QueryMemory(from, m) THEN RETURN 0 END;
			IF (m.state # memCommit) OR (BITS(m.protect) * {0,8} # {}) THEN RETURN 0 END;
			p := m.protect MOD 256;
			IF (p # 2) & (p # 4) & (p # 8) & (p # 20H) & (p # 40H) & (p # 80H) THEN RETURN 0 END;
			end := m.base + m.size; IF end <= from THEN RETURN 0 END; from := end
		END;
		RETURN 1
	END WkReadable;
	PROCEDURE AllocatePages (size: S.ADDRESS; low: BOOLEAN): S.ADDRESS;
		VAR addr, limit, p, pages, i, next: S.ADDRESS; m: Allocation; region: MemoryInfo; ok, error, attempts: INTEGER;
	BEGIN
		IF (size <= 0) OR (size > MAX(INTEGER)-65536) THEN RETURN 0 END;
		size := (size + pageSize - 1) DIV pageSize * pageSize; pages := size DIV pageSize;
		IF low THEN addr := 10000000H; limit := 70000000H
		ELSE
			IF nextHeapAddress = 0 THEN nextHeapAddress := 100000000L END;
			addr := nextHeapAddress; limit := maxApplicationAddress+1
		END;
		Trace.Value("allocation search begin", addr); Trace.Value("allocation search limit", limit); Trace.Value("allocation size", size);
		attempts := 0;
		WHILE addr <= limit-size DO
			INC(attempts);
			p := A.VirtualAlloc(addr, size, memReserve + memCommit, 4);
			IF p # 0 THEN
				Trace.Value("VirtualAlloc result", p); Trace.Value("allocation attempts", attempts);
				m := S.VAL(Allocation, A.HeapAlloc(heap, 8, 48 + pages));
				Trace.Value("HeapAlloc bookkeeping", S.VAL(S.ADDRESS, m));
				IF m = NIL THEN ok := A.VirtualFree(p, 0, memRelease); RETURN 0 END;
				m.base := p; m.size := size; m.remaining := pages;
				i := 0; WHILE i < pages DO S.PUT(S.ADR(m.live)+i, SHORT(SHORT(1))); INC(i) END;
				m.next := allocations; allocations := m;
				IF ~low THEN nextHeapAddress := (p+size+65535) DIV 65536 * 65536 END;
				RETURN p
			END;
			error := A.GetLastError(); IF attempts = 1 THEN Trace.Value("first VirtualAlloc error", error) END;
			IF (error = 8) OR (error = 1455) THEN RETURN 0 END;
			next := addr+65536;
			IF QueryMemory(addr, region) THEN
				IF ((region.state # 10000H) OR (region.base+region.size-addr < size)) & (region.base+region.size > next) THEN
					next := (region.base+region.size+65535) DIV 65536 * 65536
				END
			ELSE Trace.Value("VirtualQuery failed", A.GetLastError()); RETURN 0
			END;
			IF next <= addr THEN RETURN 0 END; addr := next
		END;
		Trace.Write("allocation search exhausted"); RETURN 0
	END AllocatePages;

	PROCEDURE WkAlloc* (size: S.ADDRESS): S.ADDRESS;
	BEGIN RETURN AllocatePages(size, TRUE) END WkAlloc;

	PROCEDURE WkAllocHeap* (size: S.ADDRESS): S.ADDRESS;
	BEGIN RETURN AllocatePages(size, FALSE) END WkAllocHeap;

	PROCEDURE WkFree* (addr, size: S.ADDRESS; release: INTEGER): INTEGER;
		VAR m, prev: Allocation; first, count, i: S.ADDRESS; live: BYTE; ok: INTEGER;
	BEGIN
		m := allocations; prev := NIL;
		WHILE (m # NIL) & ~((addr >= m.base) & (addr < m.base+m.size)) DO prev := m; m := m.next END;
		IF m = NIL THEN RETURN 0 END;
		IF size = 0 THEN IF addr # m.base THEN RETURN 0 END; size := m.size END;
		IF (size < 0) OR (addr MOD pageSize # 0) OR (size > m.size) OR (addr-m.base > m.size-size) THEN RETURN 0 END;
		size := (size+pageSize-1) DIV pageSize * pageSize;
		first := (addr-m.base) DIV pageSize; count := size DIV pageSize;
		IF first+count > m.size DIV pageSize THEN RETURN 0 END;
		i := 0;
		WHILE i < count DO S.GET(S.ADR(m.live)+first+i, live); IF live = 0 THEN RETURN 0 END; INC(i) END;
		IF A.VirtualFree(addr, size, memDecommit) = 0 THEN RETURN 0 END;
		IF release # 0 THEN
			i := 0; WHILE i < count DO S.PUT(S.ADR(m.live)+first+i, SHORT(SHORT(0))); INC(i) END;
			DEC(m.remaining, count);
			IF m.remaining = 0 THEN
				IF A.VirtualFree(m.base, 0, memRelease) = 0 THEN RETURN 0 END;
				IF (m.base >= 100000000L) & (m.base < nextHeapAddress) THEN nextHeapAddress := m.base END;
				IF prev = NIL THEN allocations := m.next ELSE prev.next := m.next END;
				ok := A.HeapFree(heap, 0, S.VAL(S.ADDRESS, m))
			END
		END;
		RETURN 1
	END WkFree;
	PROCEDURE WkProtect* (addr, size: S.ADDRESS; exec: INTEGER): INTEGER;
		VAR old, protection: INTEGER; m: Allocation;
	BEGIN
		IF exec # 0 THEN protection := 20H ELSE protection := 2 END;
		IF A.VirtualProtect(addr, size, protection, old) = 0 THEN RETURN 0 END;
		m := allocations;
		WHILE (m # NIL) & ~((addr >= m.base) & (addr-m.base < m.size)) DO m := m.next END;
		IF (m # NIL) & (exec # 0) THEN m.execbase := addr; m.execsize := size END;
		IF exec # 0 THEN RETURN A.FlushInstructionCache(A.GetCurrentProcess(), addr, size) END;
		RETURN 1
	END WkProtect;
	PROCEDURE CpCode (pc: S.ADDRESS): BOOLEAN;
		VAR base, section, start: S.ADDRESS; offset, rva, size, flags, i: INTEGER; count, optional: SHORTINT; m: Allocation;
	BEGIN
		base := A.GetModuleHandleW(0); S.GET(base+60, offset);
		S.GET(base+offset+6, count); S.GET(base+offset+20, optional);
		section := base+offset+24+optional;
		FOR i := 0 TO count-1 DO
			S.GET(section+8, size); S.GET(section+12, rva); S.GET(section+36, flags); start := base+rva;
			IF (29 IN BITS(flags)) & (pc >= start) & (pc-start < size) THEN RETURN TRUE END;
			INC(section, 40)
		END;
		m := allocations;
		WHILE m # NIL DO
			IF (m.execsize > 0) & (pc >= m.execbase) & (pc-m.execbase < m.execsize) THEN RETURN TRUE END;
			m := m.next
		END;
		RETURN FALSE
	END CpCode;
	PROCEDURE [ms64] Catch (info: S.ADDRESS): INTEGER;
		VAR rec, ctx, pc, sp, fp, kind, value, ret: S.ADDRESS; code, flags, error: INTEGER;
			op, sub, no: BYTE; ours: BOOLEAN; f: FramePtr; report: Call3;
	BEGIN
		IF (A.GetCurrentThreadId() # owner) OR (current = 0) THEN RETURN 0 END;
		S.GET(info, rec); S.GET(info+8, ctx); S.GET(rec, code); S.GET(rec+4, flags);
		IF 0 IN BITS(flags) THEN RETURN 0 END; (* noncontinuable *)
		S.GET(ctx+contextPc, pc); S.GET(ctx+contextSp, sp); S.GET(ctx+contextFp, fp);
		kind := 0; value := 0;
		IF code = 0C0000005H THEN S.GET(rec+32, kind); S.GET(rec+40, value) END;
		ours := CpCode(pc);
		IF ~ours & (code = 0C0000005H) & (kind = 8) & (WkReadable(sp, sp+8) # 0) THEN
			S.GET(sp, ret); ours := (ret # 0) & CpCode(ret-1)
		END;
		IF ~ours THEN RETURN 0 END;
		CASE code OF
		| 0C0000005H: IF kind = 0 THEN error := 203 ELSE error := 204 END
		| 0C000001DH: error := 202
		| 0C0000094H: error := 139
		| 0C0000095H: error := 138
		| 0C000008EH: error := 140
		| 0C0000090H: error := 143
		| 0C0000091H: error := 142
		| 0C0000093H: error := 141
		ELSE RETURN 0
		END;
		IF reporting THEN A.ExitProcess(127) END;
		trap.pc := pc; trap.sp := sp; trap.fp := fp; trap.error := error; trap.value := value; trap.recursive := 0;
		IF (code = 0C0000005H) & (kind = 8) & (WkReadable(sp, sp+8) # 0) THEN
			S.GET(sp, ret); IF ret # 0 THEN trap.pc := ret-1 END;
			INC(trap.sp, 8); trap.error := 205
		END;
		IF (code = 0C000001DH) & (WkReadable(pc, pc+3) # 0) THEN
			S.GET(pc, op); S.GET(pc+1, sub); S.GET(pc+2, no);
			IF op MOD 256 = 8DH THEN
				IF sub MOD 256 = 0F0H THEN trap.error := no MOD 256
				ELSIF sub MOD 256 DIV 16 = 0EH THEN trap.error := 128 + sub MOD 16 END
			END
		END;
		f := S.VAL(FramePtr, current); report := S.VAL(Call3, f.report);
		reporting := TRUE; report(0,0,0); reporting := FALSE;
		(* Resume the capture point only after the report has used the faulting stack.
					Windows restores the supplied context after this VEH returns -1. No longjmp,
					foreign-frame cleanup or jump out of the callback itself is performed. *)
		f.resumed := 1; S.MOVE(f.context, ctx, contextSize);
		RETURN -1
	END Catch;
	PROCEDURE WkInstall* (): INTEGER;
		VAR hr, ok: INTEGER;
	BEGIN
		Trace.Write("WinKernel.WkInstall begin");
		IF handler # 0 THEN IF owner = A.GetCurrentThreadId() THEN RETURN 1 ELSE RETURN 0 END END;
		ASSERT(SIZE(MemoryInfo) = 48); ASSERT(SIZE(Trap) = 40);
		owner := A.GetCurrentThreadId(); heap := A.GetProcessHeap(); pageSize := WkPageSize();
		Trace.Value("process heap", heap); Trace.Value("page size", pageSize); Trace.Value("maximum address", maxApplicationAddress);
		handler := A.AddVectoredExceptionHandler(1, S.ADR(Catch)); IF handler = 0 THEN RETURN 0 END;
		Trace.Write("before OleInitialize"); hr := O.OleInitialize(0); Trace.Value("OleInitialize result", hr);
		IF hr < 0 THEN ok := A.RemoveVectoredExceptionHandler(handler); handler := 0; RETURN 0 END;
		oleInitialized := TRUE; RETURN 1
	END WkInstall;
	PROCEDURE WkRemove*;
		VAR ok: INTEGER;
	BEGIN
		Trace.Write("WinKernel.WkRemove begin");
		IF handler # 0 THEN ok := A.RemoveVectoredExceptionHandler(handler); handler := 0 END;
		IF oleInitialized THEN O.OleUninitialize; oleInitialized := FALSE END
	END WkRemove;
	PROCEDURE [ms64] WkRun* (fn, a,b,c, report: S.ADDRESS): INTEGER;
		VAR f: Frame; call: Call3;
	BEGIN
		IF (handler = 0) OR (owner # A.GetCurrentThreadId()) THEN RETURN -1 END;
		f.prev := current; f.report := report; f.resumed := 0;
		f.context := (S.ADR(f.storage)+15) DIV 16 * 16; current := S.ADR(f);
		(* CP pointer initialization can leave RSP only four-byte aligned. Keep
					callbacks and the captured boundary aligned without moving RBP/locals. *)
		AlignStack; N.RtlCaptureContext(f.context);
		IF f.resumed = 0 THEN call := S.VAL(Call3, fn); call(a,b,c) END;
		current := f.prev; RETURN f.resumed
	END WkRun;
	PROCEDURE WkTrap* (info: S.ADDRESS);
	BEGIN S.MOVE(S.ADR(trap), info, SIZE(Trap)); trap.pc := 0 END WkTrap;
	PROCEDURE WkTime* (): LONGINT;
	BEGIN RETURN A.GetTickCount64() END WkTime;
	PROCEDURE WkLoad* (name: S.ADDRESS): INTEGER;
	BEGIN IF A.LoadLibraryW(S.VAL(A.PtrWSTR, name)) # 0 THEN RETURN 1 ELSE RETURN 0 END END WkLoad;
	PROCEDURE WkResolve* (dll, name: S.ADDRESS): INTEGER;
		VAR narrow: ARRAY 1024 OF SHORTCHAR; h, p, addr: S.ADDRESS; s: Stub; ok: INTEGER;
	BEGIN
		IF A.WideCharToMultiByte(65001, 80H, name, -1, S.ADR(narrow), LEN(narrow), 0, 0) = 0 THEN RETURN 0 END;
		h := A.GetModuleHandleW(dll);
		(* Динамически загруженный interface-модуль (например AsyncWinSock ->
		   WS2_32.dll) может ссылаться на библиотеку, которую образ PE не
		   импортирует: тогда загружаем её здесь. *)
		IF h = 0 THEN h := A.LoadLibraryW(S.VAL(A.PtrWSTR, dll)) END;
		IF h = 0 THEN RETURN 0 END;
		p := S.VAL(S.ADDRESS, A.GetProcAddress(h, narrow)); IF p = 0 THEN RETURN 0 END;
		s := stubs; WHILE s # NIL DO IF s.proc = p THEN RETURN s.addr END; s := s.next END;
		addr := WkAlloc(16); IF addr = 0 THEN RETURN 0 END;
		S.PUT(addr, SHORT(SHORT(-1))); S.PUT(addr+1, SHORT(SHORT(25H)));
		S.PUT(addr+2, 0); S.PUT(addr+6, p);
		IF WkProtect(addr, 16, 1) = 0 THEN ok := WkFree(addr, 0, 1); RETURN 0 END;
		s := S.VAL(Stub, A.HeapAlloc(heap, 8, SIZE(StubRec)));
		IF s = NIL THEN ok := WkFree(addr, 0, 1); RETURN 0 END;
		s.proc := p; s.addr := SHORT(addr); s.next := stubs; stubs := s; RETURN s.addr
	END WkResolve;
	PROCEDURE WkLocalTime* (t: S.ADDRESS);
		VAR time: ARRAY 8 OF SHORTINT;
	BEGIN
		A.GetLocalTime(S.ADR(time)); S.PUT(t, time[0]); S.PUT(t+2, time[1]);
		S.PUT(t+4, time[3]); S.PUT(t+6, time[4]); S.PUT(t+8, time[5]); S.PUT(t+10, time[6])
	END WkLocalTime;
	PROCEDURE WkWrite* (s: S.ADDRESS; n: INTEGER);
		VAR buf: ARRAY 4096 OF SHORTCHAR; handle: S.ADDRESS; count, bytes, written, ok: INTEGER; ch: CHAR;
	BEGIN
		handle := A.GetStdHandle(-12);
		WHILE n > 0 DO
			count := n; IF count > 1000 THEN count := 1000 END;
			IF count < n THEN
				S.GET(s+2*(count-1), ch); IF (ch >= 0D800X) & (ch <= 0DBFFX) THEN DEC(count) END
			END;
			bytes := A.WideCharToMultiByte(65001, 0, s, count, S.ADR(buf), LEN(buf), 0, 0);
			IF bytes > 0 THEN ok := A.WriteFile(handle, S.ADR(buf), bytes, written, NIL) END;
			DEC(n, count); INC(s, 2*count)
		END
	END WkWrite;
	PROCEDURE WkExit* (code: INTEGER);
	BEGIN
		Trace.Value("ExitProcess code", code); WkRemove; A.ExitProcess(code) END WkExit;

	PROCEDURE String (IN msg: ARRAY OF CHAR);
	BEGIN
		IF logString # NIL THEN logString(msg)
		ELSE WkWrite(S.ADR(msg), LEN(msg$)) END
	END String;
	PROCEDURE Ln;
		VAR crlf: ARRAY 3 OF CHAR;
	BEGIN crlf := 0DX + 0AX; IF logLn # NIL THEN logLn ELSE WkWrite(S.ADR(crlf), 2) END END Ln;
	PROCEDURE SetErr* (string: PROCEDURE (IN msg: ARRAY OF CHAR); ln: PROCEDURE);
	BEGIN logString := string; logLn := ln END SetErr;
	PROCEDURE SetMsgBoxProc* (p: PROCEDURE (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER);
	BEGIN messageBox := p END SetMsgBoxProc;

	PROCEDURE (VAR p: Platform) PageSize (): INTEGER;
	BEGIN RETURN pageSize END PageSize;
	PROCEDURE (VAR p: Platform) AllocateModMem (size: INTEGER; OUT addr: S.ADDRESS; forCode: BOOLEAN);
	BEGIN addr := WkAlloc(size) END AllocateModMem;
	PROCEDURE (VAR p: Platform) RegisterCode (code: S.ADDRESS; size: INTEGER; IN unwind: ARRAY OF BYTE): BOOLEAN;
		VAR u: Unwind; count, i, pos, total, offset, n, first, last, ok: INTEGER; mem: S.ADDRESS;
		PROCEDURE Word (p: INTEGER): INTEGER;
		BEGIN RETURN unwind[p] MOD 256 + 100H*(unwind[p+1] MOD 256) +
			10000H*(unwind[p+2] MOD 256) + 1000000H*unwind[p+3] END Word;
	BEGIN
		IF (LEN(unwind) < 12) OR (Word(0) # 34365755H) OR (Word(4) # 1) THEN RETURN FALSE END;
		count := Word(8); IF (count < 0) OR (count > (LEN(unwind)-12) DIV 16) THEN RETURN FALSE END;
		IF count = 0 THEN RETURN LEN(unwind) = 12 END;
		IF (code <= 0) OR (size <= 0) OR (code+size > MAX(INTEGER)) THEN RETURN FALSE END;
		pos := 12; total := 12*count; last := 0;
		FOR i := 0 TO count-1 DO
			IF pos > LEN(unwind)-16 THEN RETURN FALSE END;
			first := Word(pos); IF first < last THEN RETURN FALSE END;
			last := Word(pos+4); n := Word(pos+8);
			IF (last <= first) OR (last > size) OR (n < 4) OR (n > 260) OR
				(n MOD 4 # 0) OR (n > LEN(unwind)-pos-12) THEN RETURN FALSE END;
			INC(total, n); INC(pos, 12+n)
		END;
		IF pos # LEN(unwind) THEN RETURN FALSE END;
		NEW(u); mem := WkAlloc(total); IF mem = 0 THEN RETURN FALSE END;
		pos := 12; offset := 12*count;
		FOR i := 0 TO count-1 DO
			first := SHORT(code)+Word(pos); last := SHORT(code)+Word(pos+4); n := Word(pos+8);
			S.PUT(mem+12*i, first); S.PUT(mem+12*i+4, last); S.PUT(mem+12*i+8, SHORT(mem)+offset);
			S.MOVE(S.ADR(unwind[pos+12]), mem+offset, n); INC(offset, n); INC(pos, 12+n)
		END;
		IF (WkProtect(mem, total, 0) = 0) OR ~N.RtlAddFunctionTable(mem, count, 0) THEN ok := WkFree(mem, 0, 1); RETURN FALSE END;
		u.code := code; u.size := size; u.table := mem; u.next := unwindTables; unwindTables := u;
		RETURN TRUE
	END RegisterCode;

	PROCEDURE RemoveUnwind (addr: S.ADDRESS; size: INTEGER);
		VAR u, prev, next: Unwind; ok: INTEGER;
	BEGIN
		u := unwindTables; prev := NIL;
		WHILE u # NIL DO
			next := u.next;
			IF (addr < u.code+u.size) & (u.code < addr+size) THEN
				ASSERT(N.RtlDeleteFunctionTable(u.table), 111);
				IF prev = NIL THEN unwindTables := next ELSE prev.next := next END;
				ok := WkFree(u.table, 0, 1); ASSERT(ok # 0, 111)
			ELSE prev := u END;
			u := next
		END
	END RemoveUnwind;

	PROCEDURE (VAR p: Platform) DeallocateModMem (adr: S.ADDRESS; size: INTEGER; release: BOOLEAN);
		VAR ok: INTEGER;
	BEGIN
		IF size > 0 THEN
			RemoveUnwind(adr, size);
			IF release THEN ok := WkFree(adr, size, 1) ELSE ok := WkFree(adr, size, 0) END;
			ASSERT(ok # 0, 111)
		END
	END DeallocateModMem;
	PROCEDURE (VAR p: Platform) ProtectMemRead (addr: S.ADDRESS; size: INTEGER): BOOLEAN;
	BEGIN RETURN WkProtect(addr, size, 0) # 0 END ProtectMemRead;
	PROCEDURE (VAR p: Platform) ProtectMemExec (addr: S.ADDRESS; size: INTEGER): BOOLEAN;
	BEGIN RETURN WkProtect(addr, size, 1) # 0 END ProtectMemExec;
	PROCEDURE (VAR p: Platform) AllocateClusterMem (VAR size: S.ADDRESS; OUT base, capacity: S.ADDRESS);
	BEGIN
		IF size < 262144 THEN size := 262144 END;
		size := (size + pageSize - 1) DIV pageSize * pageSize;
		base := WkAllocHeap(size); capacity := size
	END AllocateClusterMem;
	PROCEDURE (VAR p: Platform) DeallocateClusterMem (base: S.ADDRESS; size: INTEGER);
		VAR ok: INTEGER;
	BEGIN ok := WkFree(base, 0, 1); ASSERT(ok # 0, 111) END DeallocateClusterMem;
	PROCEDURE (VAR p: Platform) AllocateStackMem (size: INTEGER; OUT lo, hi: S.ADDRESS);
	(* Стек задачи: VirtualAlloc (reserve+commit) и одна сторожевая
	   страница PAGE_NOACCESS снизу. Страницы физически выделяются по
	   касанию, поэтому тысячи задач стоят адресного пространства. *)
		VAR guard, total, base: S.ADDRESS; old, ok: INTEGER;
	BEGIN
		guard := p.PageSize();
		total := ((size + guard - 1) DIV guard) * guard + guard;
		base := A.VirtualAlloc(0, total, memReserve + memCommit, 4);	(* PAGE_READWRITE *)
		IF base = 0 THEN lo := 0; hi := 0; RETURN END;
		ok := A.VirtualProtect(base, guard, 1, old);	(* PAGE_NOACCESS *)
		IF ok = 0 THEN
			ok := A.VirtualFree(base, 0, memRelease);
			lo := 0; hi := 0; RETURN
		END;
		lo := base + guard; hi := base + total
	END AllocateStackMem;
	PROCEDURE (VAR p: Platform) DeallocateStackMem (lo, hi: S.ADDRESS);
		VAR ok: INTEGER;
	BEGIN
		IF lo # 0 THEN
			ok := A.VirtualFree(lo - p.PageSize(), 0, memRelease);
			ASSERT(ok # 0, 112)
		END
	END DeallocateStackMem;
	PROCEDURE (VAR p: Platform) Commit (base, committed, capacity, request: S.ADDRESS; OUT granted: S.ADDRESS);
	BEGIN granted := committed END Commit; (* multi-cluster; gradualCommit is FALSE *)
	PROCEDURE (VAR p: Platform) Time (): LONGINT;
	BEGIN RETURN WkTime() END Time;
	PROCEDURE (VAR p: Platform) LoadDll (IN name: ARRAY OF CHAR; VAR ok: BOOLEAN);
	BEGIN ok := WkLoad(S.ADR(name)) # 0 END LoadDll;
	PROCEDURE (VAR p: Platform) ThisDllObj (mode, fprint: INTEGER; IN dll, name: ARRAY OF CHAR): INTEGER;
	BEGIN
		IF mode = 4 THEN RETURN WkResolve(S.ADR(dll), S.ADR(name)) ELSE RETURN 0 END
	END ThisDllObj;
	PROCEDURE (VAR p: Platform) GetLoadTime (OUT loadTime: ARRAY OF SHORTINT);
	BEGIN ASSERT(LEN(loadTime) >= 6); WkLocalTime(S.ADR(loadTime)) END GetLoadTime;
	PROCEDURE (VAR p: Platform) IsReadable (from, to: S.ADDRESS): BOOLEAN;
	BEGIN RETURN WkReadable(from, to) # 0 END IsReadable;
	PROCEDURE (VAR p: Platform) InActivationStack (sp: S.ADDRESS): BOOLEAN;
	BEGIN RETURN (sp >= trapTop) & (sp < baseStack) END InActivationStack;
	PROCEDURE (VAR p: Platform) GetTryContext (): S.ADDRESS;
	(* Голова цепочки Try текущего стека — для SwitchSave/Restore задач. *)
	BEGIN RETURN current END GetTryContext;
	PROCEDURE (VAR p: Platform) SetTryContext (ctx: S.ADDRESS);
	BEGIN current := ctx END SetTryContext;
	PROCEDURE (VAR p: Platform) GetTrapInfo (VAR pc, sp, fp: S.ADDRESS; VAR err: INTEGER; VAR val: S.ADDRESS; OUT recursive: BOOLEAN);
		VAR t: Trap;
	BEGIN
		WkTrap(S.ADR(t)); pc := t.pc; sp := t.sp; fp := t.fp;
		err := t.error; val := t.value; recursive := t.recursive # 0; trapTop := t.sp
	END GetTrapInfo;
	PROCEDURE Report (a, b, c: S.ADDRESS);
	BEGIN Kernel.HandleTrap END Report;
	PROCEDURE Try (h: Kernel.TryHandler; a, b, c: S.ADDRESS);
		VAR result: INTEGER;
	BEGIN
		result := WkRun(S.VAL(S.ADDRESS, h), a, b, c, S.ADR(Report));
		IF result < 0 THEN WkExit(126) END
	END Try;
	PROCEDURE RunCommand (a, b, c: S.ADDRESS);
		VAR cmd: Kernel.Command;
	BEGIN cmd := S.VAL(Kernel.Command, a); cmd() END RunCommand;
	PROCEDURE (VAR p: Platform) Start (code: Kernel.Command);
		VAR result: INTEGER;
	BEGIN
		REPEAT result := WkRun(S.ADR(RunCommand), S.VAL(S.ADDRESS, code), 0, 0, S.ADR(Report))
		UNTIL result # 1;
		IF result < 0 THEN WkExit(126) END
	END Start;
	PROCEDURE (VAR p: Platform) Terminate (exitCode: INTEGER);
	BEGIN WkExit(exitCode) END Terminate;
	PROCEDURE (VAR p: Platform) FatalError (id: INTEGER; IN str: ARRAY OF CHAR);
		VAR result: INTEGER;
	BEGIN
		String(str); Ln;
		IF messageBox # NIL THEN result := messageBox("BlackBox", str, {}) END;
		WkExit(id)
	END FatalError;
	PROCEDURE (VAR p: Platform) Setup (stackBase: S.ADDRESS; opts: SET; OUT id: INTEGER; OUT eoln: Kernel.Eoln;
		VAR monoCluster: BOOLEAN; OUT gradualCommit: BOOLEAN; VAR physicalMemory: INTEGER;
		OUT try: PROCEDURE (h: Kernel.TryHandler; a, b, c: S.ADDRESS));
	BEGIN
		id := 10; eoln := 0DX + 0AX;
		monoCluster := FALSE; gradualCommit := FALSE;
		physicalMemory := MAX(INTEGER); baseStack := WkStackBase(); trapTop := 0;
		try := Try; p.String := String; p.Ln := Ln
	END Setup;
BEGIN
	Trace.Write("WinKernel body begin");
	pageSize := WkPageSize();
	IF WkInstall() = 0 THEN WkExit(126) END;
	platform.tag := S.TYP(Platform);
	Trace.Write("before Kernel.SetPlatform");
	Kernel.SetPlatform(S.VAL(S.PTR, S.ADR(platform.p)));
	Trace.Write("after Kernel.SetPlatform");
	Kernel.SetErr(String, Ln)
(* WkExit removes the handler after Kernel.Quit has run all finalizers. *)
END WinKernel.
