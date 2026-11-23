MODULE CheckWinFilesLong64;

	IMPORT S := SYSTEM, Kernel, WinKernel, Files := Files64, WinFiles := WinFiles64, A := WinApi;
	VAR loc: Files.Locator; root: ARRAY 1024 OF CHAR; messages: INTEGER;

	PROCEDURE Say (IN text: ARRAY OF CHAR);
	BEGIN WinKernel.WkWrite(S.ADR(text), LEN(text$)) END Say;

	PROCEDURE Fail (code: INTEGER);
	BEGIN Say("WinFiles64 x64 FAILED"); Kernel.Quit(code) END Fail;

	PROCEDURE Viewer;
	BEGIN Fail(100) END Viewer;

	PROCEDURE NewByte (value: BYTE): Files.File;
		VAR f: Files.File;
	BEGIN
		f := Files.dir.New(loc, Files.dontAsk); IF f = NIL THEN Fail(81) END;
		f.NewWriter(NIL).WriteByte(value); RETURN f
	END NewByte;

	PROCEDURE First (IN name: Files.Name): BYTE;
		VAR f: Files.File; byte: BYTE;
	BEGIN
		f := Files.dir.Old(loc, name, Files.shared); IF f = NIL THEN Fail(82) END;
		f.NewReader(NIL).ReadByte(byte); f.Close; RETURN byte
	END First;

	PROCEDURE NoStage;
		VAR list: Files.FileInfo;
	BEGIN
		list := Files.dir.FileList(loc); IF loc.res # 0 THEN Fail(83) END;
		WHILE list # NIL DO
			IF (list.name[0] = ".") & (list.name[1] = "b") & (list.name[2] = "b") THEN Fail(84) END;
			list := list.next
		END
	END NoStage;

	PROCEDURE Cancel (title, text: ARRAY OF CHAR; flags: SET): INTEGER;
	BEGIN INC(messages); RETURN 2 END Cancel;

	PROCEDURE Confirm (title, text: ARRAY OF CHAR; flags: SET): INTEGER;
	BEGIN INC(messages); RETURN 6 END Confirm;

	PROCEDURE Abandon;
		VAR f: Files.File; w: Files.Writer;
	BEGIN
		f := Files.dir.Old(loc, "update.bin", Files.exclusive); IF f = NIL THEN Fail(85) END;
		w := f.NewWriter(NIL); w.WriteByte(7); w := NIL; f := NIL
	END Abandon;

	PROCEDURE Buffers;
		VAR f, old: Files.File; w, second: Files.Writer; r, other: Files.Reader;
			data, copy: ARRAY 4096 OF BYTE; i, j, res: INTEGER; byte: BYTE;
	BEGIN
		FOR i := 0 TO LEN(data)-1 DO data[i] := SHORT(SHORT(i MOD 127)) END;
		f := Files.dir.Temp(); w := f.NewWriter(NIL); second := f.NewWriter(NIL);
		FOR i := 0 TO 15 DO w.WriteBytes(data, 0, LEN(data)) END;
		(* More than four blocks must spill; readers and writers share cached data
			but retain independent positions when another rider evicts a buffer. *)
		r := f.NewReader(NIL); other := f.NewReader(NIL);
		FOR i := 15 TO 0 BY -1 DO
			r.SetPos(i*4096+2047); r.ReadBytes(copy, 13, 33);
			FOR j := 0 TO 32 DO IF copy[13+j] # data[2047+j] THEN Fail(130) END END;
			second.SetPos(i*4096+2048); second.WriteByte(99);
			other.SetPos(i*4096+2048); other.ReadByte(byte); IF byte # 99 THEN Fail(131) END
		END;
		f.Flush;
		FOR i := 0 TO 15 DO
			r.SetPos(i*4096); r.ReadBytes(copy, 0, LEN(copy));
			FOR j := 0 TO LEN(copy)-1 DO
				IF j = 2048 THEN IF copy[j] # 99 THEN Fail(132) END
				ELSIF copy[j] # data[j] THEN Fail(133)
				END
			END
		END;
		r.SetPos(f.Length()-1); r.ReadBytes(copy, 0, 2);
		IF ~r.eof OR (r.Pos() # f.Length()) THEN Fail(134) END; f.Close;
		(* Existing shared readers must also retain blocks never read before the
			named file is replaced, renamed or deleted. *)
		f := Files.dir.New(loc, Files.dontAsk); w := f.NewWriter(NIL);
		FOR i := 0 TO 15 DO w.WriteBytes(data, 0, LEN(data)) END;
		f.Register("readers", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(135) END;
		old := Files.dir.Old(loc, "readers.bin", Files.shared);
		f := Files.dir.Old(loc, "readers.bin", Files.shared);
		r := old.NewReader(NIL); r.ReadByte(byte);
		w := NIL; f.Close;
		f := NewByte(55); f.Register("readers", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(136) END;
		r.SetPos(15*4096+31); r.ReadByte(byte); IF byte # data[31] THEN Fail(137) END; old.Close;
		old := Files.dir.Old(loc, "readers.bin", Files.shared);
		Files.dir.Rename(loc, "readers.bin", "readers2.bin", Files.dontAsk);
		IF (loc.res # 0) OR (old.NewReader(NIL).Base() # old) THEN Fail(138) END;
		old.NewReader(NIL).ReadByte(byte); IF byte # 55 THEN Fail(139) END; old.Close;
		old := Files.dir.Old(loc, "readers2.bin", Files.shared);
		Files.dir.Delete(loc, "readers2.bin"); IF loc.res # 0 THEN Fail(140) END;
		old.NewReader(NIL).ReadByte(byte); IF byte # 55 THEN Fail(141) END; old.Close
	END Buffers;

	PROCEDURE Large;
		CONST blocks = 70 * 1024 * 1024 DIV 4096;
		VAR f: Files.File; w: Files.Writer; r: Files.Reader;
			data, copy: ARRAY 4096 OF BYTE; i, j, res: INTEGER; before: LONGINT; byte: BYTE;
	BEGIN
		FOR i := 0 TO LEN(data)-1 DO data[i] := SHORT(SHORT(i MOD 127)) END;
		before := Kernel.Allocated(); f := Files.dir.New(loc, Files.dontAsk); w := f.NewWriter(NIL);
		FOR i := 1 TO blocks DO w.WriteBytes(data, 0, LEN(data)) END;
		IF (f.Length() # blocks*4096) OR (Kernel.Allocated()-before > 800000L) THEN Fail(142) END;
		f.Register("large", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(143) END;
		f := Files.dir.Old(loc, "large.bin", Files.shared); IF f = NIL THEN Fail(144) END;
		r := f.NewReader(NIL);
		FOR i := 1 TO blocks DO
			r.ReadBytes(copy, 0, LEN(copy)); IF r.eof THEN Fail(145) END;
			FOR j := 0 TO LEN(copy)-1 DO IF copy[j] # data[j] THEN Fail(146) END END
		END;
		IF Kernel.Allocated()-before > 800000L THEN Fail(147) END;
		r.SetPos(64*1024*1024-1); r.ReadBytes(copy, 0, 3);
		IF (copy[0] # data[4095]) OR (copy[1] # data[0]) OR (copy[2] # data[1]) THEN Fail(148) END;
		r.SetPos(f.Length()); r.ReadByte(byte); IF ~r.eof THEN Fail(149) END; f.Close;
		(* Sparse fixtures check the INTEGER limit without writing two GiB. *)
		f := Files.dir.Old(loc, "oversize.bin", Files.shared);
		IF (f = NIL) OR (f.Length() # 80000000L) THEN Fail(150) END; f.Close;
		f := Files.dir.Old(loc, "max.bin", Files.exclusive);
		IF (f = NIL) OR (f.Length() # MAX(INTEGER)) THEN Fail(151) END;
		r := f.NewReader(NIL); r.SetPos(MAX(INTEGER)-1); r.ReadByte(byte);
		IF byte # 50 THEN Fail(152) END;
		w := f.NewWriter(NIL); w.SetPos(MAX(INTEGER)-1); w.WriteByte(51); f.Flush;
		r.SetPos(MAX(INTEGER)-1); r.ReadByte(byte); IF byte # 51 THEN Fail(153) END;
		r.ReadByte(byte); IF ~r.eof THEN Fail(154) END; f.Close;
		Say("WinFiles64 x64 70 MiB streaming and INTEGER boundary OK")
	END Large;

	PROCEDURE Huge;
		VAR f: Files.File; r: Files.Reader; w: Files.Writer; list: Files.FileInfo;
			data, copy: ARRAY 8 OF BYTE; i, k, year, month, day, hour, minute, second, before: INTEGER; at, size: LONGINT;
	BEGIN
		before := WinFiles.NofFiles();
		f := Files.dir.Old(loc, "huge.bin", Files.exclusive);
		IF (f = NIL) OR (f.Length() # 100002000L) THEN Fail(160) END;
		IF WinFiles.NofFiles() # before+1 THEN Fail(161) END;
		r := f.NewReader(NIL); w := f.NewWriter(NIL);
		FOR i := 0 TO 7 DO data[i] := SHORT(SHORT(i+65)) END;
		at := 7FFFFFFCL;
		FOR k := 0 TO 1 DO
			w.SetPos(at); w.WriteBytes(data, 0, LEN(data)); f.Flush;
			r.SetPos(at); r.ReadBytes(copy, 0, LEN(copy));
			FOR i := 0 TO 7 DO IF copy[i] # data[i] THEN Fail(162) END END;
			IF r.Pos() # at+8 THEN Fail(163) END;
			at := 0FFFFFFFCL
		END;
		w.SetPos(f.Length()); w.WriteBytes(data, 0, LEN(data)); f.Flush;
		size := f.Length(); IF size # 100002008L THEN Fail(164) END;
		r.SetPos(size); r.ReadByte(copy[0]); IF ~r.eof THEN Fail(165) END;
		WinFiles.GetModDate(f, year, month, day, hour, minute, second);
		IF (year < 2020) OR (month < 1) OR (month > 12) THEN Fail(166) END;
		f.Close; IF WinFiles.NofFiles() # before THEN Fail(167) END;
		list := Files.dir.FileList(loc);
		WHILE (list # NIL) & (list.name # "huge.bin") DO list := list.next END;
		IF (list = NIL) OR (list.length # size) THEN Fail(168) END;
		Say("WinFiles64 x64 LONGINT positions across 2/4 GiB OK")
	END Huge;

	PROCEDURE AbandonTemp;
		VAR f: Files.File; w: Files.Writer; data: ARRAY 2048 OF BYTE; i: INTEGER;
	BEGIN
		FOR i := 0 TO LEN(data)-1 DO data[i] := 1 END;
		f := Files.dir.Temp(); w := f.NewWriter(NIL);
		FOR i := 0 TO 5 DO w.WriteBytes(data, 0, LEN(data)) END
	END AbandonTemp;

	PROCEDURE TempCleanup;
		VAR path: ARRAY 1024 OF CHAR; n: INTEGER; list: Files.FileInfo;
	BEGIN
		AbandonTemp; Kernel.Collect; Kernel.Collect;
		n := A.GetTempPathW(LEN(path), S.ADR(path)); IF (n <= 0) OR (n >= LEN(path)) THEN Fail(155) END;
		list := Files.dir.FileList(Files.dir.This(path));
		WHILE list # NIL DO
			IF (list.name[0] = ".") & (list.name[1] = "b") & (list.name[2] = "b") THEN
				Say(list.name); Fail(156)
			END;
			list := list.next
		END
	END TempCleanup;

	PROCEDURE MaxMinus (x: LONGINT): LONGINT;
	BEGIN RETURN MAX(LONGINT)-x END MaxMinus;

	PROCEDURE MixedMinus (x: LONGINT): LONGINT;
	BEGIN RETURN 1FFFFFFFFL-x END MixedMinus;

	PROCEDURE MinusOne (x: LONGINT): LONGINT;
	BEGIN RETURN -1L-x END MinusOne;

	PROCEDURE WideAdd (x: LONGINT): LONGINT;
	BEGIN RETURN x+100000000L END WideAdd;

	PROCEDURE WideSub (x: LONGINT): LONGINT;
	BEGIN RETURN x-100000000L END WideSub;

	PROCEDURE Run;
		VAR f, old, other: Files.File; w: Files.Writer; r: Files.Reader;
			data, copy: ARRAY 4096 OF BYTE; i, res, result: INTEGER; pos: LONGINT;
			path: ARRAY 1024 OF CHAR; setting: ARRAY 256 OF CHAR; handle: S.ADDRESS;
			child: Files.Locator; byte: BYTE;
	BEGIN
		Kernel.InstallTrapViewer(Viewer);
		IF (MaxMinus(4096) # 7FFFFFFFFFFFEFFFL) OR (MixedMinus(4096) # 1FFFFEFFFL) OR (MinusOne(4096) # -4097) THEN Fail(180) END;
		IF (WideAdd(4096) # 100001000L) OR (WideSub(4096) # -0FFFFF000L) THEN Fail(181) END;
		setting := "BB_FILES_TEST_DIR"; path := "";
		result := A.GetEnvironmentVariableW(S.ADR(setting), S.ADR(path), LEN(path));
		IF (result <= 0) OR (result >= LEN(path)) THEN Fail(86) END;
		loc := Files.dir.This(path); root := loc(WinFiles.Locator).path$;
		FOR i := 0 TO LEN(data)-1 DO data[i] := SHORT(SHORT(i MOD 127)) END;
		f := Files.dir.New(loc, Files.dontAsk); IF f = NIL THEN Fail(87) END;
		w := f.NewWriter(NIL); w.WriteBytes(data, 0, LEN(data)); w.WriteBytes(data, 0, LEN(data));
		w.SetPos(23); w.WriteByte(77); pos := w.Pos();
		w := f.NewWriter(w); IF w.Pos() # pos THEN Fail(88) END;
		w := f.NewWriter(NIL); IF w.Pos() # 8192 THEN Fail(89) END; w.WriteByte(85);
		r := f.NewReader(NIL); r.SetPos(23); r.ReadByte(byte); IF byte # 77 THEN Fail(90) END;
		r := f.NewReader(r); IF r.Pos() # 24 THEN Fail(91) END;
		f.Flush; f.Register("данные", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(92) END;
		f.Close; old := Files.dir.Old(loc, "данные.bin", Files.shared);
		IF (old = NIL) OR (old.Length() # 8193) OR (old.NewWriter(NIL) # NIL) THEN Fail(93) END;
		f := NewByte(42); f.Register("данные.bin", "bin", Files.dontAsk, res);
		IF (res # 0) OR (First("данные.bin") # 42) OR (old.Length() # 8193) THEN Fail(94) END;
		r := old.NewReader(NIL); r.SetPos(23); r.ReadByte(byte); IF byte # 77 THEN Fail(95) END; old.Close;
		(* A destination locked without FILE_SHARE_DELETE must survive failed replacement. *)
		path := root + "\\locked.bin";
		handle := A.CreateFileW(path, {31}, {}, NIL, 3, {7}, 0); IF handle = -1 THEN Fail(96) END;
		f := NewByte(9); f.Register("locked.bin", "bin", Files.dontAsk, res); IF res # 6 THEN Fail(97) END;
		NoStage; result := A.CloseHandle(handle); IF First("locked.bin") # ORD("L") THEN Fail(98) END;
		f.Register("locked.bin", "bin", Files.dontAsk, res); IF (res # 0) OR (First("locked.bin") # 9) THEN Fail(99) END;
		handle := A.CreateFileW(path, {31}, {}, NIL, 3, {7}, 0); IF handle = -1 THEN Fail(101) END;
		WinFiles.MessageBox := Cancel; f := NewByte(10); f.Register("locked.bin", "bin", Files.ask, res);
		IF (res # 8) OR (messages # 1) THEN Fail(102) END;
		result := A.CloseHandle(handle); f.Close; NoStage;
		IF First("locked.bin") # 9 THEN Fail(103) END;
		WinFiles.MessageBox := NIL;
		path := root + "\\readonly.bin"; result := A.SetFileAttributesW(S.ADR(path), 1); IF result = 0 THEN Fail(104) END;
		f := NewByte(11); f.Register("readonly.bin", "bin", Files.dontAsk, res);
		IF (res # 4) OR (First("readonly.bin") # ORD("R")) THEN Fail(105) END;
		result := A.SetFileAttributesW(S.ADR(path), 80H); IF result = 0 THEN Fail(106) END;
		f.Register("readonly.bin", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(107) END;
		f := NewByte(12); f.Register("update.bin", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(108) END;
		f := Files.dir.Old(loc, "update.bin", Files.exclusive); IF f = NIL THEN Fail(109) END;
		other := Files.dir.Old(loc, "update.bin", Files.shared); IF (other # NIL) OR (loc.res # 6) THEN Fail(110) END;
		w := f.NewWriter(NIL); IF w.Pos() # 1 THEN Fail(111) END; w.WriteByte(13); f.Flush;
		(* Exclusive Windows handles keep replacement blocked until Close. *)
		other := NewByte(14); other.Register("update.bin", "bin", Files.dontAsk, res); IF res # 6 THEN Fail(112) END;
		w.SetPos(0); w.WriteByte(15); f.Close;
		IF First("update.bin") # 15 THEN Fail(113) END;
		other.Register("update.bin", "bin", Files.dontAsk, res);
		IF (res # 0) OR (First("update.bin") # 14) THEN Fail(113) END;
		Abandon; Kernel.Collect; Kernel.Collect;
		f := Files.dir.Old(loc, "update.bin", Files.shared); IF (f = NIL) OR (f.Length() # 2) THEN Fail(114) END;
		r := f.NewReader(NIL); r.SetPos(1); r.ReadByte(byte); IF byte # 7 THEN Fail(115) END; f.Close;
		child := loc.This("missing/child/"); f := Files.dir.New(child, Files.dontAsk);
		IF (f = NIL) OR (child.res # 0) THEN Fail(116) END;
		f.Register("empty", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(118) END;
		other := Files.dir.Old(child, "empty.bin", Files.shared); IF (other = NIL) OR (other.Length() # 0) THEN Fail(118) END;
		r := other.NewReader(NIL); r.ReadByte(byte); IF ~r.eof THEN Fail(118) END; other.Close;
		Files.dir.Delete(child, "empty.bin"); IF child.res # 0 THEN Fail(119) END;
		Files.dir.Delete(loc, "missing/child"); IF loc.res # 0 THEN Fail(120) END;
		Files.dir.Delete(loc, "missing"); IF loc.res # 0 THEN Fail(121) END;
		child := loc.This("forced/child"); child.res := 76;
		f := Files.dir.New(child, Files.ask); IF f = NIL THEN Fail(182) END; f.Close;
		Files.dir.Delete(loc, "forced/child"); IF loc.res # 0 THEN Fail(183) END;
		Files.dir.Delete(loc, "forced"); IF loc.res # 0 THEN Fail(184) END;
		child := loc.This("cancelled/child"); WinFiles.MessageBox := Cancel;
		f := Files.dir.New(child, Files.ask);
		IF (f # NIL) OR (child.res # 8) THEN Fail(185) END;
		WinFiles.MessageBox := Confirm; f := Files.dir.New(child, Files.ask);
		IF (f = NIL) OR (child.res # 0) THEN Fail(186) END; f.Close;
		WinFiles.MessageBox := NIL;
		Files.dir.Delete(loc, "cancelled/child"); IF loc.res # 0 THEN Fail(187) END;
		Files.dir.Delete(loc, "cancelled"); IF loc.res # 0 THEN Fail(188) END;
		f := NewByte(16); f.Register("bad<name", "bin", Files.dontAsk, res); IF res # 1 THEN Fail(122) END;
		f.Register("rename", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(123) END;
		Files.dir.Rename(loc, "rename.bin", "renamed.bin", Files.dontAsk); IF loc.res # 0 THEN Fail(124) END;
		IF ~Files.dir.SameFile(loc, "RENAMED.bin", loc.This("."), "renamed.bin") THEN Fail(125) END;
		Files.dir.Delete(loc, "renamed.bin"); IF loc.res # 0 THEN Fail(126) END;
		f := Files.dir.New(loc, Files.dontAsk); w := f.NewWriter(NIL);
		FOR i := 0 TO 299 DO w.WriteBytes(data, 0, LEN(data)) END;
		f.Register("persist", "bin", Files.dontAsk, res); IF res # 0 THEN Fail(127) END;
		Buffers; Large; Huge; TempCleanup; NoStage; Say("WinFiles64 x64 disk write OK"); Kernel.Quit(0)
	END Run;
BEGIN Kernel.Start(Run)
END CheckWinFilesLong64.
