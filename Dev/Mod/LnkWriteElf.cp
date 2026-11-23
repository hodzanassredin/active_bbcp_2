MODULE DevLnkWriteElf;
(**	project	= "BlackBox 2.0"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Igor Dehtyarenko"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)


	IMPORT Dialog, LB := DevLnkBase, Files, SYSTEM(*LSH*);

	CONST	
		exeBaseAddress = 8048000H; dllBaseAddress = 0;

		linuxInterpreter* = "/lib/ld-linux.so.2"; (* ELF32 x86 *)
		openBSDInterpreter* = "/usr/libexec/ld.so"; (* ELF32, ELF64 *)
		freeBSDInterpreter* = "/libexec/ld-elf.so.1";
		netBSDInterpreter* = "/usr/libexec/ld.elf_so";

		(* OS ABI  *)
			ELFOSABI_NONE* = 0X;
			ELFOSABI_NETBSD* = 2X;
			ELFOSABI_LINUX* = 3X;
			ELFOSABI_FREEBSD* = 9X;
			ELFOSABI_OPENBSD* = 0CX;
			ELFOSABI_ARM* = 61X;

	TYPE 
		Module = LB.Module;
		
		Section = RECORD
			namIdx: INTEGER;
			type: INTEGER;
			flags: INTEGER; 
			size: INTEGER;
			addr: INTEGER;
			offs: INTEGER;
			align: INTEGER;
			link: INTEGER; 
			info: INTEGER;
			entrySize: INTEGER;
		END;

		Segment = RECORD
			type: INTEGER;
			addr: INTEGER;	(* virtual address *)
			offs: INTEGER;
			msize: INTEGER;	(* size in memory *)
			fsize: INTEGER;	(* size in file image *)
			align: INTEGER;	(* alignment in memory and in the file *)
			flags: INTEGER;
		END;
		
	CONST
		pageSize = 1000H; (* I386 page size *) 
		elfHeaderSize = 52; (* ELF32 *)

		(* program header table *)
		phtOffset = elfHeaderSize; 
		phtEntrySize = 32; (* ELF32 *)

		(* section header table  *)
		shtEntrySize = 40; (* ELF32 *)

	VAR
		outFile: Files.File;
		out: Files.Writer;
    entry_addr, init_addr, fini_addr: INTEGER;	
		interpreter: ARRAY 128 OF SHORTCHAR;
    elfType : INTEGER;

		sectionCount: INTEGER;
		shtOffset, shtSize: INTEGER;	(* section header table offset&size *)
		idxSecNames: INTEGER; 	(* index of section names string table  *)

		code, data, vars, dyna, dsym, dstr, hash, relo: Section;  
		empty, intp,
			noteobsd,
			notefbsd,
			notenbsd, notenbsdpax,
			shstr: Section;

		phtseg, intseg, codeseg, dataseg, dynaseg,
			noteobsdseg, obsdwxneedseg,
			notefbsdseg,
			notenbsdseg,
			dsymseg: Segment;

		symNames, secNames: LB.StringTable;
		siOutName, siRPath: INTEGER;			
		symCount, symIdx, symImp : INTEGER; 
		
		hashtab: POINTER TO ARRAY OF INTEGER;

	PROCEDURE SegmentCount (): INTEGER;
		VAR res: INTEGER;
	BEGIN
		CASE LB.opt.OS OF LB.osLinux:
			res := 6
		| LB.osFreeBSD:
			res := 7
		| LB.osOpenBSD:
			res := 8
		| LB.osNetBSD:
			res := 7
		END;
		RETURN res
	END SegmentCount;

	PROCEDURE PhtSize (): INTEGER;
	BEGIN
		RETURN phtEntrySize * SegmentCount()
	END PhtSize;
		
(**********************************)
	PROCEDURE WriteCh (ch: SHORTCHAR);
	BEGIN
		out.WriteByte(SHORT(ORD(ch)))
	END WriteCh;

	PROCEDURE Write1(x: INTEGER);
	BEGIN
		out.WriteByte(SHORT(SHORT(x MOD 256)))
	END Write1;

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

	PROCEDURE WriteStr(IN s: LB.STRING; len: INTEGER);
		VAR i: INTEGER;
	BEGIN 
		i := 0;
		WHILE s[i] # 0X DO out.WriteByte(SHORT(ORD(s[i]))); INC(i) END;
		WHILE i < len DO out.WriteByte(0); INC(i) END
	END WriteStr;

	PROCEDURE PadTo(pos: INTEGER);
	BEGIN
		ASSERT(pos >= out.Pos(), 101);
		WHILE out.Pos() < pos DO out.WriteByte(0) END;
	END PadTo;

(**********************************)	
	PROCEDURE HashStr (IN name: LB.STRING): INTEGER;
			VAR i, h: INTEGER; g: SET;
		BEGIN
			h := 0; i := 0;
			WHILE name[i] # 0X DO
				h := h * 16 + ORD(name[i]);
				g := BITS(h) * {28..31};
				h := ORD(BITS(h) / SYSTEM.LSH(g, -24) - g);
				INC(i)
			END;
			RETURN h
		END HashStr;

	PROCEDURE AddSymbol(IN name: LB.STRING; OUT idx, namIdx: INTEGER);
	BEGIN
		LB.AddName(symNames, name, namIdx);
		idx := symIdx;
		hashtab[idx] := HashStr(name$);
		INC(symIdx);
	END AddSymbol;


(**********************************)	
	PROCEDURE DefSegments;
		CONST 
			PT_NULL = 0; PT_LOAD = 1; PT_DYNAMIC = 2; PT_INTERP  = 3; PT_NOTE = 4; PT_PHDR = 6;
				PT_OPENBSD_WXNEEDED = 65A3DBE7H; (* types *)
			exec = 0; write = 1; read = 2; (* flags *)
			
		PROCEDURE DefSeg(VAR seg: Segment; type: INTEGER; flags:SET; align: INTEGER); 
		BEGIN
			seg.type := type;	
			seg.flags:= ORD(flags);	
			seg.align := align;
		END DefSeg;
		
	BEGIN		
		DefSeg(phtseg,  PT_PHDR, {read, exec}, 4 );
		DefSeg(intseg,  PT_INTERP, {read}, 1 );
		DefSeg(codeseg, PT_LOAD, {read, exec}, pageSize);
		DefSeg(dataseg, PT_LOAD,  {read, write}, pageSize);
		DefSeg(dynaseg, PT_DYNAMIC, {read, write}, 4);
		IF LB.opt.OS = LB.osOpenBSD THEN
			DefSeg(noteobsdseg, PT_NOTE, {read}, 4);
			DefSeg(obsdwxneedseg, PT_OPENBSD_WXNEEDED, {exec}, 4)
		ELSIF LB.opt.OS = LB.osFreeBSD THEN
			DefSeg(notefbsdseg, PT_NOTE, {read}, 4)
		ELSIF LB.opt.OS = LB.osNetBSD THEN
			DefSeg(notenbsdseg, PT_NOTE, {read}, 4)
		END;
		DefSeg(dsymseg, PT_LOAD, {read, write}, pageSize);
	END DefSegments;

	PROCEDURE DefSections;
		CONST 
		(* section types *)
				SHT_NULL = 0;  SHT_PROGBITS = 1; SHT_SYMTAB = 2;  SHT_STRTAB = 3;  
				SHT_DYNAMIC = 6; SHT_DYNSYM = 11; 
				SHT_HASH = 5;   SHT_REL = 9;  SHT_NOBITS = 8;  
				SHT_NOTE = 7;
			(* section flags : is writable (0), occupies memory(1), is executable(2) *)
				write = 0; alloc = 1; exec = 2;
				
		PROCEDURE DefSection(VAR scn: Section; IN name: LB.STRING; type, align: INTEGER; flags:SET); 		BEGIN
			LB.AddName(secNames, name, scn.namIdx);
			scn.type := type;	
			scn.flags:= ORD(flags);	
			scn.align := align;

			INC(sectionCount)
		END DefSection;

	BEGIN
		sectionCount := 0;

		(* 0 *) DefSection(empty, "", SHT_NULL,0, {});
		(* 1 *) DefSection(code, ".text", SHT_PROGBITS,  4, {alloc, exec});
		(* 2 *) DefSection(data, ".data", SHT_PROGBITS,  8, {alloc});
		(* 3 *) DefSection(vars, ".bss", SHT_NOBITS,  4, {alloc, write});
		(* 4 *) DefSection(dstr, ".dynstr", SHT_STRTAB,  1, {alloc});
		(* 5 *) DefSection(dsym, ".dynsym", SHT_DYNSYM,  4, {alloc});
		(* 6 *) DefSection(hash, ".hash", SHT_HASH,  4, {alloc});
		(* 7 *) DefSection(relo, ".relo", SHT_REL,  4, {alloc});
		(* 8 *) DefSection(dyna, ".dynamic", SHT_DYNAMIC,  4, {alloc});
		(* 9 *) DefSection(shstr, ".shstrtab", SHT_STRTAB,  4, {}); 
		(*10 *) DefSection(intp, ".interp", SHT_PROGBITS,  1, {alloc});

		IF LB.opt.OS = LB.osOpenBSD THEN
			(*11 *) DefSection(noteobsd, ".note.openbsd.ident", SHT_NOTE, 4, {alloc});

			noteobsd.size := 18H; noteobsd.entrySize := 0; noteobsd.link := 0; noteobsd.info := 0
		ELSIF LB.opt.OS = LB.osFreeBSD THEN
			(*11 *) DefSection(notefbsd, ".note.tag", SHT_NOTE, 4, {alloc});

			notefbsd.size := 48H; notefbsd.entrySize := 0; notefbsd.link := 0; notefbsd.info := 0
		ELSIF LB.opt.OS = LB.osNetBSD THEN
			(*11 *) DefSection(notenbsd, ".note.netbsd.ident", SHT_NOTE, 4, {alloc});
			(*12 *) DefSection(notenbsdpax, ".note.netbsd.pax", SHT_NOTE, 4, {alloc});

			notenbsd.size := 18H; notenbsd.entrySize := 0; notenbsd.link := 0; notenbsd.info := 0;
			notenbsdpax.size := 14H; notenbsdpax.entrySize := 0;
				notenbsdpax.link := 0; notenbsdpax.info := 0
		END;

		dsym.entrySize := 16 (* ELF32 *); dsym.link := 4; dsym.info := 1;
		relo.entrySize := 8 (* ELF32 *); relo.link := 5; relo.info := 1;
		dyna.entrySize := 8 (* ELF32 *); dyna.link := 4;
		hash.entrySize := 4; hash.link := 5;
		dstr.link := 5;
		idxSecNames := 9
	END DefSections;

	PROCEDURE Init*;
	CONST ET_REL = 1; ET_EXEC = 2; ET_DYN = 3; 
		VAR	si, symi: INTEGER;	
	BEGIN
		LB.InitStringTable(symNames, 1000);
		LB.InitStringTable(secNames, 100);
		NEW(hashtab, LB.impCount + LB.expCount + 1);		
		symIdx := 0;
		AddSymbol("", symi, si);
		siOutName :=0;  siRPath := 0;	
		IF LB.opt.rpath # "" THEN
			LB.AddName(symNames, LB.opt.rpath, siRPath);
		END;
		
		DefSections;
		DefSegments;
		
		IF LB.outDll THEN 
			elfType := ET_DYN;
			LB.ImageBase := dllBaseAddress;;
			LB.AddName(symNames, SHORT(LB.outputName$), siOutName);
			intp.size := 0;
		ELSE
			elfType := ET_EXEC;
			LB.ImageBase := exeBaseAddress;

			ASSERT(LB.opt.elfInterpreter[0] # 0X);
			interpreter := LB.opt.elfInterpreter$;

			intp.size := LEN(interpreter$) + 1;
		END;
		intp.offs := elfHeaderSize + PhtSize();
		intp.addr := intp.offs + LB.ImageBase;
		
		code.size := LB.codeSize; 
		data.size := LB.dataSize;
		vars.size := LB.varsSize;

		IF LB.opt.OS = LB.osOpenBSD THEN
			noteobsd.offs := LB.Aligned(intp.offs + intp.size, noteobsd.align);
			noteobsd.addr := noteobsd.offs + LB.ImageBase;
			code.offs := LB.Aligned(noteobsd.offs + noteobsd.size, code.align)
		ELSIF LB.opt.OS = LB.osFreeBSD THEN
			notefbsd.offs := LB.Aligned(intp.offs + intp.size, notefbsd.align);
			notefbsd.addr := notefbsd.offs + LB.ImageBase;
			code.offs := LB.Aligned(notefbsd.offs + notefbsd.size, code.align)
		ELSIF LB.opt.OS = LB.osNetBSD THEN
			notenbsd.offs := LB.Aligned(intp.offs + intp.size, notenbsd.align);
			notenbsd.addr := notenbsd.offs + LB.ImageBase;
			notenbsdpax.offs := LB.Aligned(notenbsd.offs + notenbsd.size, notenbsdpax.align);
			notenbsdpax.addr := notenbsdpax.offs + LB.ImageBase;
			code.offs := LB.Aligned(notenbsdpax.offs + notenbsdpax.size, code.align)
		ELSE
			code.offs := LB.Aligned(intp.offs + intp.size, code.align)
		END;
		code.addr := code.offs + LB.ImageBase;
		data.offs := LB.Aligned(code.offs + code.size, pageSize);
		data.addr := LB.Aligned(code.addr + code.size, pageSize);
		vars.addr := LB.Aligned(data.addr + data.size, vars.align);

		entry_addr:= code.addr + LB.initOffs;
		init_addr := entry_addr;
		fini_addr := code.addr + LB.finiOffs;
	END Init;


(**********************************)
	PROCEDURE GetBases*(OUT codeBase, dataBase, varsBase: INTEGER);
	BEGIN
		codeBase := code.addr;
		dataBase := data.addr;
		varsBase := vars.addr;
	END GetBases;

(**********************************)	
	PROCEDURE CollectImport;
		VAR lib: Module; sym: LB.Symbol;
	BEGIN
		symImp := symIdx;
		lib := LB.dllList;
		WHILE lib # NIL DO
			LB.AddDllName(symNames, lib.exp.name, ".so", lib.exp.namIdx);
			sym := lib.exp.next;
			WHILE sym # NIL DO
				AddSymbol(sym.name, sym.symIdx, sym.namIdx);
				sym := sym.next
			END;
			lib := lib.next
		END
	END CollectImport;
	
	PROCEDURE CollectExport;
		VAR sym: LB.Symbol;
	BEGIN
		IF LB.expCount > 0 THEN  
			sym := LB.expList;
			WHILE sym # NIL DO 
				AddSymbol(sym.name, sym.symIdx, sym.namIdx);
				sym := sym.next
			END
		ELSE	
		END;
	END CollectExport;

	PROCEDURE CalculateLayout;

		PROCEDURE DefSeg(VAR seg: Segment; offs, addr, size: INTEGER); 
		BEGIN
			seg.offs:= offs;
			seg.addr:= addr;
			seg.msize:= size;
			seg.fsize:= size;
		END DefSeg;

	BEGIN
		CollectImport;
		CollectExport;
		symCount := symIdx;
				
		dyna.size := (LB.impDllCount + 20) * dyna.entrySize;
		dsym.size := symCount * dsym.entrySize;
		dstr.size := symNames.len;
		hash.size := 8 + symCount * 4 * 2;
		relo.size := relo.entrySize * (LB.relCount + LB.impCount);
		
		dyna.offs := LB.Aligned(data.offs + data.size, pageSize);
		dyna.addr := LB.Aligned(vars.addr + vars.size, pageSize);

		dsym.offs := LB.Aligned(dyna.offs + dyna.size, dsym.align);
		dstr.offs := LB.Aligned(dsym.offs + dsym.size, dstr.align);
		hash.offs := LB.Aligned(dstr.offs + dstr.size, hash.align);
		relo.offs := LB.Aligned(hash.offs + hash.size, relo.align);
		
		dsym.addr := dsym.offs + dyna.addr - dyna.offs;
		dstr.addr := dstr.offs + dsym.addr - dsym.offs;
		hash.addr := hash.offs + dstr.addr - dstr.offs;
		relo.addr := relo.offs + hash.addr - hash.offs;
	
		shtOffset := LB.Aligned(relo.offs + relo.size, 4);
		shtSize := sectionCount * shtEntrySize;
		
		DefSeg(phtseg, phtOffset, LB.ImageBase + phtOffset, PhtSize());
		DefSeg(intseg, intp.offs, LB.ImageBase + intp.offs, intp.size);
		DefSeg(codeseg, 0, LB.ImageBase, code.offs + code.size);
		DefSeg(dataseg, data.offs, data.addr, data.size); 
			dataseg.msize := dataseg.fsize + vars.size;
		DefSeg(dynaseg, dyna.offs, dyna.addr, dyna.size);
		IF LB.opt.OS = LB.osOpenBSD THEN
			DefSeg(noteobsdseg, noteobsd.offs, noteobsd.addr, noteobsd.size);
			DefSeg(obsdwxneedseg, 0, 0, 0)
		ELSIF LB.opt.OS = LB.osFreeBSD THEN
			DefSeg(notefbsdseg, notefbsd.offs, notefbsd.addr, notefbsd.size)
		ELSIF LB.opt.OS = LB.osNetBSD THEN
			DefSeg(notenbsdseg, notenbsd.offs, notenbsd.addr, notenbsd.size + notenbsdpax.size)
		END;
		DefSeg(dsymseg, dyna.offs, dyna.addr, shtOffset - dyna.offs);
	END CalculateLayout;

(**********************************)		
 PROCEDURE WriteElfHeader; (* ELF32 x86 *)
	CONST
			ELFCLASS32 = 1X; ELFDATA2LSB = 1X; EV_CURRENT = 1X;
			EM_386 = 3;
	VAR	e_ident : ARRAY 16 OF SHORTCHAR; 
	BEGIN
		e_ident := 7FX + 'ELF'+ ELFCLASS32 + ELFDATA2LSB + EV_CURRENT;
		e_ident[7] := LB.opt.OSABI;
		e_ident[8] := 0X;
		WriteStr(e_ident,16); 
		Write2(elfType); 
		Write2(EM_386); 
		Write4(1); (* version  *)
		Write4(entry_addr); 
		Write4(phtOffset); 
		Write4(shtOffset); 
		Write4(0); (* flags  *)
		Write2(elfHeaderSize); 
		Write2(phtEntrySize); 
		Write2(SegmentCount()); 
		Write2(shtEntrySize); 		
		Write2(sectionCount); 
		Write2(idxSecNames); 
		ASSERT(out.Pos() = elfHeaderSize, 101)
	END WriteElfHeader;

	PROCEDURE WriteProgramHeaderTable;
 	PROCEDURE WritePhtEntry(VAR seg: Segment); (* ELF32 *)
		BEGIN
			Write4(seg.type);
			Write4(seg.offs);
			Write4(seg.addr);
			Write4(seg.addr); (* physical address, ignored *)
			Write4(seg.fsize); 
			Write4(seg.msize);
			Write4(seg.flags);
			Write4(seg.align) 
		END WritePhtEntry;
	BEGIN
		WritePhtEntry(phtseg);
		WritePhtEntry(intseg);
		WritePhtEntry(codeseg);
		WritePhtEntry(dataseg);
		WritePhtEntry(dsymseg);
		WritePhtEntry(dynaseg);
		IF LB.opt.OS = LB.osOpenBSD THEN
			WritePhtEntry(noteobsdseg);
			WritePhtEntry(obsdwxneedseg)
		ELSIF LB.opt.OS = LB.osFreeBSD THEN
			WritePhtEntry(notefbsdseg)
		ELSIF LB.opt.OS = LB.osNetBSD THEN
			WritePhtEntry(notenbsdseg)
		END
	END WriteProgramHeaderTable;

	PROCEDURE WriteInterpreter;
	BEGIN
		IF intp.size > 0 THEN 
			WriteStr(interpreter, 0);
		END;
	END WriteInterpreter;

	PROCEDURE WritePlt;
	VAR mod: Module; sym: LB.Symbol; 
	BEGIN
		mod := LB.dllList; 
		WHILE mod # NIL DO
			sym := mod.exp.next; 
			WHILE sym # NIL DO
				WriteCh(0E9X); Write4(-4); (* jmp rel *)
				WriteCh(90X);	(* nop *)
				sym := sym.next
			END;
			mod := mod.next
		END
	END WritePlt;

	PROCEDURE WriteCode;
	VAR mod: Module; 
	BEGIN
		PadTo(code.offs);
		out.WriteBytes(LB.initMod.code.bytes, 0, LB.initMod.code.size);
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
		END;		
	END WriteData;

	PROCEDURE WriteSymbol(namIdx, val, si, typ: INTEGER); (* ELF32 *)
	CONST GLOBAL_FUNC = 18; GLOBAL_OBJECT = 17; GLOBAL_NOTYPE = 16;	
	BEGIN
		Write4(namIdx);
		Write4(val);

		IF typ = LB.typObj THEN
			Write4(4); (*size*);
			Write1(GLOBAL_OBJECT)
		ELSIF typ = LB.typProc THEN
			Write4(0); (*size*);
			Write1(GLOBAL_FUNC)
		ELSE HALT(100)
		END;

		Write1(0);
		Write2(si);
	END WriteSymbol;

	PROCEDURE WriteSymbolTable;
	VAR sym: LB.Symbol; m: Module; 
	BEGIN
		PadTo(dsym.offs);
		WriteSymbol(0, 0, 0, LB.typProc);
		(* import *)
		m := LB.dllList;
		WHILE m # NIL DO
			sym := m.exp.next;
			WHILE sym # NIL DO
				WriteSymbol(sym.namIdx, 0, 0, sym.typ);
				sym := sym.next
			END;
			m := m.next
		END;
		
		(* export *)
		sym := LB.expList;
		WHILE sym # NIL DO
			WriteSymbol(sym.namIdx, sym.addr, 1, sym.typ);
			sym := sym.next
		END;
		ASSERT(dsym.size = out.Pos() - dsym.offs, 101);
		PadTo(dstr.offs); 
		LB.WriteStringTable(out, symNames);
	END WriteSymbolTable;
	
	PROCEDURE MakeHash(OUT bucket, chain: POINTER TO ARRAY OF INTEGER);
		VAR nbucket, symi, hi, k: INTEGER; 
	BEGIN
		nbucket := symCount;
		NEW(bucket, nbucket);
		NEW(chain, symCount);
		FOR symi := 0 TO symCount - 1 DO
			chain[symi] := 0; 
			hi := hashtab[symi] MOD nbucket;
			IF bucket[hi] # 0 THEN 
				k := symi;
				WHILE chain[k] # 0 DO k := chain[k] END;
				chain[k] := bucket[hi]
			END;
			bucket[hi] := symi;
		END;
	END MakeHash;
	
	PROCEDURE WriteHash;
	VAR i: INTEGER; 
			bucket, chain: POINTER TO ARRAY OF INTEGER;
	BEGIN
		MakeHash(bucket, chain);
		PadTo(hash.offs);
		Write4(LEN(bucket)); 
		Write4(LEN(chain)); 
		FOR i := 0 TO LEN(bucket)-1 DO Write4(bucket[i]) END;
		FOR i := 0 TO LEN(chain)-1  DO Write4(chain[i]) END;
		ASSERT(hash.size = out.Pos() - hash.offs, 101)
	END WriteHash;

		
	PROCEDURE WriteReloc; (* ELF32 x86 *)
		CONST R_386_RELATIVE = 8;  R_386_JMP_SLOT = 7; R_386_PC32 = 2; R_386_32 = 1; 
		VAR mod: Module; sym: LB.Symbol; i: INTEGER;		
	BEGIN
		PadTo(relo.offs);
		(* Import *)
		mod := LB.dllList; 
		WHILE mod # NIL DO
			sym := mod.exp.next; 
			WHILE sym # NIL DO
				Write4(sym.addr + 1); 
				Write4(R_386_PC32 + sym.symIdx * 256);
				sym := sym.next
			END;
			mod := mod.next
		END;
		(* Image  *)
		LB.SortReloc;
		FOR i := 0 TO LB.relCount-1 DO
			Write4(LB.relTab[i]);	Write4(R_386_RELATIVE);
		END;
	END WriteReloc;
	
	PROCEDURE WriteDynamic;
		CONST DT_NULL = 0; DT_NEEDED = 1; DT_PLTRELSZ = 2;  DT_PLTGOT = 3; 
				DT_HASH = 4; DT_STRTAB = 5; DT_SYMTAB = 6; DT_STRSZ = 10; DT_SYMENT = 11; 
				DT_INIT = 12; DT_FINI = 13; DT_SONAME = 14; DT_RPATH = 15; 
				DT_REL = 17; DT_RELSZ = 18; DT_RELENT = 19; DT_PLTREL = 20;  
				DT_TEXTREL = 22; DT_JMPREL = 23;  DT_BIND_NOW = 24;
		VAR lib: Module;
	
		PROCEDURE DynaEntry(tag, val: INTEGER); (* ELF32 *)
		BEGIN
			Write4(tag);
			Write4(val)
		END DynaEntry;
		
	BEGIN
		PadTo(dyna.offs);		
		lib := LB.dllList;
		WHILE lib # NIL DO 
			DynaEntry(DT_NEEDED, lib.exp.namIdx); 
			lib := lib.next 
		END;
		IF siRPath > 0 THEN DynaEntry(DT_RPATH, siRPath) END;
		
		DynaEntry(DT_SYMTAB, dsym.addr);
		DynaEntry(DT_SYMENT, dsym.entrySize);
		DynaEntry(DT_HASH, hash.addr);
		DynaEntry(DT_STRTAB, dstr.addr);
		DynaEntry(DT_STRSZ, dstr.size);
		
		DynaEntry(DT_TEXTREL, 0);
		DynaEntry(DT_REL, relo.addr);
		DynaEntry(DT_RELSZ, relo.size);
		DynaEntry(DT_RELENT, relo.entrySize);
		
		IF LB.outDll THEN 
			DynaEntry(DT_SONAME, siOutName);
			DynaEntry(DT_INIT, init_addr);
			DynaEntry(DT_FINI, fini_addr);
		END;
		DynaEntry(DT_BIND_NOW, 1);
		DynaEntry(DT_NULL, 0); (* marks the end *)
	END WriteDynamic;

	PROCEDURE WriteNote (IN name: ARRAY OF SHORTCHAR; type: INTEGER; desc: INTEGER);
		CONST align = 4;
			descsz = 4;
		VAR namesz: INTEGER;
	BEGIN
		namesz := LEN(name$) + 1;
		Write4(namesz);
		Write4(descsz);
		Write4(type);
		WriteStr(name, ((namesz - 1) DIV align + 1) * align);
		Write4(desc)
	END WriteNote;

	(*
		objdump -s -j .note.openbsd.ident
	*)
	PROCEDURE WriteObsdNote;
	BEGIN
		PadTo(noteobsd.offs);

		WriteNote("OpenBSD", 1, 0)
	END WriteObsdNote;

	(*
		man 5 elf
		objdump -s -j .note.tag
	*)
	PROCEDURE WriteFbsdNote;
		CONST
			NT_FREEBSD_ABI_TAG = 1;
				major = 14; minor = 3; patch = 0;
			NT_FREEBSD_NOINIT_TAG = 2;
			NT_FREEBSD_ARCH_TAG = 3;
			NT_FREEBSD_FEATURE_CTL = 4;
				NT_FREEBSD_FCTL_ASLR_DISABLE = {0};
				NT_FREEBSD_FCTL_PROTMAX_DISABLE = {1};
				NT_FREEBSD_FCTL_STKGAP_DISABLE = {2};
				NT_FREEBSD_FCTL_WXNEEDED = {3};
	BEGIN
		PadTo(notefbsd.offs);

		WriteNote("FreeBSD", NT_FREEBSD_ABI_TAG, major * 100000 + minor * 1000 + patch);
		WriteNote("FreeBSD", NT_FREEBSD_FEATURE_CTL, ORD({}));
		WriteNote("FreeBSD", NT_FREEBSD_NOINIT_TAG, 0)
	END WriteFbsdNote;

	(*
		objdump -s -j .note.netbsd.ident
		objdump -s -j .note.netbsd.pax
	*)
	PROCEDURE WriteNbsdNotes;
		CONST major = 10; minor = 1;
	BEGIN
		PadTo(notenbsd.offs);

		(* .note.netbsd.ident *)
			WriteNote("NetBSD", 1, major * 100000000 + minor * 1000000);
		(* .note.netbsd.pax *)
			WriteNote("PaX", 3, 0)
	END WriteNbsdNotes;

	PROCEDURE WriteSectionTable;
	
		PROCEDURE WriteSHEntry(VAR scn: Section); (* ELF32 *)
		BEGIN
			Write4(scn.namIdx); 	
			Write4(scn.type);	
			Write4(scn.flags);	
			Write4(scn.addr); 	
			Write4(scn.offs); 	
			Write4(scn.size);
			Write4(scn.link); 		
			Write4(scn.info);
			Write4(scn.align); 
			Write4(scn.entrySize); 
		END WriteSHEntry;
		
	BEGIN
		PadTo(shtOffset);
		shstr.offs := shtOffset + shtSize;
		shstr.size := secNames.len;

		WriteSHEntry(empty);
		WriteSHEntry(code);
		WriteSHEntry(data);
		WriteSHEntry(vars);
		WriteSHEntry(dstr);
		WriteSHEntry(dsym);
		WriteSHEntry(hash);
		WriteSHEntry(relo);
		WriteSHEntry(dyna);
		WriteSHEntry(shstr);
		WriteSHEntry(intp);
		IF LB.opt.OS = LB.osOpenBSD THEN
			WriteSHEntry(noteobsd)
		ELSIF LB.opt.OS = LB.osFreeBSD THEN
			WriteSHEntry(notefbsd)
		ELSIF LB.opt.OS = LB.osNetBSD THEN
			WriteSHEntry(notenbsd);
			WriteSHEntry(notenbsdpax)
		END;
		PadTo(shstr.offs); 
		LB.WriteStringTable(out, secNames);
	END WriteSectionTable;


	PROCEDURE WriteOut*;
	VAR res: INTEGER; loc: Files.Locator; path: Dialog.String;
	BEGIN
		CalculateLayout;
		
		loc := Files.dir.This("");
		outFile := Files.dir.New(loc, Files.ask);
		out := outFile.NewWriter(out); 
		out.SetPos(0);
		WriteElfHeader;
		WriteProgramHeaderTable;
		WriteInterpreter;

		IF LB.opt.OS = LB.osOpenBSD THEN
			WriteObsdNote
		ELSIF LB.opt.OS = LB.osFreeBSD THEN
			WriteFbsdNote
		ELSIF LB.opt.OS = LB.osNetBSD THEN
			WriteNbsdNotes
		END;

		WriteCode;
		WriteData;
		WriteDynamic;
		WriteSymbolTable;
		WriteHash;
		WriteReloc;
		
		WriteSectionTable;
		
		out.SetPos(0);
		WriteElfHeader;
		IF ~LB.error THEN
			outFile.Register(LB.outputName, LB.outputType, Files.ask, res);
			IF res # 0 THEN LB.Error('Register') ELSE
				Dialog.GetLocPath(loc, path);
				IF path # "" THEN path := path + "/" END;
				LB.Chmod(path + LB.outputName, {0, 2, 3, 4, 5, 6, 7, 8}) (* rwxrwxr-x *)
			END
		END
	END WriteOut;

END DevLnkWriteElf.

/ Elf
	PHT
	interpreter
	OpenBSD: noteobsd
	FreeBSD: notefbsd
	NetBSD: notenbsd, notenbsdpax
\ Code
	
/ Data
\ Vars
/ Dynamic
	SymbolTable
	StringTable
	Hash;
\ Reloc;
	
	SectionHeaderTable
	SectionNames
