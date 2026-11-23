MODULE DevLnkBase;
(**	project	= "BlackBox 2.0, new module"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Igor Dehtyarenko"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	IMPORT Files, Log, Dialog, Kernel, Librarian;

	TYPE
		ChmodHook* = POINTER TO ABSTRACT RECORD END;

	CONST
	
	(* targets *)
		tgtElfStatic = 0; tgtElfExe = 1; tgtElfDll = 2; tgtPeExe = 3;  tgtPeDll = 4; 

	(* fixup types *)
		fixAbs = 1; fixRel = 2;  fixCopy = 5;
		
	(* Symbol type *)
		typProc* = 0;
		typObj* = 1;

	(* OS *)
		osUnspecified* = 0;
		osLinux* = 1;
		osFreeBSD* = 2;
		osOpenBSD* = 3;
		osNetBSD* = 4;

	TYPE
		Name* = ARRAY 256 OF SHORTCHAR;
		SName = ARRAY 256 OF SHORTCHAR;
		STRING* = ARRAY OF SHORTCHAR;
		BYTES* = ARRAY OF BYTE;

		Section* = POINTER TO RECORD
			addr*:INTEGER;
			size*:INTEGER;
			bytes*: POINTER TO BYTES;
		END;

		Symbol* = POINTER TO RECORD
			next*: Symbol;
			sect*: Section;
			addr*: INTEGER;
			namIdx*: INTEGER;
			symIdx*: INTEGER;
			name*: SName;
			typ*: INTEGER;
		END;

		FixRec* = RECORD
			ftype* : INTEGER;
			offs*: INTEGER;
			sec*: Section;
		END;

		FixTable* = RECORD
			sec-: Section;
			tab-: POINTER TO ARRAY OF FixRec;
			len-: INTEGER
		END;

		StringTable* = RECORD
			tab-: POINTER TO ARRAY OF SHORTCHAR;
			len-: INTEGER
		END;

		Unwind* = POINTER TO RECORD
			next*: Unwind; first*, last*, offset*: INTEGER; bytes*: POINTER TO BYTES
		END;

		Module* =  POINTER TO RECORD
			next*: Module;
			name*: Name;
			data*: Section;
			code*: Section;
			unwind*: Unwind;
			vars*: Section;
			codeFix*: FixTable; 
			dataFix*: FixTable;	
			mDesc*: INTEGER;   
			termadr*: INTEGER; 
			isDll*: BOOLEAN; 
			exported*: BOOLEAN;
			exp*: Symbol;
		END;

	VAR
		error-: BOOLEAN;
		amd64*: BOOLEAN; (* AMD64 code and metadata remain in the low 2 GiB. *)
		bootInfoOffset*: INTEGER;

		target-: INTEGER;
		outPe-, outElf-, outDll- : BOOLEAN;
		dynaInit*: BOOLEAN;
		
		outputName*: Files.Name; outputType-: Files.Type;
		KernelName*, mainName*: Name;

		kernelMod*: Module;
		mainMod*: Module;
		modList-: Module;
		lastMod-: Module;
		initMod-:Module;
		
		dllList-: Module;

		modCount-:INTEGER;
		impCount-, impDllCount-: INTEGER;
		expCount- : INTEGER;
		expList-: Symbol;
		
		ImageBase*: INTEGER; 
		codeSize-, dataSize-, varsSize-: INTEGER;
		
		initOffs-, finiOffs-: INTEGER;
		
		pltEntrySize, pltIniSize : INTEGER;
		
		relCount-: INTEGER;
		relTab-: POINTER TO ARRAY OF INTEGER;
		
		opt*: RECORD
			imageBase*: INTEGER; 
			
			subsystem*: INTEGER; 
			
			elfStatic*: BOOLEAN;
			elfInterpreter*: ARRAY 128 OF SHORTCHAR;
			rpath*: ARRAY 128 OF SHORTCHAR;
			OSABI*: SHORTCHAR;
			OS*: INTEGER
		END;

	chmodHook: ChmodHook;
	
	PROCEDURE SetChmodHook* (hook: ChmodHook);
	BEGIN
		chmodHook := hook
	END SetChmodHook;
	
	PROCEDURE (hook: ChmodHook) Chmod* (name0: Files.Name; mode: SET), NEW, ABSTRACT;
	
	PROCEDURE Chmod* (name0: Files.Name; mode: SET);
	BEGIN
		IF chmodHook # NIL THEN
			chmodHook.Chmod(name0, mode)
		END
	END Chmod;
					

 PROCEDURE Init* (tgt: INTEGER);
	BEGIN
		error := FALSE;
		
		amd64 := FALSE; bootInfoOffset := -1;
		
		outPe := FALSE; outElf := FALSE; outDll := FALSE;
		dynaInit := FALSE;
		opt.imageBase := 0;
		opt.subsystem := 0; 
		opt.elfStatic := FALSE;
		opt.elfInterpreter := '';
		opt.OSABI := 0X;
		opt.OS := osUnspecified;
		opt.rpath := '';
		
		modCount:= 0;
		modList:= NIL;
		lastMod:= NIL;
		kernelMod:= NIL;
		mainMod:= NIL;
		initMod:= NIL;

		dllList:= NIL;
		impCount:=0; impDllCount:=0;

		expList:= NIL;
		expCount :=0;
		
		relCount := 0;
		
		ImageBase:= 0;
		varsSize := 0;
		dataSize := 0;
		codeSize := 0;
		outputName := "";
		mainName := "";
		KernelName := "Kernel";

		target := tgt;
		CASE tgt OF
		| tgtPeExe:	outputType := "exe"; outPe := TRUE; 
		| tgtPeDll:	outputType := "dll"; outPe := TRUE; outDll := TRUE; 
		| tgtElfExe: outputType := ""; outElf := TRUE; 
		| tgtElfDll: outputType := "so" ; outElf := TRUE; outDll := TRUE; 
		| tgtElfStatic: outputType := "out"; outElf := TRUE; opt.elfStatic := TRUE;
		END;
		pltIniSize := 0; pltEntrySize := 6;
(* got:		IF outElf THEN pltEntrySize := 16; pltIniSize := 16 END; *)
	END Init;
	
	PROCEDURE Error*(msg:ARRAY OF CHAR);
	BEGIN
		error := TRUE; 
		Log.String(msg);	Log.Ln; 
	END Error;

	PROCEDURE NewSection*(size: INTEGER): Section;  
	VAR sec: Section;
	BEGIN
		NEW(sec);
		sec.size := size;
		sec.addr := 0;
		RETURN sec
	END NewSection;

	PROCEDURE RegMod*(mod: Module);
	BEGIN
		IF amd64 & (LONG(codeSize) + dataSize + varsSize + mod.code.size + mod.data.size + mod.vars.size > 70000000H) THEN
			Error("PE64 image exceeds supported low-address range"); RETURN
		END;
		IF mod.name$ = KernelName THEN kernelMod := mod END;
		IF mod.name$ = mainName THEN mainMod := mod END;
		IF lastMod = NIL THEN 
			modList := mod;
		ELSE
			lastMod.next := mod;
		END;
		lastMod := mod;
		INC(modCount);
		INC(codeSize, mod.code.size);
		INC(varsSize, mod.vars.size);
		INC(dataSize, mod.data.size);
	END RegMod;

	PROCEDURE ThisDll*(IN name: STRING): Module;
		VAR i, j: INTEGER; lib: Module; 
	BEGIN
		lib := dllList;
		WHILE (lib # NIL) & (lib.name$ # name) DO lib := lib.next END;
		IF lib = NIL THEN
			NEW(lib);
			lib.name := name$;
			lib.isDll := TRUE;
			lib.code := NewSection(0);
			NEW(lib.exp); 
				(* name witout '$' *)
				i:=0; j:= 1;
				WHILE name[j] # 0X DO lib.exp.name[i] := name[j]; INC(i); INC(j) END;
				lib.exp.name[i] := 0X;
				lib.exp.addr := -1;
			lib.next := dllList;
			dllList := lib;
			INC(impDllCount); 
		END;
		RETURN lib
	END ThisDll;

	PROCEDURE ImportProc*(mod: Module; IN name: STRING; OUT adr: INTEGER);
	VAR sym, prev: Symbol;
	BEGIN
		prev :=  mod.exp; 
		sym := prev.next;
		WHILE (sym # NIL) & (sym.name # name) DO prev := sym; sym := prev.next END;
		IF (sym = NIL) THEN
			NEW(sym); 
			sym.name := name$; 
			sym.addr := mod.code.size;
			prev.next := sym;
			INC(mod.code.size, pltEntrySize);
			INC(impCount); 
		END;
		adr := sym.addr;
	END ImportProc;

	PROCEDURE ExportProc*(mod: Module; IN name: STRING; addr: INTEGER);
		VAR sym: Symbol;
	BEGIN
		NEW(sym); 
		sym.name := mod.name$ + '_' + name$; 
		sym.sect := mod.code;
		sym.addr := addr;
		sym.next := expList;
		sym.typ  := typProc;
		expList := sym; 
		INC(expCount);
	END ExportProc;

	PROCEDURE ExportVar*(mod: Module; IN name: STRING; addr: INTEGER);
		VAR sym: Symbol;
	BEGIN
		NEW(sym); 
		sym.name := name$; 
		sym.sect := mod.data;
		sym.addr := addr;
		sym.next := expList; 
		sym.typ  := typObj;
		expList := sym; 
		INC(expCount);
	END ExportVar;


  PROCEDURE InitFixTable*(VAR t: FixTable; sec: Section);
	BEGIN
		t.sec := sec;
		t.len := 0;
	END InitFixTable;

	PROCEDURE CheckFixSize(VAR t: FixTable);
	VAR newtab: POINTER TO ARRAY OF FixRec;
			i:INTEGER; 
	BEGIN
		IF t.tab = NIL THEN
  		NEW(t.tab, 256);	
		ELSIF t.len >= LEN(t.tab) THEN  (* enlarge *)
			NEW(newtab, 2 * LEN(t.tab));
			i := 0; 
			WHILE i < LEN(t.tab) DO newtab[i] := t.tab[i]; INC(i) END;
			t.tab := newtab
		END;
	END CheckFixSize;

	PROCEDURE AddFix(VAR t: FixTable; addr, fixtype: INTEGER; sec: Section);
	BEGIN
		IF sec = NIL THEN Error("unresolved fixup target"); RETURN END;
		CheckFixSize(t);
		t.tab[t.len].offs := addr;
		t.tab[t.len].ftype := fixtype; 
		t.tab[t.len].sec:= sec;
		INC(t.len)
	END AddFix;

	PROCEDURE FixCode* (mod: Module; addr: INTEGER; sec: Section);
	BEGIN
		AddFix(mod.codeFix, addr, fixAbs, sec); INC(relCount)
	END FixCode;

	PROCEDURE FixRel* (mod: Module; addr: INTEGER; sec: Section);
	BEGIN
		AddFix(mod.codeFix, addr, fixRel, sec);
	END FixRel;
	
	PROCEDURE FixData* (mod: Module; addr: INTEGER; sec: Section);
	BEGIN
		AddFix(mod.dataFix, addr, fixAbs, sec); INC(relCount)
	END FixData;

	PROCEDURE FixCopy* (mod: Module; addr: INTEGER; sec: Section);
	BEGIN
		AddFix(mod.dataFix, addr, fixCopy, sec); INC(relCount)
	END FixCopy;

	PROCEDURE Get4* (VAR d:BYTES; a: INTEGER): INTEGER;
	BEGIN
		RETURN ((d[a+3] * 256 + d[a+2] MOD 256) * 256 + d[a+1] MOD 256) * 256 + d[a] MOD 256
	END Get4;

	PROCEDURE Put4* (VAR d:BYTES; a: INTEGER; x: INTEGER);
	BEGIN
		d[a] := SHORT(SHORT(x));
		d[a+1] := SHORT(SHORT(x DIV 100H));
		d[a+2] := SHORT(SHORT(x DIV 10000H));
		d[a+3] := SHORT(SHORT(x DIV 1000000H))
	END Put4;
 
	PROCEDURE Put8* (VAR d: BYTES; a: INTEGER; x: LONGINT);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO 7 DO d[a+i] := SHORT(SHORT(SHORT(x))); x := x DIV 256 END
	END Put8;
	
	PROCEDURE AddReloc* (addr: INTEGER);
	BEGIN
		relTab[relCount] := addr; 
		INC(relCount)
	END AddReloc;

	PROCEDURE DoFixups*;
		VAR  mod: Module; 
	
		PROCEDURE DoFix (t: FixTable);
		VAR i, offs, old, new: INTEGER; wide: LONGINT; tsec, ssec: Section; 
		BEGIN
			tsec := t.sec;
			FOR i := 0 TO t.len - 1 DO
				offs := t.tab[i].offs;
				ssec := t.tab[i].sec;
				old := Get4(tsec.bytes, offs);
				CASE t.tab[i].ftype OF
				| fixAbs:
					wide := LONG(ssec.addr) + old;
					IF amd64 & ((wide < 0) OR (wide > MAX(INTEGER))) THEN
						Error("absolute address outside low 2 GiB");
						RETURN
					END;
					new := SHORT(wide);
					AddReloc(t.sec.addr + offs)
				| fixRel:
					wide := LONG(ssec.addr) - t.sec.addr + old;
					IF amd64 & ((wide < MIN(INTEGER)) OR (wide > MAX(INTEGER))) THEN
						Error("RIP-relative displacement overflow");
						RETURN
					END;
					new := SHORT(wide);
				| fixCopy:
					IF (ssec.bytes = NIL) OR (old < 0) OR (old > ssec.size - 4) THEN
						Error("invalid copy fixup");
						RETURN
					END;
					IF amd64 & (old > ssec.size - 8) THEN
						Error("invalid 64-bit copy fixup");
						RETURN
					END;
					new := Get4(ssec.bytes, old);
					IF amd64 & (Get4(ssec.bytes, old + 4) # 0) THEN
						Error("copy fixup exceeds low address range")
					END;
					IF new # 0 THEN AddReloc(t.sec.addr + offs) END;
				END;
				IF amd64 & (t.tab[i].ftype # fixRel) THEN
					Put8(tsec.bytes, offs, new)
				ELSE
					Put4(tsec.bytes, offs,  new)
				END
			END
		END DoFix;
	BEGIN
		NEW(relTab, relCount + impCount);
		relCount := 0;
		DoFix(initMod.codeFix);
		mod := modList;
		WHILE (mod # NIL) DO 
			DoFix(mod.dataFix);
			DoFix(mod.codeFix);
			mod := mod.next 
		END;
	END DoFixups;

	PROCEDURE SortReloc*;
		PROCEDURE QuickSort(l, r: INTEGER);
			VAR i, j, x, t: INTEGER;
		BEGIN
			i := l; j := r; 
			x := relTab[(l + r) DIV 2];
			REPEAT
				WHILE relTab[i] < x DO INC(i) END;
				WHILE relTab[j] > x DO DEC(j) END;
				IF i <= j THEN 
					t := relTab[i]; relTab[i] := relTab[j]; relTab[j] := t; 
					INC(i); DEC(j) 
				END
			UNTIL i > j;
			IF l < j THEN QuickSort(l, j) END;
			IF i < r THEN QuickSort(i, r) END
		END QuickSort;
	BEGIN
		IF relCount > 1 THEN QuickSort(0, relCount - 1) END
	END SortReloc;


	PROCEDURE InitStringTable* (VAR t: StringTable; size:INTEGER);
	BEGIN
		IF (size > 0) & ((t.tab = NIL)  OR (LEN(t.tab) < size)) THEN
  		NEW(t.tab, size);	
		END;
		t.len := 0;
	END InitStringTable;

	PROCEDURE CheckStrSize (VAR t: StringTable; len:INTEGER);
	VAR newtab: POINTER TO ARRAY OF SHORTCHAR;
			i:INTEGER; 
	BEGIN
		IF t.tab = NIL THEN
  		NEW(t.tab, 500);	
		ELSIF t.len + len >= LEN(t.tab) THEN  (* enlarge *)
			NEW(newtab, 2 * LEN(t.tab));
			i := 0; 
			WHILE i < LEN(t.tab) DO newtab[i] := t.tab[i]; INC(i) END;
			t.tab := newtab
		END;
	END CheckStrSize;

	PROCEDURE AddName* (VAR t: StringTable; IN name: STRING; OUT idx: INTEGER);
		VAR i,len : INTEGER;
	BEGIN
		len:= LEN(name$);
		CheckStrSize(t, len);
		idx := t.len;
		i := 0;
		WHILE name[i] # 0X DO t.tab[t.len] := name[i]; INC(t.len); INC(i)	END;
		t.tab[t.len] := 0X; INC(t.len);
		IF ODD(t.len) THEN t.tab[t.len] := 0X; INC(t.len) END
	END AddName;

	PROCEDURE AddHintName* (VAR t: StringTable; hint: INTEGER; IN name: STRING; OUT idx: INTEGER);
		VAR i: INTEGER;
	BEGIN
		CheckStrSize(t, 2);
		idx := t.len;
		t.tab[t.len] := SHORT(CHR(hint)); INC(t.len);
		t.tab[t.len] := SHORT(CHR(hint DIV 256)); INC(t.len);
		AddName(t, name, i);
	END AddHintName;	

	PROCEDURE AddDllName* (VAR t: StringTable; IN name, ext: STRING; VAR idx: INTEGER);
		VAR i, dot: INTEGER; 
	BEGIN
		i := 0; dot := 0;
		WHILE name[i] # 0X DO IF name[i] = "." THEN dot := i END; INC(i) END;
		IF dot > 0	THEN
			AddName(t, name, idx);
		ELSE	
			AddName(t, name$ + ext$, idx);
		END;
	END AddDllName;	
	
	PROCEDURE WriteStringTable* (out: Files.Writer; VAR t: StringTable);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE i # t.len DO  out.WriteByte(SHORT(ORD(t.tab[i]))); INC(i) END
	END WriteStringTable;
		
	PROCEDURE Aligned* (pos, alignment: INTEGER): INTEGER;
	BEGIN
		RETURN (pos + (alignment - 1)) DIV alignment * alignment
	END Aligned;

	PROCEDURE ThisObjFile* (IN modname: ARRAY OF CHAR): Files.File; 
	VAR loc: Files.Locator; fname: Files.Name; 
	BEGIN
		Librarian.lib.GetSpec(modname, "Code", loc, fname);
		RETURN Files.stdDir.Old(loc, fname, TRUE)
	END ThisObjFile;


	PROCEDURE CountDllSizes;
		VAR lib: Module; 
	BEGIN
		lib := dllList; 
		INC(codeSize, pltIniSize);
		WHILE lib # NIL DO
			INC(codeSize, lib.code.size);
			lib := lib.next 
		END;
	END CountDllSizes;
	
	PROCEDURE GenInitCode;
		VAR init, fini, dllmain : INTEGER;
			cp: INTEGER; bytes: POINTER TO BYTES;

		PROCEDURE Emit1(c: SHORTCHAR);
		BEGIN
			bytes[cp] := SHORT(ORD(c));
			INC(cp);
		END Emit1;

		PROCEDURE Emit2(x0, x1: SHORTCHAR);
		BEGIN
			Emit1(x0);
			Emit1(x1);
		END Emit2;

		PROCEDURE Emit3(x0, x1, x2: SHORTCHAR);
		BEGIN
			Emit1(x0);
			Emit1(x1);
			Emit1(x2)
		END Emit3;

		PROCEDURE Emit1W(c: SHORTCHAR; x: INTEGER);
		BEGIN
			bytes[cp] := SHORT(ORD(c));
			bytes[cp+1] := SHORT(SHORT(x));
			bytes[cp+2] := SHORT(SHORT(x DIV 100H));
			bytes[cp+3] := SHORT(SHORT(x DIV 10000H));
			bytes[cp+4] := SHORT(SHORT(x DIV 1000000H));
			INC(cp,5);
		END Emit1W;
		
		PROCEDURE InitDynamic;
		BEGIN
			Emit1W(0BBX, lastMod.mDesc);   (* mov ebx, modlist *)
				FixCode(initMod, cp - 4,  lastMod.data);
			Emit1W(0E8X, - cp - 5);	(* call main *)
				FixRel(initMod, cp-4,  mainMod.code);
		END InitDynamic;

		PROCEDURE InitStatic(mod: Module);
		BEGIN
			WHILE mod # NIL DO 
				IF (mod = kernelMod) OR (mod = mainMod) THEN
					Emit1W(0BBX, lastMod.mDesc);  (*  mov ebx, modlist *)
						FixCode(initMod, cp-4, lastMod.data);
				END;
				Emit1W(0E8X, - cp - 5);  (* call body *)
					FixRel(initMod, cp-4,  mod.code);
				mod := mod.next 
			END;
		END InitStatic; (* 10 + n*5 *)

		PROCEDURE FiniDynamic;
		BEGIN
			IF mainMod.termadr # 0 THEN
				Emit1W(0E8X, mainMod.termadr - cp - 5);  (* call terminator *)
				FixRel(initMod, cp-4,  mainMod.code);
			END;
			Emit1(0C3X); (* ret *)
		END FiniDynamic;

		PROCEDURE FiniStatic(mod: Module);
		BEGIN
			IF mod # NIL THEN  
				FiniStatic(mod.next);
				IF mod.termadr # 0 THEN
					Emit1W(0E8X, mod.termadr- cp - 5);  (* call terminator *)
					FixRel(initMod, cp-4, mod.code);
				END;
			END;
		END FiniStatic; (* 1 + n*5 *)

		PROCEDURE WinDllMain;
		BEGIN
		Emit1(055X);	(* push ebp *)
		Emit2(089X, 0E5X);	(* mov ebp, esp *)
		Emit3(08BX, 045X, 0CX);	(* mov eax, [12, ebp] *)
		Emit3(083X, 0F8X, 01X);	(* cmp  eax, 1 *)
		Emit2(074X, 0BX);	(* je L1 *)
		Emit2(085X, 0C0X);	(* test eax,eax *)
		Emit2(075X, 0CX);	(* jnz  R *)
			(* L0: *)                     
		Emit1W(0E8X, fini - cp - 5);	(* call fini *)	
		Emit2(0EBX, 05X);	(* jmp R *)
			(* L1: *)                     
		Emit1W(0E8X, init - cp - 5);	(* call init *)	
			(* R: *)                      
		Emit1W(0B8X, 1);	(* mov eax,1 *)	
		Emit1(0C9X);	(* leave *)
		Emit3(0C2X, 0CX, 0X);	(* ret 12 *)
		END WinDllMain; (* 24 *)

	BEGIN
		NEW(initMod);
		initMod.code := NewSection(50 + 10 * modCount);
		NEW(initMod.code.bytes, initMod.code.size);
		InitFixTable(initMod.codeFix, initMod.code);
		bytes := initMod.code.bytes;
		cp := 0;
		init := cp;
		IF dynaInit THEN InitDynamic ELSE InitStatic(modList) END;
		IF outDll THEN Emit1(0C3X) END; (* ret *)
		
		fini := cp;
		IF dynaInit THEN FiniDynamic ELSE FiniStatic(modList) END;
		Emit1(0C3X); (* ret *)

		initOffs:= init; 
		finiOffs:= fini;
		IF outDll & outPe THEN
			dllmain := cp;
			WinDllMain; 
			initOffs:= dllmain; 
		END;
		initMod.code.size := cp;
		INC(codeSize, initMod.code.size);
	END GenInitCode;
	
	PROCEDURE GenInitCode64;
		VAR cp: INTEGER; m, exitDll: Module; exitAdr: INTEGER; u: Unwind;
		PROCEDURE B (b: INTEGER);
		BEGIN initMod.code.bytes[cp] := SHORT(SHORT(b)); INC(cp) END B;
		PROCEDURE D (x: INTEGER);
		BEGIN Put4(initMod.code.bytes, cp, x); INC(cp, 4) END D;
		PROCEDURE Call (sec: Section; offset: INTEGER);
		BEGIN B(0E8H); D(offset - cp - 4); FixRel(initMod, cp-4, sec) END Call;
	BEGIN
		(* Process entry never returns to Windows: CP code need not preserve the
		   Windows nonvolatile registers. Every external call gets shadow space. *)
		exitDll := ThisDll("$kernel32.dll"); ImportProc(exitDll, "ExitProcess", exitAdr);
		NEW(initMod); initMod.code := NewSection(64 + 32 * modCount);
		NEW(initMod.code.bytes, initMod.code.size); InitFixTable(initMod.codeFix, initMod.code);
		cp := 0; B(55H); B(48H); B(89H); B(0E5H); (* push rbp; mov rbp,rsp *)
		B(48H); B(83H); B(0E4H); B(0F0H); (* and rsp,-16 *)
		B(48H); B(83H); B(0ECH); B(32); (* shadow space *)
		m := modList;
		WHILE m # NIL DO
			IF (m = kernelMod) OR (m = mainMod) THEN
				B(48H); B(0BBH); Put8(initMod.code.bytes, cp, lastMod.mDesc);
				FixCode(initMod, cp, lastMod.data); INC(cp, 8) (* mov rbx,modlist *)
			END;
			Call(m.code, 0); m := m.next
		END;
		(* Terminators run in reverse dependency order. *)
		m := lastMod;
		WHILE m # NIL DO
			IF m.termadr # 0 THEN Call(m.code, m.termadr) END;
			exitDll := modList;
			IF exitDll = m THEN m := NIL
			ELSE WHILE exitDll.next # m DO exitDll := exitDll.next END; m := exitDll END
		END;
		exitDll := ThisDll("$kernel32.dll");
		B(31H); B(0C9H); Call(exitDll.code, exitAdr); B(0CCH);
		initOffs := 0; finiOffs := 0;
		initMod.code.size := cp; INC(codeSize, cp);
		NEW(u); u.first := 0; u.last := cp; NEW(u.bytes, 8);
		u.bytes[0] := 1; u.bytes[1] := 4; u.bytes[2] := 2; u.bytes[3] := 5;
		u.bytes[4] := 4; u.bytes[5] := 3; u.bytes[6] := 1; u.bytes[7] := 50H;
		initMod.unwind := u
	END GenInitCode64;

	PROCEDURE GenInitElf64;
		VAR cp, entry, main, libcMain, exitAdr: INTEGER; m, lib, prev: Module;
		PROCEDURE B (x: INTEGER);
		BEGIN initMod.code.bytes[cp] := SHORT(SHORT(x)); INC(cp) END B;
		PROCEDURE D (x: INTEGER);
		BEGIN Put4(initMod.code.bytes, cp, x); INC(cp, 4) END D;
		PROCEDURE Call (sec: Section; offset: INTEGER);
		BEGIN B(0E8H); D(offset - cp - 4); FixRel(initMod, cp-4, sec) END Call;
	BEGIN
		lib := ThisDll("$libc.so.6");
		ImportProc(lib, "__libc_start_main", libcMain); ImportProc(lib, "exit", exitAdr);
		NEW(initMod); initMod.code := NewSection(256 + 16 * modCount);
		NEW(initMod.code.bytes, initMod.code.size); InitFixTable(initMod.codeFix, initMod.code);
		cp := 0;
		(* SysV process entry, including the dynamic linker's finalizer. *)
		B(49H); B(89H); B(0D1H); B(5EH); B(48H); B(89H); B(0E2H);
		B(48H); B(83H); B(0E4H); B(0F0H); B(50H); B(54H);
		B(45H); B(31H); B(0C0H); B(31H); B(0C9H);
		B(48H); B(8DH); B(3DH); entry := cp; D(0);
		Call(lib.code, libcMain); B(0F4H);
		main := cp; Put4(initMod.code.bytes, entry, main-entry-4);
		B(55H); B(48H); B(89H); B(0E5H);
		B(48H); B(83H); B(0E4H); B(0F0H); B(48H); B(83H); B(0ECH); B(32);
		(* BootInfo lives in the active startup frame: modList, argv, argc. *)
		B(48H); B(0B8H); Put8(initMod.code.bytes, cp, lastMod.mDesc);
		FixCode(initMod, cp, lastMod.data); INC(cp, 8);
		B(48H); B(89H); B(04H); B(24H);
		B(48H); B(89H); B(74H); B(24H); B(8);
		B(89H); B(7CH); B(24H); B(16);
		B(48H); B(89H); B(0E0H);
		B(48H); B(89H); B(05H); D(bootInfoOffset-cp-4);
		FixRel(initMod, cp-4, kernelMod.vars);
		m := modList;
		WHILE m # NIL DO Call(m.code, 0); m := m.next END;
		m := lastMod;
		WHILE m # NIL DO
			IF m.termadr # 0 THEN Call(m.code, m.termadr) END;
			prev := modList;
			IF prev = m THEN m := NIL
			ELSE WHILE prev.next # m DO prev := prev.next END; m := prev END
		END;
		B(31H); B(0FFH); Call(lib.code, exitAdr); B(0F4H);
		initOffs := 0; finiOffs := 0; initMod.code.size := cp; INC(codeSize, cp)
	END GenInitElf64;

	PROCEDURE BeginLinking*;
	BEGIN
		IF error THEN RETURN END;
		IF lastMod = NIL THEN Error("no modules to link"); RETURN END;
		IF mainMod = NIL THEN mainMod := lastMod END;
		IF outputName = "" THEN 
			outputName := mainMod.name$;
			Files.stdDir.GetFileName(outputName, outputType, outputName);
		END;
		IF amd64 & outElf THEN
			GenInitElf64
		ELSIF amd64 THEN
			GenInitCode64
		ELSE
			GenInitCode
		END;
		CountDllSizes;
	END BeginLinking;


	PROCEDURE SetAddr* (codeBase, dataBase, varsBase: INTEGER);
		VAR  mod: Module; sym: Symbol; 
	BEGIN
		mod := initMod;
		mod.code.addr := codeBase; INC(codeBase, mod.code.size);
		mod := modList;
		WHILE (mod # NIL) DO
			mod.code.addr := codeBase; INC(codeBase, mod.code.size);
			mod.data.addr := dataBase; INC(dataBase, mod.data.size);
			mod.vars.addr := varsBase; INC(varsBase, mod.vars.size);
			mod := mod.next 
		END;
		INC(codeBase, pltIniSize);
		mod := dllList;
		WHILE (mod # NIL) DO 
			mod.code.addr := codeBase; INC(codeBase, mod.code.size);
			sym := mod.exp.next; 
			WHILE sym # NIL DO
				INC(sym.addr, mod.code.addr);
				sym := sym.next
			END;
			mod := mod.next 
		END;
		sym := expList;
		WHILE sym # NIL DO 
			INC(sym.addr, sym.sect.addr);
			sym := sym.next
		END;		
	END SetAddr;

BEGIN
	IF Dialog.platform = Dialog.linux THEN
		Kernel.LoadMod("DevChmod__Lin")
	ELSIF Dialog.platform = Dialog.openbsd THEN
		Kernel.LoadMod("DevChmod__Obsd")
	ELSIF Dialog.platform = Dialog.freebsd THEN
		Kernel.LoadMod("DevChmod__Fbsd")
	ELSIF Dialog.platform = Dialog.netbsd THEN
		Kernel.LoadMod("DevChmod__Nbsd")
	END
END DevLnkBase.
