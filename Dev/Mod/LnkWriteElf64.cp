MODULE DevLnkWriteElf64;

	IMPORT LB := DevLnkBase, Files;

	CONST base = 400000H; page = 4096; text = 4096;
		interpreter = "/lib64/ld-linux-x86-64.so.2";

	VAR out: Files.Writer; names: LB.StringTable;
		data, strings, symbols, hash, rela, got, dynamic, vars, dynamicSize, sectionTable: INTEGER;
		sectionNames: LB.StringTable; sectionName: ARRAY 12 OF INTEGER;

	PROCEDURE W4 (x: INTEGER);
	BEGIN
		out.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		out.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		out.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		out.WriteByte(SHORT(SHORT(x)))
	END W4;

	PROCEDURE W8 (x: INTEGER);
	BEGIN W4(x); W4(0) END W8;

	PROCEDURE W2 (x: INTEGER);
	BEGIN out.WriteByte(SHORT(SHORT(x MOD 256))); out.WriteByte(SHORT(SHORT(x DIV 256))) END W2;

	PROCEDURE Pad (position: INTEGER);
	BEGIN
		IF out.Pos() > position THEN LB.Error("ELF64 layout overlap"); RETURN END;
		WHILE out.Pos() < position DO out.WriteByte(0) END
	END Pad;

	PROCEDURE Init*;
		VAR lib: LB.Module; sym: LB.Symbol; n: INTEGER;
	BEGIN
		LB.ImageBase := base; LB.InitStringTable(names, 1024);
		LB.AddName(names, "", n); lib := LB.dllList; n := 1;
		WHILE lib # NIL DO
			LB.AddName(names, lib.exp.name$, lib.exp.namIdx);
			sym := lib.exp.next;
			WHILE sym # NIL DO
				LB.AddName(names, sym.name$, sym.namIdx); sym.symIdx := n; INC(n); sym := sym.next
			END;
			lib := lib.next
		END;
		data := LB.Aligned(text + LB.codeSize, page);
		strings := data + LB.dataSize; symbols := LB.Aligned(strings + names.len, 8);
		hash := symbols + 24 * (LB.impCount+1);
		rela := LB.Aligned(hash + 4 * (LB.impCount+4), 8);
		got := rela + 24 * LB.impCount;
		dynamic := got + 8 * LB.impCount; dynamicSize := 16 * (LB.impDllCount+11);
		vars := LB.Aligned(dynamic+dynamicSize, 16);
		LB.InitStringTable(sectionNames, 256);
		LB.AddName(sectionNames, "", sectionName[0]);
		LB.AddName(sectionNames, ".text", sectionName[1]); LB.AddName(sectionNames, ".data", sectionName[2]);
		LB.AddName(sectionNames, ".bss", sectionName[3]); LB.AddName(sectionNames, ".dynstr", sectionName[4]);
		LB.AddName(sectionNames, ".dynsym", sectionName[5]); LB.AddName(sectionNames, ".hash", sectionName[6]);
		LB.AddName(sectionNames, ".rela.dyn", sectionName[7]); LB.AddName(sectionNames, ".got", sectionName[8]);
		LB.AddName(sectionNames, ".dynamic", sectionName[9]); LB.AddName(sectionNames, ".interp", sectionName[10]);
		LB.AddName(sectionNames, ".shstrtab", sectionName[11]);
		sectionTable := LB.Aligned(vars+sectionNames.len, 8);
		IF LONG(base)+vars+LB.varsSize > MAX(INTEGER) THEN LB.Error("ELF64 image exceeds low 2 GiB") END
	END Init;

	PROCEDURE GetBases* (OUT codeBase, dataBase, varsBase: INTEGER);
	BEGIN codeBase := base+text; dataBase := base+data; varsBase := base+vars END GetBases;

	PROCEDURE Segment (kind, flags, offset, fileSize, memSize, alignment: INTEGER);
	BEGIN
		W4(kind); W4(flags); W8(offset); W8(base+offset); W8(base+offset);
		W8(fileSize); W8(memSize); W8(alignment)
	END Segment;

	PROCEDURE Dyn (tag, value: INTEGER);
	BEGIN W8(tag); W8(value) END Dyn;

	PROCEDURE Section (index, kind, flags, offset, size, link, info, alignment, entrySize: INTEGER);
	BEGIN
		W4(sectionName[index]); W4(kind); W8(flags);
		IF ODD(flags DIV 2) THEN W8(base+offset) ELSE W8(0) END;
		W8(offset); W8(size); W4(link); W4(info); W8(alignment); W8(entrySize)
	END Section;

	PROCEDURE WriteOut*;
		VAR file: Files.File; loc: Files.Locator; lib, mod: LB.Module; sym: LB.Symbol;
			i, j, split, res, count: INTEGER; path, name: Files.Name; interp: ARRAY 64 OF CHAR;
	BEGIN
		split := -1; i := 0;
		WHILE LB.outputName[i] # 0X DO
			IF LB.outputName[i] = "/" THEN split := i END; INC(i)
		END;
		i := 0; WHILE i <= split DO path[i] := LB.outputName[i]; INC(i) END; path[i] := 0X;
		j := 0; WHILE LB.outputName[i] # 0X DO name[j] := LB.outputName[i]; INC(i); INC(j) END; name[j] := 0X;
		loc := Files.dir.This(path); file := Files.dir.New(loc, Files.ask);
		IF file = NIL THEN LB.Error("cannot create ELF64 output"); RETURN END;
		out := file.NewWriter(NIL);
		W4(464C457FH); W4(00010102H); W8(0);
		W2(2); W2(62); W4(1); W8(base+text+LB.initOffs); W8(64); W8(sectionTable); W4(0);
		W2(64); W2(56); W2(6); W2(64); W2(12); W2(11);
		Segment(6, 4, 64, 6*56, 6*56, 8);
		Segment(3, 4, 512, LEN(interpreter)+1, LEN(interpreter)+1, 1);
		Segment(1, 5, 0, data, data, page);
		Segment(1, 6, data, vars-data, vars-data+LB.varsSize, page);
		Segment(2, 6, dynamic, dynamicSize, dynamicSize, 8);
		(* Non-executable stack. *)
		W4(6474E551H); W4(6); W8(0); W8(0); W8(0); W8(0); W8(0); W8(16);
		interp := interpreter; Pad(512); FOR i := 0 TO LEN(interpreter) DO out.WriteByte(SHORT(SHORT(ORD(interp[i])))) END;
		Pad(text); out.WriteBytes(LB.initMod.code.bytes, 0, LB.initMod.code.size);
		mod := LB.modList;
		WHILE mod # NIL DO out.WriteBytes(mod.code.bytes, 0, mod.code.size); mod := mod.next END;
		lib := LB.dllList;
		WHILE lib # NIL DO
			sym := lib.exp.next;
			WHILE sym # NIL DO
				out.WriteByte(-1); out.WriteByte(37);
				W4(base+got+8*(sym.symIdx-1)-sym.addr-6); sym := sym.next
			END;
			lib := lib.next
		END;
		Pad(data); mod := LB.modList;
		WHILE mod # NIL DO out.WriteBytes(mod.data.bytes, 0, mod.data.size); mod := mod.next END;
		Pad(strings); FOR i := 0 TO names.len-1 DO out.WriteByte(SHORT(ORD(names.tab[i]))) END;
		Pad(symbols); FOR i := 1 TO 6 DO W4(0) END;
		lib := LB.dllList;
		WHILE lib # NIL DO
			sym := lib.exp.next;
			WHILE sym # NIL DO
				W4(sym.namIdx); out.WriteByte(18); out.WriteByte(0); W2(0); W8(0); W8(0); sym := sym.next
			END;
			lib := lib.next
		END;
		Pad(hash); count := LB.impCount;
		W4(1); W4(count+1); W4(1); W4(0);
		FOR i := 1 TO count DO IF i = count THEN W4(0) ELSE W4(i+1) END END;
		Pad(rela);
		FOR i := 1 TO count DO W8(base+got+8*(i-1)); W4(6); W4(i); W8(0) END;
		Pad(got); FOR i := 1 TO count DO W8(0) END;
		Pad(dynamic); lib := LB.dllList;
		WHILE lib # NIL DO Dyn(1, lib.exp.namIdx); lib := lib.next END;
		Dyn(4, base+hash); Dyn(5, base+strings); Dyn(6, base+symbols); Dyn(10, names.len); Dyn(11, 24);
		Dyn(7, base+rela); Dyn(8, 24*count); Dyn(9, 24); Dyn(21, 0); Dyn(24, 0); Dyn(0, 0);
		Pad(vars); LB.WriteStringTable(out, sectionNames); Pad(sectionTable);
		FOR i := 1 TO 16 DO W4(0) END;
		Section(1, 1, 6, text, LB.codeSize, 0, 0, 16, 0);
		Section(2, 1, 3, data, LB.dataSize, 0, 0, 8, 0);
		Section(3, 8, 3, vars, LB.varsSize, 0, 0, 16, 0);
		Section(4, 3, 2, strings, names.len, 0, 0, 1, 0);
		Section(5, 11, 2, symbols, 24*(count+1), 4, 1, 8, 24);
		Section(6, 5, 2, hash, 4*(count+4), 5, 0, 4, 4);
		Section(7, 4, 2, rela, 24*count, 5, 0, 8, 24);
		Section(8, 1, 3, got, 8*count, 0, 0, 8, 8);
		Section(9, 6, 3, dynamic, dynamicSize, 4, 0, 8, 16);
		Section(10, 1, 2, 512, LEN(interpreter)+1, 0, 0, 1, 0);
		Section(11, 3, 0, vars, sectionNames.len, 0, 0, 1, 0);
		IF ~LB.error THEN
			file.Register(name, LB.outputType, Files.ask, res);
			IF res # 0 THEN LB.Error("cannot register ELF64 output")
			ELSE LB.Chmod(LB.outputName, {0, 2, 3, 4, 5, 6, 7, 8}) END
		END;
		out := NIL
	END WriteOut;

END DevLnkWriteElf64.
