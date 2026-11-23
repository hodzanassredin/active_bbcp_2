MODULE WinDialog;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		SYSTEM, WinApi, WinDlg,
		Strings, Fonts, Ports, Files, Stores, Views, Controllers,
		Printers, Dialog, Windows, Converters, Services, Librarian, 
		HostFiles := WinFiles,
		StdRegistry, 
		HostPorts := WinPorts,
		WinPrinters,
		HostEnv := WinEnv, 
		StdCmds, StdDialog, WinGui, WinFonts, WinEnv ;
	
	CONST
		dirtyString = "#Std:SaveChanges";

	TYPE

		DialogGetHook = POINTER TO RECORD (Dialog.GetHook) END;
		DialogGetParHook = POINTER TO RECORD (Dialog.GetParHook) END;
		GetSpecHook = POINTER TO RECORD (Views.GetSpecHook) END;
		LanguageHook = POINTER TO RECORD (Dialog.LanguageHook) END;
		ExtCallHook = POINTER TO RECORD (Dialog.ExtCallHook) END;
		CallName = ARRAY [untagged] 8191 OF CHAR;
		CallNamePtr = POINTER TO CallName;
		HostLocHook = POINTER TO RECORD (Dialog.HostLocHook) END;
		BeepHook = POINTER TO RECORD (Dialog.BeepHook) END;
		PrintHook = POINTER TO RECORD (StdDialog.PrintHook) END;
		
	VAR
		window-: Windows.Window;	(** window created/selected by most recent Old or Open **)
		oldWindow-: BOOLEAN;	(** most recent Old or Open selected existing window **)

		osVersion-: INTEGER;
		extType*: Files.Type;
		
		impType*: RECORD
			list*: Dialog.List;
			done: BOOLEAN
		END;

		visualScroll-: BOOLEAN;
		
		prefs*: RECORD
			visualScroll*: BOOLEAN;
			useTTMetric*: BOOLEAN;
		END;

		ShowOleStatus*: PROCEDURE (w: Windows.Window; s: ARRAY OF CHAR);
		WaitFrontWindowClose* : PROCEDURE;	(* emulate modal dialogs: block until front window is closed *)

		actualPath*: ARRAY 256 OF CHAR;

		fn: WinDlg.OPENFILENAMEW;
		ftype: ARRAY 16 OF CHAR;
		filter: ARRAY 2048 OF CHAR;
		fname: ARRAY 260 OF CHAR;
		prt: WinDlg.PRINTDLGW;
		font: WinDlg.CHOOSEFONTW;
		logFont: WinApi.LOGFONTW;
		col: WinDlg.CHOOSECOLORW;
		customColors: ARRAY 16 OF Ports.Color;
		typeList: ARRAY 64 OF Converters.Converter;
		defConv: Converters.Converter;
		all: Converters.Converter;
		
		callNameTaken: BOOLEAN; (* params of OpenExternal and RunExternal *)
		openExtErr, runExtErr: ARRAY 64 OF CHAR;
		hideExtRunWindow*: BOOLEAN;

	
	
	PROCEDURE ActualWnd (): WinApi.HANDLE;
	(* To replace WinWindows.ActualWnd(). It was used to supply the .hwndOwner to standard dialog boxes (open, save, print, color). However, winapi doc says this field may be NULL; it is not clear to me how the functioning would change if it were NULL. Furthermore, WinGui.mainWnd could be used for .hwndOwner, couldn't it? WinWindows.ActualWnd supplies the front window's .wnd, if it's not a .child - i.e. for tool dialogs.
	
	Hey, .hwndOwner becomes unavailable to the user, because the dialog boxes are modal. Right?
	 *)
	BEGIN 
		(* ? maybe add special case of a single main document window? *)
		RETURN WinGui.mainWnd
	END ActualWnd;

	PROCEDURE Append (VAR s: ARRAY OF CHAR; VAR idx: INTEGER; IN t: ARRAY OF CHAR);
		VAR len, i: INTEGER; ch: CHAR;
	BEGIN
		i := 0; ch := t[0]; len := LEN(s) - 1;
		WHILE (idx < len) & (ch # 0X) DO s[idx] := ch; INC(idx); INC(i); ch := t[i] END;
		s[idx] := 0X
	END Append;

	PROCEDURE MapConv (cmd: ARRAY OF CHAR; VAR name: ARRAY OF CHAR);
		VAR i: INTEGER; sub, str: Dialog.String;
	BEGIN
		str := cmd$; i := 0;
		WHILE str[i] >= "0" DO INC(i) END;
		str[i] := 0X;
		Librarian.lib.SplitName(str, sub, str);
		str := "#"; i := 1;
		Append(str, i, sub);
		Append(str, i, ":");
		Append(str, i, cmd);
		Dialog.MapString(str, name)
	END MapConv;

	(*WaitDialogClose -> WaitFrontWindowClose, WinWindows.WaitFrontWindowClose *)

	PROCEDURE (hook: PrintHook) GetPage (p: Printers.Printer; VAR w, h: INTEGER);
	BEGIN
		WinPrinters.GetPage(p, w, h)
	END GetPage;
	
	PROCEDURE (hook: PrintHook) PrintSetup;
		VAR res: INTEGER; pt: Printers.Printer;
	BEGIN
		pt := Printers.dir.Current();
		IF pt # NIL THEN
			pt.SetOrientation(StdDialog.setup.landscape);
			WinPrinters.GetCurrent(prt.hDevNames, prt.hDevMode)
		END;
		prt.Flags := {6};	(* PrintSetup *)
		prt.hwndOwner := (*HostWindws.*)ActualWnd();
		res := WinDlg.PrintDlgW(prt);
		IF (res = 0) & (WinDlg.CommDlgExtendedError() = 4108) THEN
			prt.hDevMode := 0; prt.hDevNames := 0;
			res := WinDlg.PrintDlgW(prt)
		END;
		WinPrinters.SetCurrent(prt.hDevNames, prt.hDevMode);
		pt := Printers.dir.Current();
		IF pt # NIL THEN
			WinPrinters.GetPage(pt, StdDialog.setup.w, StdDialog.setup.h);
			StdDialog.setup.landscape := StdDialog.setup.w > StdDialog.setup.h
		END;
		StdDialog.SetupNotify(Dialog.changed, 0, 0)
	END PrintSetup;

	PROCEDURE (hook: PrintHook) PrintDialog (
		hasSelection: BOOLEAN; VAR from, to, copies: INTEGER; VAR selection: BOOLEAN
	);
		VAR res: INTEGER;
	BEGIN
		prt.Flags := {18 (*, 20 *)};	(* use dev mode copies, hide print to file *)
		IF ~hasSelection THEN INCL(prt.Flags, 2) END;	(* no selection *)
		prt.nCopies := 1;
		prt.hwndOwner := (*HostWindws.*)ActualWnd();
		prt.nFromPage := 1; prt.nToPage := 1;
		res := WinDlg.PrintDlgW(prt);
		IF (res = 0) & (WinDlg.CommDlgExtendedError() = 4106) THEN
			prt.hDevMode := 0;
			res := WinDlg.PrintDlgW(prt)
		END;
		IF (res = 0) & (WinDlg.CommDlgExtendedError() = 4108) THEN
			prt.hDevMode := 0; prt.hDevNames := 0;
			res := WinDlg.PrintDlgW(prt)
		END;
		WinPrinters.SetCurrent(prt.hDevNames, prt.hDevMode);
		IF res # 0 THEN
			IF 0 IN prt.Flags THEN selection := TRUE; from := 0; to := 0	(* print selection *)
			ELSIF 1 IN prt.Flags THEN selection := FALSE; from := prt.nFromPage - 1; to := prt.nToPage - 1 (* print pages *)
			ELSE selection := FALSE; from := 0; to := 32767	(* print all *)
			END;
			copies := prt.nCopies
		ELSE
			copies := 0;
			res := WinDlg.CommDlgExtendedError();
			ASSERT(res = 0, 100)
		END
	END PrintDialog;



	(* Dialog with external paramiters *)
	
	PROCEDURE (hook: DialogGetParHook) GetPar (IN key: ARRAY OF CHAR;	
		OUT val: ARRAY OF CHAR; OUT res: INTEGER);
	BEGIN
		WinEnv.GetPar(key, val, res)
	END GetPar;

	(** general OK dialog **)

	PROCEDURE (hook: DialogGetHook) GetOK (IN str, p0, p1, p2: ARRAY OF CHAR; form: SET; OUT res: INTEGER);
		VAR r: INTEGER; st: ARRAY 512 OF CHAR; type: SET;
	BEGIN
		ASSERT(str # "", 20);
		Dialog.MapParamString(str, p0, p1, p2, st);
		IF Dialog.yes IN form THEN
			IF Dialog.cancel IN form THEN type := WinApi.MB_YESNOCANCEL + WinApi.MB_ICONQUESTION
			ELSE type := WinApi.MB_YESNO + WinApi.MB_ICONQUESTION
			END
		ELSE
			IF Dialog.cancel IN form THEN type := WinApi.MB_OKCANCEL + WinApi.MB_ICONWARNING
			ELSE type := WinApi.MB_OK + WinApi.MB_ICONWARNING
			END
		END;
		r := WinApi.MessageBoxW((*HostWindws.*)ActualWnd(), st, Dialog.appName, type);
		IF r = WinApi.IDOK THEN res := Dialog.ok
		ELSIF r = WinApi.IDCANCEL THEN res := Dialog.cancel
		ELSIF r = WinApi.IDYES THEN res := Dialog.yes
		ELSIF r = WinApi.IDNO THEN res := Dialog.no
		ELSE res := 0
		END
	END GetOK;


	PROCEDURE SetVisualScroll* (do: BOOLEAN);
	BEGIN
		IF visualScroll # do THEN
			visualScroll := do;
			StdRegistry.WriteBool("VisualScroll", do)
		END
	END SetVisualScroll;
	
	PROCEDURE PrefOk*;
	BEGIN
		WinFonts.SetTTMetric(prefs.useTTMetric);
		SetVisualScroll(prefs.visualScroll);
	END PrefOk;
	
	PROCEDURE InitPrefDialog*;
	BEGIN
		prefs.useTTMetric := WinFonts.useTTMetric;
		prefs.visualScroll := visualScroll;
	END InitPrefDialog;

	(** import type dialog **)
	
	PROCEDURE ImpOk*;
	BEGIN
		impType.done := TRUE;
		StdCmds.CloseDialog
	END ImpOk;


	(** file dialogs **)

	(* asks user for a file name (for file internalization) *)
	PROCEDURE (hook: DialogGetHook) GetIntSpec (
		IN defType: Files.Type; VAR loc: Files.Locator; OUT name: Files.Name
	);
		VAR res, i, j: INTEGER; lfn: ARRAY 260 OF CHAR;
	BEGIN
		(* set filter *)
		ftype := defType$; i := 0;
		IF ftype = "" THEN ftype := "odc" END;
		IF ftype # "*" THEN
			Append(filter, i, "*."); Append(filter, i, ftype); INC(i);
			Append(filter, i, "*."); Append(filter, i, ftype); INC(i)
		ELSE ftype := ""
		END;
		Append(filter, i, "*.*"); INC(i);
		Append(filter, i, "*.*"); INC(i);
		filter[i] := 0X;
		(* open dialog *)
		fn.nFilterIndex := 1;
		fname := "";
		IF (loc # NIL) THEN	(* set old dir *)
			Dialog.GetLocPath(loc, actualPath)
		END;
		fn.Flags := WinDlg.OFN_HIDEREADONLY + WinDlg.OFN_NOCHANGEDIR + WinDlg.OFN_PATHMUSTEXIST + WinDlg.OFN_FILEMUSTEXIST + WinDlg.OFN_EXPLORER;
		fn.hwndOwner := (*HostWindws.*)ActualWnd();
		IF WinDlg.GetOpenFileNameW(fn) # 0 THEN
			i := 0; j := fn.nFileOffset;
			WHILE fname[j] # 0X DO name[i] := fname[j]; INC(i); INC(j) END;
			name[i] := 0X; fname[fn.nFileOffset - 1] := 0X;
			actualPath := fname$; lfn := fname$;
			loc := HostFiles.NewLocator(lfn)
		ELSE
			loc := NIL;
			res := WinDlg.CommDlgExtendedError();
			ASSERT(res = 0, 100)
		END;
		res := WinApi.SetCursor(HostPorts.cursors[HostPorts.busyCursor])
	END GetIntSpec;

	(** asks user for a file name (for file internalization) **)
	PROCEDURE GetIntSpec* (VAR loc: Files.Locator; VAR name: Files.Name; VAR conv: Converters.Converter);
		VAR res, i, j, n: INTEGER; t: Dialog.String; c: Converters.Converter; lfn: ARRAY 260 OF CHAR;
	BEGIN
		(* set filter *)
		ftype := "odc";
		IF loc # NIL THEN
			defConv := conv;
			IF conv # NIL THEN ftype := conv.fileType$ END
		END;
		typeList[0] := Converters.list;	(* document converter *)
		Dialog.MapString("#System:Document", t);
		i := 0; Append(filter, i, t); Append(filter, i, " (*.odc)"); INC(i);
		Append(filter, i, "*.odc"); INC(i); n := 1;
		fn.nFilterIndex := 1;
		c := Converters.list;
		WHILE c # NIL DO
			IF (c.imp # "") & (c.fileType # "odc") THEN
				typeList[n] := c;
				MapConv(c.imp, t); Append(filter, i, t);
				Append(filter, i, " (*."); Append(filter, i, c.fileType);
				Append(filter, i, ")"); INC(i);
				Append(filter, i, "*."); Append(filter, i, c.fileType); INC(i);
				IF defConv = c THEN fn.nFilterIndex := n + 1 END;
				INC(n)
			END;
			c := c.next
		END;
		Dialog.MapString("#System:AllFiles", t);
		Append(filter, i, t); Append(filter, i, " (*.*)"); INC(i);
		Append(filter, i, "*.*"); INC(i);
		filter[i] := 0X;
		IF defConv = all THEN fn.nFilterIndex := n + 1 END;
		(* open dialog *)
		IF loc # NIL THEN fname := name$
		ELSE fname := ""
		END;
		IF (loc # NIL) & (loc IS HostFiles.Locator) THEN	(* set old dir *)
			Dialog.GetLocPath(loc, actualPath);
			i := 0; WHILE actualPath[i] # 0X DO INC(i) END;
			actualPath[i] := "\"; actualPath[i+1] := 0X
		END;
		fn.Flags := WinDlg.OFN_HIDEREADONLY + WinDlg.OFN_NOCHANGEDIR + WinDlg.OFN_PATHMUSTEXIST + WinDlg.OFN_FILEMUSTEXIST + WinDlg.OFN_EXPLORER;
		fn.hwndOwner := (*HostWindws.*)ActualWnd();
		IF WinDlg.GetOpenFileNameW(fn) # 0 THEN
			i := 0; j := fn.nFileOffset;
			WHILE fname[j] # 0X DO name[i] := fname[j]; INC(i); INC(j) END;
			name[i] := 0X; fname[fn.nFileOffset - 1] := 0X;
			actualPath := fname$; lfn := fname$;
			loc := HostFiles.NewLocator(lfn);
			IF fn.nFilterIndex <= n THEN
				conv := typeList[fn.nFilterIndex - 1];
				defConv := conv
			ELSE	(* ask for desired file type *)
				impType.list.SetLen(n);
				Dialog.MapString("#System:Document", t); impType.list.SetItem(0, t);
				i := 1;
				WHILE i < n DO
					MapConv(typeList[i].imp, t); impType.list.SetItem(i, t);
					INC(i)
				END;
				impType.list.index := 0;
				impType.done := FALSE;
				Dialog.Call("StdCmds.OpenToolDialog('WinDialog.impType', '#System:OpenFile')", " ", res);
				ASSERT((res = 0) & (StdCmds.result = 0), 101);
				
				ASSERT(WaitFrontWindowClose # NIL, 20);
				WaitFrontWindowClose;
				
				IF impType.done THEN
					conv := typeList[impType.list.index]
				ELSE
					loc := NIL; conv := NIL
				END;
				defConv := all
			END
		ELSE
			loc := NIL; conv := NIL;
			res := WinDlg.CommDlgExtendedError();
			ASSERT(res = 0, 100)
		END;
		res := WinApi.SetCursor(HostPorts.cursors[HostPorts.busyCursor])
	END GetIntSpec;

	PROCEDURE (h: GetSpecHook) GetIntSpec (VAR loc: Files.Locator; VAR name: Files.Name;
															VAR conv: Converters.Converter);
	BEGIN
		GetIntSpec(loc, name, conv)
	END GetIntSpec;
	

	(* ask user for a file name (for file externalization) *)
	PROCEDURE (hook: DialogGetHook) GetExtSpec (
		IN default: Files.Name; IN defType: Files.Type; VAR loc: Files.Locator; OUT name: Files.Name
	);
		VAR res, i, j: INTEGER; lfn: ARRAY 260 OF CHAR;
	BEGIN
		(* set filter *)
		ftype := defType$; i := 0;
		IF ftype = "" THEN ftype := "odc" END;
		IF ftype # "*" THEN
			Append(filter, i, "*."); Append(filter, i, ftype); INC(i);
			Append(filter, i, "*."); Append(filter, i, ftype); INC(i)
		ELSE ftype := ""
		END;
		Append(filter, i, "*.*"); INC(i);
		Append(filter, i, "*.*"); INC(i);
		filter[i] := 0X;
		(* open dialog *)
		fn.nFilterIndex := 1;
		fname := default$;
		IF (loc # NIL) THEN	(* set old dir *)
			Dialog.GetLocPath(loc, actualPath)
		END;
		fn.Flags := WinDlg.OFN_OVERWRITEPROMPT + WinDlg.OFN_HIDEREADONLY + WinDlg.OFN_NOCHANGEDIR + WinDlg.OFN_PATHMUSTEXIST + WinDlg.OFN_EXPLORER;
		fn.hwndOwner := (*HostWindws.*)ActualWnd();
		res := WinDlg.GetSaveFileNameW(fn);
		IF (res = 0) & (WinDlg.CommDlgExtendedError() = 3002H) THEN
			fname := "";
			res := WinDlg.GetSaveFileNameW(fn)
		END;
		IF res # 0 THEN
			i := 0; j := fn.nFileOffset;
			WHILE fname[j] # 0X DO name[i] := fname[j]; INC(i); INC(j) END;
			name[i] := 0X; fname[fn.nFileOffset - 1] := 0X;
			actualPath := fname$; lfn := fname$;
			loc := HostFiles.NewLocator(lfn)
		ELSE
			loc := NIL;
			res := WinDlg.CommDlgExtendedError();
			ASSERT(res = 0, 100)
		END;
		res := WinApi.SetCursor(HostPorts.cursors[HostPorts.busyCursor])
	END GetExtSpec;

	(** ask user for a file name (for file externalization) **)
	PROCEDURE GetExtSpec* (
		s: Stores.Store; VAR loc: Files.Locator; VAR name: Files.Name; VAR conv: Converters.Converter
	);
		VAR res, i, j, n: INTEGER; t: ARRAY 64 OF CHAR; c: Converters.Converter;
			lfn: ARRAY 260 OF CHAR;
	BEGIN
		(* set filter *)
		IF conv # NIL THEN ftype := conv.fileType$ ELSE ftype := "odc" END;
		typeList[0] := Converters.list;	(* document converter *)
		c := Converters.list; fn.nFilterIndex := 1;
		Dialog.MapString("#System:Document", t);
		i := 0; Append(filter, i, t); Append(filter, i, " (*.odc)"); INC(i);
		Append(filter, i, "*.odc"); INC(i); n := 1;
		WHILE c # NIL DO
			IF (c.exp # "") & ((c.storeType = "") OR Services.Is(s, c.storeType)) & (c.fileType # "odc") THEN
				typeList[n] := c;
				MapConv(c.exp, t); Append(filter, i, t);
				Append(filter, i, " (*."); Append(filter, i, c.fileType);
				Append(filter, i, ")"); INC(i);
				Append(filter, i, "*."); Append(filter, i, c.fileType); INC(i);
				IF c = conv THEN fn.nFilterIndex := n + 1 END;
				INC(n)
			END;
			c := c.next
		END;
		filter[i] := 0X;
		(* open dialog *)
		fname := name$; i := 0;
		(* overwritePrompt, hideReadOnly, noChangeDir, (* en hook, *) pathMustExist *)
		fn.Flags := {1, 2, 3, (* 5 ,*) 11, 19};
		WHILE fname[i] # 0X DO INC(i) END;
		WHILE (i > 0) & (fname[i] # ".") DO DEC(i) END;
		IF i > 0 THEN
			IF (conv # NIL) & (conv.exp = "") THEN
				ftype := "odc"; fname[i] := 0X
			ELSE
				j := 0;
				WHILE (ftype[j] # 0X) & (CAP(fname[i+j+1]) = CAP(ftype[j])) DO INC(j) END;
				IF fname[i+j+1] = ftype[j] THEN fname[i] := 0X END
			END
		END;
		IF (loc # NIL) THEN (* set old dir *)
			Dialog.GetLocPath(loc, actualPath);
			i := 0; WHILE actualPath[i] # 0X DO INC(i) END;
			actualPath[i] := "\"; actualPath[i+1] := 0X
		END;
		fn.hwndOwner := (*HostWindws.*)ActualWnd();
		res := WinDlg.GetSaveFileNameW(fn);
		IF (res = 0) & (WinDlg.CommDlgExtendedError() = 3002H) THEN
			fname := "";
			res := WinDlg.GetSaveFileNameW(fn)
		END;
		IF res # 0 THEN
			i := 0; j := fn.nFileOffset;
			WHILE fname[j] # 0X DO name[i] := fname[j]; INC(i); INC(j) END;
			name[i] := 0X; fname[fn.nFileOffset - 1] := 0X;
			actualPath := fname$; lfn := fname$;
			loc := HostFiles.NewLocator(lfn);
			conv := typeList[fn.nFilterIndex - 1]
		ELSE
			loc := NIL;
			res := WinDlg.CommDlgExtendedError();
			ASSERT(res = 0, 100)
		END;
		res := WinApi.SetCursor(HostPorts.cursors[HostPorts.busyCursor])
	END GetExtSpec;

	PROCEDURE (h: GetSpecHook) GetExtSpec (
		s: Stores.Store; VAR loc: Files.Locator; VAR name: Files.Name; VAR conv: Converters.Converter
	);
	BEGIN
		GetExtSpec(s, loc, name, conv)
	END GetExtSpec;




	PROCEDURE DirtyHook (w: Windows.Window; OUT action: INTEGER);
		VAR r: INTEGER; title: Views.Title; text: ARRAY 256 OF CHAR;
	BEGIN
		w.GetTitle(title);
		Dialog.MapParamString(dirtyString, title, 0DX, 0DX, text);
		r := WinApi.MessageBoxW(ActualWnd(), text, Dialog.appName, {0, 1, 5});
		IF r = 6 THEN action := StdCmds.save
		ELSIF r = 2 THEN action := StdCmds.cancel
		ELSE action := StdCmds.close
		END;
		r := WinApi.SetCursor(HostPorts.cursors[HostPorts.busyCursor])
	END DirtyHook;

	PROCEDURE (hook: DialogGetHook) GetColor (in: Ports.Color; OUT out: Ports.Color; OUT set: BOOLEAN);
		VAR res: INTEGER;
	BEGIN
		col.rgbResult := in;
		IF col.rgbResult = Ports.defaultColor THEN col.rgbResult := HostPorts.textCol END;
		col.hwndOwner := (*HostWindws.*)ActualWnd();
		set := WinDlg.ChooseColorW(col) # 0;
		out := col.rgbResult;
		IF ~set THEN
			res := WinDlg.CommDlgExtendedError();
			ASSERT(res = 0, 100)
		END;
		res := WinApi.SetCursor(HostPorts.cursors[HostPorts.busyCursor])
	END GetColor;

	PROCEDURE (hook: DialogGetHook) GetFont (full: BOOLEAN; VAR typeface: Fonts.Typeface; VAR size, color, weight: INTEGER; VAR style: SET; VAR set: BOOLEAN; IN title: Dialog.String );
		VAR unit, res: INTEGER;
	BEGIN
		unit := Ports.inch DIV WinApi.GetDeviceCaps(WinApi.GetDC(0), WinApi.LOGPIXELSY);
		logFont.lfHeight := -((size + (*HostWindws.*)unit DIV 2) DIV (*HostWindws.*)unit);
		logFont.lfWeight := weight;
		IF Fonts.italic IN style THEN logFont.lfItalic := 1X ELSE logFont.lfItalic := 0X END;
		IF Fonts.underline IN style THEN logFont.lfUnderline := 1X ELSE logFont.lfUnderline := 0X END;
		IF Fonts.strikeout IN style THEN logFont.lfStrikeOut := 1X ELSE logFont.lfStrikeOut := 0X END;
		logFont.lfFaceName := typeface$;
		font.hwndOwner := (*HostWindws.*)ActualWnd();
		IF full THEN
			font.Flags := {0, 6, 8, 11, 16}	(* screenFonts, initToLogFont, effects, noVectorFonts, forceFontExist *)
		ELSE
			font.Flags := {0, 6, 11, 20, 21}	(* screenFonts, initToLogFont, noVectorFonts, no style, no size *)
		END;
		IF color = Ports.defaultColor THEN font.rgbColors := HostPorts.textCol ELSE font.rgbColors := color END;
		set := WinDlg.ChooseFontW(font) # 0;
		IF set THEN
			typeface := logFont.lfFaceName$;
			size := font.iPointSize * Ports.point DIV 10;
			ASSERT((size + (*HostWindws.*)unit DIV 2) DIV (*HostWindws.*)unit = ABS(logFont.lfHeight), 120);
			weight := logFont.lfWeight;
			style := {};
			IF logFont.lfItalic # 0X THEN INCL(style, Fonts.italic) END;
			IF logFont.lfUnderline # 0X THEN INCL(style, Fonts.underline) END;
			IF logFont.lfStrikeOut # 0X THEN INCL(style, Fonts.strikeout) END;
			IF font.rgbColors = HostPorts.textCol THEN color := Ports.defaultColor ELSE color := font.rgbColors END
		ELSE
			res := WinDlg.CommDlgExtendedError();
			(* ASSERT(res = 0, 100) *)
		END;
		res := WinApi.SetCursor(HostPorts.cursors[HostPorts.busyCursor])
	END GetFont;



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

	PROCEDURE IsWine(): BOOLEAN;
		VAR h: WinApi.HMODULE; adr: WinApi.FARPROC; res: INTEGER;
	BEGIN
		(* http://habrahabr.ru/blogs/development/76131/ *)
		h := WinApi.LoadLibraryW("ntdll.dll");
		IF h # WinApi.NULL THEN
			adr := WinApi.GetProcAddress(h, "wine_get_version");
			res := WinApi.FreeLibrary(h);
			IF adr # NIL THEN RETURN TRUE END
		END ;
		RETURN FALSE
	END IsWine;

	(* thread safe; error numbers as returned from WinApi.GetLastError() *)
	PROCEDURE FormatErrorMsg* (error: INTEGER; IN arg1, arg2, arg3: ARRAY OF CHAR; 
													OUT errMsg: ARRAY OF CHAR);
		CONST format =
			WinApi.FORMAT_MESSAGE_FROM_SYSTEM + WinApi.FORMAT_MESSAGE_ARGUMENT_ARRAY;
		VAR args: ARRAY 4 OF WinApi.PtrWSTR; res: INTEGER;
	BEGIN
		args[0] := arg1; args[1] := arg2; args[2] := arg3; args[3] :=  NIL;
		res := WinApi.FormatMessageW(format, 0, error, 0, errMsg, LEN(errMsg), args[0])
	END FormatErrorMsg;

	(* thread safe; error numbers as returned from WinApi.GetLastError *)
	PROCEDURE ShowErrorDialog* (error: INTEGER; IN arg1, arg2, arg3, msgPrefix: ARRAY OF CHAR);
		VAR errMsg: CallName; res: INTEGER;
	BEGIN
		FormatErrorMsg(error, arg1, arg2, arg3, errMsg);
		errMsg := msgPrefix + errMsg;
		res := WinApi.MessageBoxW(WinGui.mainWnd, errMsg, Dialog.appName, WinApi.MB_ICONERROR)
	END ShowErrorDialog;

	PROCEDURE Start* (name: ARRAY OF CHAR); (* deprecated; better use Dialog.RunExternal *)
		VAR info: WinApi.STARTUPINFOW; process: WinApi.PROCESS_INFORMATION;
	BEGIN
		(* res := WinApi.WinExec(name, WinApi.SW_NORMAL) *)
		WinApi.GetStartupInfoW(info);
		info.wShowWindow := WinApi.SW_NORMAL;
		IF WinApi.CreateProcessW(NIL, name, NIL, NIL, WinApi.FALSE, {}, 0, NIL, info, process) = 0 THEN
			ShowErrorDialog(WinApi.GetLastError(), name, "%2", "%3", runExtErr + name + 0DX + 0DX)
		END
	END Start;

	PROCEDURE OpenExternal (lpParameter: INTEGER): INTEGER;	(* runs in a thread *)
		VAR
			fileNamePtr: CallNamePtr; fileName, fileName2: CallName;
			sxinfo: WinApi.SHELLEXECUTEINFOW; error: INTEGER;
	BEGIN
		fileNamePtr := SYSTEM.VAL(CallNamePtr, lpParameter);
		fileName := fileNamePtr$;
		callNameTaken := TRUE;
		sxinfo.cbSize := SIZE(WinApi.SHELLEXECUTEINFOW);
		sxinfo.fMask := {10}; (* SEE_MASK_DEFAULT + SEE_MASK_FLAG_NO_UI *)
		sxinfo.hwnd := 0;
		sxinfo.lpVerb := "open";
		sxinfo.lpFile := fileName;
		sxinfo.lpParameters := NIL;
		sxinfo.lpDirectory := NIL;
		sxinfo.nShow := WinApi.SW_SHOW;
		IF WinApi.ShellExecuteExW(sxinfo) = 1 THEN RETURN 0 END;
		error := WinApi.GetLastError();
		IF Dialog.isWine & (error = 0) & (sxinfo.hInstApp <= 32) THEN	(* work around a bug in wine: error can be 0 *)
			error := WinApi.ERROR_NO_ASSOCIATION
		END;
		IF Dialog.isWine & (error = WinApi.ERROR_NO_ASSOCIATION) THEN (* retry with Linux desktop associations *)
			fileName2 := "winebrowser " + fileName;
			sxinfo.lpFile := fileName2;
			IF WinApi.ShellExecuteExW(sxinfo) = 1 THEN RETURN 0 END
			(* use original (error, fileName) for error reporting*)
		END;
		ShowErrorDialog(error, fileName, "%2", "%3", openExtErr + fileName + 0DX + 0DX);
		RETURN 0
	END OpenExternal;

	PROCEDURE (h: ExtCallHook) OpenExternal* (IN fileName: ARRAY OF CHAR);
		VAR t: WinApi.HANDLE; res: WinApi.BOOL;
	BEGIN
		callNameTaken := FALSE;
		t := WinApi.CreateThread(NIL, 1024 * 64, OpenExternal, SYSTEM.ADR(fileName), {}, NIL);
		IF t # 0 THEN
			WHILE ~callNameTaken DO
				WinApi.Sleep(1)
			END;
			res := WinApi.CloseHandle(t)
		END
	END OpenExternal;
	
	PROCEDURE RunExternal (lpParameter: INTEGER): INTEGER;	(* runs in a thread *)
		VAR
			exeNamePtr: CallNamePtr; exeName: CallName; opts: SET;
			pos: INTEGER; info: WinApi.STARTUPINFOW; process: WinApi.PROCESS_INFORMATION;
			cmd: ARRAY 12 OF CHAR;
	BEGIN
		exeNamePtr := SYSTEM.VAL(CallNamePtr, lpParameter);
		exeName := exeNamePtr$;
		WinApi.GetStartupInfoW(info);
		IF hideExtRunWindow THEN
			info.wShowWindow := WinApi.SW_HIDE;
			opts := WinApi.CREATE_NO_WINDOW
		ELSE
			info.wShowWindow := WinApi.SW_NORMAL;
			opts := {}
		END;
		callNameTaken := TRUE;
		IF Dialog.isWine THEN (* work around a bug in wine 'cmd': requires a console to be already open *)
			WHILE exeName[0] = " " DO Strings.Extract(exeName, 1, LEN(exeName$) - 1, exeName) END; (* trim left *)
			Strings.Find(exeName, " ", 0, pos);
			IF pos > 0 THEN Strings.Extract(exeName, 0, MIN(pos, 8), cmd)
			ELSE Strings.Extract(exeName, 0, 8, cmd)
			END;
			Strings.ToUpper(cmd, cmd);
			IF (cmd = "CMD") OR (cmd = "CMD.EXE") THEN exeName := "wineconsole " + exeName END
		END;
		IF WinApi.CreateProcessW(NIL, exeName, NIL, NIL, WinApi.FALSE, opts, 0, NIL, info, process) = 0 THEN
			ShowErrorDialog(WinApi.GetLastError(), exeName, "%2", "%3", runExtErr + exeName + 0DX + 0DX)
		END;
		RETURN 0
	END RunExternal;

	PROCEDURE (h: ExtCallHook) RunExternal* (IN exeName: ARRAY OF CHAR);
		VAR t: WinApi.HANDLE; res: WinApi.BOOL;
	BEGIN
		callNameTaken := FALSE;
		t := WinApi.CreateThread(NIL, 1024 * 64, RunExternal, SYSTEM.ADR(exeName), {}, NIL);
		IF t # 0 THEN
			WHILE ~callNameTaken DO
				WinApi.Sleep(1)
			END;
			hideExtRunWindow := FALSE;
			res := WinApi.CloseHandle(t)
		END
	END RunExternal;
	
	PROCEDURE (h: HostLocHook) GetLocPath (loc: Files.Locator; OUT path: ARRAY OF CHAR);
	BEGIN
		IF loc # NIL THEN
			WITH loc: HostFiles.Locator DO
				path := loc.path$; loc.res := 0
			ELSE
				path := ""; loc.res := 1 (* не получен *)
			END
		ELSE
			path := ""
		END
	END GetLocPath;

	PROCEDURE (h: HostLocHook) CheckHostLoc (loc: Files.Locator);
	BEGIN
		IF loc # NIL THEN
			IF loc IS HostFiles.Locator THEN loc.res := 0 ELSE loc.res := 1 END
		END
	END CheckHostLoc;
	
	PROCEDURE (h: BeepHook) Beep;
	VAR res: INTEGER;
	BEGIN
		res := WinApi.MessageBeep(BITS(-1))
	END Beep;

	(* initialization *)

	PROCEDURE InitDialogs;
		VAR i: INTEGER;
	BEGIN
		(* file dialog data structure *)
		fn.lStructSize := SIZE(WinDlg.OPENFILENAMEW);
		fn.hInstance := 0;
		fn.lpstrCustomFilter := NIL;
		fn.lpstrFilter := filter; fn.nFilterIndex := 1;
		i := WinApi.GetCurrentDirectoryW(LEN(actualPath), actualPath);
		fn.lpstrInitialDir := actualPath;
		fn.lpstrTitle := NIL; fn.lpstrFileTitle := NIL;
		fn.lpstrFile := fname; fn.nMaxFile := LEN(fname);
		fn.lpstrDefExt := ftype; ftype := "$";
		fn.lpfnHook := NIL;
		(* print dialog data structure *)
		prt.lStructSize := SIZE(WinDlg.PRINTDLGW);
		prt.hDevMode := 0;
		prt.hDevNames := 0;
		prt.nMinPage := 1;
		prt.nMaxPage := -1;
		(* font dialog data structure *)
		font.lStructSize := SIZE(WinDlg.CHOOSEFONTW);
		font.hDC := 0;
		font.lpLogFont := SYSTEM.VAL(WinApi.PtrLOGFONTW, SYSTEM.ADR(logFont));
		font.rgbColors := 0;
		font.hInstance := 0;
		font.lpszStyle := NIL;
		font.nFontType := 2000H;	(* screenFontType *)
		logFont.lfWidth := 0;
		logFont.lfEscapement := 0;
		logFont.lfOrientation := 0;
		logFont.lfCharSet := 0X;
		logFont.lfOutPrecision := 0X;
		logFont.lfClipPrecision := 0X;
		logFont.lfQuality := 0X;
		logFont.lfPitchAndFamily := 0X;
		(* color dialog data structure *)
		col.lStructSize := SIZE(WinDlg.CHOOSECOLORW);
		col.lpCustColors := customColors;
		col.Flags := {0};	(* rgbInit *)
		i := 0;
		WHILE i < 16 DO customColors[i] := 0FFFFFFH; INC(i) END
	END InitDialogs;
	
	
	PROCEDURE Init*;
		VAR n, v, res: INTEGER; b: BOOLEAN;
			getSpecHook: GetSpecHook;
			languageHook: LanguageHook;
			extCallHook: ExtCallHook;
			beepHook: BeepHook;
			hostLocHook: HostLocHook;
			printHook: PrintHook;
			dialogGetHook: DialogGetHook;
			dialogGetParHook: DialogGetParHook;
			
	BEGIN
		NEW(all);
		v := WinApi.GetVersion();
		osVersion := v MOD 256 * 100 + v DIV 256 MOD 256;
		(*
		IF v >= 0 THEN
			IF osVersion < 400 THEN Dialog.platform := Dialog.windowsNT3
			ELSIF osVersion < 500 THEN Dialog.platform := Dialog.windowsNT4
			ELSIF osVersion = 500 THEN Dialog.platform := Dialog.windows2000
			ELSIF osVersion < 600 THEN Dialog.platform := Dialog.windowsXP
			ELSIF osVersion = 600 THEN Dialog.platform := Dialog.windowsVista
			ELSIF osVersion = 601 THEN Dialog.platform := Dialog.windows7
			ELSE Dialog.platform := Dialog.windows8
			END
		ELSE Dialog.platform := Dialog.windows98
		END;
		*)
		Dialog.platform := Dialog.windows;
		Dialog.isWine := IsWine();
		
		HostEnv.GetPar("PAR", Dialog.commandLinePars, res);
		
		NEW(dialogGetHook); Dialog.SetGetHook(dialogGetHook);
		NEW(dialogGetParHook); Dialog.SetGetParHook(dialogGetParHook);
		NEW(getSpecHook);
			Views.SetGetSpecHook(getSpecHook); 
			StdCmds.SetGetSpecHook(getSpecHook);
		StdCmds.stdDirtyHook := DirtyHook; 
		NEW(languageHook); Dialog.SetLanguageHook(languageHook); 
		NEW(extCallHook); Dialog.SetExtCallHook(extCallHook);
		NEW(printHook); StdDialog.SetPrintHook(printHook);
		NEW(beepHook); Dialog.SetBeepHook(beepHook);
		NEW(hostLocHook); Dialog.SetHostLocHook(hostLocHook);
		
		HostFiles.MapParamString := Dialog.MapParamString;
		Dialog.ResetLanguage;
		Dialog.MapString("#System:openExtErr", openExtErr);
		Dialog.MapString("#System:runExtErr", runExtErr);
		hideExtRunWindow := FALSE;
		
		StdRegistry.ReadBool("VisualScroll", b, res); visualScroll := (res # 0) OR b;
		
		StdDialog.platformPrefOk  := PrefOk;
		StdDialog.platformInitPrefDialog  := InitPrefDialog;
		
	END Init;

BEGIN
	Init;
	InitDialogs
END WinDialog.
