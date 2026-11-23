MODULE DevLnkWritePe;
(**	project	= "BlackBox 2.0"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Igor Dehtyarenko"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	
	IMPORT LB := DevLnkBase, Files, Res := DevLnkResources;

	TYPE 
		Module = LB.Module;

		Section = RECORD
			size: INTEGER;
			offs: INTEGER;
			addr: INTEGER; (* rva *)
		END;
		
	VAR
		outFile: Files.File;
		out: Files.Writer;

	CONST
		cui = 3; gui = 2;
		ExeImageBase = 400000H; DllImageBase = 10000000H;
		SizeOfHeapReserve = 400000H;  (* primary heap *)  
		SizeOfHeapCommit =   10000H;
		SizeOfStackReserve = 200000H;
		SizeOfStackCommit=   10000H;
			
		SectionAlignment = 1000H;
		FileAlignment = 512;
		SizeOfHeaders = 1024;
		expDirSize = 40;
		
	VAR
		subsystem: INTEGER;  
		AddressOfEntryPoint: INTEGER;		
		NumberOfSections: INTEGER;
		TimeDateStamp: INTEGER;
		SizeOfImage : INTEGER;
		code, data, vars, rsrc, impo, expo, pdata, xdata, relo: Section; 
		unwindCount: INTEGER;
		impDirSize: INTEGER;
		expStrTable, impStrTable: LB.StringTable;
		outNameStrIndex: INTEGER;
		exeFlags: SET;

	PROCEDURE WriteCh (ch: SHORTCHAR);
	BEGIN
		out.WriteByte(SHORT(ORD(ch)))
	END WriteCh;

	PROCEDURE Write2 (x: INTEGER);
	BEGIN
		out.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		out.WriteByte(SHORT(SHORT(x MOD 256)))
	END Write2;

	PROCEDURE Write4 (x: INTEGER);
	BEGIN
		out.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		out.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		out.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		out.WriteByte(SHORT(SHORT(x MOD 256)))
	END Write4;

	PROCEDURE WritePtr (x: INTEGER);
	BEGIN Write4(x); IF LB.amd64 THEN Write4(0) END END WritePtr;

	PROCEDURE WriteStr(IN s: ARRAY OF SHORTCHAR; len: INTEGER);
		VAR i: INTEGER;
	BEGIN 
		i := 0;
		WHILE s[i] # 0X DO out.WriteByte(SHORT(ORD(s[i]))); INC(i) END;
		WHILE i < len DO out.WriteByte(0); INC(i) END
	END WriteStr;

	PROCEDURE PadTo(pos: INTEGER);
	BEGIN
		WHILE out.Pos() < pos DO out.WriteByte(0) END;
	END PadTo;

(**********************************)	
	PROCEDURE Init*;
		CONST relocs_stripped = 0; executable_image = 1; 
					line_nums_stripped = 2; local_syms_stripped = 3;
					machine_32bit = 8; debug_stripped = 9; 
					removable_run_from_swap = 10; net_run_from_swap = 11;
					system = 12; dll = 13; up_system_only = 14;
	BEGIN
		LB.InitStringTable(impStrTable, 1000);
		LB.InitStringTable(expStrTable, 0);

		subsystem := gui; (* *)
		IF LB.opt.subsystem # 0 THEN 
			subsystem := LB.opt.subsystem;
		END;
		
		exeFlags := {executable_image, line_nums_stripped, debug_stripped};
		IF LB.amd64 THEN INCL(exeFlags, 5) END; (* IMAGE_FILE_LARGE_ADDRESS_AWARE *)
		IF LB.outDll THEN
			INCL(exeFlags,  dll);
			LB.ImageBase := DllImageBase;
		ELSE 
			LB.ImageBase := ExeImageBase;
		END;
		IF LB.opt.imageBase > 0 THEN 
			LB.ImageBase := LB.opt.imageBase;
		END;

		IF LB.amd64 & ((LB.ImageBase < 0) OR
			(LONG(LB.ImageBase) + LB.codeSize + LB.dataSize + LB.varsSize + 100000H > MAX(INTEGER))) THEN
			LB.Error("PE64 image must fit below 2 GiB"); RETURN
		END;
		NumberOfSections := 0;

		code.size := LB.codeSize; 
		data.size := LB.dataSize;
		vars.size := LB.varsSize;

		code.addr := SectionAlignment;
		data.addr := LB.Aligned(code.addr + code.size, SectionAlignment);
		vars.addr := LB.Aligned(data.addr + data.size, SectionAlignment);

		AddressOfEntryPoint := code.addr + LB.initOffs;
	END Init;
	
(**********************************)	
	PROCEDURE GetBases*(OUT codeBase, dataBase, varsBase: INTEGER);
	BEGIN
		codeBase := code.addr + LB.ImageBase;
		dataBase := data.addr + LB.ImageBase;
		varsBase := vars.addr + LB.ImageBase;
	END GetBases;

(**********************************)	
	PROCEDURE CollectImp;
		VAR lib: Module;  sym: LB.Symbol; 
				symIdx: INTEGER;
	BEGIN
		symIdx := 0;
		lib := LB.dllList; 
		WHILE lib # NIL DO
			LB.AddDllName(impStrTable, lib.exp.name, ".dll", lib.exp.namIdx);
			lib.exp.symIdx := symIdx;
			sym := lib.exp.next; 
			WHILE sym # NIL DO
				LB.AddHintName(impStrTable, 0, sym.name, sym.namIdx);
				sym.symIdx := symIdx;
				INC(symIdx);
				sym := sym.next
			END;
			INC(symIdx);
			lib := lib.next
		END;
		impDirSize := 20 * (LB.impDllCount + 1);
		IF LB.amd64 THEN impDirSize := LB.Aligned(impDirSize, 8) END;
		impo.size := impDirSize + 8 * (LB.impCount + LB.impDllCount) + impStrTable.len;
		IF LB.amd64 THEN INC(impo.size, 8 * (LB.impCount + LB.impDllCount)) END;
	END CollectImp;

	PROCEDURE SortExports;
		VAR a, b: LB.Symbol; n: LB.Name; addr, typ: INTEGER; sec: LB.Section;
	BEGIN
		(* The PE name pointer table must be lexicographically sorted. Keep the
		   public linked-list head stable for other writers. *)
		a := LB.expList;
		WHILE a # NIL DO
			b := a.next;
			WHILE b # NIL DO
				IF b.name < a.name THEN
					n := a.name$; a.name := b.name; b.name := n$;
					addr := a.addr; a.addr := b.addr; b.addr := addr;
					sec := a.sect; a.sect := b.sect; b.sect := sec;
					typ := a.typ; a.typ := b.typ; b.typ := typ
				END;
				b := b.next
			END;
			a := a.next
		END
	END SortExports;

	PROCEDURE CollectExport;
		VAR sym: LB.Symbol;  
	BEGIN
		IF LB.expCount > 0 THEN
			SortExports;
			LB.AddName(expStrTable, SHORT(LB.outputName$), outNameStrIndex);
			sym := LB.expList;
			WHILE sym # NIL DO 
				LB.AddName(expStrTable, sym.name, sym.namIdx);
				sym := sym.next
			END;
			expo.size := expDirSize + 10 * LB.expCount + expStrTable.len;
		ELSE	
			expo.size := 0;
		END;
	END CollectExport;

	PROCEDURE CalculateLayout;
		VAR  m: Module; u: LB.Unwind; x: INTEGER;
	BEGIN
		CollectImp;
		CollectExport;
		
		unwindCount := 0; pdata.size := 0; xdata.size := 0;
		IF LB.amd64 THEN
			m := LB.initMod;
			WHILE m # NIL DO u := m.unwind;
				WHILE u # NIL DO INC(unwindCount); INC(xdata.size, LEN(u.bytes) + 4); u := u.next END;
				IF m = LB.initMod THEN m := LB.modList ELSE m := m.next END
			END;
			pdata.size := 12 * unwindCount; x := 0; m := LB.initMod;
			WHILE m # NIL DO u := m.unwind;
				WHILE u # NIL DO u.offset := x; INC(x, LEN(u.bytes) + 4); u := u.next END;
				IF m = LB.initMod THEN m := LB.modList ELSE m := m.next END
			END
		END;
		
		(* rva's *)
		impo.addr := LB.Aligned(vars.addr + vars.size, SectionAlignment);
		expo.addr := LB.Aligned(impo.addr + impo.size, SectionAlignment);
		rsrc.size := Res.Size();
		rsrc.addr := LB.Aligned(expo.addr + expo.size, SectionAlignment);
		pdata.addr := LB.Aligned(rsrc.addr + rsrc.size, SectionAlignment);
		xdata.addr := LB.Aligned(pdata.addr + pdata.size, SectionAlignment);
		relo.addr := LB.Aligned(xdata.addr + xdata.size, SectionAlignment);

		(* file pos's *)
		code.offs := LB.Aligned(SizeOfHeaders, FileAlignment); 
		data.offs := LB.Aligned(code.offs + code.size, FileAlignment);
		vars.offs := 0;
		impo.offs := LB.Aligned(data.offs + data.size, FileAlignment);
		expo.offs := LB.Aligned(impo.offs + impo.size, FileAlignment);
		rsrc.offs := LB.Aligned(expo.offs + expo.size, FileAlignment);
		pdata.offs := LB.Aligned(rsrc.offs + rsrc.size, FileAlignment);
		xdata.offs := LB.Aligned(pdata.offs + pdata.size, FileAlignment);
		relo.offs := LB.Aligned(xdata.offs + xdata.size, FileAlignment);
		
		IF pdata.size = 0 THEN pdata.addr := 0; pdata.offs := 0; xdata.addr := 0; xdata.offs := 0 END;
		IF expo.size = 0 THEN expo.addr := 0; expo.offs := 0 END;
		IF rsrc.size = 0 THEN rsrc.addr := 0; rsrc.offs := 0 END;
	END CalculateLayout;

(**********************************)
	PROCEDURE WriteSectionHeaders;
	CONST cnt_code=5; cnt_data= 6; cnt_udata= 7; discard = 25; exec = 29; read = 30; write = 31;
	TYPE 
		SectName = ARRAY 8 OF SHORTCHAR;
		
		PROCEDURE SectionHeader(IN scn: Section; name:SectName; flags:SET);
		BEGIN
			IF scn.addr # 0 THEN
				WriteStr(name, 8); 
				Write4(scn.size); (* VirtualSize *)
				Write4(scn.addr);  (* VirtualAddress *)
				IF scn.offs = 0 THEN
					Write4(0)
				ELSE
					Write4(LB.Aligned(scn.size, FileAlignment))  (* SizeOfRawData *)
				END;
				Write4(scn.offs);  (* PointerToRawData *)
				Write4(0); (* PointerToRelocations *)
				Write4(0); (* PointerToLinenumbers *)
				Write4(0); (* NumberOfRelocations, NumberOfLinenumbers *)
				Write4(ORD(flags)); 
				
				INC(NumberOfSections)
			END 
		END SectionHeader;
		
	BEGIN
		SectionHeader(code, ".text",  {cnt_code, read, exec});
		SectionHeader(data, ".data",  {cnt_data, read, write});
		SectionHeader(vars, ".var",   {cnt_udata, read, write});
		SectionHeader(impo, ".idata", {cnt_data, read, write});
		SectionHeader(expo, ".edata", {cnt_data, read, write});
		SectionHeader(rsrc, ".rsrc",  {cnt_data, read});
		SectionHeader(pdata, ".pdata", {cnt_data, read});
		SectionHeader(xdata, ".xdata", {cnt_data, read});
		SectionHeader(relo, ".reloc", {cnt_data, read, discard});
	END WriteSectionHeaders;

	PROCEDURE WriteHeader;
	CONST SizeOfOptionalHeader = 224;
	
		PROCEDURE WriteDosStub;
		CONST sizeOfStub = 128; (* >= 64 *)
		BEGIN
			WriteStr('MZ',60);
			Write4(sizeOfStub); (* offset to the PE signature*)
			PadTo(sizeOfStub);
		END WriteDosStub;
		
		PROCEDURE WriteDirEntry(rva, Size: INTEGER);
		BEGIN
			Write4(rva); Write4(Size);
		END WriteDirEntry;
	
	BEGIN
		WriteDosStub;
		WriteStr("PE", 4); (* signature *)
		(* COFF header *)
		IF LB.amd64 THEN
			Write2(8664H)
		ELSE
			Write2(014CH) (* Machine  i386 *)
		END;
		Write2(NumberOfSections);
		Write4(TimeDateStamp);
		Write4(0); 
		Write4(0);
		IF LB.amd64 THEN
			Write2(240)
		ELSE
			Write2(SizeOfOptionalHeader)
		END;
		Write2(ORD(exeFlags)) ;
		(* 'Optional' header *)
		IF LB.amd64 THEN
			Write2(20BH)
		ELSE
			Write2(10BH) (* magic (normal ececutable file) *)
		END;
		WriteCh(2X); WriteCh(5X); (* linker version 2.5 *) 
		Write4(code.size); (* SizeOfCode *)
		Write4(data.size); (* SizeOfInitializedData  *)
		Write4(vars.size); (* SizeOfUninitializedData *)
		Write4(AddressOfEntryPoint); 
		Write4(code.addr); (* BaseOfCode rva *)
		IF ~LB.amd64 THEN
			Write4(data.addr); (* BaseOfData rva only in PE32 *)
		END;
		WritePtr(LB.ImageBase); 
		Write4(SectionAlignment); 
		Write4(FileAlignment); 
		Write2(5); Write2(0); (* OperatingSystemVersion (Major, Minor) *)
		Write2(4); Write2(0); (* ImageVersion *)
		Write2(5); Write2(0); (* SubsystemVersion: 5.0 - Windows 2000 *) 
		Write4(0); (* reserved = 0*)
		
		Write4(SizeOfImage); 
		Write4(SizeOfHeaders); 
		Write4(0); (* CheckSum *)
		Write2(subsystem);
		Write2(0); (* DllCharacteristics *)
		WritePtr(SizeOfStackReserve); 
		WritePtr(SizeOfStackCommit); 
		WritePtr(SizeOfHeapReserve); 
		WritePtr(SizeOfHeapCommit); 
		Write4(0);	(* LoaderFlags =0 *)

		Write4(16); (* NumberOfRvaAndSizes *)
		(* IMAGE_DIRECTORY: [16] *)
		WriteDirEntry(expo.addr, expo.size); (* Export Table *)
		WriteDirEntry(impo.addr, 20 * (LB.impDllCount + 1)); (* Import Table  *)	
		WriteDirEntry(rsrc.addr, rsrc.size); (* Resource Table *)	
		WriteDirEntry(pdata.addr, 12 * unwindCount); (* Exception Table *)	
		WriteDirEntry(0, 0); (* Certificate Table   *)	
		WriteDirEntry(relo.addr, relo.size); (* Base Relocation Table *)	
		WriteDirEntry(0, 0); (* Debug *)	
		WriteDirEntry(0, 0); (* Architecture =0  *)	
		WriteDirEntry(0, 0); (* Global Ptr, size=0 *)
		WriteDirEntry(0, 0); (* TLS Table *)
		WriteDirEntry(0, 0); (* Load Config Table *)
		WriteDirEntry(0, 0); (* Bound Import *)
		WriteDirEntry(0, 0); (* Import Address Table *)
		WriteDirEntry(0, 0); (* Delay Import Descriptor *)
		WriteDirEntry(0, 0); (* CLR Runtime Header *)
		WriteDirEntry(0, 0); (* Reserved *)
		
		NumberOfSections := 0;
		WriteSectionHeaders;
	END WriteHeader;

	PROCEDURE WriteInitCode;
	BEGIN
		out.WriteBytes(LB.initMod.code.bytes, 0, LB.initMod.code.size);
	END WriteInitCode;
	
	PROCEDURE WritePlt;
	VAR lib: Module;  sym: LB.Symbol; 
	VAR thunk: INTEGER;
	BEGIN
		thunk := LB.ImageBase + impo.addr + impDirSize; (* absolute addr *)
		lib := LB.dllList; 
		WHILE lib # NIL DO
			sym := lib.exp.next; 
			WHILE sym # NIL DO
				WriteCh(0FFX); WriteCh(25X);
				IF LB.amd64 THEN 
					Write4(thunk + sym.symIdx * 8 - sym.addr - 6) (* jmp [rip+disp32] *)
				ELSE
					Write4(thunk + sym.symIdx*4);	(* jmp indirect *)
					LB.AddReloc(sym.addr + 2)
				END;
				sym := sym.next
			END;
			lib := lib.next
		END;
	END WritePlt;

	PROCEDURE WriteCode;
	VAR mod: Module; 
	BEGIN
		PadTo(code.offs);
		WriteInitCode;
		mod := LB.modList;
		WHILE mod # NIL DO 
			out.WriteBytes(mod.code.bytes, 0, mod.code.size);
			mod := mod.next
		END;
		WritePlt;
	END WriteCode;

	PROCEDURE WriteData;
		VAR mod: Module; 
	BEGIN
		PadTo(data.offs);
		mod := LB.modList; 
		WHILE mod # NIL DO
			out.WriteBytes(mod.data.bytes, 0, mod.data.size);
			mod := mod.next
		END
	END WriteData;
	
	PROCEDURE WriteImport;
		VAR i, lookup, thunk, names, ptrSize: INTEGER; 
		VAR lib: Module;  sym: LB.Symbol; 
	BEGIN
		PadTo(impo.offs);
		
		IF LB.amd64 THEN ptrSize := 8 ELSE ptrSize := 4 END;
		thunk := impo.addr + impDirSize; (* Address Table *)
		lookup := thunk + ptrSize * (LB.impCount + LB.impDllCount);  (* Lookup Table *)
		names := lookup + ptrSize * (LB.impCount + LB.impDllCount);  (* Hint/Name Table *)

		(* Import Directory *)
		lib := LB.dllList; 
		WHILE lib # NIL DO
			Write4(lookup + lib.exp.symIdx * ptrSize); 
			Write4(0); (* time/data =  0 *)
			Write4(0); (* ForwarderChain *)
			Write4(names + lib.exp.namIdx); 
			Write4(thunk + lib.exp.symIdx * ptrSize); 
			lib := lib.next
		END;		
		Write4(0); Write4(0); Write4(0); Write4(0); Write4(0);
		
		PadTo(impo.offs + impDirSize);
		FOR i := 1 TO 2 DO	(* lookup table, name table  *)
			lib := LB.dllList; 
			WHILE lib # NIL DO
				sym := lib.exp.next;
				WHILE sym # NIL DO WritePtr(names + sym.namIdx); sym := sym.next END;
				WritePtr(0);
				lib := lib.next
			END;		
		END;		
		LB.WriteStringTable(out, impStrTable);
	END WriteImport;
	
	PROCEDURE WriteExport;
		CONST OrdinalBase = 1;
		VAR sym: LB.Symbol; 
			functions, names, ordinals, strings, i: INTEGER; 
	BEGIN
		PadTo(expo.offs);
		
		functions := expo.addr + expDirSize; 	(* Address Table *)
		names := functions + 4 * LB.expCount;	(* Name Pointer Table *)
		ordinals := names + 4 * LB.expCount; 	(* Ordinal Table *)
		strings := ordinals+ 2 * LB.expCount;	(* Name Table *)

		(*  EXPORT_DIRECTORY *)
		Write4(0);	(* Characteristics  *)
		Write4(TimeDateStamp);	
		Write4(0);	(* version *)
		Write4(strings + outNameStrIndex);	(* Name of the DLL  *)
		Write4(OrdinalBase);	
		Write4(LB.expCount);	(* NumberOfFunctions *)
		Write4(LB.expCount);	(* NumberOfNames *)
		Write4(functions);	
		Write4(names);	
		Write4(ordinals);

		sym := LB.expList; 
		WHILE sym # NIL DO Write4(sym.addr- LB.ImageBase); sym := sym.next END; (* rva *)
		sym := LB.expList; 
		WHILE sym # NIL DO Write4(strings + sym.namIdx); sym := sym.next END;
		i := 0; WHILE i < LB.expCount DO Write2(i); INC(i) END;
		LB.WriteStringTable(out, expStrTable);
	END WriteExport;

	(* Keep a DWORD after each UNWIND_INFO for older binutils readers. *)
	PROCEDURE WriteUnwind;
		VAR m: Module; u: LB.Unwind; base: INTEGER;
	BEGIN
		IF unwindCount = 0 THEN RETURN END;
		PadTo(pdata.offs); m := LB.initMod;
		WHILE m # NIL DO
			base := m.code.addr - LB.ImageBase; u := m.unwind;
			WHILE u # NIL DO
				Write4(base + u.first); Write4(base + u.last); Write4(xdata.addr + u.offset);
				u := u.next
			END;
			IF m = LB.initMod THEN m := LB.modList ELSE m := m.next END
		END;
		PadTo(xdata.offs); m := LB.initMod;
		WHILE m # NIL DO u := m.unwind;
			WHILE u # NIL DO out.WriteBytes(u.bytes, 0, LEN(u.bytes)); Write4(0); u := u.next END;
			IF m = LB.initMod THEN m := LB.modList ELSE m := m.next END
		END
	END WriteUnwind;

	PROCEDURE WriteReloc;
		CONST pageSize = 4096; IMAGE_REL_BASED_HIGHLOW = 3;
		VAR i, hi, blockSize, page, limit, pad, relType: INTEGER; 
	BEGIN
		LB.SortReloc; 
		IF LB.amd64 THEN
			relType := 10
		ELSE
			relType := IMAGE_REL_BASED_HIGHLOW
		END;
		PadTo(relo.offs);
		relo.size := 0; 
		i := 0;
		WHILE i < LB.relCount DO
			page := LB.relTab[i] DIV pageSize * pageSize; 
			limit := page + pageSize;
			hi := i; 
			WHILE (hi < LB.relCount) & (LB.relTab[hi] < limit) DO INC(hi) END;
			blockSize := 8 + 2 * (hi - i);
			pad := blockSize MOD 4;
			INC(blockSize, pad);
			Write4(page - LB.ImageBase);  (* rva *)
			Write4(blockSize); 
			WHILE i < hi DO 
				Write2(LB.relTab[i] - page + relType * pageSize); 
				INC(i) 
			END; 
			IF pad # 0 THEN Write2(0) END;
			INC(relo.size, blockSize);
		END;
		IF ~LB.amd64 THEN
			Write4(0); Write4(0); 
			INC(relo.size, 8)
		END;
		PadTo(LB.Aligned(relo.offs + relo.size, FileAlignment));
	END WriteReloc;

	PROCEDURE WriteOut*;
		VAR res, i, split, j: INTEGER; path, name: Files.Name;
	BEGIN
		IF LB.error THEN RETURN END;
		IF LB.expCount > 65535 THEN LB.Error("too many PE exports"); RETURN END;
		
		CalculateLayout;
		split := -1; i := 0;
		WHILE LB.outputName[i] # 0X DO
			IF (LB.outputName[i] = "/") OR (LB.outputName[i] = "\") THEN split := i END; INC(i)
		END;
		i := 0; WHILE i <= split DO path[i] := LB.outputName[i]; INC(i) END; path[i] := 0X;
		j := 0;
		WHILE LB.outputName[i] # 0X DO
			name[j] := LB.outputName[i]; INC(i); INC(j)
		END;
		name[j] := 0X;
		outFile := Files.dir.New(Files.dir.This(path), Files.ask);
		IF outFile = NIL THEN LB.Error("cannot create output file"); RETURN END; 
		out := outFile.NewWriter(out); 
		out.SetPos(0);
		WriteHeader; 
		WriteCode;
		WriteData;
		WriteImport;
		IF LB.expCount > 0 THEN WriteExport END;
		IF rsrc.size > 0 THEN PadTo(rsrc.offs); Res.Write(out, rsrc.addr) END;
		WriteUnwind;
		WriteReloc;
		SizeOfImage := LB.Aligned(relo.addr + relo.size, SectionAlignment);
		out.SetPos(0);
		WriteHeader;
		IF ~LB.error THEN
			outFile.Register(name, LB.outputType, Files.ask, res);
			IF res # 0 THEN LB.Error('Register') END
		END
	END WriteOut;

END DevLnkWritePe.
