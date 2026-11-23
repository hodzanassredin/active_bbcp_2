MODULE DevLnkLoad64;
(**	project	= "BlackBox 2.0, new module"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Igor Dehtyarenko, Alexander Shiryaev, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)
	
	IMPORT LB := DevLnkBase, SYSTEM, Files;
	
	CONST
		newRecName = "NewRec";
		newArrName = "NewArr";
		
		(* meta interface consts *)
		mConst = 1; mTyp = 2; mVar = 3; mProc = 4; 
		mInternal = 1;  mExported = 4;

		(* Module descriptor fields *)
			mdf_next = 0; mdf_opts = 8; mdf_refcnt = 12; mdf_term = 48; 
			mdf_names = 120; mdf_imports = 136; mdf_export = 144;

	TYPE
		Module =  LB.Module;
		Section = LB.Section;
		STRING = ARRAY OF SHORTCHAR; (* Native parameter descriptor when built on AMD64. *)
		StrPtr = POINTER TO ARRAY [untagged] OF SHORTCHAR;
	
	VAR loading: ARRAY 128 OF LB.Name; depth: INTEGER;

	PROCEDURE^ LoadModule(IN modName: STRING):Module;
	
	PROCEDURE ThisMod*(IN name: STRING): Module;
		VAR mod: Module; i: INTEGER;
	BEGIN
		IF name[0] = "$" THEN
			mod := LB.ThisDll(name)
		ELSE
			mod := LB.modList;
			WHILE (mod # NIL) & (mod.name$ # name) DO mod := mod.next END;
			IF mod = NIL THEN
				i := 0;
				WHILE i < depth DO
					IF loading[i] = name THEN LB.Error(name$ + ": cyclic import"); RETURN NIL END; INC(i)
				END;
				IF depth = LEN(loading) THEN LB.Error("import nesting too deep"); RETURN NIL END;
				loading[depth] := name$; INC(depth);
				mod := LoadModule(name); DEC(depth);
				IF mod # NIL THEN LB.RegMod(mod) END;
			END;
		END;
		RETURN mod
	END ThisMod;

	PROCEDURE Read4(inp: Files.Reader; VAR x: INTEGER);
		VAR b: BYTE; y: INTEGER;
	BEGIN
		inp.ReadByte(b); y := b MOD 256;
		inp.ReadByte(b); y := y + 100H * (b MOD 256);
		inp.ReadByte(b); y := y + 10000H * (b MOD 256);
		inp.ReadByte(b); x := y + 1000000H * b;
		IF inp.eof THEN LB.Error("truncated OCF header"); x := 0 END
	END Read4;

	PROCEDURE ReadNum(inp: Files.Reader; VAR x: INTEGER);
		VAR b: BYTE; s, y: INTEGER;
	BEGIN
		s := 0; y := 0; 
		inp.ReadByte(b);
		WHILE (b < 0) & ~inp.eof DO
			IF s >= 28 THEN LB.Error("invalid OCF integer"); x := 0; RETURN END; 
			INC(y, ASH(b + 128, s)); 
			INC(s, 7); 
			inp.ReadByte(b) 
		END;
		x := ASH((b + 64) MOD 128 - 64, s) + y;
		IF inp.eof THEN LB.Error("truncated OCF integer"); x := 0 END
	END ReadNum;
	
	PROCEDURE ReadName (inp: Files.Reader; VAR name: STRING);
		VAR i: INTEGER; b: BYTE;
	BEGIN 
		i := 0;
		REPEAT
			IF i >= LEN(name)-1 THEN LB.Error("OCF name too long"); name[i] := 0X; RETURN END;
			inp.ReadByte(b);
			IF inp.eof THEN LB.Error("truncated OCF name"); name[i] := 0X; RETURN END;
			name[i] := SHORT(CHR(b MOD 256)); INC(i)
		UNTIL b = 0
	END ReadName;

	PROCEDURE Get3(VAR d:ARRAY OF BYTE; a: INTEGER): INTEGER;
	BEGIN
		RETURN d[a] MOD 256 + d[a+1] MOD 256 * 256 + d[a+2] * 10000H;
	END Get3;

	PROCEDURE Get4(VAR d:ARRAY OF BYTE; a: INTEGER): INTEGER;
	BEGIN
		RETURN ((d[a+3] * 256 + d[a+2] MOD 256) * 256 + d[a+1] MOD 256) * 256 + d[a] MOD 256
	END Get4;

	PROCEDURE Put4(VAR d:ARRAY OF BYTE; a: INTEGER; x: INTEGER);
	BEGIN
		d[a] := SHORT(SHORT(x));
		d[a+1] := SHORT(SHORT(x DIV 100H));
		d[a+2] := SHORT(SHORT(x DIV 10000H));
		d[a+3] := SHORT(SHORT(x DIV 1000000H))
	END Put4;
 
	PROCEDURE DWord(mod: Module; a: INTEGER): INTEGER;
	BEGIN
		IF (a < 0) OR (a > mod.data.size - 4) THEN LB.Error("invalid metadata offset"); RETURN 0 END;
		RETURN Get4(mod.data.bytes, a);
	END DWord;

	PROCEDURE MWord(mod: Module; offs: INTEGER): INTEGER;
	BEGIN
		RETURN DWord(mod, mod.mDesc + offs);
	END MWord;

	PROCEDURE PutMWord(mod: Module; offs: INTEGER; x: INTEGER);
	BEGIN
		Put4(mod.data.bytes, mod.mDesc + offs, x);
	END PutMWord;

	PROCEDURE GetPName(mod: Module; idx: INTEGER): StrPtr;
	VAR pNames, i: INTEGER; 
	BEGIN
		pNames := MWord(mod, mdf_names);
		IF (idx < 0) OR (pNames < 0) OR (LONG(pNames) + idx >= mod.data.size) THEN
			LB.Error("invalid metadata name"); RETURN NIL
		END;
		i := pNames + idx;
		WHILE (i < mod.data.size) & (mod.data.bytes[i] # 0) DO INC(i) END;
		IF i = mod.data.size THEN LB.Error("unterminated metadata name"); RETURN NIL END;
		RETURN SYSTEM.VAL(StrPtr, SYSTEM.ADR(mod.data.bytes[pNames + idx]));
	END GetPName;

	PROCEDURE GetDir(mod: Module; VAR pDir, numobj: INTEGER);
	BEGIN
		pDir := MWord(mod, mdf_export);	(* numobj:4 {Object}	*)
		numobj := DWord(mod,  pDir); 
		IF (pDir < 0) OR (numobj < 0) OR (LONG(pDir) + 4 + LONG(numobj) * 24 > mod.data.size) THEN
			LB.Error("invalid export directory"); numobj := 0; pDir := 0
		END;
		pDir := pDir + 4; 
	END GetDir;


	PROCEDURE SearchFprint(mod: Module; fprint: INTEGER): INTEGER;
	VAR p, numobj, fp: INTEGER; 
	BEGIN
		GetDir(mod, p, numobj);
		IF numobj = 0 THEN RETURN -1 END;
		fp := DWord(mod, p + 4);
		WHILE (fp # fprint) & (numobj > 0) DO
			INC(p, 24); DEC(numobj);
			IF numobj > 0 THEN fp := DWord(mod, p + 4) END;
		END;
		IF (numobj > 0) & (fp = fprint) THEN RETURN p ELSE RETURN -1 END;
	END SearchFprint;

	PROCEDURE SearchName(mod: Module; IN name: STRING): INTEGER;
	VAR  l, r, m, p, id: INTEGER; 
			p0, numobj: INTEGER; 
			s:StrPtr;
	BEGIN
		GetDir(mod, p0, numobj);
		l := 0; r := numobj-1; 
		WHILE (l <= r) DO	(* binary search *)
			m := (l + r) DIV 2; 
			p := p0 + m * 24;
			id := DWord(mod, p + 8);
			s := GetPName(mod, id DIV 256);
			IF s = NIL THEN RETURN -1 END;
			IF name = s$ THEN
				RETURN p 
			ELSIF name > s$ THEN 
				l := m + 1 
			ELSE 
				r := m - 1
			END
		END;
		RETURN -1
	END SearchName;

	
	PROCEDURE SearchObj(mod: Module; IN name: STRING; mode, fprint, opt: INTEGER; 
			OUT sec: Section; OUT adr: INTEGER);
	VAR pObj, id, md,  fp: INTEGER; 
	BEGIN
		sec:= NIL; adr := -1;
		IF mod = NIL THEN LB.Error("missing imported module"); RETURN END;
		IF mod.isDll THEN
			IF mode = mProc THEN
				sec := mod.code;
				LB.ImportProc(mod, name, adr);
			ELSIF mode = mTyp THEN
				LB.Error(name$ + " import type from DLL"); 
			ELSIF mode = mVar THEN
				LB.Error(name$ + " import var from DLL"); 
			END;
		
		ELSIF name = "" THEN 
			pObj := SearchFprint(mod, fprint);
			IF pObj < 0 THEN
				LB.Error("anonymous type not found"); 
			ELSE
				id := DWord(mod,  pObj + 8);
				IF (id MOD 16 = mTyp) & (id DIV 256 = 0) THEN
					adr := DWord(mod,  pObj + 16); 
					sec := mod.data;
				ELSE
					LB.Error("anonymous type not found"); 
				END;
			END
			
		ELSE 
			pObj := SearchName(mod, name);
			IF pObj < 0 THEN
				LB.Error(mod.name$+ "."+ name$ + " not found "); 
			ELSE
				fp := DWord(mod, pObj);
				id := DWord(mod, pObj + 8);
				md := id MOD 16;
				IF (opt >= 0) & ODD(opt) & (mode = mTyp) THEN fp := DWord(mod, pObj + 4) END;
				IF md # mode THEN 
					LB.Error(mod.name$+ "."+ name$ + " has wrong class");
				ELSIF (opt # -1) & (fp # fprint) THEN
					LB.Error(mod.name$+ "."+ name$ + " has wrong fingerprint");
				ELSIF mode = mTyp THEN
					IF (opt > 1) & (id DIV 16 MOD 16 # mExported) THEN
						LB.Error(mod.name$+ "."+ name$ + " has wrong visibility");
					END;
					sec := mod.data; 
					adr := DWord(mod,  pObj + 16)
				ELSIF mode = mVar THEN
					sec := mod.vars; 
					adr := DWord(mod,  pObj + 4);
				ELSIF mode = mProc THEN
					sec := mod.code; 
					adr := DWord(mod,  pObj + 4)
				END;
			END;
		END;
	END SearchObj;


	PROCEDURE LoadModule(IN modName: STRING):Module;
	VAR mod: Module;
			file: Files.File;
			inp: Files.Reader; 
			nofimp: INTEGER;
			import: ARRAY 128 OF Module;

		PROCEDURE ReadHeader;
		CONST	OFtag = 6F4F4346H; (*  "oOCF" *)
		VAR mtag, processor, i : INTEGER;
				iname: LB.Name;
				headSize, descSize, metaSize, codeSize, varsSize: INTEGER;
				dataSize: INTEGER;
		BEGIN
			Read4(inp, mtag);         (* mtag = OFtag *);
			Read4(inp, processor);
			IF (mtag # OFtag) OR (processor # 12) THEN
				LB.Error(modName$ + ": expected Dev AMD64 OCF"); RETURN
			END;
			Read4(inp, headSize); 
			Read4(inp, metaSize); 
			Read4(inp, descSize); 
			Read4(inp, codeSize); 
			Read4(inp, varsSize); 
			IF (headSize < 32) OR (metaSize < 0) OR (descSize < 160) OR
				(codeSize < 1) OR (varsSize < 0) OR
				(LONG(headSize) + metaSize + descSize + codeSize > file.Length()) THEN
				LB.Error(modName$ + ": invalid OCF sizes"); RETURN
			END;
			IF LONG(metaSize) + descSize + codeSize + varsSize > 70000000H THEN
				LB.Error("OCF exceeds supported low-address image size"); RETURN
			END;
			dataSize := metaSize + descSize;
			mod.mDesc := metaSize;
			mod.data := LB.NewSection(dataSize);	
			mod.code := LB.NewSection(codeSize);	
			mod.vars := LB.NewSection(LB.Aligned(varsSize, 16));	

			ReadNum(inp, nofimp);
			IF LB.error THEN RETURN END;
			IF (nofimp < 0) OR (nofimp >= LEN(import)) THEN LB.Error("invalid OCF import count"); RETURN END;
			ReadName(inp, iname); 
			IF iname # modName THEN LB.Error(modName$ + ": OCF name mismatch (Hr OCF is not supported)"); RETURN END;
			mod.name := modName$;
			FOR i := 1 TO nofimp  DO 
				ReadName(inp, iname);
				IF iname = "$$" THEN iname := LB.KernelName END;
				import[i] := ThisMod(iname);
				IF LB.error THEN RETURN END;
			END;
			IF inp.Pos() > headSize THEN LB.Error("OCF header overlaps blocks"); RETURN END;
			inp.SetPos(headSize);
		END ReadHeader;

		PROCEDURE ReadSection(sec: Section);
		BEGIN
			NEW(sec.bytes, sec.size);
			inp.ReadBytes(sec.bytes, 0, sec.size); 
		END ReadSection;
		
		PROCEDURE WrongLink (fixt, link : INTEGER);
		BEGIN
			LB.Error(" Wrong link in " + modName$); 
		END WrongLink;

		PROCEDURE FixChain(link, offset: INTEGER; sec: Section; val: INTEGER);
			VAR fixtype, fixdat, at, steps, width, addend: INTEGER;
				dst: Section; isCode: BOOLEAN; v: LONGINT;
		BEGIN
			steps := 0;
			WHILE (link # 0) & ~LB.error DO
				INC(steps);
				IF steps > mod.code.size + mod.data.size THEN LB.Error("cyclic OCF fixup"); RETURN END;
				isCode := link > 0;
				IF isCode THEN dst := mod.code; at := link ELSE dst := mod.data; at := -link END;
				IF (at < 0) OR (at > dst.size - 4) THEN WrongLink(0, link); RETURN END;
				fixdat := Get3(dst.bytes, at); fixtype := dst.bytes[at+3] MOD 256;
				IF (fixtype = 101) OR ((fixtype = 106) OR (fixtype = 107) OR (fixtype = 108) OR (fixtype = 110) OR (fixtype = 114)) THEN width := 4 ELSE width := 8 END;
				IF at > dst.size - width THEN WrongLink(fixtype, link); RETURN END;
				v := LONG(offset) + val;
				IF (v < MIN(INTEGER)) OR (v > MAX(INTEGER)) THEN LB.Error("OCF addend overflow"); RETURN END;
				addend := SHORT(v);
				IF width = 4 THEN
					IF ~isCode THEN WrongLink(fixtype, link); RETURN END;
					v := LONG(addend) - at - 4;
					IF fixtype # 101 THEN DEC(v, fixtype - 106) END;
					IF (v < MIN(INTEGER)) OR (v > MAX(INTEGER)) THEN LB.Error("disp32 overflow"); RETURN END;
					Put4(dst.bytes, at, SHORT(v)); LB.FixRel(mod, at, sec); link := fixdat
				ELSIF (fixtype = 100) OR (fixtype = 103) OR (fixtype = 104) THEN
					IF fixtype # 100 THEN addend := fixdat + val END;
					LB.Put8(dst.bytes, at, addend);
					IF isCode THEN LB.FixCode(mod, at, sec) ELSE LB.FixData(mod, at, sec) END;
					IF fixtype = 103 THEN IF isCode THEN INC(link, 8) ELSE DEC(link, 8) END
					ELSIF fixtype = 104 THEN link := 0 ELSE link := fixdat END
				ELSIF (fixtype = 102) & ~isCode THEN
					LB.Put8(dst.bytes, at, addend); LB.FixCopy(mod, at, sec); link := fixdat
				ELSE WrongLink(fixtype, link); RETURN
				END
			END
		END FixChain;

		
		PROCEDURE FixLocal(sec: Section; val: INTEGER);
		VAR offset, link: INTEGER;
		BEGIN
			ReadNum(inp, link);
			WHILE (link # 0) & ~LB.error DO
				ReadNum(inp, offset);
				FixChain(link, offset, sec, val);
				ReadNum(inp, link)
			END
		END FixLocal;

		PROCEDURE FixExt(imod:Module; IN name: STRING; mode, fp, opt: INTEGER);
		VAR offset, link: INTEGER; sec: Section; adr: INTEGER; 
		BEGIN
			ReadNum(inp, link);
			(* A DLL declaration may export compile-time types without any runtime
			   descriptor references. Such a use needs no DLL symbol. *)
			IF (imod # NIL) & imod.isDll & (mode = mTyp) & (link = 0) THEN RETURN END;
			SearchObj(imod, name, mode, fp, opt, sec, adr);
			WHILE (link # 0) & ~LB.error DO
				ReadNum(inp, offset);
				FixChain(link, offset, sec, adr);
				ReadNum(inp,link)
			END
		END FixExt;

		PROCEDURE FixKernel(IN name: STRING);
		VAR offset, link: INTEGER; sec: Section; oadr: INTEGER; k: Module; 
		BEGIN
			ReadNum(inp, link);
			IF link # 0 THEN
				IF mod.name = LB.KernelName THEN k := mod ELSE k := LB.kernelMod END;
				IF k # NIL THEN
					SearchObj(k, name, mProc, 0, -1, sec, oadr);
					WHILE (link # 0) & ~LB.error DO
						ReadNum(inp, offset);
						FixChain(link, offset, sec, oadr);
						ReadNum(inp, link)
					END
				ELSE
					LB.Error("no kernel"); RETURN
				END
			END;
		END FixKernel;

		PROCEDURE ReadFixups;
		VAR mno : INTEGER; otag, fprint, opt, kernelPos, usesPos, link, offset: INTEGER; 
				name: LB.Name;
		BEGIN
			(* FixBlk *)
			kernelPos := inp.Pos();
			FOR mno := 1 TO 2 DO
				ReadNum(inp, link);
				WHILE (link # 0) & ~LB.error DO ReadNum(inp, offset); ReadNum(inp, link) END
			END;
			FixLocal(mod.data, 0);	(* Const, Meta *); 
			FixLocal(mod.data, mod.mDesc);	(* Descriptors  *)
			FixLocal(mod.code, 0);	(* proc, CaseLinks, Code.links *)
			FixLocal(mod.vars, 0); 
			usesPos := inp.Pos(); inp.SetPos(kernelPos);
			FixKernel(newRecName); FixKernel(newArrName); inp.SetPos(usesPos);
			(* UseBlk: dlls, modules *)
			FOR mno := 1 TO nofimp DO
				ReadNum(inp, otag);
				WHILE (otag # 0) & ~LB.error DO
					IF (otag < mConst) OR (otag > mProc) THEN LB.Error("invalid OCF use tag"); RETURN END;
					ReadName(inp, name);
					ReadNum(inp, fprint); 
					IF otag = mTyp THEN ReadNum(inp, opt) ELSE opt := 0 END;
					IF otag # mConst THEN 
						FixExt(import[mno], name, otag, fprint, opt);
						IF LB.error THEN RETURN END;  
					END;
					ReadNum(inp, otag)
				END;
			END;	
		END ReadFixups;
		
		PROCEDURE ReadUnwind;
			VAR magic, version, count, i, n, previous: INTEGER; u, tail: LB.Unwind;
		BEGIN
			IF inp.Pos() = file.Length() THEN RETURN END; (* legacy OCF *)
			Read4(inp, magic); Read4(inp, version); Read4(inp, count);
			IF LB.error THEN RETURN END;
			IF (magic # 34365755H) OR (version # 1) OR (count < 0) OR
				(count > (file.Length() - inp.Pos()) DIV 16) THEN LB.Error("invalid UW64 header"); RETURN END;
			previous := 0; tail := NIL;
			FOR i := 0 TO count - 1 DO
				NEW(u); Read4(inp, u.first); Read4(inp, u.last); Read4(inp, n);
				IF LB.error THEN RETURN END;
				IF (u.first < previous) OR (u.last <= u.first) OR (u.last > mod.code.size) OR
					(n < 4) OR (n > 260) OR (n MOD 4 # 0) OR (n > file.Length() - inp.Pos()) THEN
					LB.Error("invalid UW64 function"); RETURN
				END;
				NEW(u.bytes, n); inp.ReadBytes(u.bytes, 0, n);
				IF (u.bytes[0] # 1) OR ((4 + 2 * (u.bytes[2] MOD 256) + 3) DIV 4 * 4 # n) OR
					(u.bytes[1] MOD 256 > u.last - u.first) THEN LB.Error("invalid UNWIND_INFO"); RETURN END;
				IF tail = NIL THEN mod.unwind := u ELSE tail.next := u END;
				tail := u; previous := u.last
			END;
			IF inp.Pos() # file.Length() THEN LB.Error("unexpected data after UW64") END
		END ReadUnwind;

		PROCEDURE FixImpRef;
		VAR imod: Module; i, imptab, refcnt: INTEGER; 
		BEGIN
			imptab := MWord(mod, mdf_imports);
			IF (imptab < 0) OR (LONG(imptab) + nofimp * 8 > mod.data.size) THEN LB.Error("invalid module import table"); RETURN END;	
			FOR i := 1 TO nofimp DO
				imod :=	import[i];
				IF (imod # NIL) & ~imod.isDll THEN
					(* import ref *)
					LB.Put8(mod.data.bytes, imptab, imod.mDesc);	(* ->DescBlk*)
					LB.FixData(mod, imptab, imod.data);
					(* inc ref count of imported module *)
					refcnt := MWord(imod, mdf_refcnt);	
					PutMWord(imod, mdf_refcnt, refcnt + 1);	
				END;
				INC(imptab, 8);
			END;
		END FixImpRef;

		PROCEDURE SetOpts;
		VAR opts: SET; 
		BEGIN
			opts := BITS(MWord(mod, mdf_opts));
			IF ~LB.dynaInit THEN INCL(opts, 16) END;	(* init bit (16) *)
			IF LB.outDll THEN INCL(opts, 24) END;	(* dll bit (24) *)
			PutMWord(mod, mdf_opts, ORD(opts)); 
		END SetOpts;

	BEGIN
		file := LB.ThisObjFile(modName$);
		IF file = NIL THEN
			LB.Error(modName$ + " not found"); 
			RETURN NIL;
		END;
		IF file.Length() < 32 THEN LB.Error(modName$ + ": truncated OCF header"); file.Close; RETURN NIL END;
		inp := file.NewReader(NIL); 
		inp.SetPos(0);
		NEW(mod);
		mod.isDll := FALSE;

		ReadHeader;
		IF LB.error THEN file.Close; RETURN NIL END;
		ReadSection(mod.data); 
		ReadSection(mod.code); 
		LB.InitFixTable(mod.dataFix, mod.data); 
		LB.InitFixTable(mod.codeFix, mod.code);
		ReadFixups;
		IF ~LB.error THEN ReadUnwind END;
		IF LB.error THEN file.Close; RETURN NIL END;
		mod.termadr := MWord(mod, mdf_term);
		FixImpRef;
		SetOpts;
		IF LB.lastMod # NIL THEN 
			LB.Put8(mod.data.bytes, mod.mDesc, LB.lastMod.mDesc); 
			LB.FixData(mod, mod.mDesc, LB.lastMod.data);
		END;
		file.Close;
		RETURN mod;
	END LoadModule;

	PROCEDURE CollectExports(mod: Module);
	VAR p, numobj: INTEGER; 
			id, vis, mode, addr: INTEGER; 
			name: StrPtr;
	BEGIN
		IF ~mod.exported THEN
			GetDir(mod, p, numobj);
			WHILE numobj > 0 DO
				id := DWord(mod,  p + 8);	(* id = nameIdx*256 + vis*16 + mode *)
				mode := id MOD 16;
				vis := id DIV 16 MOD 16;
				IF (vis # mInternal) & (mode = mProc) THEN
					name := GetPName(mod, id DIV 256);
				IF name = NIL THEN RETURN END;
					addr := DWord(mod,  p + 4);
					LB.ExportProc(mod, name$, addr);
				END;
				DEC(numobj); INC(p, 24)
			END;
			mod.exported := TRUE;
		END;
	END CollectExports;

	PROCEDURE ExportVariable*(IN modname: ARRAY OF CHAR; varname: ARRAY OF SHORTCHAR);
	VAR p, numobj: INTEGER; 
			id, vis, mode, addr: INTEGER; 
			name: StrPtr;
			mod: Module;
	BEGIN
		mod := LB.modList;
		WHILE mod # NIL DO
			(* Log.String(mod.name$);Log.Ln; *)
			mod := mod.next 
		END;
		(* Log.String("HIA: varname = " + varname$); Log.Ln; *)

		mod := ThisMod(SHORT(modname$));
				
		GetDir(mod, p, numobj);
		WHILE numobj > 0 DO
			id := DWord(mod,  p + 8);	(* id = nameIdx*256 + vis*16 + mode *)
			mode := id MOD 16;
			vis := id DIV 16 MOD 16;
			IF (vis # mInternal) & (mode = mVar) THEN
				name := GetPName(mod, id DIV 256);
				IF name = NIL THEN RETURN END;
				
				(* Log.String("HIA: name = " + name$); Log.Ln; *)
				addr := DWord(mod,  p + 4);
				IF varname$ = name$ THEN
					(* Log.String("Export var " + varname);Log.Ln; *)
					LB.ExportVar(mod, name$, addr);
				END;
			END;
			DEC(numobj); INC(p, 24)
		END;
	END ExportVariable;

	PROCEDURE AddModule*(IN modname: ARRAY OF CHAR);
	VAR mod: Module;
	BEGIN
		mod := ThisMod(SHORT(modname$));
	END AddModule;

	PROCEDURE MainModule*(IN modname: ARRAY OF CHAR);
	VAR mod: Module;
	BEGIN
		mod := ThisMod(SHORT(modname$));
		IF mod # NIL THEN LB.mainMod := mod END;
	END MainModule;
	
	PROCEDURE ExportModule*(IN modname: ARRAY OF CHAR);
	VAR mod: Module;
	BEGIN
		mod := ThisMod(SHORT(modname$));
		IF mod # NIL THEN CollectExports(mod) END;
	END ExportModule;

	PROCEDURE VariableAddress* (mod: Module; IN name: ARRAY OF SHORTCHAR): INTEGER;
		VAR sec: Section; adr: INTEGER;
	BEGIN
		SearchObj(mod, name, mVar, 0, -1, sec, adr);
		RETURN adr
	END VariableAddress;

	PROCEDURE ExportAll*;
	VAR mod: Module;
	BEGIN
		mod := LB.modList;
		WHILE (mod # NIL) DO
			CollectExports(mod); 
			mod := mod.next 
		END;
	END ExportAll;

END DevLnkLoad64.
