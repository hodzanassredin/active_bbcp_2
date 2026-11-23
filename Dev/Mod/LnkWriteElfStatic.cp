MODULE DevLnkWriteElfStatic;
(**	project	= "BlackBox 2.0"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Igor Dehtyarenko"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	IMPORT Dialog, LB := DevLnkBase, Files;

	CONST	
		defBaseAddress = 8048000H;

	TYPE 
		Module = LB.Module;
		
		Section = RECORD
			size: INTEGER;
			addr: INTEGER;
			offs: INTEGER;
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
		elfHeaderSize = 52;	
		
		(* program header table *)
		phtOffset = elfHeaderSize; 
		phtEntrySize = 32;
		phtEntryCount = 3; 
		phtSize = phtEntrySize * phtEntryCount;		

	VAR
		outFile: Files.File;
		out: Files.Writer;
		entry_addr: INTEGER;	
		elfType : INTEGER;
		code, data, vars: Section; 
		
		phtseg, codeseg, dataseg: Segment;
		
(**********************************)
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

	PROCEDURE WriteStr(IN s: LB.STRING);
		VAR i: INTEGER;
	BEGIN 
		i := 0;
		WHILE s[i] # 0X DO out.WriteByte(SHORT(ORD(s[i]))); INC(i) END;
		out.WriteByte(0);
	END WriteStr;

	PROCEDURE PadTo(pos: INTEGER);
	BEGIN
		WHILE out.Pos() < pos DO out.WriteByte(0) END;
	END PadTo;
	
(**********************************)	
	PROCEDURE AddReloc*(addr: INTEGER);
	BEGIN
	END AddReloc;

(**********************************)	
	PROCEDURE DefSegments;
		CONST 
			PT_LOAD = 1; PT_PHDR = 6; (* types *)
			exec = 0; write = 1; read = 2; (* flags *)
			
		PROCEDURE DefSeg(VAR seg: Segment; type: INTEGER; flags:SET; align: INTEGER); 
		BEGIN
			seg.type := type;	
			seg.flags:= ORD(flags);	
			seg.align := align;
		END DefSeg;
		
	BEGIN		
		DefSeg(phtseg,  PT_PHDR, {read, exec}, 4 );
		DefSeg(codeseg, PT_LOAD, {read, exec}, pageSize);
		DefSeg(dataseg, PT_LOAD, {read, write}, pageSize);
	END DefSegments;	


	PROCEDURE Init*;
	CONST ET_EXEC = 2; 
	BEGIN
		IF LB.impCount >0 THEN LB.Error('Import ffrom dll') END;
		DefSegments;
		elfType := ET_EXEC;
		LB.ImageBase := defBaseAddress;
		
		code.size := LB.codeSize; 
		data.size := LB.dataSize;
		vars.size := LB.varsSize;

		code.offs := LB.Aligned(elfHeaderSize + phtSize, 4);
		code.addr := code.offs + LB.ImageBase;
		data.offs := LB.Aligned(code.offs + code.size, pageSize);
		data.addr := LB.Aligned(code.addr + code.size, pageSize);
		vars.addr := LB.Aligned(data.addr + data.size, 4);

		entry_addr:= code.addr + LB.initOffs;
	END Init;


(**********************************)
	PROCEDURE GetBases*(OUT codeBase, dataBase, varsBase: INTEGER);
	BEGIN
		codeBase := code.addr;
		dataBase := data.addr;
		varsBase := vars.addr;
	END GetBases;

(**********************************)	
	PROCEDURE CalculateLayout;
		PROCEDURE DefSeg(VAR seg: Segment; offs, addr, size: INTEGER); 
		BEGIN
			seg.offs:= offs;
			seg.addr:= addr;
			seg.msize:= size;
			seg.fsize:= size;
		END DefSeg;

	BEGIN
		DefSeg(phtseg, phtOffset, LB.ImageBase + phtOffset, phtSize);
		DefSeg(codeseg, 0, LB.ImageBase, code.offs + code.size);
		DefSeg(dataseg, data.offs, data.addr, data.size); 
			dataseg.msize := dataseg.fsize + vars.size;
	END CalculateLayout;

(**********************************)		
 PROCEDURE WriteElfHeader;
	CONST
			ELFCLASS32 = 1X; ELFDATA2LSB = 1X; EV_CURRENT = 1X;
		(* OS ABI  *)
			ELFOSABI_NONE  = 0X; 
			ELFOSABI_LINUX = 3X;
			ELFOSABI_NETBSD  = 2X; ELFOSABI_FREEBSD =9X; ELFOSABI_OPENBSD = 0BX;
			ELFOSABI_ARM = 61X;

			EM_386 = 3;
	VAR	e_ident : ARRAY 16 OF SHORTCHAR; 
	BEGIN
		e_ident := 7FX + 'ELF'+ ELFCLASS32 + ELFDATA2LSB + EV_CURRENT + ELFOSABI_NONE;
		WriteStr(e_ident); PadTo(16);
		Write2(elfType); 
		Write2(EM_386); 
		Write4(1); (* version  *)
		Write4(entry_addr); 
		Write4(phtOffset); 
		Write4(0); 
		Write4(0); (* flags  *)
		Write2(elfHeaderSize); 
		Write2(phtEntrySize);
		Write2(phtEntryCount); 
		Write2(0); 		
		Write2(0); 
		Write2(0); 
		ASSERT(out.Pos() = elfHeaderSize, 101)
	END WriteElfHeader;

	PROCEDURE WriteProgramHeaderTable;
 	PROCEDURE WritePhtEntry(VAR seg: Segment);
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
		WritePhtEntry(codeseg);
		WritePhtEntry(dataseg);
	END WriteProgramHeaderTable;

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
		
		WriteCode;
		WriteData;
		
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

END DevLnkWriteElfStatic.

/ Elf
	PHT
\ Code;
	
/ Const
\ Vars
