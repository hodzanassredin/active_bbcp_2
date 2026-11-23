MODULE StdFilesBrowser;
(**
	project	= "BlackBox 2.0"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Ivan Denisov, Ketmar Dark (k8)"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	IMPORT
		Files, Stores, Models, Sequencers, Views, Fonts,
		Dialog, Properties, Converters, Ports, Controllers, Controls,
		Librarian, StdDialog, StdScrollers;

	CONST
		moduleName = "StdFilesBrowser";
	
		(* for standard files browser *)
		undefined = -2; back = -1;
		location = 1; open = 2; save = 3;
		itemHeight = 5 * Ports.mm;
		closedConvList = 6 * Ports.mm;
		expandedConvList = 25 * Ports.mm;

		(* char codes *)
		CtrlPageUp = 10X;
		CtrlPageDown = 11X;
		PageUp = 12X;
		PageDown = 13X;
		HomeKey = 14X;
		EndKey = 15X;
		CtrlHome = 16X;
		
		UpArrow = 1EX;
		DownArrow = 1FX;
		
		Enter = 0DX;
		Esc = 1BX;
		Backspace = 08X;
		Tab = 09X;

		filesTableTopMargin = Ports.mm;
		
	TYPE
		DialogStdGetHook = POINTER TO RECORD (Dialog.StdGetHook) END;
		
		(* for standard files browser *)
		
		Context = POINTER TO RECORD (Models.Context)
			base: FilesBrowser;
			view: Views.View;
			l, t, r, b: INTEGER;
			wrapped: BOOLEAN
		END;

		FilesBrowser = POINTER TO RECORD (Views.View)
			action: Dialog.GetAction;
			font: Fonts.Font;
			files, places, converters, confirmAddButton, 
				confirmButton, cancelButton, addButton,
				pathField, nameField, fNameField: Context;
			focusView: Views.View;
			margin, convertersHeight: INTEGER;
			type: BYTE;
			convList: Converters.Converter;
			converter: Converters.Converter;
			focusLocInfoNum, focusFileInfoNum: INTEGER;
			activeAddFolder: BOOLEAN;
			initTypes: POINTER TO ARRAY OF Files.Type;
			(* k8:HACK! for some reason, "go up" with `MakeVisible()` doesn't work; force it in repaint with this autoresetflag *)
			forceMakeCurrFVisible: BOOLEAN
		END;

		PlaceActionProc = PROCEDURE (v: FilesBrowserPlaces);

		Place = POINTER TO RECORD
			name: Files.Name;
			loc: Files.Locator;
			next: Place
		END;

		FilesBrowserPlaces = POINTER TO RECORD (Views.View)
			base: FilesBrowser
		END;

		FilesBrowserTable = POINTER TO RECORD (Views.View)
			base: FilesBrowser
		END;

		FilesBrowserConverters = POINTER TO RECORD (Views.View)
			base: FilesBrowser
		END;

		CloseNotifier  =  POINTER TO RECORD (Sequencers.Notifier) END;
		
		PathItem = POINTER TO RECORD
			name: Files.Name;
			loc: Files.Locator;
			up: PathItem
		END;

		Param* = POINTER TO RECORD	
			bg*,
			bgCursorUpDir*,
			bgCursorDir*,
			bgCursorFile*,
			bgActivePlace*,
			bgActiveConverter*,
			text*, textCursor*: Ports.Color;
			upDirStr*, rootDirStr*: ARRAY 8 OF CHAR;
			dialogWidth, dialogHeight: INTEGER;
			InitDefaults-, Init-: PROCEDURE (par: ANYPTR)
		END;
		
	VAR
		browser-: FilesBrowser;
		showHidden*, preventCleanup: BOOLEAN;
		name*: Files.Name;
		newFolderName*: Files.Name;  (* to create new folder *)
		places*: Place; (* left fast access panel *)
		param*: Param;
		currentPath: PathItem;
		path- : ARRAY 1024 OF CHAR;
		
		(* remember last directory for next dialogs *)
		lastLoc: Files.Locator;
		lastPath: PathItem;

	PROCEDURE UpdatePath;
		VAR item: PathItem;
	BEGIN
		IF currentPath # NIL THEN
			path := "";
			item := currentPath;
			WHILE item # NIL DO
				IF path = "" THEN
					IF item.up = NIL THEN
						path := item.name$ + ":\"
					ELSE
						path := item.name$
					END
				ELSIF item.up = NIL THEN
					path := item.name$ + ":\" + path
				ELSE
					path := item.name$ + "\" + path
				END;
				item := item.up
			END;
			Dialog.UpdateString(path)
		END
	END UpdatePath;

	PROCEDURE IsHidden (item: ANYPTR): BOOLEAN;
	VAR hidden: BOOLEAN; name: Files.Name; i: INTEGER;
	BEGIN
		hidden := TRUE;
		IF browser # NIL THEN
			WITH item: Files.LocInfo DO
				name := item.name;
				hidden := ~showHidden & (name[0] = ".")
			| item: Files.FileInfo DO
				name := item.name;
				hidden := ~showHidden & (name[0] = ".");
				IF ~hidden THEN
					IF (browser.converter # NIL) THEN
						hidden := (item.type = "") OR (browser.converter.fileType # item.type)
					ELSE
						IF (browser.initTypes # NIL) & (browser.initTypes[0] # "*") THEN
							hidden := TRUE;
							IF (item.type # "") THEN
								FOR i := 0 TO LEN(browser.initTypes) - 1 DO
									IF item.type = browser.initTypes[i] THEN
										hidden := FALSE
									END
								END
							END
						END
					END
				END
			ELSE END
		END;
		RETURN hidden
	END IsHidden;

	PROCEDURE CountLocs (nav: FilesBrowser): INTEGER;
	VAR
		locInfo: Files.LocInfo;
		locs: INTEGER;
	BEGIN
		locs := 0;
		locInfo := Files.dir.LocList(nav.action.loc);
		WHILE locInfo # NIL DO
			IF ~IsHidden(locInfo) THEN INC(locs) END;
			locInfo := locInfo.next
		END;
		RETURN locs
	END CountLocs;

	PROCEDURE CountLocsFiles (nav: FilesBrowser; OUT locs, files: INTEGER);
	VAR locInfo: Files.LocInfo; fileInfo: Files.FileInfo; 
	BEGIN
		locs := 0;
		locInfo := Files.dir.LocList(nav.action.loc);
		WHILE locInfo # NIL DO
			IF ~IsHidden(locInfo) THEN
				INC(locs)
			END;
			locInfo := locInfo.next
		END;
		files := 0;
		fileInfo := Files.dir.FileList(nav.action.loc);
		WHILE fileInfo # NIL DO
			IF ~IsHidden(fileInfo) THEN
				INC(files)
			END;
			fileInfo := fileInfo.next
		END
	END CountLocsFiles;

	PROCEDURE LookupNextDirForChar (nav: FilesBrowser; ch: CHAR; fidx: INTEGER): INTEGER;
	VAR
		info: Files.LocInfo;
		res, idx: INTEGER;
	BEGIN
		res := -1; ch := CAP(ch); idx := 0;
		info := Files.dir.LocList(nav.action.loc);
		WHILE (res < 0) & (info # NIL) DO
			IF ~IsHidden(info) THEN
				IF (idx > fidx) & (CAP(info.name[0]) = ch) THEN res := idx END;
				INC(idx)
			END;
			info := info.next
		END;
		RETURN res
	END LookupNextDirForChar;

	PROCEDURE LookupNextFileForChar (nav: FilesBrowser; ch: CHAR; fidx: INTEGER): INTEGER;
	VAR
		info: Files.FileInfo;
		res, idx: INTEGER;
	BEGIN
		res := -1; ch := CAP(ch); idx := 0;
		info := Files.dir.FileList(nav.action.loc);
		WHILE (res < 0) & (info # NIL) DO
			IF ~IsHidden(info) THEN
				IF (idx > fidx) & (CAP(info.name[0]) = ch) THEN res := idx END;
				INC(idx)
			END;
			info := info.next
		END;
		RETURN res
	END LookupNextFileForChar;

	(* return `FALSE` if not found *)
	PROCEDURE LookupNextItemForChar (nav: FilesBrowser; ch: CHAR; OUT loc, file: INTEGER): BOOLEAN;
		VAR idx: INTEGER; isdir: BOOLEAN;
	BEGIN
		idx := -1; isdir := FALSE;
		IF (nav.focusLocInfoNum # undefined) OR
			 ((nav.focusLocInfoNum = undefined) & (nav.focusFileInfoNum = undefined))
		THEN
			(* look into dirs *)
			idx := LookupNextDirForChar(nav, ch, nav.focusLocInfoNum);
			isdir := TRUE
		END;
		(* look into files *)
		IF idx < 0 THEN
			idx := LookupNextFileForChar(nav, ch, nav.focusFileInfoNum);
			isdir := FALSE
		END;
		(* and again *)
		IF idx < 0 THEN idx := LookupNextDirForChar(nav, ch, -1); isdir := TRUE END;
		IF idx < 0 THEN idx := LookupNextFileForChar(nav, ch, -1); isdir := FALSE END;
		(* setup results *)
		IF idx >= 0 THEN
			IF isdir THEN loc := idx; file := undefined ELSE loc := undefined; file := idx END
		END;
		RETURN idx >= 0
	END LookupNextItemForChar;

	PROCEDURE LocNum (loc: Files.Locator; IN dname: ARRAY OF CHAR): INTEGER;
		VAR res, n, y: INTEGER; locInfo: Files.LocInfo;
	BEGIN
		res := undefined;
		IF dname[0] # 0X THEN
			locInfo := Files.dir.LocList(loc);
			n := 0; y := itemHeight * 2;
			WHILE (locInfo # NIL) & (locInfo.name # dname) DO
				IF ~IsHidden(locInfo) THEN
					INC(y, itemHeight);
					INC(n)
				END;
				locInfo := locInfo.next
			END
		END;
		IF locInfo # NIL THEN
			res := n
		END;
		RETURN res
	END LocNum;

	PROCEDURE GoToLoc (v: FilesBrowser; loc: Files.Locator; select: INTEGER);
		VAR locs: INTEGER;
	BEGIN
		v.action.loc := loc;
		locs := CountLocs(v);
		v.focusFileInfoNum := undefined;
		IF locs > 0 THEN
			IF select # undefined THEN
				v.focusLocInfoNum := select
			ELSE
				v.focusLocInfoNum := 0
			END
		ELSE
			v.focusLocInfoNum := undefined
		END;
		v.forceMakeCurrFVisible := TRUE;
		UpdatePath;
		Views.Update(v, Views.rebuildFrames)
	END GoToLoc;

	PROCEDURE GoOneDirUp (v: FilesBrowser);
	BEGIN
		IF (currentPath # NIL) & (currentPath.up # NIL) THEN
			GoToLoc(v, currentPath.up.loc,
				LocNum(currentPath.up.loc, currentPath.name$));
			currentPath := currentPath.up;
			UpdatePath
		END;
	END GoOneDirUp;

	PROCEDURE (c: Context) ThisModel (): Models.Model;
	BEGIN RETURN NIL
	END ThisModel;

	PROCEDURE (c: Context) GetSize (OUT w, h: INTEGER);
	BEGIN
		IF c.wrapped THEN
			w := c.r - c.l;
			h := 300 * Ports.mm
		ELSE
			w := c.r - c.l;
			h := c.b - c.t
		END
	END GetSize;

	PROCEDURE (c: Context) Consider (VAR p: Models.Proposal);
	BEGIN
		c.base.context.Consider(p)
	END Consider;

	PROCEDURE (c: Context) Normalize (): BOOLEAN;
	BEGIN
		RETURN c.base.context.Normalize()
	END Normalize;

	PROCEDURE NewContext (v: Views.View; base: FilesBrowser): Context;
		VAR c: Context;
	BEGIN
		ASSERT(v # NIL, 21);
		NEW(c); c.view := v; c.base := base;
		c.wrapped := FALSE;
		v.InitContext(c); Stores.Join(v, base);
		RETURN c
	END NewContext;

	(* get spec dialogs *)

	PROCEDURE BrowserConfirm*;
		VAR action: Dialog.GetAction; locInfo: Files.LocInfo; n: INTEGER;
	BEGIN
		IF (browser # NIL)
			& ~((browser.type = open) &
				(browser.focusFileInfoNum = undefined)) THEN
			
			IF browser.type = location THEN
				browser.action.name := "";
				IF browser.focusLocInfoNum > back THEN
					locInfo := Files.dir.LocList(browser.action.loc);
					n := 0;
					WHILE (n < browser.focusLocInfoNum) & (locInfo # NIL) DO
						IF ~IsHidden(locInfo) THEN
							INC(n)
						END;
						locInfo := locInfo.next
					END;
					IF locInfo # NIL THEN
						browser.action.loc := browser.action.loc.This(locInfo.name)
					END
				END
			ELSE
				browser.action.name := name$
			END;
			
			IF browser.converter # NIL THEN
				IF browser.type = save THEN
					browser.action.converterName := browser.converter.exp
				ELSIF browser.type = open THEN
					browser.action.converterName := browser.converter.imp
				END;
				browser.action.type :=  browser.converter.fileType$
			ELSE
				browser.action.converterName := "";
				browser.action.type := ""
			END;
			
			browser.action.cancel := FALSE;
			lastLoc := browser.action.loc;
			lastPath := currentPath;
			action := browser.action;
			browser := NIL;
			action.Do;
		END
	END BrowserConfirm;

	PROCEDURE BrowserConfirmGuard* (VAR par: Dialog.Par);
	BEGIN
		IF browser # NIL THEN
			par.disabled := FALSE;
			CASE browser.type OF
			| open:
				par.disabled := (browser.focusFileInfoNum = undefined)
			| save:
				par.disabled := (name$ = "")
			| location:
				par.disabled := (browser.focusLocInfoNum < -1)
			ELSE END
		END
	END BrowserConfirmGuard;

	PROCEDURE BrowserCancel*;
	BEGIN
		IF browser # NIL THEN
			browser.action.cancel := TRUE;
			browser.action.Do;
			browser := NIL
		END
	END BrowserCancel;

	PROCEDURE BrowserNewFolderDialog*;
	BEGIN
		IF browser # NIL THEN
			newFolderName := "";
			browser.activeAddFolder := TRUE;
			browser.focusView := browser.fNameField.view;
			Views.Update(browser, Views.rebuildFrames)
		END
	END BrowserNewFolderDialog;

	PROCEDURE CloseAddFolder;
	BEGIN
		IF browser # NIL THEN
			newFolderName := "";
			browser.focusView := browser.files.view;
			browser.activeAddFolder := FALSE;
			Views.Update(browser, Views.rebuildFrames)
		END
	END CloseAddFolder;

	PROCEDURE BrowserCreateFolderGuard* (VAR par: Dialog.Par);
	VAR
		locInfo: Files.LocInfo;
	BEGIN
		IF browser # NIL THEN
			par.disabled := (newFolderName$ = "");
			(* check if folder already exists *)
			IF ~par.disabled THEN
				locInfo := Files.dir.LocList(browser.action.loc);
				WHILE (locInfo # NIL) & (locInfo.name # newFolderName) DO
					locInfo := locInfo.next
				END;
				par.disabled := (locInfo # NIL)
			END
		END
	END BrowserCreateFolderGuard;

	PROCEDURE BrowserCreateFolder*;
		VAR file: Files.File; res, n, y: INTEGER; loc: Files.Locator; locInfo: Files.LocInfo;
	BEGIN
		IF (browser # NIL) & (newFolderName$ # "") THEN
			(* check if folder already exists *)
			locInfo := Files.dir.LocList(browser.action.loc);
			WHILE (locInfo # NIL) & (locInfo.name # newFolderName) DO
				locInfo := locInfo.next
			END;
			IF locInfo = NIL THEN
				(* create new folder by making temp file *)
				loc := browser.action.loc.This(newFolderName);
				IF loc.res = 0 THEN
					file := Files.dir.New(loc, Files.dontAsk);
					IF loc.res = 0 THEN
						file.Register('.temp', '', Files.dontAsk, res);
						Files.dir.Delete(loc, ".temp")
					ELSE
						Dialog.ShowMsg("error")
					END
				ELSE
					Dialog.ShowMsg("error")
				END
			END;
			
			(* select folder *)
			locInfo := Files.dir.LocList(browser.action.loc);
			n := 0; y := itemHeight * 2;
			WHILE (locInfo # NIL) & (locInfo.name # newFolderName) DO
				IF ~IsHidden(locInfo) THEN
					INC(y, itemHeight);
					INC(n)
				END;
				locInfo := locInfo.next
			END;
			IF locInfo # NIL THEN
				browser.focusFileInfoNum := undefined;
				browser.focusLocInfoNum := n;
				(* assign newFolderName to name
					like it should be after selection by click or arrow *)
				name := newFolderName; 
			END;

			Views.Update(browser, Views.rebuildFrames);
			StdScrollers.WrappedView(browser.files.view).context.MakeVisible(0, y, 0, y + itemHeight);
			browser.focusView := browser.files.view;
			browser.activeAddFolder := FALSE
		END
	END BrowserCreateFolder;


	PROCEDURE PreventCleanup*;
	BEGIN
		preventCleanup := TRUE
	END PreventCleanup;

	PROCEDURE (c: CloseNotifier) Notify (VAR msg: Sequencers.Message);
	BEGIN
		IF ~preventCleanup THEN
			browser := NIL
		END
	END Notify;

	PROCEDURE GetFirstLocInfo (): Files.LocInfo;
		VAR locInfo: Files.LocInfo;
	BEGIN
		IF ~showHidden THEN
			locInfo := Files.dir.LocList(browser.action.loc);
			WHILE (locInfo # NIL) & IsHidden(locInfo) DO
				locInfo := locInfo.next
			END
		ELSE
			locInfo := Files.dir.LocList(browser.action.loc)
		END;
		RETURN locInfo
	END GetFirstLocInfo;

	PROCEDURE GetFirstFileInfo (): Files.FileInfo;
		VAR fileInfo: Files.FileInfo; 
	BEGIN
		IF ~showHidden THEN
			fileInfo := Files.dir.FileList(browser.action.loc);
			WHILE (fileInfo # NIL) & IsHidden(fileInfo) DO
				fileInfo := fileInfo.next
			END
		ELSE
			fileInfo := Files.dir.FileList(browser.action.loc)
		END;
		RETURN fileInfo
	END GetFirstFileInfo;

	PROCEDURE (v: FilesBrowser) IsConvOpen (): BOOLEAN, NEW;
	BEGIN
		RETURN (v.convertersHeight # closedConvList)
	END IsConvOpen;

	PROCEDURE (v: FilesBrowser) ConvOpen, NEW;
	BEGIN
		IF ~v.IsConvOpen() & (v.convList.next # NIL) THEN
			v.forceMakeCurrFVisible := TRUE;
			v.convertersHeight := expandedConvList;
			v.focusView := v.converters.view;
			Views.Update(v, Views.rebuildFrames)
		END
	END ConvOpen;

	PROCEDURE (v: FilesBrowser) ConvClose, NEW;
	BEGIN
		IF v.IsConvOpen() THEN
			v.forceMakeCurrFVisible := TRUE;
			v.convertersHeight := closedConvList;
			v.focusView := v.files.view;
			Views.Update(v, Views.rebuildFrames)
			(*StdScrollers.WrappedView(v.files.view).context.MakeVisible(0, 0, 1, 1)*)
		END
	END ConvClose;

	PROCEDURE (v: FilesBrowser) IsGoodConv (cc: Converters.Converter): BOOLEAN, NEW;
		VAR res: BOOLEAN;
	BEGIN
		res := FALSE;
		IF cc # NIL THEN
			IF (v.type = open) & (cc.imp # "") THEN res := TRUE
			ELSIF (v.type = save) & (cc.exp # "") THEN res := TRUE
			END
		END;
		RETURN res
	END IsGoodConv;

	PROCEDURE (v: FilesBrowser) ConvResetFileCursor, NEW;
		VAR locs: INTEGER;
	BEGIN
		IF v.focusFileInfoNum # undefined THEN
			v.focusFileInfoNum := undefined;
			locs := CountLocs(v);
			IF locs > 0 THEN v.focusLocInfoNum := locs - 1
			ELSE v.focusLocInfoNum := undefined
			END
		END;
		v.forceMakeCurrFVisible := TRUE
	END ConvResetFileCursor;

	PROCEDURE RecalcLayout (v: FilesBrowser; w, h: INTEGER);
		VAR
			sep, leftTitlesWidth, w1, w2, nameShift: INTEGER;
			nameStr, locatorStr: Dialog.String;
			c: Context;
	BEGIN
		Dialog.MapString("#Std:Name:", nameStr);
		w1 := v.font.StringWidth(nameStr);
		Dialog.MapString("#Std:Locator:", locatorStr);
		w2 := v.font.StringWidth(locatorStr);
		leftTitlesWidth := MAX(w1, w2) + v.margin * 2;

		sep := w DIV 3;

		(* name input field *)
		IF v.nameField # NIL THEN
			c := v.nameField;
			c.l := v.margin + leftTitlesWidth;
			c.t := v.margin;
			c.r := w - v.margin;
			c.b := v.margin + 7 * Ports.mm;

			nameShift := 7 * Ports.mm + v.margin
		END;

		IF v.fNameField # NIL THEN
			IF v.activeAddFolder THEN
				(* path and "add folder" button *)
				c := v.pathField;
				c.l := v.margin + leftTitlesWidth;
				c.t := v.margin + nameShift;
				c.r := w - 70 * Ports.mm - v.margin * 3;
				c.b := v.margin + nameShift + 7 * Ports.mm;

				c := v.fNameField;
				c.l := w - 70 * Ports.mm - 2 * v.margin;
				c.t := v.margin + nameShift;
				c.r := w - 20 * Ports.mm - 2 * v.margin;
				c.b := v.margin + nameShift + 7 * Ports.mm;

				c := v.confirmAddButton;
				c.l := w - 20 * Ports.mm - v.margin;
				c.t := v.margin + nameShift;
				c.r := w - v.margin;
				c.b := v.margin + nameShift + 7 * Ports.mm
			ELSE
				(* path only *)
				c := v.pathField;
				c.l := v.margin + leftTitlesWidth;
				c.t := v.margin + nameShift;
				c.r := w - 30 * Ports.mm - v.margin * 2;
				c.b := v.margin + nameShift + 7 * Ports.mm;

				c := v.addButton;
				c.l := w - 30 * Ports.mm - v.margin;
				c.t := v.margin + nameShift;
				c.r := w - v.margin;
				c.b := v.margin + nameShift + 7 * Ports.mm
			END;
			nameShift := nameShift + 7 * Ports.mm + v.margin
		ELSE
			(* path only *)
			c := v.pathField;
			c.l := v.margin + leftTitlesWidth;
			c.t := v.margin;
			c.r := w - v.margin;
			c.b := v.margin + 7 * Ports.mm;

			nameShift := v.margin + 7 * Ports.mm
		END;

		c := v.places;
		c.l := v.margin;
		c.t := v.margin + nameShift;
		c.r := sep - v.margin;
		c.b := h - v.margin - 10 * Ports.mm;

		IF v.converters # NIL THEN
			c := v.files;
			c.l := sep + v.margin;
			c.t := v.margin + nameShift;
			c.r := w - v.margin;
			c.b := h - v.margin - 10 * Ports.mm - (v.convertersHeight + v.margin);

			c := v.converters;
			c.l := sep + v.margin;
			c.t := h - v.margin - 10 * Ports.mm - (v.convertersHeight);
			c.r := w - v.margin;
			c.b := h - v.margin - 10 * Ports.mm
		ELSE
			c := v.files;
			c.l := sep + v.margin;
			c.t := v.margin + nameShift;
			c.r := w - v.margin;
			c.b := h - v.margin - 10 * Ports.mm
		END;

		c := v.confirmButton;
		c.l := w - v.margin - 74 * Ports.mm;
		c.t := h - v.margin - 8 * Ports.mm;
		c.r := w - v.margin - 38 * Ports.mm;
		c.b := h - v.margin;

		c := v.cancelButton;
		c.l := w - v.margin - 36 * Ports.mm;
		c.t := h - v.margin - 8 * Ports.mm;
		c.r := w - v.margin;
		c.b := h - v.margin
	END RecalcLayout;

	PROCEDURE (v: FilesBrowser) Restore (f: Views.Frame; l_, t_, r_, b_: INTEGER);
		VAR
			c: Context; conv: Converters.Converter;
			d, w, h, asc, dsc, w_, h_, locs, files: INTEGER; 
			nameStr, locatorStr: Dialog.String; removeFrame: Views.Frame; 
	BEGIN
		IF browser = NIL THEN RETURN END;
		
		v.context.GetSize(w, h); 
		f.DrawRect(0, 0, w, h, Ports.fill, Ports.dialogBackground);
		
		RecalcLayout(v, w, h);
		
		v.font.GetBounds(asc, dsc, w_);

		(* draw name input field *)
		IF v.nameField # NIL THEN
			c := v.nameField;
			Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE);
			Dialog.MapString("#Std:Name:", nameStr);
			f.DrawString(v.margin+Ports.mm, c.t+(c.b-c.t) DIV 2 + asc DIV 2 - Ports.mm DIV 2, 
				Ports.black, nameStr, v.font)
		END;

		(* draw locator label *)
		c := v.pathField;
		Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE);
		Dialog.MapString("#Std:Locator:", locatorStr);
		f.DrawString(v.margin + Ports.mm, c.t+(c.b-c.t) DIV 2 + asc DIV 2 - Ports.mm DIV 2, Ports.black, locatorStr, v.font);

		(* draw places *)
		c := v.places;
		Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE);
		
		(* draw files table *)
		c := v.files;
		c.view.context.GetSize(w_, h);
		StdScrollers.WrappedView(c.view).context.GetSize(w, h_);
		CountLocsFiles(v, locs, files);
		IF v.type = location THEN
			asc := (locs + 2) * itemHeight
		ELSE
			asc := (locs + files + 2) * itemHeight
		END;
		IF asc > h THEN h := asc END;
		StdScrollers.WrappedView(c.view).context.SetSize(w_, h);
		Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE);

		(* draw file types *)
		IF v.converters # NIL THEN
			c := v.converters;
			c.view.context.GetSize(w_, h);
			StdScrollers.WrappedView(c.view).context.GetSize(w, h_);
			IF ~v.IsConvOpen() THEN
				h := closedConvList
			ELSE
				asc := itemHeight * 2;
				conv := v.convList;
				WHILE conv # NIL DO
					IF v.IsGoodConv(conv) THEN INC(asc, itemHeight) END;
					conv := conv.next
				END;
				IF asc > h THEN h := asc END
			END;
			StdScrollers.WrappedView(c.view).context.SetSize(w_, h);
			Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE)
		END;

		(* draw buttons *)
		c := v.confirmButton;
		Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE);
		c := v.cancelButton;
		Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE);

		(* add new direcrory button *)
		IF v.activeAddFolder THEN
			IF v.addButton # NIL THEN
				removeFrame := Views.ThisFrame(f, v.addButton.view);
				IF removeFrame # NIL THEN Views.RemoveFrame(f, removeFrame) END
			END;
			IF v.fNameField # NIL THEN
				c := v.fNameField;
				Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE)
			END;
			IF v.confirmAddButton # NIL THEN
				c := v.confirmAddButton;
				Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE)
			END
		ELSE
			IF v.fNameField # NIL THEN
				removeFrame := Views.ThisFrame(f, v.fNameField.view);
				IF removeFrame # NIL THEN Views.RemoveFrame(f, removeFrame) END
			END;
			IF v.confirmAddButton # NIL THEN
				removeFrame := Views.ThisFrame(f, v.confirmAddButton.view);
				IF removeFrame # NIL THEN Views.RemoveFrame(f, removeFrame) END
			END;
			IF v.addButton # NIL THEN
				c := v.addButton;
				Views.InstallFrame(f, c.view, c.l, c.t, 0, TRUE)
			END
		END;
		
		(* draw frame around the subviews *)
		d := f.dot;
		IF v.places # NIL THEN
			c := v.places;
			IF (c.t - d < c.b + d) & (c.l - d < c.r + d) THEN
				IF v.focusView = c.view THEN
					f.DrawRect(c.l - d, c.t - d, c.r + d, c.b + d, 2 * f.dot, Ports.black)
				ELSE
					f.DrawRect(c.l - d, c.t - d, c.r + d, c.b + d, f.dot, Ports.black)
				END
			END
		END;
		
		IF v.files # NIL THEN
			c := v.files; 
			IF (c.t - d < c.b + d) & (c.l - d < c.r + d) THEN
				IF v.focusView = c.view THEN
					f.DrawRect(c.l - d, c.t - d, c.r + d, c.b + d, 2 * f.dot, Ports.black)
				ELSE
					f.DrawRect(c.l - d, c.t - d, c.r + d, c.b + d, f.dot, Ports.black)
				END
			END
		END;

		IF v.converters # NIL THEN
			c := v.converters;
			IF (c.t - d < c.b + d) & (c.l - d < c.r + d) THEN
				IF v.focusView = c.view THEN
					f.DrawRect(c.l - d, c.t - d, c.r + d, c.b + d, 2 * f.dot, Ports.black)
				ELSE
					f.DrawRect(c.l - d, c.t - d, c.r + d, c.b + d, f.dot, Ports.black)
				END
			END
		END
	END Restore;


	PROCEDURE (v: FilesBrowser) HandleCtrlMsg (f: Views.Frame;
										VAR msg: Controllers.Message; VAR focus: Views.View);
		VAR g: Views.Frame; res_: INTEGER; eaten: BOOLEAN;

		(* foward edit msgs to focused view *)
		PROCEDURE PassMessage;
		BEGIN
			IF v.focusView # NIL THEN
				IF (v.addButton # NIL) & (v.focusView = v.addButton.view) THEN focus := v.files.view
				ELSE focus := v.focusView
				END
			END
		END PassMessage;

		(* k8: sorry for this mess! *)
		PROCEDURE DoTabNext;
		BEGIN
			IF v.focusView = NIL THEN
				eaten := TRUE;
				IF v.nameField # NIL THEN v.focusView := v.nameField.view
				ELSE v.focusView := v.files.view
				END
			ELSIF (v.nameField # NIL) & (v.focusView = v.nameField.view) THEN
				eaten := TRUE;
				IF v.addButton # NIL THEN v.focusView := v.addButton.view
				ELSE v.focusView := v.files.view
				END
			ELSIF (v.addButton # NIL) & (v.focusView = v.addButton.view) THEN
				eaten := TRUE;
				v.focusView := v.files.view
			ELSIF (v.fNameField # NIL) & (v.focusView = v.fNameField.view) THEN
				eaten := TRUE;
				IF v.confirmAddButton # NIL THEN
					v.focusView := v.confirmAddButton.view
				END
			ELSIF (v.confirmAddButton # NIL) & (v.focusView = v.confirmAddButton.view) THEN
				eaten := TRUE;
				IF v.fNameField # NIL THEN
					v.focusView := v.fNameField.view
				END
			ELSIF v.focusView = v.files.view THEN
				IF v.nameField # NIL THEN v.focusView := v.nameField.view; eaten := TRUE END
			END
		END DoTabNext;

		(* k8: sorry for this mess! *)
		PROCEDURE DoTabPrev;
		BEGIN
			IF v.focusView = NIL THEN
				eaten := TRUE;
				IF v.nameField # NIL THEN v.focusView := v.nameField.view
				ELSE v.focusView := v.files.view
				END
			ELSIF (v.nameField # NIL) & (v.focusView = v.nameField.view) THEN
				eaten := TRUE;
				v.focusView := v.files.view
			ELSIF (v.addButton # NIL) & (v.focusView = v.addButton.view) THEN
				eaten := TRUE;
				IF v.nameField # NIL THEN v.focusView := v.nameField.view
				ELSE v.focusView := v.files.view
				END
			ELSIF (v.fNameField # NIL) & (v.focusView = v.fNameField.view) THEN
				eaten := TRUE;
				IF v.confirmAddButton # NIL THEN
					v.focusView := v.confirmAddButton.view
				END
			ELSIF (v.confirmAddButton # NIL) & (v.focusView = v.confirmAddButton.view) THEN
				eaten := TRUE;
				IF v.fNameField # NIL THEN
					v.focusView := v.fNameField.view
				END
			ELSIF v.focusView = v.files.view THEN
				IF v.nameField # NIL THEN v.focusView := v.nameField.view; eaten := TRUE END
			END
		END DoTabPrev;

	BEGIN
		WITH msg: Controllers.TrackMsg DO
			g := Views.FrameAt(f, msg.x, msg.y);
			IF g # NIL THEN
				focus := g.view;
				v.focusView := g.view
			END
		| msg: Controllers.CursorMessage DO
			g := Views.FrameAt(f, msg.x, msg.y);
			IF g # NIL THEN focus := g.view END
		| msg: Controllers.EditMsg DO
			eaten := FALSE;
			IF (msg.op = Controllers.pasteChar) & (msg.char = Tab) &
					(msg.modifiers*{Controllers.extend,Controllers.modify,Controllers.pick} # {}) THEN
				DoTabPrev
			ELSIF (msg.op = Controllers.pasteChar) & (msg.modifiers = {}) THEN
				CASE msg.char OF
				| Enter:
					IF (v.nameField # NIL) & (v.focusView = v.nameField.view) THEN
						eaten := TRUE;
						PreventCleanup;
						Dialog.Call("StdCmds.CloseTopDialog", "", res_);
						BrowserConfirm
					ELSIF (v.fNameField # NIL) & (v.focusView = v.fNameField.view) THEN
						eaten := TRUE;
						BrowserCreateFolder
					END
				| Esc:
					IF (v.fNameField # NIL) & (v.focusView = v.fNameField.view) THEN
						eaten := TRUE;
						CloseAddFolder
					ELSIF ~v.IsConvOpen() THEN
						eaten := TRUE;
						BrowserCancel;
						Dialog.Call("StdCmds.CloseTopDialog", "", res_)
					END
				| Tab:
					DoTabNext
				ELSE END
			END;
			IF eaten THEN
				Views.Update(v, Views.rebuildFrames(*keepFrames*))
			ELSE
				PassMessage
			END
		| msg: Controllers.Message DO
			IF msg IS Controllers.ScrollMsg THEN
				msg(Controllers.ScrollMsg).done := TRUE;
			ELSIF (v.nameField # NIL) & (v.focusView = v.nameField.view) THEN
				focus := v.focusView
			END
		ELSE END
	END HandleCtrlMsg;

	PROCEDURE AddItemToPath (loc: Files.Locator; name: Files.Name);
		VAR item: PathItem;
	BEGIN
		IF currentPath = NIL THEN
			NEW(currentPath);
			IF loc # NIL THEN
				currentPath.loc := loc;
			ELSE
				currentPath.loc := Files.dir.This(name)
			END;
			currentPath.name := name$;
			currentPath.up := NIL;
		ELSE
			NEW(item);
			item.up := currentPath;
			IF loc # NIL THEN
				item.loc := loc;
			ELSE
				item.loc := Files.dir.This(name);
			END;
			item.name := name$;
			currentPath := item
		END
	END AddItemToPath;

	(* places table *)

	PROCEDURE (v: FilesBrowserPlaces) Restore (f: Views.Frame; l_, t_, r_, b_: INTEGER);
		VAR x, y: INTEGER; str: Dialog.String; p: Place;
	BEGIN
		v.context.GetSize(x, y);
		f.DrawRect(0, 0, x, y, Ports.fill, param.bg);
		x := 5 * Ports.mm;
		y := itemHeight + filesTableTopMargin * 2;
		p := places;
		WHILE p # NIL DO
			Dialog.MapString("#Std:" + p.name, str);
			IF browser.action.loc = p.loc THEN
				f.DrawRect(
					x - filesTableTopMargin * 2,
					y - itemHeight,
					x + browser.font.StringWidth(str) + filesTableTopMargin * 2,
					y + filesTableTopMargin * 2,
					Ports.fill, param.bgActivePlace)
			END;
			f.DrawString(x, y, Ports.black, str, v.base.font);
			INC(y, itemHeight + filesTableTopMargin * 2);
			p := p.next
		END
	END Restore;

	PROCEDURE (v: FilesBrowserPlaces) HandleCtrlMsg (f_: Views.Frame;
								VAR msg: Controllers.Message; VAR focus: Views.View);
		VAR pos: INTEGER; p: Place;
	BEGIN
		WITH msg: Controllers.TrackMsg DO
			pos := itemHeight + filesTableTopMargin * 2;
			p := places;
			WHILE (p # NIL) & (msg.y > pos + filesTableTopMargin * 2) DO
				INC(pos, itemHeight + filesTableTopMargin * 2);
				p := p.next
			END;
			IF p # NIL THEN
				v.base.action.loc := p.loc;
				v.base.focusLocInfoNum := back;
				v.base.focusFileInfoNum := undefined;
				currentPath := NIL;
				AddItemToPath(p.loc, p.name$);
				UpdatePath;
				StdScrollers.WrappedView(v.base.files.view).context.MakeVisible(0, 0, 1, 1);
				Views.Update(v.base.files.view, Views.rebuildFrames(*keepFrames*))
			END
		ELSE END
	END HandleCtrlMsg;


	(* converters table *)

	PROCEDURE (v: FilesBrowserConverters) Restore (f: Views.Frame; l_, t_, r, b_: INTEGER);
		VAR x, y, n, i, color: INTEGER; str: Dialog.String; conv: Converters.Converter;

		PROCEDURE MapConv (IN cmd: ARRAY OF CHAR; VAR name: ARRAY OF CHAR);
			VAR i: INTEGER; sub, str: Dialog.String;
		BEGIN
			str := cmd$; i := 0;
			WHILE str[i] >= "0" DO INC(i) END;
			str[i] := 0X;
			Librarian.lib.SplitName(str, sub, str);
			Dialog.MapString("#" + sub + ":" + cmd, name)
		END MapConv;

	BEGIN
		v.context.GetSize(x, y);
		IF ~v.base.IsConvOpen() THEN
			IF  v.base.convList.next # NIL THEN
				f.DrawRect(0, 0, x, y, Ports.fill, param.bg)
			ELSE
				f.DrawRect(0, 0, x, y, Ports.fill, Ports.grey12)
			END;
			x := 3 * Ports.mm; y := 4 * Ports.mm + Ports.mm DIV 2;
			IF v.base.converter # NIL THEN
				IF v.base.type = open THEN
					MapConv(v.base.converter.imp, str)
				ELSIF  browser.type = save THEN
					MapConv(v.base.converter.exp, str)
				END;
				f.DrawString(x, y, param.text, str+" (*."+v.base.converter.fileType+")", v.base.font)
			ELSIF (v.base.initTypes = NIL) OR (v.base.initTypes[0] = "*") THEN
				Dialog.MapString("#System:AllFiles", str);
				f.DrawString(x, y, param.text, str + " (*.*)", v.base.font)
			ELSE
				Dialog.MapString("#System:AllSupportedFiles", str);
				str := str + " (";
				FOR i := 0 TO LEN(v.base.initTypes) - 1 DO
					str := str + "*." + v.base.initTypes[i] + ","
				END;
				str[LEN(str$)-1] := 0X; (* remove last comma *)
				str := str + ")";
				f.DrawString(x, y, param.text, str, v.base.font)
			END
		ELSE
			f.DrawRect(0, 0, x, y, Ports.fill, param.bg);
			x := 3 * Ports.mm; y := 5 * Ports.mm;
			conv := v.base.convList;
			n := 0;
			WHILE conv # NIL DO
				str := "";
				IF v.base.type = open THEN
					MapConv(conv.imp, str)
				ELSIF v.base.type = save THEN
					MapConv(conv.exp, str)
				END;
				IF str # "" THEN
					IF conv = v.base.converter THEN
						f.DrawRect(
							x - Ports.mm, y - (itemHeight - filesTableTopMargin),
							r - Ports.mm * 2, y + filesTableTopMargin,
							Ports.fill, param.bgActiveConverter);
							color := param.textCursor
					ELSE
						color := param.text
					END;
					f.DrawString(x, y, color, str + " (*." + conv.fileType + ")", v.base.font);
					INC(y, itemHeight)
				END;
				conv := conv.next;
				INC(n)
			END;
			IF (n > 1) & (v.base.type = open) THEN
				IF v.base.converter = NIL THEN
					f.DrawRect(
						x - Ports.mm, y - (itemHeight - filesTableTopMargin),
						r - Ports.mm * 2, y + filesTableTopMargin,
						Ports.fill, param.bgActiveConverter);
					color := param.textCursor
				ELSE
					color := param.text
				END;
				IF (v.base.initTypes = NIL) OR (v.base.initTypes[0] = "*") THEN
					Dialog.MapString("#System:AllFiles", str);
					f.DrawString(x, y, color, str + " (*.*)", v.base.font)
				ELSE
					Dialog.MapString("#System:AllSupportedFiles", str);
					str := str + " (";
					FOR i := 0 TO LEN(v.base.initTypes) - 1 DO
						str := str + "*." + v.base.initTypes[i] + ","
					END;
					str[LEN(str$)-1] := 0X; (* remove last comma *)
					str := str + ")";
					f.DrawString(x, y, color, str, v.base.font)
				END
			END
		END
	END Restore;

	PROCEDURE (v: FilesBrowserConverters) HandleCtrlMsg (f_: Views.Frame;
								VAR msg: Controllers.Message; VAR focus: Views.View);
	VAR
		okconv: Converters.Converter;
		forceSet: BOOLEAN;

		PROCEDURE MakeCurrConvVisible;
		VAR
			conv: Converters.Converter;
			y: INTEGER;
			done: BOOLEAN;
		BEGIN
			done := FALSE;
			y := 5 * Ports.mm;
			conv :=  v.base.convList;
			WHILE ~done & (conv # NIL) DO
				IF conv = v.base.converter THEN done := TRUE
				ELSE
					IF v.base.IsGoodConv(conv) THEN INC(y, itemHeight) END;
					conv := conv.next
				END
			END;
			IF (conv = NIL) & (v.base.type = open) THEN done := TRUE END;
			IF done THEN
				DEC(y, itemHeight - Ports.mm);
				v.context.MakeVisible(0, y, 1, y+itemHeight-1)
			END
		END MakeCurrConvVisible;

		PROCEDURE DoMouseSelect (y: INTEGER);
		VAR
			conv: Converters.Converter;
			h: INTEGER;
		BEGIN
			conv :=  v.base.convList;
			h := itemHeight;
			WHILE (conv # NIL) & (h < y) DO
				conv := conv.next;
				IF v.base.IsGoodConv(conv) THEN INC(h, itemHeight) END
			END;
			IF (conv # NIL) OR (v.base.type = open) THEN 
				v.base.converter := conv;
				IF conv # NIL THEN v.base.action.type := conv.fileType$ END;
				v.base.ConvClose
			END
		END DoMouseSelect;

		PROCEDURE GoUp;
		VAR
			conv: Converters.Converter;
		BEGIN
			conv := v.base.convList;
			WHILE (conv # NIL) & (conv # v.base.converter) DO
				IF v.base.IsGoodConv(conv) THEN okconv := conv END;
				conv := conv.next
			END
		END GoUp;

		PROCEDURE GoDown;
		VAR
			conv: Converters.Converter;
		BEGIN
			conv := v.base.converter;
			WHILE (okconv = NIL) & (conv # NIL) & (conv.next # NIL) DO
				conv := conv.next;
				IF v.base.IsGoodConv(conv) THEN okconv := conv END
			END;
			IF (okconv = NIL) & (v.base.type = open) THEN forceSet := TRUE END
		END GoDown;

		PROCEDURE GoHome;
		VAR
			conv: Converters.Converter;
		BEGIN
			conv := v.base.convList;
			WHILE (okconv = NIL) & (conv # NIL) DO
				IF v.base.IsGoodConv(conv) THEN okconv := conv END;
				conv := conv.next
			END;
			IF (okconv # NIL) & (okconv = v.base.converter) THEN okconv := NIL END
		END GoHome;

		PROCEDURE GoEnd;
		VAR
			conv: Converters.Converter;
		BEGIN
			IF v.base.type = open THEN
				okconv := NIL; forceSet := (v.base.converter # NIL)
			ELSE
				conv := v.base.convList;
				WHILE (conv # NIL) DO
					IF v.base.IsGoodConv(conv) THEN okconv := conv END;
					conv := conv.next
				END;
				IF (okconv # NIL) & (okconv = v.base.converter) THEN okconv := NIL END
			END
		END GoEnd;

	BEGIN
		WITH msg: Controllers.ScrollMsg DO v.base.ConvOpen
		| msg: Controllers.TrackMsg DO
			IF ~v.base.IsConvOpen() THEN
				v.base.ConvOpen
			ELSE
				DoMouseSelect(msg.y)
			END
		| msg: Controllers.EditMsg DO
			okconv := NIL; forceSet := FALSE;
			IF (msg.op = Controllers.pasteChar) & (msg.modifiers = {}) THEN
				CASE msg.char OF
					| Enter, Esc: v.base.ConvClose
					| UpArrow: GoUp
					| DownArrow: GoDown
					| HomeKey: GoHome
					| EndKey: GoEnd
				ELSE END;
				IF forceSet OR ((okconv # NIL) & (okconv # v.base.converter)) THEN
					v.base.converter := okconv;
					IF okconv # NIL THEN v.base.action.type := okconv.fileType$ END;
					MakeCurrConvVisible;
					v.base.ConvResetFileCursor;
					Views.Update(v.base, Views.rebuildFrames)
				END
			END
		ELSE END
	END HandleCtrlMsg;


	(* files table *)

	PROCEDURE (v: FilesBrowserTable) GetBackground (VAR color: Ports.Color);
	BEGIN
		color := param.bg
	END GetBackground;

	PROCEDURE (v: FilesBrowserTable) Restore (f: Views.Frame; l_, t_, r, b_: INTEGER);
		VAR locInfo: Files.LocInfo; fileInfo: Files.FileInfo; x, y, n, visy, color: INTEGER;
	BEGIN
		visy := -1024;
		x := Ports.mm * 5;
		y := itemHeight;
		
		IF v.base.focusLocInfoNum = back THEN
			visy := y;
			f.DrawRect(
				x - Ports.mm, y - (itemHeight - filesTableTopMargin),
				r - Ports.mm * 2, y + filesTableTopMargin,
				Ports.fill, param.bgCursorUpDir);
			color := param.textCursor
		ELSE
			color := param.text
		END;
		
		IF currentPath.up = NIL THEN
			f.DrawString(x, y, Ports.black, param.rootDirStr, v.base.font);
		ELSE
			f.DrawString(x, y, Ports.black, param.upDirStr, v.base.font);
		END;
		INC(y, itemHeight);
		
		(* draw dirs *)
		locInfo := Files.dir.LocList(v.base.action.loc);
		n := 0;
		WHILE locInfo # NIL DO
			IF ~IsHidden(locInfo) THEN
				IF n = v.base.focusLocInfoNum THEN
					(* selected dir background *)
					visy := y;
					f.DrawRect(
						x - Ports.mm,
						y - (itemHeight - filesTableTopMargin),
						r - Ports.mm * 2,
						y + filesTableTopMargin,
						Ports.fill, param.bgCursorDir);
					color := param.textCursor
				ELSE
					color := param.text
				END;
				
				(* folder icon *)
				f.DrawRect(x, y - 12 * f.dot, x + 12 * f.dot, y - 9 * f.dot, Ports.fill, Ports.grey50);
				f.DrawRect(x, y - 9 * f.dot, x + 14 * f.dot, y + 2 * f.dot, Ports.fill, Ports.grey75);
				
				(* folder name *)
				f.DrawString(x + 16 * f.dot, y, color, locInfo.name, v.base.font);
				INC(y, itemHeight);
				INC(n)
			END;
			locInfo := locInfo.next
		END;
		
		(* draw files *)
		IF v.base.type # location THEN
			fileInfo := Files.dir.FileList(v.base.action.loc);
			n := 0;
			WHILE fileInfo # NIL DO
				IF ~IsHidden(fileInfo) THEN
					IF n = v.base.focusFileInfoNum THEN
						(* selected file background *)
						visy := y;
						f.DrawRect(
							x - Ports.mm, y - (itemHeight - filesTableTopMargin),
							r - Ports.mm * 2, y + filesTableTopMargin,
							Ports.fill, param.bgCursorFile);
						color := param.textCursor
					ELSE
						color := param.text
					END;
					f.DrawString(x, y, color, fileInfo.name, v.base.font);
					INC(y, itemHeight);
					INC(n)
				END;
				fileInfo := fileInfo.next
			END
		END;

		(* k8: i'm not sure if it is legal to do this here, but meh... it seems to work *)
		IF v.base.forceMakeCurrFVisible THEN
			v.base.forceMakeCurrFVisible := FALSE;
			IF visy >= 0 THEN
				v.context.MakeVisible(0, visy - (itemHeight - filesTableTopMargin), 1, visy + itemHeight)
			END
		END
	END Restore;


	PROCEDURE (v: FilesBrowserTable) HandleCtrlMsg (f_: Views.Frame;
										VAR msg: Controllers.Message; VAR focus: Views.View);
		VAR
			locInfo: Files.LocInfo; fileInfo: Files.FileInfo; tmp: Files.Name;
			y, n, locs, files, w, h: INTEGER;

		PROCEDURE OpenItem (msgy: INTEGER);
			VAR res_: INTEGER;
		BEGIN
			IF v.base.type # save THEN
				name := ""
			END;
			IF (currentPath # NIL) & (msgy < itemHeight + filesTableTopMargin) THEN
				GoOneDirUp(v.base)
			ELSE
				y := itemHeight * 2 + filesTableTopMargin;
				locInfo := GetFirstLocInfo();
				WHILE (locInfo # NIL) & (y < msgy) DO
					IF ~IsHidden(locInfo) THEN
						INC(y, itemHeight)
					END;
					locInfo := locInfo.next
				END;
				IF locInfo # NIL THEN
					IF (v.base.type IN {save, open})
						OR (Files.dir.LocList(v.base.action.loc.This(locInfo.name)) # NIL) THEN
						v.base.focusLocInfoNum := back;
						v.base.focusFileInfoNum := undefined;
						v.base.action.loc := v.base.action.loc.This(locInfo.name);
						
						AddItemToPath(v.base.action.loc, locInfo.name$);
						UpdatePath
					END
				ELSIF v.base.type # location THEN
					(* open file *)
					n := 0; tmp := ""; DEC(y, itemHeight);
					fileInfo := GetFirstFileInfo();
					WHILE (fileInfo # NIL) & (y < msgy) DO
						IF ~IsHidden(fileInfo) THEN
							INC(y, itemHeight);
							INC(n);
							tmp := fileInfo.name
						END;
						fileInfo := fileInfo.next
					END;
					IF n # 0 THEN
						IF (v.base.type = open) OR (tmp # "") THEN
							name := tmp;
							PreventCleanup;
							Dialog.Call("StdCmds.CloseTopDialog", "", res_);
							BrowserConfirm
						END
					END
				END
			END
		END OpenItem;

		PROCEDURE MakeItemVisible;
		BEGIN
			IF v.base.focusLocInfoNum < 0 THEN
				IF v.base.focusFileInfoNum < 0 THEN
					v.context.MakeVisible(0, 0, 1, 1)
				ELSE
					v.context.MakeVisible(
						0, (locs + (v.base.focusFileInfoNum+1))*itemHeight, 
						1, (locs + (v.base.focusFileInfoNum+2))*itemHeight+itemHeight DIV 2)
				END
			ELSE
				v.context.MakeVisible(
					0, (v.base.focusLocInfoNum+1)*itemHeight, 
					1, (v.base.focusLocInfoNum+2)*itemHeight + itemHeight DIV 2)
			END
		END MakeItemVisible;

		PROCEDURE CursorMoved;
		BEGIN
			UpdatePath;
			Views.Update(v.base.files.view, Views.rebuildFrames(*keepFrames*));
			MakeItemVisible
		END CursorMoved;

		PROCEDURE MoveUpDown (delta: INTEGER);
		BEGIN
			CountLocsFiles(v.base, locs, files);
			WHILE delta # 0 DO
				IF delta < 0 THEN
					IF v.base.focusFileInfoNum > 0 THEN
						DEC(v.base.focusFileInfoNum)
					ELSE
						v.base.focusFileInfoNum := undefined;
						IF v.base.focusLocInfoNum = undefined THEN
							IF locs = 0 THEN
								v.base.focusLocInfoNum := back
							ELSE
								v.base.focusLocInfoNum := locs - 1
							END
						ELSIF v.base.focusLocInfoNum > back THEN
							DEC(v.base.focusLocInfoNum)
						END
					END;
					INC(delta)
				ELSE
					IF (v.base.focusFileInfoNum = undefined) & (v.base.focusLocInfoNum < locs-1) THEN
						INC(v.base.focusLocInfoNum)
					ELSIF (v.base.type # location) & (files > 0) THEN
						v.base.focusLocInfoNum := undefined;
						IF v.base.focusFileInfoNum < files THEN
							IF v.base.focusFileInfoNum = undefined THEN
								v.base.focusFileInfoNum := 0
							ELSE
								IF v.base.focusFileInfoNum < files - 1 THEN
									INC(v.base.focusFileInfoNum)
								END
							END
						END
					END;
					DEC(delta)
				END
			END;
			CursorMoved
		END MoveUpDown;

		PROCEDURE MoveHome;
		BEGIN
			CountLocsFiles(v.base, locs, files);
			IF v.base.focusFileInfoNum > 0 THEN
				v.base.focusFileInfoNum := 0
			ELSIF v.base.focusFileInfoNum = 0 THEN
				IF locs > 0 THEN
					v.base.focusFileInfoNum := undefined;
					v.base.focusLocInfoNum := 0
				END
			ELSIF v.base.focusLocInfoNum > 0 THEN
				v.base.focusFileInfoNum := undefined;
				v.base.focusLocInfoNum := 0
			ELSIF v.base.focusLocInfoNum = 0 THEN
				IF files > 0 THEN
					v.base.focusFileInfoNum := 0;
					v.base.focusLocInfoNum := undefined
				END
			END;
			CursorMoved
		END MoveHome;

		PROCEDURE MoveEnd;
		BEGIN
			CountLocsFiles(v.base, locs, files);
			IF (files > 0) & (v.base.focusFileInfoNum # undefined) &(v.base.focusFileInfoNum < files-1) THEN
				v.base.focusFileInfoNum := files-1
			ELSIF (files > 0) & (v.base.focusFileInfoNum = files-1) THEN
				IF locs > 0 THEN
					v.base.focusFileInfoNum := undefined;
					v.base.focusLocInfoNum := locs-1
				END
			ELSIF (locs > 0) & (v.base.focusLocInfoNum # undefined) & (v.base.focusLocInfoNum < locs-1) THEN
				v.base.focusFileInfoNum := undefined;
				v.base.focusLocInfoNum := locs-1
			ELSIF (locs > 0) & (v.base.focusLocInfoNum = locs-1) THEN
				IF files > 0 THEN
					v.base.focusFileInfoNum := files-1;
					v.base.focusLocInfoNum := undefined
				END
			END;
			CursorMoved
		END MoveEnd;

		PROCEDURE MovePageUp;
		BEGIN
			v.base.files.view.context.GetSize(w, h);
			MoveUpDown(-(h DIV itemHeight))
		END MovePageUp;

		PROCEDURE MovePageDown;
		BEGIN
			v.base.files.view.context.GetSize(w, h);
			MoveUpDown(h DIV itemHeight)
		END MovePageDown;

		PROCEDURE DoAcceptDirDown (): BOOLEAN;
		VAR
			res: BOOLEAN;
		BEGIN
			res := FALSE;
			IF v.base.focusLocInfoNum = back THEN
				res := TRUE;
				GoOneDirUp(v.base)
			ELSIF v.base.focusLocInfoNum # undefined THEN
				res := TRUE;
				locInfo := GetFirstLocInfo();
				n := 0;
				WHILE (locInfo # NIL) & (n < v.base.focusLocInfoNum) DO
					IF ~IsHidden(locInfo) THEN INC(n) END;
					locInfo := locInfo.next
				END;
				IF locInfo # NIL THEN
					IF (v.base.type IN {save, open})
						OR (Files.dir.LocList(v.base.action.loc.This(locInfo.name)) # NIL)
					THEN
						v.base.focusLocInfoNum := back;
						v.base.focusFileInfoNum := undefined;
						v.base.action.loc := v.base.action.loc.This(locInfo.name);
						AddItemToPath(v.base.action.loc, locInfo.name$);
						UpdatePath;
						Views.Update(v.base.files.view, Views.rebuildFrames(*keepFrames*));
						v.context.MakeVisible(0, 0, 1, 1)
					END
				END
			END;
			RETURN res
		END DoAcceptDirDown;

		PROCEDURE DoAccept;
		VAR
			n: INTEGER;
			res_: INTEGER;
		BEGIN
			IF ~DoAcceptDirDown() & (v.base.focusFileInfoNum # undefined) THEN
				fileInfo := GetFirstFileInfo();
				n := 0;
				WHILE (fileInfo # NIL) & (n <= v.base.focusFileInfoNum) DO
					IF ~IsHidden(fileInfo) THEN
						INC(n);
						name := fileInfo.name$
					END;
					fileInfo := fileInfo.next
				END;
				IF name # "" THEN
					PreventCleanup;
					Dialog.Call("StdCmds.CloseTopDialog", "", res_);
					BrowserConfirm
				END
			END
		END DoAccept;

		PROCEDURE LookupNext (ch: CHAR);
		VAR
			l, f: INTEGER;
		BEGIN
			IF LookupNextItemForChar(v.base, ch, l, f) THEN
				v.base.focusLocInfoNum := l;
				v.base.focusFileInfoNum := f;
				CountLocsFiles(v.base, locs, files); (* requred for `MakeItemVisible()` *)
				CursorMoved
			END
		END LookupNext;

	BEGIN
		WITH msg: Controllers.ScrollMsg DO
				v.base.ConvClose
		| msg: Controllers.TrackMsg DO
			IF Controllers.doubleClick IN msg.modifiers THEN
				OpenItem(msg.y);
				IF browser # NIL THEN
					UpdatePath;
					Views.Update(v.base.files.view, Views.rebuildFrames(*keepFrames*));
					v.context.MakeVisible(0, 0, 1, 1)
				END
			ELSE
				IF msg.y < itemHeight + filesTableTopMargin THEN
					v.base.focusLocInfoNum := back;
					v.base.focusFileInfoNum := undefined
				ELSE
					(* search focus folder name *)
					locInfo := GetFirstLocInfo();
					y := itemHeight * 2 + filesTableTopMargin;
					n := 0;
					WHILE (locInfo # NIL) & (y < msg.y) DO
						IF ~IsHidden(locInfo) THEN
							INC(y, itemHeight);
							INC(n)
						END;
						locInfo := locInfo.next
					END;
					IF locInfo # NIL THEN
						IF v.base.type = location THEN (* store folder name to use later *)
							name := locInfo.name$
						END;
						v.base.focusFileInfoNum := undefined;
						v.base.focusLocInfoNum := n
					ELSIF v.base.type # location THEN
						v.base.focusLocInfoNum := undefined;
						(* search focus file name *)
						fileInfo := GetFirstFileInfo();
						n := 0; tmp := "";
						DEC(y, itemHeight);
						WHILE (fileInfo # NIL) & (y < msg.y) DO
							IF ~IsHidden(fileInfo) THEN
								INC(y, itemHeight);
								INC(n);
								tmp := fileInfo.name
							END;
							fileInfo := fileInfo.next
						END;
						IF n = 0 THEN
							v.base.focusFileInfoNum := undefined
						ELSE
							name := tmp;
							v.base.focusFileInfoNum := n - 1
						END
					END
				END;
				Views.Update(v.base.files.view, Views.rebuildFrames(*keepFrames*))
			END;
			v.base.ConvClose
		| msg: Controllers.EditMsg DO
			IF msg.op = Controllers.pasteChar THEN
				(*
					shift	24	extend	1	Shift key
					ctrl	25	modify	2	Ctrl key
					alt	28	pick	4	Alt key
				*)
				IF Controllers.pick IN msg.modifiers THEN (* Alt *)
					IF msg.char = DownArrow THEN v.base.ConvOpen END
				ELSIF msg.modifiers*{0,1,2,3,4,5,6,7} = {} THEN
					CASE msg.char OF
					| CtrlPageUp: GoOneDirUp(v.base)
					| CtrlPageDown: IF DoAcceptDirDown() THEN END
					| CtrlHome:
						IF places # NIL THEN
							currentPath := NIL;
							AddItemToPath(places.loc, places.name);
							GoToLoc(v.base, places.loc, undefined)
						END
					| PageUp: MovePageUp
					| PageDown: MovePageDown
					| HomeKey: MoveHome
					| EndKey: MoveEnd
					| UpArrow: MoveUpDown(-1)
					| DownArrow: MoveUpDown(1)
					| Backspace: GoOneDirUp(v.base)
					| Enter: IF msg.modifiers = {} THEN DoAccept END
					ELSE (* process letters and digits *)
						IF msg.modifiers = {} THEN
							IF (msg.char >= 020X) & (msg.char <= 07EX) THEN
								LookupNext(msg.char)
							END
						END
					END
				END
			END
		ELSE END
	END HandleCtrlMsg;

	PROCEDURE (v: FilesBrowser) HandlePropMsg (VAR msg: Properties.Message);
	BEGIN
		WITH msg: Properties.SizePref DO
			IF (msg.w = Views.undefined) OR (msg.h = Views.undefined) THEN
				msg.w := param.dialogWidth;
				msg.h := param.dialogHeight
			END
		| msg: Properties.FocusPref DO
			msg.setFocus := TRUE
		ELSE
		END
	END HandlePropMsg;

	PROCEDURE (v: FilesBrowser) ConsiderFocusRequestBy (view: Views.View);
	BEGIN
		IF view = v.places.view THEN
			v.focusView := v.places.view
		ELSIF view = v.files.view THEN
			v.focusView := v.files.view
		ELSE
			HALT(20)
		END
	END ConsiderFocusRequestBy;


	PROCEDURE (v: FilesBrowserTable) CopyFromSimpleView- (source: Views.View);
	BEGIN
		WITH source: FilesBrowserTable DO
			v.base := source.base;
			v.base.focusLocInfoNum := source.base.focusLocInfoNum
		END
	END CopyFromSimpleView;

	PROCEDURE (v: FilesBrowserPlaces) CopyFromSimpleView- (source: Views.View);
	BEGIN
		WITH source: FilesBrowserPlaces DO
			v.base := source.base
		END
	END CopyFromSimpleView;

	PROCEDURE (v: FilesBrowserConverters) CopyFromSimpleView- (source: Views.View);
	BEGIN
		WITH source: FilesBrowserConverters DO
			v.base := source.base
		END
	END CopyFromSimpleView;

	PROCEDURE SameLoc (loc1, loc2: Files.Locator): BOOLEAN;
		VAR path1, path2: Files.Name;
	BEGIN
		Dialog.GetLocPath(loc1, path1);
		Dialog.GetLocPath(loc2, path2);
		RETURN path1 = path2
	END SameLoc;

	(* common code to create various browsers *)
	PROCEDURE CreateBrowserFormCommon (navtype: BYTE; action: Dialog.GetAction; IN svstr, dlgstr: Dialog.String);
	VAR
		nav: FilesBrowser;
		w1: FilesBrowserPlaces; w2: FilesBrowserTable; w3: FilesBrowserConverters;
		context2: Context; title: Dialog.String;
		tf: Fonts.Typeface; size, weight, pos, pos2, n: INTEGER; style: SET;
		p: Controls.Prop; cNotif: CloseNotifier; conv, conv2: Converters.Converter;
		type: ARRAY 16 OF CHAR; pathName: Files.Name;
		locs, files: INTEGER; tmpPlace: Place;
	BEGIN
		IF param # NIL THEN
			param.InitDefaults(param)
		END;
		
		NEW(nav);
		preventCleanup := FALSE;
		nav.type := navtype;
		nav.margin := 2 * Ports.mm;
		nav.action := action;
		nav.action.cancel := TRUE;

		(* build converters list; this is not required for directory browser *)
		IF navtype # location THEN
			IF action.type # "" THEN
				n := 0; pos := 0;
				WHILE action.type[pos] # 0X DO
					IF action.type[pos] = "," THEN INC(n) END;
					INC(pos)
				END;
				NEW(nav.initTypes, n+1);
				pos := -1; n := 0; 
				REPEAT
					INC(pos);
					pos2 := 0;
					WHILE (action.type[pos] # 0X) & (action.type[pos] # ",") DO
						type[pos2] := action.type[pos]; INC(pos); INC(pos2)
					END;
					type[pos2] := 0X;
					nav.initTypes[n] := type$;
					INC(n)
				UNTIL action.type[pos] = 0X
			END;

			IF action.converterName # "" THEN
				conv := Converters.list;
				WHILE (conv # NIL) & (conv.imp # action.converterName) DO
					conv := conv.next
				END
			END;
			IF conv # NIL THEN
				NEW(nav.convList);
				nav.convList^ := conv^;
				nav.convList.next := NIL;
				nav.converter := nav.convList
			ELSE
				IF action.type = "" THEN
					nav.convList := Converters.list;
					IF action.selectConverter # "" THEN
						conv := Converters.list;
						WHILE (conv # NIL) & (conv.imp # action.selectConverter) DO
							conv := conv.next
						END
					END;
					IF conv # NIL THEN
						nav.converter := conv
					ELSE
						nav.converter := Converters.list
					END
				ELSIF action.type = "*" THEN
					nav.convList := Converters.list;
					nav.converter := NIL
				ELSE
					pos := -1;
					REPEAT
						INC(pos);
						pos2 := 0;
						WHILE (action.type[pos] # 0X) & (action.type[pos] # ",") DO
							type[pos2] := action.type[pos]; INC(pos); INC(pos2)
						END;
						type[pos2] := 0X;

						conv := Converters.list;
						WHILE (conv # NIL) & ~((conv.imp # "") & (conv.fileType = type)) DO
							conv := conv.next
						END;
						IF conv # NIL THEN
							IF nav.convList = NIL THEN
								NEW(nav.convList);
								nav.convList^ := conv^;
								nav.converter := nav.convList;
								nav.convList.next := NIL
							ELSE
								conv2 := nav.convList;
								WHILE conv2.next # NIL DO conv2 := conv2.next END;
								NEW(conv2.next);
								conv2.next^ := conv^;
								conv2.next.next := NIL
							END
						END
					UNTIL action.type[pos] = 0X;

					IF nav.convList = NIL THEN
						nav.convList := Converters.list
					END
				END
			END
		END;

		nav.focusLocInfoNum := undefined;
		nav.focusFileInfoNum := undefined;
		
		Dialog.GetDialogFont(tf, size, style, weight);
		nav.font := Fonts.dir.This(tf, size, style, weight);
		
		(* places *)
		NEW(w1);
		w1.base := nav;
		nav.places := NewContext(w1, nav);

		(* file/dir list *)
		NEW(w2);
		w2.base := nav;
		context2 := NewContext(w2, nav);
		context2.wrapped := TRUE;
		nav.files := NewContext(StdScrollers.WrapView(w2), nav);

		(* converters are not required for directory browser *)
		IF navtype # location THEN
			NEW(w3);
			w3.base := nav;
			context2 := NewContext(w3, nav);
			context2.wrapped := TRUE;
			nav.converters := NewContext(StdScrollers.WrapView(w3), nav);
			nav.convertersHeight := closedConvList
		END;

		(*k8: assign it here to activate the guard early *)
		newFolderName := "";
		IF navtype = save THEN
			(* file name entry field *)
			name := action.name$
		END;
		browser := nav;

		(* "accept" button *)
		NEW(p);
		p.link := moduleName
			+ ".PreventCleanup; StdCmds.CloseTopDialog; " 
			+ moduleName + ".BrowserConfirm";
		p.label := svstr$;
		p.guard := moduleName + ".BrowserConfirmGuard"; p.notifier := "";
		p.level := 0;
		p.opt[0] := FALSE; p.opt[1] := FALSE; p.opt[2] := FALSE;
		p.opt[3] := FALSE; p.opt[4] := FALSE;
		nav.confirmButton := NewContext(Controls.dir.NewPushButton(p), nav);

		(* "cancel" button *)
		NEW(p);
		p.link := moduleName + ".BrowserCancel; StdCmds.CloseTopDialog";
		p.label := "#Std:Cancel";
		p.guard := ""; p.notifier := "";
		p.level := 0;
		p.opt[0] := FALSE; p.opt[1] := FALSE; p.opt[2] := FALSE;
		p.opt[3] := FALSE; p.opt[4] := FALSE;
		nav.cancelButton := NewContext(Controls.dir.NewPushButton(p), nav);

		IF (navtype = save) OR (navtype = location) THEN
			(* "new directory" button *)
			NEW(p);
			p.link := moduleName + ".BrowserNewFolderDialog";
			p.label := "#Std:Add folder";
			p.guard := ""; p.notifier := "";
			p.level := 0;
			p.opt[0] := FALSE; p.opt[1] := FALSE; p.opt[2] := FALSE;
			p.opt[3] := FALSE; p.opt[4] := FALSE;
			nav.addButton := NewContext(Controls.dir.NewPushButton(p), nav);

			(* "confirm new directory" button *)
			NEW(p);
			p.link := moduleName + ".BrowserCreateFolder";
			p.label := "#Std:Confirm";
			p.guard := moduleName + ".BrowserCreateFolderGuard"; p.notifier := "";
			p.level := 0;
			p.opt[0] := FALSE; p.opt[1] := FALSE; p.opt[2] := FALSE;
			p.opt[3] := FALSE; p.opt[4] := FALSE;
			nav.confirmAddButton := NewContext(Controls.dir.NewPushButton(p), nav);

			(* directory name entry field *)
			NEW(p);
			p.link := moduleName + ".newFolderName";
			p.label := "";
			p.guard := ""; p.notifier := "";
			p.level := 0;
			p.opt[0] := TRUE; p.opt[1] := FALSE; p.opt[2] := FALSE;
			p.opt[3] := FALSE; p.opt[4] := FALSE;
			nav.fNameField := NewContext(Controls.dir.NewField(p), nav)
		END;

		IF navtype = save THEN
			(* file name entry field *)
			(*name := action.name$;*)
			NEW(p);
			p.link := moduleName + ".name";
			p.label := "";
			p.guard := ""; p.notifier := "";
			p.level := 0;
			p.opt[0] := TRUE; p.opt[1] := FALSE; p.opt[2] := FALSE;
			p.opt[3] := FALSE; p.opt[4] := FALSE;
			nav.nameField := NewContext(Controls.dir.NewField(p), nav)
		END;
		
		IF action.loc = NIL THEN
			IF lastLoc # NIL THEN
				action.loc := lastLoc;
				currentPath := lastPath
			ELSIF places # NIL THEN
				action.loc := places.loc;
				currentPath := NIL;
				AddItemToPath(places.loc, places.name)
			ELSE
				action.loc := Files.dir.This("");
				currentPath := NIL;
				Dialog.MapString("#Std:Project", title);
				AddItemToPath(action.loc, pathName);
			END
		ELSE
			(* check if it exists in places *)
			tmpPlace := places;
			WHILE (tmpPlace # NIL) & ~SameLoc(tmpPlace.loc, action.loc) DO
				tmpPlace := tmpPlace.next
			END;
			IF tmpPlace # NIL THEN
				currentPath := NIL;
				AddItemToPath(tmpPlace.loc, tmpPlace.name)
			ELSE
				currentPath := NIL;
				AddItemToPath(action.loc, "Loc")
			END
		END;
		
		NEW(p);
		p.link := moduleName + ".path";
		p.label := "";
		p.guard := ""; p.notifier := "";
		p.level := 0;
		p.opt[0] := TRUE; p.opt[1] := FALSE; p.opt[2] := FALSE;
		p.opt[3] := FALSE; p.opt[4] := FALSE;
		nav.pathField := NewContext(Controls.dir.NewField(p), nav);

		CountLocsFiles(nav, locs, files);
		IF locs > 0 THEN nav.focusLocInfoNum := 0
		ELSIF files > 0 THEN nav.focusFileInfoNum := 0
		END;

		IF navtype = save THEN
			nav.focusView := nav.nameField.view
		ELSE
			nav.focusView := nav.files.view
		END;
		
		IF action.title = "" THEN
			Dialog.MapString(dlgstr$, title)
		ELSE
			Dialog.MapString(action.title, title)
		END;
		
		UpdatePath;
		
		StdDialog.Open(nav, title, NIL, "", NIL, TRUE, FALSE, FALSE, FALSE, TRUE);
			(*  asTool, asAux, noResize, allowDuplicates, neverDirty *)

		NEW(cNotif);
		nav.Domain().GetSequencer()(Sequencers.Sequencer).InstallNotifier(cNotif)
	END CreateBrowserFormCommon;


	(* select a directory *)
	PROCEDURE (hook: DialogStdGetHook) GetLoc (action: Dialog.GetAction);
	BEGIN
		IF browser = NIL THEN
			CreateBrowserFormCommon(location, action, "#Std:Choose folder", "#Std:Choose folder")
		END
	END GetLoc;

	(* open existing file *)
	PROCEDURE (hook: DialogStdGetHook) GetIntLocName (action: Dialog.GetAction);
	BEGIN
		IF browser = NIL THEN
			CreateBrowserFormCommon(open, action, "#Std:OpenFile", "#Std:Open...")
		END
	END GetIntLocName;

	(* save file with a new name *)
	PROCEDURE (hook: DialogStdGetHook) GetExtLocName (action: Dialog.GetAction);
	BEGIN
		IF browser = NIL THEN
			CreateBrowserFormCommon(save, action, "#Std:Save", "#Std:Save...")
		END
	END GetExtLocName;

	PROCEDURE AddPlace* (IN name: Dialog.String; loc: Files.Locator);
		VAR c: Place;
	BEGIN
		ASSERT(LEN(name$) > 0);
		c := places;
		WHILE (c # NIL) & (c.name$ # name$) DO c := c.next END;
		IF c = NIL THEN
			c := places;
			IF c = NIL THEN
				NEW(c);
				places := c
			ELSE
				WHILE c.next # NIL DO c := c.next END;
				NEW(c.next);
				c := c.next
			END
		END;
		c.name := name$;
		c.loc := loc;
	END AddPlace;

	PROCEDURE SetupPlaces*;
		VAR path, pathName: Dialog.String; res_: INTEGER;
	BEGIN
		Dialog.MapString("#Std:Project", pathName);
		(* use only project directory *)
		Dialog.GetLocPath(Files.dir.This(""), path);
		AddPlace(pathName, Files.dir.This(path));
		
		Dialog.MapString("#Std:Repository", pathName);
		AddPlace(pathName, Files.dir.This(""));
		
		IF Dialog.platform = Dialog.windows THEN
			Dialog.GetPar("USERPROFILE", path, res_)
		ELSE
			Dialog.GetPar("HOME", path, res_)
		END;
		Dialog.MapString("#Std:Home", pathName);
		AddPlace(pathName, Files.dir.This(path));
		
		currentPath := NIL;
		AddItemToPath(places.loc, places.name);
		UpdatePath
	END SetupPlaces;

	PROCEDURE InitParam (p: ANYPTR);
	BEGIN
		WITH p: Param DO
			
		END
	END InitParam;

	PROCEDURE ParamDefaults (p: ANYPTR); 
	BEGIN
		WITH p: Param DO
			p.bg := Ports.white;
			p.bgCursorUpDir := Ports.RGBColor(220,210,235);
			p.bgCursorDir := Ports.RGBColor(220,210,235);
			p.bgCursorFile := Ports.RGBColor(220,210,235);
			p.bgActivePlace := Ports.RGBColor(220,210,235);
			p.bgActiveConverter := Ports.RGBColor(220,210,235);
			p.text := Ports.black;
			p.textCursor := Ports.black;
			p.dialogWidth := 600 * (3 * Ports.point DIV 4);
			p.dialogHeight := 300 * (3 * Ports.point DIV 4);
			p.upDirStr := "<..>";
			p.rootDirStr := "\";
			p.Init := InitParam;
			p.InitDefaults := ParamDefaults;
		END
	END ParamDefaults;

	PROCEDURE Init;
		VAR dh: DialogStdGetHook;
	BEGIN
		NEW(dh);
		Dialog.SetStdGetHook(dh);
		browser := NIL;
		name := "";
		showHidden := FALSE;
		NEW(param); ParamDefaults(param);
		places := NIL;
		SetupPlaces;
	END Init;

BEGIN
	Init
END StdFilesBrowser.
