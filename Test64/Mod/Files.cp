MODULE Test64Files;
(** Временные файлы LinFiles: имена не должны запираться занятым блоком,
	а файлы процесса не должны оставаться в каталоге временных файлов
	после закрытия.

	Проверка 1 (красная на старом коде): занять все 100000 имён
	odc00000..odc99999, которые перебирал прежний
	LinFiles.CreateFile (не более 200 попыток от Libc.clock()). Старый
	код не создаёт временный файл; новый строит имя из времени,
	счётчика и девяти цифр, поэтому создаёт.

	Проверка 2: после N созданий/закрытий в каталоге нет файлов
	процесса (имён ровно из 12 символов: "odc" + 9 цифр).

	Тест помечен slow: создаёт и удаляет 100000 пустых файлов в
	каталоге временных файлов стенда (/tmp). Если процесс упадёт
	посреди теста, tools/env.sh (clean_host_temp) уберёт имена
	odc[0-9]{5} при следующем запуске. *)

	IMPORT S := SYSTEM, Kernel, Files, LinLibc, UnitAssert;

	CONST
		digits5 = 5; digits9 = 9;

	VAR
		dirPath: ARRAY 32 OF SHORTCHAR;	(* каталог временных файлов *)
		name: ARRAY 64 OF SHORTCHAR;	(* <dirPath>/odcNNNNNNNNN *)

	PROCEDURE InitDir;
		VAR i: INTEGER; s: ARRAY 32 OF CHAR;
	BEGIN
		s := LinLibc.P_tmpdir;
		i := 0;
		WHILE s[i] # 0X DO dirPath[i] := SHORT(s[i]); INC(i) END;
		dirPath[i] := 0X
	END InitDir;

	PROCEDURE MakeName (num, count: INTEGER);
	(* полное имя: dirPath + "/odc" + ровно count десятичных цифр *)
		VAR k, j: INTEGER; dig: ARRAY 16 OF SHORTCHAR;
	BEGIN
		k := 0;
		j := 0;
		WHILE dirPath[j] # 0X DO name[k] := dirPath[j]; INC(k); INC(j) END;
		name[k] := SHORT(CHR(02FH)); INC(k);	(* "/" *)
		name[k] := SHORT(CHR(06FH)); name[k+1] := SHORT(CHR(064H)); name[k+2] := SHORT(CHR(063H)); INC(k, 3);	(* "odc" *)
		j := count;
		WHILE j > 0 DO
			dig[j-1] := SHORT(CHR(num MOD 10 + 48));
			DEC(j); num := num DIV 10
		END;
		j := 0;
		WHILE j < count DO name[k] := dig[j]; INC(k); INC(j) END;
		name[k] := 0X
	END MakeName;

	PROCEDURE NamePtr (): LinLibc.PtrSTR;
	BEGIN
		RETURN S.VAL(LinLibc.PtrSTR, S.ADR(name))
	END NamePtr;

	PROCEDURE DirPtr (): LinLibc.PtrSTR;
	BEGIN
		RETURN S.VAL(LinLibc.PtrSTR, S.ADR(dirPath))
	END DirPtr;

	PROCEDURE OccupyAll (): BOOLEAN;
		VAR f: LinLibc.PtrFILE; i: INTEGER;
	BEGIN
		i := 0;
		WHILE i < 100000 DO
			MakeName(i, digits5);
			f := LinLibc.fopen(NamePtr(), "w");
			IF f = LinLibc.NULL THEN RETURN FALSE END;
			IF LinLibc.fclose(f) # 0 THEN RETURN FALSE END;
			INC(i)
		END;
		RETURN TRUE
	END OccupyAll;

	PROCEDURE RemoveAll5;
		VAR i, dummy: INTEGER;
	BEGIN
		i := 0;
		WHILE i < 100000 DO
			MakeName(i, digits5); dummy := LinLibc.remove(NamePtr());
			INC(i)
		END
	END RemoveAll5;

	PROCEDURE IsDigit (ch: SHORTCHAR): BOOLEAN;
	BEGIN
		RETURN (ORD(ch) >= 48) & (ORD(ch) <= 57)
	END IsDigit;

	PROCEDURE CountLen (dlen: INTEGER): INTEGER;
	(* число имён ровно "odc" + dlen цифр в каталоге временных файлов *)
		VAR d: LinLibc.PtrDirent; dir: LinLibc.PtrDIR; n, i: INTEGER; ok: BOOLEAN;
	BEGIN
		n := 0;
		dir := LinLibc.opendir(DirPtr());
		IF dir = LinLibc.NULL THEN RETURN -1 END;
		d := LinLibc.readdir(dir);
		WHILE d # NIL DO
			ok := (ORD(d.d_name[0]) = 111) & (ORD(d.d_name[1]) = 100) & (ORD(d.d_name[2]) = 99);
			i := 3;
			WHILE ok & IsDigit(d.d_name[i]) DO INC(i) END;
			IF ok & (d.d_name[i] = 0X) & (i - 3 = dlen) THEN INC(n) END;
			d := LinLibc.readdir(dir)
		END;
		IF LinLibc.closedir(dir) # 0 THEN n := -1 END;
		RETURN n
	END CountLen;

	PROCEDURE CountAll (): INTEGER;
	BEGIN
		RETURN CountLen(digits5) + CountLen(digits9)
	END CountAll;

	PROCEDURE TestProcTempNames*;
		VAR f: Files.File; w: Files.Writer; before, mid, after, base: INTEGER; ok: BOOLEAN;
	BEGIN
		InitDir;
		RemoveAll5;	(* страховка: остатки прежнего хоста *)
		base := CountLen(digits9);
		ok := OccupyAll();
		IF ~ok THEN RemoveAll5; UnitAssert.Fail("cannot occupy odc00000..odc99999"); RETURN END;
		before := CountAll();
		f := Files.dir.Temp();
		IF f = NIL THEN RemoveAll5; UnitAssert.Fail("Files.dir.Temp returned NIL"); RETURN END;
		w := f.NewWriter(NIL); w.WriteByte(65); f.Flush();
		mid := CountAll();
		UnitAssert.EqualInt(before + 1, mid, "temporary file created with occupied old names");
		f.Close;
		after := CountAll();
		UnitAssert.EqualInt(before, after, "temporary file removed on close (occupied names)");
		RemoveAll5;
		UnitAssert.EqualInt(base, CountLen(digits9), "no odc+9-digit files left after close")
	END TestProcTempNames;

	PROCEDURE MakeUnclosedTemp;
	(* Временный файл без Close: удалить его должен FINALIZE. *)
		VAR f: Files.File; w: Files.Writer;
	BEGIN
		f := Files.dir.Temp();
		IF f # NIL THEN
			w := f.NewWriter(NIL); w.WriteByte(65); f.Flush()
		END
	END MakeUnclosedTemp;

	PROCEDURE TestProcTempFinalize*;
	(* Освобождение без Close: файл удаляется финализатором. *)
		VAR base: INTEGER;
	BEGIN
		InitDir;
		base := CountLen(digits9);
		MakeUnclosedTemp;
		UnitAssert.EqualInt(base + 1, CountLen(digits9), "temporary file exists before finalize");
		Kernel.Collect; Kernel.Collect;
		UnitAssert.EqualInt(base, CountLen(digits9), "temporary file removed by finalizer")
	END TestProcTempFinalize;

	PROCEDURE TestProcTempCleanup*;
	(* N созданий/закрытий: после каждого закрытия файлов процесса нет. *)
		VAR i, base, now: INTEGER; f: Files.File; w: Files.Writer;
	BEGIN
		InitDir;
		base := CountLen(digits9);
		i := 0;
		WHILE i < 50 DO
			f := Files.dir.Temp();
			IF f = NIL THEN UnitAssert.Fail("Files.dir.Temp returned NIL"); RETURN END;
			w := f.NewWriter(NIL); w.WriteByte(SHORT(SHORT(i MOD 256))); f.Flush();
			now := CountLen(digits9);
			UnitAssert.EqualInt(base + 1, now, "temporary file exists while open");
			f.Close;
			now := CountLen(digits9);
			UnitAssert.EqualInt(base, now, "temporary file removed after close");
			INC(i)
		END
	END TestProcTempCleanup;

END Test64Files.
