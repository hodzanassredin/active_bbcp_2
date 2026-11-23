MODULE DevLinker1;
(**	project	= "BlackBox 2.0"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Igor Dehtyarenko, Alexander Shiryaev"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	IMPORT
		LB := DevLnkBase, Load := DevLnkLoad, Load64 := DevLnkLoad64,
		Res := DevLnkResources, WrPe := DevLnkWritePe, WrElf64 := DevLnkWriteElf64,
		WrElf := DevLnkWriteElf, WrElfS := DevLnkWriteElfStatic,
		Kernel, Files, Log := StdLog, Dialog, DevCommanders, TextMappers;

	CONST
		tgtElfStatic = 0; tgtElfExe = 1; tgtElfDll = 2; tgtPeExe = 3;  tgtPeDll = 4; 

	VAR
		W: TextMappers.Formatter;

	PROCEDURE LinkIt0;
		VAR codeBase, dataBase, varsBase : INTEGER;
	BEGIN
		LB.BeginLinking;
		IF LB.error THEN RETURN END;
		IF LB.outPe THEN	
			WrPe.Init;
			IF LB.error THEN RETURN END;
			WrPe.GetBases(codeBase, dataBase, varsBase);
			LB.SetAddr(codeBase, dataBase, varsBase);
			LB.DoFixups;
			WrPe.WriteOut;
		ELSIF LB.outElf & LB.amd64 THEN
			WrElf64.Init;
			IF LB.error THEN RETURN END;
			WrElf64.GetBases(codeBase, dataBase, varsBase);
			LB.SetAddr(codeBase, dataBase, varsBase); LB.DoFixups;
			IF ~LB.error THEN WrElf64.WriteOut END;
		ELSIF LB.outElf & LB.opt.elfStatic THEN
			WrElfS.Init;
			WrElfS.GetBases(codeBase, dataBase, varsBase);
			LB.SetAddr(codeBase, dataBase, varsBase);
			LB.DoFixups;
			WrElfS.WriteOut;
		ELSIF LB.outElf THEN
			WrElf.Init;
			WrElf.GetBases(codeBase, dataBase, varsBase);
			LB.SetAddr(codeBase, dataBase, varsBase);
			LB.DoFixups;
			WrElf.WriteOut;
		END;
		IF ~LB.error THEN	
			W.WriteString(LB.outputName$); W.WriteString(" written")
		END
	END LinkIt0;

	PROCEDURE LinkIt;
		VAR S: TextMappers.Scanner; name: Files.Name; end: INTEGER;
			modName: TextMappers.String;
			error, flag0: BOOLEAN;
	BEGIN
		Res.Clear; Dialog.ShowStatus("linking");
		error := FALSE;
		IF DevCommanders.par = NIL THEN RETURN END;
		S.ConnectTo(DevCommanders.par.text);
		S.SetPos(DevCommanders.par.beg);
		end := DevCommanders.par.end;
		DevCommanders.par := NIL;
		LB.opt.OS := LB.osUnspecified;
		W.ConnectTo(Log.buf); S.Scan;
		IF S.type = TextMappers.string THEN
			IF LB.target = tgtElfExe THEN
				IF S.string = "Linux" THEN
					LB.opt.OS := LB.osLinux;
					LB.opt.OSABI := WrElf.ELFOSABI_NONE;
					LB.opt.elfInterpreter := WrElf.linuxInterpreter;
				ELSIF S.string = "FreeBSD" THEN
					LB.opt.OS := LB.osFreeBSD;
					LB.opt.OSABI := WrElf.ELFOSABI_FREEBSD;
					LB.opt.elfInterpreter := WrElf.freeBSDInterpreter
				ELSIF S.string = "OpenBSD" THEN
					LB.opt.OS := LB.osOpenBSD;
					LB.opt.OSABI := WrElf.ELFOSABI_OPENBSD;
					LB.opt.elfInterpreter := WrElf.openBSDInterpreter
				ELSIF S.string = "NetBSD" THEN
					LB.opt.OS := LB.osNetBSD;
					LB.opt.OSABI := WrElf.ELFOSABI_NONE;
					LB.opt.elfInterpreter := WrElf.netBSDInterpreter
				ELSE
					W.WriteString("unknown OS: "); W.WriteString(S.string); W.WriteLn;
					error := TRUE
				END;
				S.Scan
			END;
			name := S.string$; S.Scan;
			IF (S.type = TextMappers.char) & (S.char = ".") THEN S.Scan;
				IF S.type = TextMappers.string THEN
					Files.dir.GetFileName(name, S.string$, name); S.Scan
				END
			END;
			IF (S.type = TextMappers.char) & (S.char = ":") THEN S.Scan;
				IF (S.type = TextMappers.char) & (S.char = "=") THEN S.Scan;
					LB.outputName := name$;
					WHILE (S.start < end) & (S.type = TextMappers.string) DO
						modName := S.string$; S.Scan;
						flag0 := FALSE;
						WHILE (S.start < end) & (S.type = TextMappers.char) &
							((S.char = "+") OR (S.char = "$")) DO
							IF S.char = "+" THEN LB.KernelName := SHORT(modName$);
								flag0 := TRUE
							ELSIF S.char = "$" THEN LB.mainName := SHORT(modName$);
								flag0 := TRUE
							ELSE
							END;
							S.Scan
						END;
						IF ~flag0 THEN
							IF LB.target = tgtElfStatic THEN
								Load.AddModule(modName$)
							ELSIF LB.target = tgtElfExe THEN
								Load.AddModule(modName$)
							ELSIF LB.target = tgtElfDll THEN
								Load.ExportModule(modName$)
							ELSIF LB.target = tgtPeExe THEN
								Load.AddModule(modName$)
							ELSIF LB.target = tgtPeDll THEN
								Load.ExportModule(modName$)
							ELSE HALT(102)
							END
						END
					END;
					Res.Scan(S, end);
					IF LB.target = tgtElfStatic THEN
					ELSIF LB.target = tgtElfExe THEN
						IF LB.opt.OS = LB.osFreeBSD THEN
							Load.ExportVariable(LB.KernelName$, "__progname");
							Load.ExportVariable(LB.KernelName$, "environ")
						END;
						Load.ExportAll
					ELSIF LB.target = tgtElfDll THEN
					ELSIF LB.target = tgtPeExe THEN
						Load.ExportAll
					ELSIF LB.target = tgtPeDll THEN
					ELSE HALT(101)
					END;
					IF ~error THEN LinkIt0 END
				ELSE W.WriteString(" := missing")
				END
			ELSE W.WriteString(" := missing")
			END;
			W.WriteLn; Log.text.Append(Log.buf)
		END;
		IF error OR LB.error THEN Dialog.ShowStatus("failed") ELSE Dialog.ShowStatus("ok") END;
		W.ConnectTo(NIL); S.ConnectTo(NIL)
	END LinkIt;

	PROCEDURE LinkExe64 (elf: BOOLEAN);
		VAR s: TextMappers.Scanner; names: ARRAY 128 OF LB.Name;
			n, i, end: INTEGER; kernel, main: BOOLEAN;
		PROCEDURE Fail (IN msg: ARRAY OF CHAR);
		BEGIN LB.Error(msg); Dialog.ShowStatus("failed") END Fail;
	BEGIN
		IF elf THEN LB.Init(tgtElfExe) ELSE LB.Init(tgtPeExe) END; LB.amd64 := TRUE;
		Res.Clear;
		IF DevCommanders.par = NIL THEN Fail("expected output := modules ~"); RETURN END;
		s.ConnectTo(DevCommanders.par.text); s.SetPos(DevCommanders.par.beg);
		end := DevCommanders.par.end; DevCommanders.par := NIL; s.Scan;
		IF s.type # TextMappers.string THEN Fail("output name expected"); RETURN END;
		LB.outputName := s.string$; s.Scan;
		IF (s.type = TextMappers.char) & (s.char = ".") THEN
			s.Scan; IF s.type # TextMappers.string THEN Fail("file extension expected"); RETURN END;
			LB.outputName := LB.outputName$ + "." + s.string$; s.Scan
		END;
		IF (s.type # TextMappers.char) OR (s.char # ":") THEN Fail(":= missing"); RETURN END;
		s.Scan; IF (s.type # TextMappers.char) OR (s.char # "=") THEN Fail(":= missing"); RETURN END;
		s.Scan; n := 0; kernel := FALSE; main := FALSE;
		WHILE (s.start < end) & (s.type = TextMappers.string) DO
			IF (n = LEN(names)) OR (LEN(s.string$) >= LEN(names[0])) THEN Fail("too many modules or module name too long"); RETURN END;
			names[n] := SHORT(s.string$); s.Scan;
			WHILE (s.type = TextMappers.char) & ((s.char = "+") OR (s.char = "$")) DO
				IF s.char = "+" THEN
					IF kernel THEN Fail("duplicate kernel marker"); RETURN END;
					kernel := TRUE; LB.KernelName := names[n]
				ELSE
					IF main THEN Fail("duplicate main marker"); RETURN END;
					main := TRUE; LB.mainName := names[n]
				END;
				s.Scan
			END;
			INC(n)
		END;
		Res.Scan(s, end);
		IF LB.error THEN Dialog.ShowStatus("failed"); s.ConnectTo(NIL); RETURN END;
		IF n = 0 THEN Fail("no modules specified"); RETURN END;
		IF (s.start < end) & (s.type # TextMappers.eot) &
			~((s.type = TextMappers.char) & (s.char = "~")) THEN Fail("unexpected linker parameter"); RETURN END;
		IF ~main THEN LB.mainName := names[n-1] END;
		IF kernel THEN Load64.AddModule(LB.KernelName$) END;
		i := 0;
		WHILE (i < n) & ~LB.error DO
			IF names[i] # LB.mainName THEN Load64.AddModule(names[i]$) END; INC(i)
		END;
		IF ~LB.error THEN Load64.MainModule(LB.mainName$) END;
		IF ~LB.error THEN
			IF elf THEN
				IF LB.kernelMod = NIL THEN Fail("ELF64 requires Kernel+ marker"); RETURN END;
				LB.bootInfoOffset := Load64.VariableAddress(LB.kernelMod, "bootInfo");
				IF LB.error THEN RETURN END
			END;
			Load64.ExportAll;
			W.ConnectTo(Log.buf); LinkIt0; W.WriteLn; Log.text.Append(Log.buf); W.ConnectTo(NIL)
		END;
		IF LB.error THEN Dialog.ShowStatus("failed") ELSE Dialog.ShowStatus("ok") END;
		s.ConnectTo(NIL)
	END LinkExe64;

	PROCEDURE LinkPeExe64*;
	BEGIN
		LinkExe64(FALSE)
	END LinkPeExe64;

	PROCEDURE LinkElfExe64*;
	BEGIN
		LinkExe64(TRUE)
	END LinkElfExe64;

	PROCEDURE LinkPeExe*;
	BEGIN
		LB.Init(tgtPeExe);
		LB.dynaInit := FALSE;
		LinkIt
	END LinkPeExe;

	PROCEDURE LinkPe*;
	BEGIN
		LB.Init(tgtPeExe);
		LB.dynaInit := TRUE;
		LinkIt
	END LinkPe;

	PROCEDURE LinkPeDll*;
	BEGIN
		LB.Init(tgtPeDll);
		LinkIt
	END LinkPeDll;

	PROCEDURE LinkElfStatic*;
	BEGIN
		LB.Init(tgtElfStatic);
		LinkIt
	END LinkElfStatic;

	PROCEDURE LinkElfDll*;
	BEGIN
		LB.Init(tgtElfDll);
		LinkIt
	END LinkElfDll;

	PROCEDURE LinkElfExe*;
	BEGIN
		LB.Init(tgtElfExe);
		LB.dynaInit := FALSE;
		LinkIt
	END LinkElfExe;

	PROCEDURE LinkElf*;
	BEGIN
		LB.Init(tgtElfExe);
		LB.dynaInit := TRUE;
		LinkIt
	END LinkElf;

END DevLinker1.

DevCompiler.CompileThis Dev2LnkBase Dev2LnkLoad Dev2LnkWritePe Dev2LnkWriteElf Dev2LnkWriteElfStatic Dev2LnkChmod DevLinker1

DevDebug.UnloadThis DevLinker1 Dev2LnkLoad Dev2LnkWritePe Dev2LnkWriteElf Dev2LnkWriteElfStatic Dev2LnkChmod Dev2LnkBase


DevLinker1.LinkPeExe BlackBox1 := Kernel$+ HostFiles StdLoader ~

DevLinker1.LinkElfExe Linux BlackBox1 := Kernel$+ HostFiles StdLoader ~

DevLinker1.LinkElfExe FreeBSD BlackBox1 := Kernel$+ HostFiles StdLoader ~

DevLinker1.LinkElfExe OpenBSD BlackBox1 := Kernel$+ HostFiles StdLoader ~


DevLinker1.LinkPe BlackBox1 := Kernel$+ HostFiles StdLoader ~

DevLinker1.LinkElf Linux BlackBox1 := Kernel$+ HostFiles StdLoader ~

DevLinker1.LinkElf FreeBSD BlackBox1 := Kernel$+ HostFiles StdLoader ~

DevLinker1.LinkElf OpenBSD BlackBox1 := Kernel$+ HostFiles StdLoader ~
