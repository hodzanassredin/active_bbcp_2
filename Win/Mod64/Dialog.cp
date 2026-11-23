MODULE WinDialog;
	(* Basic GUI services for the Windows AMD64 profile without OLE object embedding. *)

	IMPORT SYSTEM, Strings, Dialog, Windows, Views, Stores, Files, Fonts, Converters, Printers, Ports, TextModels, TextViews,
		StdCmds, StdDialog, StdRegistry, WinRegistry, StdLog, WinPrinters, WinGui, WinEnv, WinFiles, WinDlg, A := WinApi;

	TYPE
		ExtCallHook = POINTER TO RECORD (Dialog.ExtCallHook) END;
		ShowHook = POINTER TO RECORD (Dialog.ShowHook) END;
		GetParHook = POINTER TO RECORD (Dialog.GetParHook) END;
		HostLocHook = POINTER TO RECORD (Dialog.HostLocHook) END;
		GetHook = POINTER TO RECORD (Dialog.GetHook) END;
		GetSpecHook = POINTER TO RECORD (Views.GetSpecHook) END;
		PrintHook = POINTER TO RECORD (StdDialog.PrintHook) END;
		LanguageHook = POINTER TO RECORD (Dialog.LanguageHook) END;
	VAR printDialog, emptyPrintDialog: WinDlg.PRINTDLGW;
		colorDialog, emptyColorDialog: WinDlg.CHOOSECOLORW;
		customColors: ARRAY 16 OF INTEGER;
		emptyDialog: WinDlg.OPENFILENAMEW;
		emptyShell: A.SHELLEXECUTEINFOW; emptyStartup: A.STARTUPINFOW;
		hideExtRunWindow*: BOOLEAN; lastExternalError*: INTEGER;
		visualScroll*: BOOLEAN;

	PROCEDURE ExternalError (error: INTEGER; IN operation, name: ARRAY OF CHAR);
		VAR message: ARRAY 512 OF CHAR; text: ARRAY 2048 OF CHAR; shortName: ARRAY 1024 OF CHAR;
			prefix: ARRAY 64 OF CHAR; number: ARRAY 32 OF CHAR; count: INTEGER;
	BEGIN
		IF error = 0 THEN error := 31 END; lastExternalError := error;
		count := A.FormatMessageW({9,12}, 0, error, 0, SYSTEM.ADR(message), LEN(message), 0);
		IF count = 0 THEN Strings.IntToString(error, number); message := "Windows error " + number END;
		Dialog.MapString(operation, prefix);
		Strings.Extract(name, 0, MIN(LEN(name$),LEN(shortName)-1), shortName);
		text := prefix + shortName + 0DX + 0AX + 0DX + 0AX + message;
		StdLog.String(text); StdLog.Ln; StdLog.Open
	END ExternalError;

	PROCEDURE (h: ExtCallHook) OpenExternal (IN fileName: ARRAY OF CHAR);
		VAR info: A.SHELLEXECUTEINFOW; verb: ARRAY 5 OF CHAR; error: INTEGER;
	BEGIN
		lastExternalError := 0; info := emptyShell; verb := "open";
		info.cbSize := SIZE(A.SHELLEXECUTEINFOW); info.hwnd := WinGui.mainWnd;
		(* SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI: consume CP arguments before returning. *)
		info.fMask := {8,10}; info.lpVerb := SYSTEM.ADR(verb); info.lpFile := SYSTEM.ADR(fileName); info.nShow := 1;
		IF A.ShellExecuteExW(info) = 0 THEN
			error := A.GetLastError();
			IF (error = 0) & (info.hInstApp > 0) & (info.hInstApp <= 32) THEN error := SHORT(info.hInstApp) END;
			ExternalError(error, "#System:openExtErr", fileName)
		END
	END OpenExternal;

	PROCEDURE (h: ExtCallHook) RunExternal (IN exeName: ARRAY OF CHAR);
		VAR info: A.STARTUPINFOW; process: A.PROCESS_INFORMATION; command: ARRAY 32768 OF CHAR;
			flags: SET; ignored, error: INTEGER; hidden: BOOLEAN;
	BEGIN
		hidden := hideExtRunWindow; hideExtRunWindow := FALSE; lastExternalError := 0;
		IF LEN(exeName$) >= LEN(command) THEN ExternalError(87, "#System:runExtErr", exeName); RETURN END;
		command := exeName$; info := emptyStartup; info.cb := SIZE(A.STARTUPINFOW); info.dwFlags := {0};
		IF hidden THEN info.wShowWindow := 0; flags := {27}
		ELSE info.wShowWindow := 1; flags := {}
		END;
		IF A.CreateProcessW(0, SYSTEM.ADR(command), 0, 0, A.FALSE, flags, 0, 0, info, process) # 0 THEN
			ignored := A.CloseHandle(process.hThread); ignored := A.CloseHandle(process.hProcess)
		ELSE error := A.GetLastError(); ExternalError(error, "#System:runExtErr", exeName)
		END
	END RunExternal;


	PROCEDURE (h: ShowHook) ShowParamMsg (IN str, p0, p1, p2: ARRAY OF CHAR);
		VAR mapped: ARRAY 1024 OF CHAR; result: INTEGER;
	BEGIN
		Dialog.MapParamString(str, p0, p1, p2, mapped);
		result := WinGui.MessageBox("BlackBox", mapped, {4, 5})
	END ShowParamMsg;

	PROCEDURE (h: ShowHook) ShowParamStatus (IN str, p0, p1, p2: ARRAY OF CHAR);
		VAR mapped: ARRAY 1024 OF CHAR;
	BEGIN
		Dialog.MapParamString(str, p0, p1, p2, mapped)
	END ShowParamStatus;

	PROCEDURE (h: GetParHook) GetPar (IN key: ARRAY OF CHAR; OUT val: ARRAY OF CHAR; OUT res: INTEGER);
	BEGIN WinEnv.GetPar(key, val, res)
	END GetPar;

	PROCEDURE DirtyClose (w: Windows.Window; OUT action: INTEGER);
		VAR result: INTEGER; title: Views.Title; text: ARRAY 512 OF CHAR;
	BEGIN
		w.GetTitle(title); text := "Save changes to " + title + "?";
		result := WinGui.MessageBox(Dialog.appName, text, {0, 1, 5});
		IF result = 6 THEN action := StdCmds.save
		ELSIF result = 7 THEN action := StdCmds.close
		ELSE action := StdCmds.cancel
		END
	END DirtyClose;

	PROCEDURE New*;
		VAR text: TextModels.Model; view: TextViews.View;
	BEGIN
		text := TextModels.dir.New(); view := TextViews.dir.New(text);
		Views.OpenView(view)
	END New;

	PROCEDURE (h: HostLocHook) GetLocPath (loc: Files.Locator; OUT path: ARRAY OF CHAR);
	BEGIN
		path := "";
		IF loc # NIL THEN
			IF loc IS WinFiles.Locator THEN path := loc(WinFiles.Locator).path$; loc.res := 0
			ELSE loc.res := 1
			END
		END
	END GetLocPath;

	PROCEDURE (h: HostLocHook) CheckHostLoc (loc: Files.Locator);
	BEGIN IF loc # NIL THEN IF loc IS WinFiles.Locator THEN loc.res := 0 ELSE loc.res := 1 END END END CheckHostLoc;

	PROCEDURE (h: GetHook) GetOK (IN str, p0, p1, p2: ARRAY OF CHAR; form: SET; OUT res: INTEGER);
		VAR text: Dialog.String; flags: SET; result: INTEGER;
	BEGIN
		Dialog.MapParamString(str, p0, p1, p2, text);
		IF Dialog.yes IN form THEN
			IF Dialog.cancel IN form THEN flags := {0, 1, 5} ELSE flags := {2, 5} END
		ELSE
			IF Dialog.cancel IN form THEN flags := {0, 4, 5} ELSE flags := {4, 5} END
		END;
		result := WinGui.MessageBox(Dialog.appName, text, flags);
		CASE result OF 1: res := Dialog.ok | 2: res := Dialog.cancel
		| 6: res := Dialog.yes | 7: res := Dialog.no ELSE res := 0 END
	END GetOK;

	PROCEDURE FileSpec (save: BOOLEAN; IN default: Files.Name; IN type: Files.Type;
		VAR loc: Files.Locator; OUT name: Files.Name);
		VAR file: WinDlg.OPENFILENAMEW; path, folder: ARRAY 1024 OF CHAR;
			title: ARRAY 32 OF CHAR; extension: Files.Type; filter: ARRAY 128 OF CHAR; label, pattern: ARRAY 64 OF CHAR;
			i, j, slash, count, result: INTEGER;
	BEGIN
		file := emptyDialog; file.size := SIZE(WinDlg.OPENFILENAMEW); file.owner := WinGui.mainWnd;
		IF save THEN path := default$ ELSE path := "" END;
		folder := "";
		IF (loc # NIL) & (loc IS WinFiles.Locator) THEN folder := loc(WinFiles.Locator).path$ END;
		IF folder # "" THEN file.initialDir := SYSTEM.ADR(folder) END;
		extension := type$;
		IF type # "" THEN
			file.defaultExt := SYSTEM.ADR(extension); label := "Files (*." + type + ")"; pattern := "*." + type;
			i := 0; WHILE label[i] # 0X DO filter[i] := label[i]; INC(i) END;
			filter[i] := 0X; INC(i); j := 0;
			WHILE pattern[j] # 0X DO filter[i] := pattern[j]; INC(i); INC(j) END;
			filter[i] := 0X; filter[i+1] := 0X; file.filter := SYSTEM.ADR(filter); file.filterIndex := 1
		END;
		file.file := SYSTEM.ADR(path); file.maxFile := LEN(path); file.flags := {2, 3, 11, 19};
		name := ""; loc := NIL;
		IF save THEN
			INCL(file.flags, 1); title := "Save document"; file.title := SYSTEM.ADR(title); result := WinDlg.GetSaveFileNameW(file)
		ELSE INCL(file.flags, 12); result := WinDlg.GetOpenFileNameW(file)
		END;
		IF result # 0 THEN
			i := 0; slash := -1;
			WHILE path[i] # 0X DO IF (path[i] = "\") OR (path[i] = "/") THEN slash := i END; INC(i) END;
			IF (slash >= 0) & (i-slash < LEN(name)) THEN
				i := 0; WHILE i <= slash DO folder[i] := path[i]; INC(i) END; folder[i] := 0X;
				count := 0; WHILE path[i] # 0X DO name[count] := path[i]; INC(count); INC(i) END; name[count] := 0X;
				loc := Files.dir.This(folder)
			ELSE Dialog.ShowStatus("File name is too long")
			END
		ELSIF WinDlg.CommDlgExtendedError() # 0 THEN Dialog.ShowStatus("Cannot open the Windows file dialog")
		END
	END FileSpec;

	PROCEDURE (h: GetHook) GetIntSpec (IN type: Files.Type; VAR loc: Files.Locator; OUT name: Files.Name);
	BEGIN FileSpec(FALSE, "", type, loc, name) END GetIntSpec;

	PROCEDURE (h: GetHook) GetExtSpec (IN default: Files.Name; IN type: Files.Type; VAR loc: Files.Locator; OUT name: Files.Name);
	BEGIN FileSpec(TRUE, default, type, loc, name) END GetExtSpec;

	PROCEDURE (h: GetHook) GetColor (in: INTEGER; OUT out: INTEGER; OUT set: BOOLEAN);
		VAR error: INTEGER;
	BEGIN
		colorDialog := emptyColorDialog;
		colorDialog.lStructSize := SIZE(WinDlg.CHOOSECOLORW);
		colorDialog.hwndOwner := WinGui.mainWnd;
		colorDialog.rgbResult := in;
		IF in = Ports.defaultColor THEN colorDialog.rgbResult := A.GetSysColor(8) END;
		colorDialog.lpCustColors := SYSTEM.ADR(customColors);
		colorDialog.Flags := {0}; (* CC_RGBINIT *)
		set := WinDlg.ChooseColorW(colorDialog) # 0;
		IF set THEN out := colorDialog.rgbResult
		ELSE
			out := in; error := WinDlg.CommDlgExtendedError();
			IF error # 0 THEN ExternalError(error, "ChooseColorW: ", "") END
		END
	END GetColor;

	PROCEDURE (h: GetHook) GetFont (full: BOOLEAN; VAR typeface: Fonts.Typeface; VAR size, color, weight: INTEGER; VAR style: SET; VAR set: BOOLEAN; IN title: Dialog.String);
	BEGIN set := FALSE END GetFont;

	PROCEDURE (h: GetSpecHook) GetIntSpec (VAR loc: Files.Locator; VAR name: Files.Name; VAR conv: Converters.Converter);
	BEGIN FileSpec(FALSE, "", "", loc, name); conv := NIL END GetIntSpec;

	PROCEDURE (h: GetSpecHook) GetExtSpec (store: Stores.Store; VAR loc: Files.Locator; VAR name: Files.Name; VAR conv: Converters.Converter);
			VAR type: Files.Type; default: Files.Name;
	BEGIN
		IF (conv # NIL) & (conv.exp # "") THEN type := conv.fileType$
		ELSE conv := NIL; type := Files.docType$
		END;
		default := name$; FileSpec(TRUE, default, type, loc, name)
	END GetExtSpec;

	PROCEDURE (hook: LanguageHook) SetLanguage (
		lang: Dialog.Language; persistent: BOOLEAN; OUT ok: BOOLEAN
	);
	BEGIN
		ok := (lang = "") OR (LEN(lang$) = 2);
		IF ok & persistent THEN StdRegistry.WriteString("language", lang) END
	END SetLanguage;

	PROCEDURE (hook: LanguageHook) GetPersistentLanguage (OUT lang: Dialog.Language);
		VAR res: INTEGER; s: ARRAY 32 OF CHAR;
	BEGIN
		StdRegistry.ReadString("language", s, res);
		IF res = 0 THEN
			ASSERT((s = "") OR (LEN(s$) = 2), 100);
			lang := s$
		ELSE lang := ""
		END
	END GetPersistentLanguage;

	PROCEDURE PreparePrintDialog;
	BEGIN
		printDialog := emptyPrintDialog;
		printDialog.lStructSize := SIZE(WinDlg.PRINTDLGW);
		printDialog.hwndOwner := WinGui.mainWnd;
		printDialog.nMinPage := 1; printDialog.nMaxPage := 32767; printDialog.nCopies := 1;
		WinPrinters.GetCurrent(printDialog.hDevNames, printDialog.hDevMode)
	END PreparePrintDialog;

	PROCEDURE (hook: PrintHook) GetPage (p: Printers.Printer; VAR w, h: INTEGER);
	BEGIN WinPrinters.GetPage(p, w, h) END GetPage;

	PROCEDURE (hook: PrintHook) PrintSetup;
		VAR result: INTEGER; p: Printers.Printer;
	BEGIN
		p := Printers.dir.Current();
		IF p # NIL THEN p.SetOrientation(StdDialog.setup.landscape) END;
		PreparePrintDialog; printDialog.Flags := {6};
		result := WinDlg.PrintDlgW(printDialog);
		IF (result = 0) & (WinDlg.CommDlgExtendedError() = 4108) THEN
			printDialog.hDevMode := 0; printDialog.hDevNames := 0;
			result := WinDlg.PrintDlgW(printDialog)
		END;
		IF result # 0 THEN
			WinPrinters.SetCurrent(printDialog.hDevNames, printDialog.hDevMode);
			p := Printers.dir.Current();
			IF p # NIL THEN
				WinPrinters.GetPage(p, StdDialog.setup.w, StdDialog.setup.h);
				StdDialog.setup.landscape := StdDialog.setup.w > StdDialog.setup.h;
				StdDialog.SetupNotify(Dialog.changed, 0, 0)
			END
		END
	END PrintSetup;

	PROCEDURE (hook: PrintHook) PrintDialog (hasSelection: BOOLEAN;
		VAR from, to, copies: INTEGER; VAR selection: BOOLEAN);
		VAR result, error: INTEGER;
	BEGIN
		PreparePrintDialog; printDialog.Flags := {18};
		IF ~hasSelection THEN INCL(printDialog.Flags, 2) END;
		printDialog.nFromPage := 1; printDialog.nToPage := 1;
		result := WinDlg.PrintDlgW(printDialog);
		IF result = 0 THEN
			error := WinDlg.CommDlgExtendedError();
			IF (error = 4106) OR (error = 4108) THEN
				printDialog.hDevMode := 0;
				IF error = 4108 THEN printDialog.hDevNames := 0 END;
				result := WinDlg.PrintDlgW(printDialog)
			END
		END;
		IF result # 0 THEN
			WinPrinters.SetCurrent(printDialog.hDevNames, printDialog.hDevMode);
			IF 0 IN printDialog.Flags THEN selection := TRUE; from := 0; to := 0
			ELSIF 1 IN printDialog.Flags THEN
				selection := FALSE; from := printDialog.nFromPage-1; to := printDialog.nToPage-1
			ELSE selection := FALSE; from := 0; to := 32767
			END;
			copies := printDialog.nCopies
		ELSE copies := 0
		END
	END PrintDialog;

	PROCEDURE Init;
		VAR external: ExtCallHook; show: ShowHook; par: GetParHook; host: HostLocHook; get: GetHook; spec: GetSpecHook; print: PrintHook; language: LanguageHook;
	BEGIN
		ASSERT(SIZE(A.SHELLEXECUTEINFOW) = 112); ASSERT(SIZE(A.STARTUPINFOW) = 104);
		ASSERT(SIZE(A.PROCESS_INFORMATION) = 24);
		visualScroll := TRUE;
		NEW(external); Dialog.SetExtCallHook(external);
		Dialog.platform := Dialog.windows;
		NEW(show); Dialog.SetShowHook(show);
		NEW(par); Dialog.SetGetParHook(par);
		StdCmds.stdDirtyHook := DirtyClose; WinFiles.MessageBox := WinGui.MessageBox;
		NEW(host); Dialog.SetHostLocHook(host);
		NEW(get); Dialog.SetGetHook(get);
		NEW(spec); Views.SetGetSpecHook(spec); StdCmds.SetGetSpecHook(spec);
		NEW(print); StdDialog.SetPrintHook(print);
		NEW(language); Dialog.SetLanguageHook(language);
		Dialog.ResetLanguage
	END Init;
BEGIN Init
END WinDialog.
