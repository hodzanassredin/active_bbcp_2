MODULE UnitRunner;
(** Исполнитель модульных тестов BlackBox.

	Находит в модуле экспортированные процедуры без параметров с именем
	Test… (через Meta.Scanner), вызывает необязательные фикстуры
	Setup*/Teardown* и SetupModule*/TeardownModule*, каждый тест (и
	фикстуры) исполняет под Kernel.Try. Провал — любая неудачная
	проверка UnitAssert или трап; тест без проверок помечается EMPTY и
	считается провалом.

	Пометки в имени: TestSlow… (только с marks=slow), TestGui… (gui),
	TestProc… (proc: потенциально опасен, внешний запускатель гонит его
	отдельным процессом).

	Команда:
		UnitRunner.Run [модуль|подсистема|all] [slow] [gui] [proc]
			[junit=ФАЙЛ] [tap] [noquit]
	По умолчанию TestSlow/TestGui/TestProc пропускаются. В консоли
	(viewer трапов = NIL) в конце зовётся Kernel.Quit(0|1). *)

	IMPORT SYSTEM, Kernel, Strings, Console, Meta, Files, DevCommanders,
		TextModels, UnitAssert;

	CONST
		maxResults = 1024; maxName = 96; maxText = 1600;
		normalMark = 0; slowMark = 1; guiMark = 2; procMark = 3;
		stPass = 0; stFail = 1; stEmpty = 2; stSkip = 3;

	TYPE
		Result = RECORD
			module: ARRAY 64 OF CHAR;
			name: ARRAY maxName OF CHAR;
			status: INTEGER;
			ms: LONGINT;
			msg: UnitAssert.Message
		END;
		Counts* = RECORD passed*, failed*, empty*, skipped*: INTEGER END;

	VAR
		results: ARRAY maxResults OF Result;
		nofResults: INTEGER;
		quiet: BOOLEAN;
		current: Meta.Item;
		currentOk, currentDone: BOOLEAN;
		ju: Files.Writer;	(* вывод JUnit, если открыт *)

	(* --- вывод ----------------------------------------------------------- *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN Console.WriteStr(s) END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Ln;
	BEGIN Console.WriteLn END Ln;

	PROCEDURE NoopViewer;
	BEGIN
	END NoopViewer;

	PROCEDURE StartsWith (IN s, p: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (p[i] # 0X) & (s[i] = p[i]) DO INC(i) END;
		RETURN p[i] = 0X
	END StartsWith;

	PROCEDURE StrLen (IN s: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE s[i] # 0X DO INC(i) END;
		RETURN i
	END StrLen;

	PROCEDURE StrCopy (IN src: ARRAY OF CHAR; from, len: INTEGER; OUT dst: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (src[from + i] # 0X) & (i < LEN(dst) - 1) DO
			dst[i] := src[from + i]; INC(i)
		END;
		dst[i] := 0X
	END StrCopy;

	PROCEDURE Contains (IN s, sub: ARRAY OF CHAR): BOOLEAN;
		VAR i, j: INTEGER; found: BOOLEAN;
	BEGIN
		found := FALSE; i := 0;
		WHILE ~found & (s[i] # 0X) DO
			j := 0;
			WHILE (sub[j] # 0X) & (s[i + j] = sub[j]) DO INC(j) END;
			IF sub[j] = 0X THEN found := TRUE ELSE INC(i) END
		END;
		RETURN found
	END Contains;


	(* --- исполнение под Kernel.Try --------------------------------------- *)

	PROCEDURE Body (a, b, c: SYSTEM.ADDRESS);
	BEGIN
		currentDone := FALSE;
		current.Call(currentOk);
		currentDone := TRUE
	END Body;

	PROCEDURE Exec (IN it: Meta.Item; OUT ok, trapped: BOOLEAN);
	BEGIN
		current := it; currentOk := FALSE; currentDone := FALSE;
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(Body)), 0, 0, 0);
		ok := currentOk; trapped := ~currentDone
	END Exec;

	PROCEDURE LookupProc (IN mi: Meta.Item; IN nm: ARRAY OF CHAR; OUT it: Meta.Item): BOOLEAN;
	BEGIN
		mi.Lookup(nm, it);
		RETURN it.Valid() & (it.obj = Meta.procObj) & (it.NumParam() = 0)
	END LookupProc;

	(* --- результаты ------------------------------------------------------ *)

	PROCEDURE Add (IN module, name: ARRAY OF CHAR; status: INTEGER; ms: LONGINT; IN msg: ARRAY OF CHAR);
	BEGIN
		IF ~quiet & (nofResults < maxResults) THEN
			results[nofResults].module := module$;
			results[nofResults].name := name$;
			results[nofResults].status := status;
			results[nofResults].ms := ms;
			results[nofResults].msg := msg$;
			INC(nofResults)
		END
	END Add;

	PROCEDURE PrintResult (IN module, name: ARRAY OF CHAR; status: INTEGER; ms: LONGINT; IN msg: ARRAY OF CHAR);
	BEGIN
		IF ~quiet THEN
			CASE status OF
				stPass: W("PASS ")
			| stSkip: W("SKIP ")
			| stEmpty: W("EMPTY ")
			ELSE W("FAIL ")
			END;
			W(module); W("."); W(name); W(" "); N(ms); W(" ms");
			IF status = stSkip THEN W(" ("); W(msg); W(")")
			ELSIF status # stPass THEN W(": "); W(msg)
			END;
			Ln
		END
	END PrintResult;

	(* --- один тест ------------------------------------------------------- *)

	PROCEDURE RunOne (IN mi: Meta.Item; IN module, name: ARRAY OF CHAR;
		setupOk, teardownOk: BOOLEAN; VAR c: Counts);
		VAR it, fx: Meta.Item; ok, ok2, trapped, trap2, setupBad, teardownBad: BOOLEAN;
			chk0, chk1: LONGINT; f0, f1: INTEGER;
			t0, t1: LONGINT; st: INTEGER; msg: UnitAssert.Message;
	BEGIN
		msg := "";
		IF ~LookupProc(mi, name, it) THEN
			st := stFail; msg := "test procedure not found or has parameters"; t0 := Kernel.Time(); t1 := t0
		ELSE
			chk0 := UnitAssert.Checks(); f0 := UnitAssert.Failures();
			t0 := Kernel.Time();
			setupBad := FALSE;
			IF setupOk THEN
				IF LookupProc(mi, "Setup", fx) THEN
					Exec(fx, ok, trapped);
					IF trapped OR ~ok THEN setupBad := TRUE; msg := "Setup trapped" END
				END
			END;
			trapped := FALSE; ok := TRUE;
			IF ~setupBad THEN
				Exec(it, ok, trapped);
				IF trapped THEN msg := "test trapped"
				ELSIF ~ok THEN msg := "call failed (signature mismatch)"
				END
			END;
			t1 := Kernel.Time();
			teardownBad := FALSE;
			IF teardownOk THEN
				IF LookupProc(mi, "Teardown", fx) THEN
					Exec(fx, ok2, trap2);
					IF trap2 OR ~ok2 THEN teardownBad := TRUE; msg := "Teardown trapped" END
				END
			END;
			chk1 := UnitAssert.Checks(); f1 := UnitAssert.Failures();
			IF setupBad OR teardownBad OR trapped OR ~ok OR (f1 > f0) THEN
				st := stFail;
				IF msg = "" THEN UnitAssert.LastMessage(msg) END;
				IF msg = "" THEN msg := "assertion failed" END;
				IF trapped THEN Kernel.Collect END
			ELSIF chk1 = chk0 THEN
				st := stEmpty; msg := "no checks"
			ELSE
				st := stPass
			END
		END;
		CASE st OF
			stPass: INC(c.passed)
		| stEmpty: INC(c.empty)
		ELSE INC(c.failed)
		END;
		PrintResult(module, name, st, t1 - t0, msg);
		Add(module, name, st, t1 - t0, msg)
	END RunOne;

	(* --- модуль ---------------------------------------------------------- *)

	PROCEDURE RunModuleCounts* (IN module: ARRAY OF CHAR; IN marks: ARRAY OF CHAR;
		quietRun: BOOLEAN; OUT c: Counts);
		VAR mi, setupMod, teardownMod: Meta.Item; s: Meta.Scanner; nm: Meta.Name;
			setupOk, teardownOk, ok, trapped: BOOLEAN; mark: INTEGER;
			oldViewer: Kernel.Handler; saved: UnitAssert.State; savedQuiet: BOOLEAN;
			msg: UnitAssert.Message;
	BEGIN
		c.passed := 0; c.failed := 0; c.empty := 0; c.skipped := 0;
		UnitAssert.Save(saved); savedQuiet := quiet; quiet := quietRun OR savedQuiet;
		oldViewer := Kernel.GetTrapViewer(); Kernel.InstallTrapViewer(NoopViewer);

		Meta.Lookup(module, mi);
		IF ~mi.Valid() THEN
			msg := "module not found";
			PrintResult(module, "module", stFail, 0, msg);
			Add(module, "module", stFail, 0, msg); INC(c.failed)
		ELSE
			setupOk := LookupProc(mi, "Setup", setupMod);
			teardownOk := LookupProc(mi, "Teardown", teardownMod);
			IF LookupProc(mi, "SetupModule", setupMod) THEN
				Exec(setupMod, ok, trapped);
				IF trapped OR ~ok THEN
					msg := "SetupModule trapped";
					PrintResult(module, "SetupModule", stFail, 0, msg);
					Add(module, "SetupModule", stFail, 0, msg); INC(c.failed)
				END
			END;
			s.ConnectTo(mi); s.Scan;
			WHILE ~s.eos DO
				IF (s.this.obj = Meta.procObj) & (s.this.NumParam() = 0) THEN
					s.GetObjName(nm);
					IF StartsWith(nm, "Test") THEN
						IF StartsWith(nm, "TestSlow") THEN mark := slowMark
						ELSIF StartsWith(nm, "TestGui") THEN mark := guiMark
						ELSIF StartsWith(nm, "TestProc") THEN mark := procMark
						ELSE mark := normalMark
						END;
						IF ((mark = slowMark) & ~Contains(marks, "slow"))
								OR ((mark = guiMark) & ~Contains(marks, "gui"))
								OR ((mark = procMark) & ~Contains(marks, "proc")) THEN
							msg := "mark";
							CASE mark OF
								slowMark: msg := "slow"
							| guiMark: msg := "gui"
							| procMark: msg := "proc"
							END;
							PrintResult(module, nm, stSkip, 0, msg);
							Add(module, nm, stSkip, 0, msg); INC(c.skipped)
						ELSE
							RunOne(mi, module, nm, setupOk, teardownOk, c)
						END
					END
				END;
				s.Scan
			END;
			IF LookupProc(mi, "TeardownModule", teardownMod) THEN
				Exec(teardownMod, ok, trapped);
				IF trapped OR ~ok THEN
					msg := "TeardownModule trapped";
					PrintResult(module, "TeardownModule", stFail, 0, msg);
					Add(module, "TeardownModule", stFail, 0, msg); INC(c.failed)
				END
			END
		END;

		Kernel.InstallTrapViewer(oldViewer);
		quiet := savedQuiet; UnitAssert.Restore(saved)
	END RunModuleCounts;

	(* --- выбор цели ------------------------------------------------------ *)

	PROCEDURE CountsAdd (VAR a: Counts; IN b: Counts);
	BEGIN
		INC(a.passed, b.passed); INC(a.failed, b.failed);
		INC(a.empty, b.empty); INC(a.skipped, b.skipped)
	END CountsAdd;

	PROCEDURE RunModule (IN module, marks: ARRAY OF CHAR; VAR total: Counts);
		VAR c: Counts;
	BEGIN
		RunModuleCounts(module, marks, FALSE, c); CountsAdd(total, c)
	END RunModule;

	PROCEDURE IsTestModule (IN nm, prefix: ARRAY OF CHAR): BOOLEAN;
	(* Модуль тестов подсистемы <prefix>_tests: <prefix>_tests<Тема>.
	   Для prefix = "" (all) — любой модуль, содержащий "_tests". *)
		VAR i: INTEGER;
	BEGIN
		IF prefix = "" THEN RETURN Contains(nm, "_tests") END;
		IF ~StartsWith(nm, prefix) THEN RETURN FALSE END;
		i := StrLen(prefix);
		RETURN (nm[i] = "_") & (nm[i + 1] = "t") & (nm[i + 2] = "e")
			& (nm[i + 3] = "s") & (nm[i + 4] = "t") & (nm[i + 5] = "s")
	END IsTestModule;

	PROCEDURE RunMatching (IN prefix, marks: ARRAY OF CHAR; VAR total: Counts);
	(* Модули уже загруженного мира: Run('all') — все *_tests*;
	   Run('System') — System_tests*; prefix = "" — все *_tests*. *)
		VAR s: Meta.Scanner; nm: Meta.Name; c: Counts; any: BOOLEAN;
	BEGIN
		any := FALSE;
		s.ConnectToMods; s.Scan;
		WHILE ~s.eos DO
			IF s.this.obj = Meta.modObj THEN
				s.GetObjName(nm);
				IF IsTestModule(nm, prefix) THEN
					any := TRUE; RunModule(nm, marks, total)
				END
			END;
			s.Scan
		END;
		IF ~any THEN
			W("UnitRunner: no *_tests module for '"); W(prefix); W("'"); Ln
		END
	END RunMatching;

	(* --- параметры команды ----------------------------------------------- *)

	PROCEDURE GetPar (OUT s: ARRAY OF CHAR);
		VAR p: DevCommanders.Par; rd: TextModels.Reader; n: INTEGER;
	BEGIN
		s := ""; p := DevCommanders.par;
		IF p # NIL THEN
			DevCommanders.par := NIL;
			rd := p.text.NewReader(NIL); rd.SetPos(p.beg); n := 0; rd.Read;
			WHILE (~rd.eot) & (rd.Pos() <= p.end) & (n < LEN(s) - 1) DO
				s[n] := rd.char; INC(n); rd.Read
			END;
			s[n] := 0X
		END
	END GetPar;

	PROCEDURE Token (IN s: ARRAY OF CHAR; n: INTEGER; OUT t: ARRAY OF CHAR);
	(* n-й токен, разделители — пробелы и табы *)
		VAR i, k, len: INTEGER;
	BEGIN
		t := ""; i := 0; k := 0;
		LOOP
			WHILE (s[i] = " ") OR (s[i] = 0DX) OR (s[i] = 09X) DO INC(i) END;
			IF s[i] = 0X THEN RETURN END;
			IF k = n THEN EXIT END;
			WHILE (s[i] # 0X) & (s[i] # " ") & (s[i] # 0DX) & (s[i] # 09X) DO INC(i) END;
			INC(k)
		END;
		len := 0;
		WHILE (s[i] # 0X) & (s[i] # " ") & (s[i] # 0DX) & (s[i] # 09X) & (len < LEN(t) - 1) DO
			t[len] := s[i]; INC(len); INC(i)
		END;
		t[len] := 0X
	END Token;

	(* --- JUnit и TAP ----------------------------------------------------- *)

	PROCEDURE Jw (IN s: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		IF ju # NIL THEN
			i := 0;
			WHILE s[i] # 0X DO ju.WriteByte(SYSTEM.VAL(BYTE, ORD(s[i]))); INC(i) END
		END
	END Jw;

	PROCEDURE JwCh (ch: CHAR);
	BEGIN
		IF ju # NIL THEN ju.WriteByte(SYSTEM.VAL(BYTE, ORD(ch))) END
	END JwCh;

	PROCEDURE JwEsc (IN s: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE s[i] # 0X DO
			IF s[i] = "<" THEN Jw("&lt;")
			ELSIF s[i] = ">" THEN Jw("&gt;")
			ELSIF s[i] = "&" THEN Jw("&amp;")
			ELSIF s[i] = 22X THEN Jw("&quot;")
			ELSE ju.WriteByte(SYSTEM.VAL(BYTE, ORD(s[i])))
			END;
			INC(i)
		END
	END JwEsc;

	PROCEDURE JwTime (ms: LONGINT);
		VAR s: ARRAY 16 OF CHAR; sec, frac: INTEGER;
	BEGIN
		sec := SHORT(ms DIV 1000); frac := SHORT(ms MOD 1000);
		Strings.IntToString(sec, s); Jw(s); Jw(".");
		IF frac < 100 THEN Jw("0") END; IF frac < 10 THEN Jw("0") END;
		Strings.IntToString(frac, s); Jw(s)
	END JwTime;

	PROCEDURE WriteJUnit (IN name: ARRAY OF CHAR; IN total: Counts; OUT ok: BOOLEAN);
		VAR f: Files.File; i, res: INTEGER; s: ARRAY 16 OF CHAR;
			fname: Files.Name; ftype: Files.Type;
	BEGIN
		ok := FALSE; ju := NIL;
		f := Files.dir.New(Files.dir.This(""), Files.dontAsk);
		IF f = NIL THEN RETURN END;
		ju := f.NewWriter(NIL);
		Jw("<?xml version="); JwCh(22X); Jw("1.0"); JwCh(22X);
		Jw(" encoding="); JwCh(22X); Jw("UTF-8"); JwCh(22X); Jw("?>"); Jw(0DX);
		Jw("<testsuite name="); JwCh(22X); Jw("Unit"); JwCh(22X);
		Jw(" tests="); JwCh(22X); Strings.IntToString(nofResults, s); Jw(s); JwCh(22X);
		Jw(" failures="); JwCh(22X); Strings.IntToString(total.failed + total.empty, s); Jw(s); JwCh(22X);
		Jw(" skipped="); JwCh(22X); Strings.IntToString(total.skipped, s); Jw(s); JwCh(22X);
		Jw(" errors="); JwCh(22X); Jw("0"); JwCh(22X); Jw(">"); Jw(0DX);
		i := 0;
		WHILE i < nofResults DO
			Jw("  <testcase classname="); JwCh(22X); JwEsc(results[i].module); JwCh(22X);
			Jw(" name="); JwCh(22X); JwEsc(results[i].name); JwCh(22X);
			Jw(" time="); JwCh(22X); JwTime(results[i].ms); JwCh(22X);
			IF results[i].status = stPass THEN Jw("/>"); Jw(0DX)
			ELSIF results[i].status = stSkip THEN
				Jw("><skipped/></testcase>"); Jw(0DX)
			ELSE
				Jw("><failure message="); JwCh(22X); JwEsc(results[i].msg); JwCh(22X); Jw("/></testcase>"); Jw(0DX)
			END;
			INC(i)
		END;
		Jw("</testsuite>"); Jw(0DX);
		fname := name$; ftype := 'xml';
		f.Register(fname, ftype, Files.dontAsk, res);
		f.Close; ju := NIL;
		ok := res = 0
	END WriteJUnit;

	PROCEDURE PrintTap (IN total: Counts);
		VAR i, n: INTEGER; s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(nofResults, s); W("1.."); W(s); Ln;
		i := 0;
		WHILE i < nofResults DO
			INC(i); Strings.IntToString(i, s);
			IF results[i-1].status = stPass THEN W("ok ") ELSE W("not ok ") END;
			W(s); W(" - "); W(results[i-1].module); W("."); W(results[i-1].name);
			IF results[i-1].status = stSkip THEN W(" # SKIP ")
			ELSIF results[i-1].status # stPass THEN W(" # ")
			END;
			IF results[i-1].status # stPass THEN W(results[i-1].msg) END;
			Ln
		END
	END PrintTap;

	PROCEDURE PrintSummary (IN total: Counts);
	BEGIN
		W("== Unit: passed "); N(total.passed);
		W(", failed "); N(total.failed + total.empty);
		W(", empty "); N(total.empty);
		W(", skipped "); N(total.skipped); Ln
	END PrintSummary;

	PROCEDURE TempDir* (OUT path: ARRAY OF CHAR);
	(** Каталог для файлов тестов. Создаёт и чистит его внешний
		запускатель (tools/unit.sh): внутри процесса BlackBox нет
		переносимого mkdir. *)
	BEGIN
		path := "UnitTmp"
	END TempDir;

	(* --- команда --------------------------------------------------------- *)

	PROCEDURE Run*;
		VAR par, target, tok, marks, jfile: ARRAY 256 OF CHAR;
			total: Counts; i, n: INTEGER; noquit, tapOn, isConsole: BOOLEAN;
			oldViewer: Kernel.Handler; mi: Meta.Item; ok: BOOLEAN;
	BEGIN
		GetPar(par);
		target := ""; marks := ""; jfile := ""; noquit := FALSE; tapOn := FALSE;
		i := 0;
		LOOP
			Token(par, i, tok);
			IF tok = "" THEN EXIT END;
			IF StartsWith(tok, "junit=") THEN StrCopy(tok, 6, StrLen(tok) - 6, jfile)
			ELSIF tok = "tap" THEN tapOn := TRUE
			ELSIF tok = "noquit" THEN noquit := TRUE
			ELSIF tok = "slow" THEN marks := marks + "slow"
			ELSIF tok = "gui" THEN marks := marks + "gui"
			ELSIF tok = "proc" THEN marks := marks + "proc"
			ELSIF (target = "") & (tok # "all") & ~StartsWith(tok, "junit=") THEN target := tok$
			END;
			INC(i)
		END;

		nofResults := 0; quiet := FALSE;
		total.passed := 0; total.failed := 0; total.empty := 0; total.skipped := 0;
		oldViewer := Kernel.GetTrapViewer(); isConsole := oldViewer = NIL;

		W("== Unit: run "); W(target); IF marks # "" THEN W(" (marks="); W(marks); W(")") END; Ln;
		IF (target = "") OR (target = "all") THEN
			RunMatching("", marks, total)
		ELSE
			Meta.Lookup(target, mi);
			IF mi.Valid() & (mi.obj = Meta.modObj) THEN
				RunModule(target, marks, total)
			ELSE RunMatching(target, marks, total)
			END
		END;

		PrintSummary(total);
		IF tapOn THEN PrintTap(total) END;
		IF jfile # "" THEN
			WriteJUnit(jfile, total, ok);
			IF ok THEN W("== Unit: JUnit -> "); W(jfile); Ln
			ELSE W("== Unit: cannot write JUnit "); W(jfile); Ln
			END
		END;
		IF isConsole & ~noquit THEN
			IF total.failed + total.empty > 0 THEN Kernel.Quit(1) ELSE Kernel.Quit(0) END
		END
	END Run;

END UnitRunner.
