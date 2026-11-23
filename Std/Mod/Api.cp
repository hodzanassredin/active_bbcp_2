MODULE StdApi;
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
		Views, Files, Dialog, Converters, Windows, Sequencers, Stores, Meta, Librarian,
		Containers, StdDialog, Documents, Strings;

	VAR
		modeHook*: PROCEDURE(c: Containers.Controller); (* for setting nested forms to the container's mode *)
		
	(* Auxiliary procedures *)

	PROCEDURE CheckQualident (VAR str, mod, name: ARRAY OF CHAR);
		VAR i, j: INTEGER; ch: CHAR;
	BEGIN
		i := 0; 
		REPEAT
			ch := str[i]; mod[i] := ch; INC(i)
		UNTIL (i = LEN(str)) OR (i = LEN(mod)) OR ~Strings.IsIdent(ch);
		IF ch = "." THEN
			mod[i - 1] := 0X; j := 0;
			REPEAT
				ch := str[i]; name[j] := ch; INC(i); INC(j)
			UNTIL (i = LEN(str)) OR (j = LEN(name)) OR ~Strings.IsIdent(ch);
			IF ch # 0X THEN mod[0] := 0X; name[0] := 0X END
		ELSE mod[0] := 0X; name[0] := 0X
		END
	END CheckQualident;

	PROCEDURE FileExists (loc: Files.Locator; IN name: ARRAY OF CHAR): BOOLEAN;
		VAR fi: Files.FileInfo;
	BEGIN
		fi := Files.dir.FileList(loc);
		WHILE (fi # NIL) & (fi.name # name) DO fi := fi.next END;
		RETURN fi # NIL
	END FileExists;

	PROCEDURE PathToSpec (IN path: ARRAY OF CHAR; OUT loc: Files.Locator; OUT name: Files.Name;
											useLang: BOOLEAN);
		VAR i, j: INTEGER; ch: CHAR;
			nameUpper, fn: Files.Name; langParent: Files.Locator;
	BEGIN
		i := 0; j := 0; loc := Files.dir.This(""); langParent := NIL;
		WHILE (loc.res = 0) & (i < LEN(path) - 1) & (j < LEN(name) - 1) & (path[i] # 0X) DO
			ch := path[i]; INC(i);
			IF (j > 0) & ((ch = "/") OR (ch = "\")) THEN
				name[j] := 0X; j := 0;
				IF (loc = langParent) & (name = Dialog.defaultLanguage) THEN
					langParent := NIL (* map ".../Docu/en/..." to ".../Docu/..." *)
				ELSE
					loc := loc.This(name)
				END;
				IF useLang & (langParent = NIL) THEN Strings.ToUpper(name, nameUpper);
					IF nameUpper = "DOCU" THEN langParent := loc END
				END
			ELSE
				name[j] := ch; INC(j)
			END
		END;
		IF path[i] = 0X THEN
			name[j] := 0X;
			IF (loc = langParent) & (Dialog.language # "")
				& (Dialog.language # Dialog.defaultLanguage) THEN (* use Dialog.language *)
				loc := loc.This(Dialog.language);
				fn := name;
				Files.dir.GetFileName(fn, Files.docType, fn);

				IF ~FileExists(loc, fn) THEN
					loc := langParent
				ELSE
					loc.res := 0
				END;
			END
		ELSE loc.res := 1; name := ""
		END
	END PathToSpec;
	
	PROCEDURE ThisDialog (dialog: ARRAY OF CHAR; useLang: BOOLEAN): Views.View;
		VAR fname, submod, sub, mod: Files.Name; canCreate: BOOLEAN; conv: Converters.Converter;
			loc: Files.Locator; file: Files.File; v: Views.View; s: Stores.Store; var: Meta.Item;
	BEGIN
		ASSERT(dialog # "", 20);
		v := NIL; file := NIL; canCreate := FALSE;
		CheckQualident(dialog, submod, fname);
		IF submod # "" THEN	(* is qualident *)
			Meta.LookupPath(dialog, var);
			IF var.obj = Meta.varObj THEN	(* variable exists *)
				canCreate := TRUE;
				Librarian.lib.SplitName(submod, sub, mod);
				loc := Files.dir.This(sub);
				IF loc.res = 0 THEN
					Files.dir.GetFileName(fname, Files.docType, fname);
					loc := loc.This("Rsrc");
					IF loc.res = 0 THEN file := Files.dir.Old(loc, fname, Files.shared) END;
				END
			END
		END;
		IF (file = NIL) & ~canCreate THEN	(* try file name *)
			PathToSpec(dialog, loc, fname, useLang);
			IF loc.res = 0 THEN
				Files.dir.GetFileName(fname, Files.docType, fname);
				file := Files.dir.Old(loc, fname, Files.shared)
			END
		END;
		IF file # NIL THEN
			Files.dir.GetFileName(fname, Files.docType, fname);
			conv := NIL; Converters.Import(loc, fname, conv, s);
			IF s # NIL THEN
				v := s(Views.View)
			END
		ELSE Dialog.ShowParamMsg("#System:FileNotFound", dialog, "", "")
		END;
		RETURN v
	END ThisDialog;

	PROCEDURE ThisMask (param: ARRAY OF CHAR; useLang: BOOLEAN): Views.View;
		VAR v: Views.View; c: Containers.Controller;
	BEGIN
		v := ThisDialog(param, useLang);
		IF v # NIL THEN
			WITH v: Containers.View DO
				c := v.ThisController();
				IF c # NIL THEN
					c.SetOpts(c.opts - {Containers.noFocus} + {Containers.noCaret, Containers.noSelection});
					IF modeHook # NIL THEN modeHook(c) END
				ELSE Dialog.ShowMsg("#System:NotEditable")
				END
			ELSE Dialog.ShowMsg("#System:ContainerExpected")
			END
		END;
		RETURN v
	END ThisMask;
	
	(* Interface procedures *)
	
	PROCEDURE CloseDialog* (OUT closedView: Views.View);
		CONST canClose = {Windows.neverDirty, Windows.isTool, Windows.isAux};
		VAR w: Windows.Window; msg: Sequencers.CloseMsg;
	BEGIN
		closedView := NIL;
		w := Windows.dir.First();
		IF w # NIL THEN
			IF w.sub THEN
				closedView := w.frame.view;
				Windows.dir.Close(w);
			ELSIF (w.flags * canClose = {}) & w.seq.Dirty() THEN
				Dialog.ShowMsg("#System:CannotCloseDirtyWindow")
			ELSE
				msg.sticky := FALSE; w.seq.Notify(msg);
				IF ~msg.sticky THEN closedView := w.frame.view; Windows.dir.Close(w) END
			END
		END
	END CloseDialog;
	
	PROCEDURE OpenAux* (file, title: ARRAY OF CHAR; OUT v: Views.View);
		VAR loc: Files.Locator; name: Files.Name; t: Views.Title;
	BEGIN
		PathToSpec(file, loc, name, FALSE);
		IF loc.res = 0 THEN
			loc.res := 77; v := Views.OldView(loc, name); loc.res := 0;
			IF v # NIL THEN t := title$; Views.OpenAux(v, t)
			ELSE Dialog.ShowParamMsg("#System:FileNotFound", file, "", "")
			END
		ELSE Dialog.ShowParamMsg("#System:FileNotFound", file, "", "")
		END
	END OpenAux;
	
	PROCEDURE OpenAuxDialog* (file, title: ARRAY OF CHAR; OUT v: Views.View);
		VAR t0: Views.Title; done: BOOLEAN;
	BEGIN
		Dialog.MapString(title, t0);
		Windows.SelectByTitle(NIL, {Windows.isAux}, t0, done);
		IF ~done THEN
			v := ThisMask(file, TRUE);
			IF v # NIL THEN
				StdDialog.Open(v, title, NIL, "", NIL, FALSE, TRUE, TRUE, FALSE, TRUE)
			END
		END
	END OpenAuxDialog;
	
	(* Opening a file in 'Browser' mode is used mainly for online help files. 
	In that case, but also in others, it is convenient to bring an existing window to the front in case the 
	same file is browsed again. This behavior is the same as with opening a file as a normal window.
	In order to be able to browse and edit at the same time, i.e. in two different domains, the 
	isAux flag is used to distinguish between both.
	*)
	PROCEDURE OpenBrowser* (file, title: ARRAY OF CHAR; OUT v: Views.View);
		VAR loc: Files.Locator; name: Files.Name; t: Views.Title;
			c: Containers.Controller; w: Windows.Window; conv: Converters.Converter;
	BEGIN
		PathToSpec(file, loc, name, TRUE);
		IF loc.res = 0 THEN
			w := Windows.GetBySpec(loc, name, Converters.list, {Windows.isAux});
			IF w # NIL THEN (* bring to front; avoid re-loading the document *)
				Windows.dir.Select(w, Windows.lazy);
				v := w.doc.ThisView();
			ELSE (* load document and open reusable aux window in Browser mode *)
				loc.res := 77; v := Views.Old(Views.dontAsk, loc, name, conv); loc.res := 0;
				IF v # NIL THEN
					WITH v: Containers.View DO
						c := v.ThisController();
						IF c # NIL THEN
							c.SetOpts(c.opts - {Containers.noFocus, Containers.noSelection} + {Containers.noCaret})
						END
					ELSE
					END;
					t := title$;
					StdDialog.Open(v, t, loc, name, conv, FALSE, TRUE, FALSE, FALSE, FALSE)
				ELSE Dialog.ShowParamMsg("#System:FileNotFound", file, "", "")
				END
			END
		ELSE Dialog.ShowParamMsg("#System:FileNotFound", file, "", "")
		END
	END OpenBrowser;
	
	PROCEDURE OpenDoc* (file: ARRAY OF CHAR; OUT v: Views.View);
		VAR loc: Files.Locator; name: Files.Name; conv: Converters.Converter;
	BEGIN
		PathToSpec(file, loc, name, FALSE);
		IF loc.res = 0 THEN
			conv := NIL; v := Views.Old(Views.dontAsk, loc, name, conv);
			IF loc.res = 78 THEN loc := NIL; name := "" END;	(* stationery *)
			IF v # NIL THEN Views.Open(v, loc, name, conv)
			ELSE Dialog.ShowParamMsg("#System:FileNotFound", file, "", "")
			END
		ELSE Dialog.ShowParamMsg("#System:FileNotFound", file, "", "")
		END
	END OpenDoc;
	
	PROCEDURE OpenCopyOf* (file: ARRAY OF CHAR; OUT v: Views.View);
		VAR loc: Files.Locator; name: Files.Name; conv: Converters.Converter;
	BEGIN
		PathToSpec(file, loc, name, FALSE);
		IF loc.res = 0 THEN
			conv := NIL; v := Views.Old(Views.dontAsk, loc, name, conv);
			IF loc.res = 78 THEN loc := NIL; name := "" END;	(* stationary *)
			IF v # NIL THEN 
				IF v.context # NIL THEN
					v := Views.CopyOf(v.context(Documents.Context).ThisDoc(), Views.deep);
					Stores.InitDomain(v)
				ELSE v := Views.CopyOf(v, Views.deep)
				END;
				Views.Open(v, NIL, "", conv)
			ELSE Dialog.ShowParamMsg("#System:FileNotFound", file, "", "")
			END
		ELSE Dialog.ShowParamMsg("#System:FileNotFound", file, "", "")
		END
	END OpenCopyOf;
	
	PROCEDURE OpenToolDialog* (file, title: ARRAY OF CHAR; OUT v: Views.View);
		VAR t0: Views.Title; done: BOOLEAN;
	BEGIN
		Dialog.MapString(title, t0);
		Windows.SelectByTitle(NIL, {Windows.isTool}, t0, done);
		IF ~done THEN
			v := ThisMask(file, FALSE);
			IF v # NIL THEN
				StdDialog.Open(v, title, NIL, "", NIL, TRUE, FALSE, TRUE, FALSE, TRUE)
			END
		END
	END OpenToolDialog;
	
END StdApi.
