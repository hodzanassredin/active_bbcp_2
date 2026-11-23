MODULE StdCmds;
(**
	project	= "BlackBox 2.0, diffs vs 1.7.2 are marked by green"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, A.A. Dmitriev, I.A. Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT Kernel, Meta, Files, Converters, Printers, Printing, 
		Fonts, Ports, Services, Stores, Sequencers, Models, Views, 
		Controllers, Containers, Properties, Dialog, Documents, Windows, Strings,
		StdDialog, StdApi;

	CONST
		illegalSizeKey = "#System:IllegalFontSize";
		defaultAllocator = "TextViews.Deposit; StdCmds.Open";

		(* wType, hType *)
		fix = 0; page = 1; window = 2;
		
		(* DirtyHook.Do: action *)
		default* = 0; defer* = 1; save* = 2; cancel* = 3; close* = 4;

		(* hints *)
		expDoc = 1;	(* imported view should be stored as document *)
		
	TYPE
		DirtyHook* = PROCEDURE (w: Windows.Window; OUT action: INTEGER);
		ClipboardHook* = POINTER TO ABSTRACT RECORD END;
		
		Param* = POINTER TO RECORD
			dirtyHook*: ARRAY 128 OF CHAR;
			clearSeqOnSave*: BOOLEAN;
			proc: DirtyHook;
			InitDefaults-, Init-: PROCEDURE (a: ANYPTR);
		END;
		
		OpenAction = POINTER TO RECORD (Dialog.GetAction) END;
		OpenCopyAction = POINTER TO RECORD (Dialog.GetAction) END;
		SaveAction = POINTER TO RECORD (Dialog.GetAction)
			w: Windows.Window;
			rename: BOOLEAN;
			v: Views.View;
		END;
		SaveAsAction = POINTER TO RECORD (Dialog.GetAction)
			v: Views.View;
		END;
	
	VAR
		size*: RECORD 
			size*: INTEGER
		END;
		layout*: RECORD
			wType*, hType*: INTEGER;
			width*, height*: REAL;
			doc: Documents.Document;
			u: INTEGER
		END;
		allocator*: Dialog.String;
		
		propEra: INTEGER;	(* (propEra, props) form cache for StdProps() *)
		props: Properties.StdProp;	(* valid iff propEra = Props.era *)
		
		prop: Properties.Property;	(* usef for copy/paste properties *)
		
		stdDirtyHook*: DirtyHook;
		clipboardHook*: ClipboardHook;
		exitHook*: PROCEDURE;
		objectMenuGuard*: PROCEDURE (VAR par: Dialog.Par);
		specHook: Views.GetSpecHook;
		result*: INTEGER; (* 0 - last OpenXXX successful, 1 - last OpenXXX failed *)
		param*: Param;
	
	PROCEDURE (h: ClipboardHook) Register* (v: Views.View; wid, hei: INTEGER; isSingle: BOOLEAN), NEW, ABSTRACT;
	PROCEDURE (h: ClipboardHook) Paste*, NEW, ABSTRACT;

	(* auxiliary procedures *)
	
	PROCEDURE SetGetSpecHook* (h: Views.GetSpecHook);
	BEGIN
		specHook := h
	END SetGetSpecHook;
	
	PROCEDURE StdProp (): Properties.StdProp;
	BEGIN
		IF propEra # Properties.era THEN
			Properties.CollectStdProp(props);
			propEra := Properties.era
		END;
		RETURN props
	END StdProp;

	PROCEDURE Append (VAR s: ARRAY OF CHAR; t: ARRAY OF CHAR);
		VAR len, i, j: INTEGER; ch: CHAR;
	BEGIN
		len := LEN(s);
		i := 0; WHILE s[i] # 0X DO INC(i) END;
		j := 0; REPEAT ch := t[j]; s[i] := ch; INC(j); INC(i) UNTIL (ch = 0X) OR (i = len);
		s[len - 1] := 0X
	END Append;	

	(* standard commands *)

	PROCEDURE OpenAuxDialog* (file, title: ARRAY OF CHAR);
		VAR v: Views.View;
	BEGIN
		StdApi.OpenAuxDialog(file, title, v);
		IF v = NIL THEN result := 1 ELSE result := 0 END
	END OpenAuxDialog;

	PROCEDURE OpenToolDialog* (file, title: ARRAY OF CHAR);
		VAR v: Views.View;
	BEGIN
		StdApi.OpenToolDialog(file, title, v);
		IF v = NIL THEN result := 1 ELSE result := 0 END
	END OpenToolDialog;

	PROCEDURE OpenDoc* (file: ARRAY OF CHAR);
		VAR v: Views.View;
	BEGIN
		StdApi.OpenDoc(file, v);
		IF v = NIL THEN result := 1 ELSE result := 0 END
	END OpenDoc;
	
	PROCEDURE OpenCopyOf* (file: ARRAY OF CHAR);
		VAR v: Views.View;
	BEGIN
		StdApi.OpenCopyOf(file, v);
		IF v = NIL THEN result := 1 ELSE result := 0 END
	END OpenCopyOf;
	
	PROCEDURE OpenAux* (file, title: ARRAY OF CHAR);
		VAR v: Views.View; 
	BEGIN
		StdApi.OpenAux(file, title, v);
		IF v = NIL THEN result := 1 ELSE result := 0 END
	END OpenAux;

	PROCEDURE OpenBrowser* (file, title: ARRAY OF CHAR);
		VAR v: Views.View;
	BEGIN
		StdApi.OpenBrowser(file, title, v);
		IF v = NIL THEN result := 1 ELSE result := 0 END
	END OpenBrowser;

	PROCEDURE CloseDialog*;
	(** CloseDialog will ONLY close "dialog" windows (aux, tool, neverdirty) or clean docs. It does NOT provide the user with a dialog to save a dirty document and therefore does NOT save dirty documents. It is used in form commands, so this probably was it's initial purpose. *)
		VAR v: Views.View;
	BEGIN
		StdApi.CloseDialog(v)
	END CloseDialog;


	PROCEDURE SaveConvByName (IN name: Files.Name; VAR conv: Converters.Converter);
	VAR i, len: INTEGER; type: Files.Type;
	BEGIN
		len := LEN(name$); 
		type := ""; conv := NIL;
		i := len; WHILE (i > 0) & (name[i] # ".") DO DEC(i) END;
		IF i > 0 THEN
			Strings.Extract(name,i+1,len-i,type);
			conv := Converters.list; 
			WHILE (conv # NIL) & ~( (conv.exp # "") & (conv.fileType = type) ) DO
				conv := conv.next
			END
		END
	END SaveConvByName;
	
	PROCEDURE OpenConvByName (IN name: Files.Name; VAR conv: Converters.Converter);
	VAR i, len: INTEGER; type: Files.Type;
	BEGIN
		len := LEN(name$);
		type := "";
		i := len; WHILE (i > 0) & (name[i] # ".") DO DEC(i) END;
		IF i > 0 THEN
			Strings.Extract(name,i+1,len-i,type);
			conv := Converters.list; 
			WHILE (conv # NIL) & ~( (conv.imp # "") & (conv.fileType = type) ) DO
				conv := conv.next
			END
		END;
		IF (i <= 0) OR (conv = NIL) THEN
			conv := Converters.list; 
			WHILE (conv # NIL) & ~(Converters.importAll IN conv.opts) DO
				conv := conv.next
			END
		END
	END OpenConvByName;

	PROCEDURE SaveConvByType (IN type: Files.Type; VAR conv: Converters.Converter);
	BEGIN
		conv := Converters.list;
		WHILE (conv # NIL) & ~( (conv.exp # "") & (conv.fileType = type) ) DO
			conv := conv.next
		END
	END SaveConvByType;

	PROCEDURE OpenConvByType (IN type: Files.Type; VAR conv: Converters.Converter);
	BEGIN
		conv := Converters.list;
		WHILE (conv # NIL) & ~( (conv.imp # "") & (conv.fileType = type) ) DO
			conv := conv.next
		END
	END OpenConvByType;

	PROCEDURE ExOpen (loc: Files.Locator; name: Files.Name; conv: Converters.Converter);
	(** open an existing document or view (ex HostCmds.Open) **)
		VAR v: Views.View;
			s: Stores.Store; w: Windows.Window; 
	BEGIN
		(* w := Windows.GetBySpec(loc, name, conv, {}); *)
		w := Windows.dir.First();
		WHILE (w # NIL) 
			& ((w.loc = NIL) OR (w.name = "") OR (w.loc.res = 77)
				OR	~Files.dir.SameFile(loc, name, w.loc, w.name) 
				OR (w.conv # conv)) DO
			w := Windows.dir.Next(w)
		END;
		IF w # NIL THEN s := w.doc
		ELSE
			Converters.Import(loc, name, conv, s);
			IF s # NIL THEN StdDialog.RecalcView(s(Views.View)) END
		END;
		IF s # NIL THEN
			v := s(Views.View);
			IF (conv # NIL) & (expDoc IN conv.opts) THEN conv := NIL END;
			Views.Open(v, loc, name, conv)
		ELSE 
			Dialog.ShowParamMsg("#System:FailedToOpen", name, "", "")
		END
	END ExOpen;

	PROCEDURE (a: OpenAction) Do;
	VAR loc: Files.Locator; name: Files.Name; conv: Converters.Converter;
	BEGIN
		IF ~a.cancel THEN
			loc := a.loc;
			name := a.name;
			IF a.converterName # "" THEN
				conv := Converters.list;
				WHILE (conv # NIL) & (conv.imp # a.converterName) DO
					conv := conv.next
				END
			ELSE
				IF (a.type = "") OR (a.type = "*") THEN
					OpenConvByName(name, conv)
				ELSE
					OpenConvByType(a.type$, conv)
				END
			END;
			ExOpen(loc, name, conv);
		END
	END Do;
	
	PROCEDURE OpenDialog*;  (* for replace HostCmds.Open *)
	(** open an existing document or view **)
		VAR loc: Files.Locator; name: Files.Name; conv: Converters.Converter;
			action: OpenAction;
	BEGIN
		loc := NIL;
		name := "";
		conv := NIL;
		IF Dialog.platform = Dialog.windows THEN
			specHook.GetIntSpec(loc, name, conv);
			IF loc # NIL THEN
				ExOpen(loc, name, conv);
			END
		ELSE
			NEW(action);
			action.title := "#Std:Open...";
			Dialog.GetIntLocName("", loc, action);
		END
	END OpenDialog;
	
	
	PROCEDURE (a: OpenCopyAction) Do;
	VAR loc: Files.Locator; name: Files.Name; conv: Converters.Converter; v: Views.View;
	BEGIN
		IF ~a.cancel THEN
			loc := a.loc;
			name := a.name;
			IF a.converterName # "" THEN
				conv := Converters.list;
				WHILE (conv # NIL) & (conv.imp # a.converterName) DO
					conv := conv.next
				END
			ELSE
				IF (a.type = "") OR (a.type = "*") THEN
					OpenConvByName(name, conv)
				ELSE
					OpenConvByType(a.type$, conv)
				END
			END;
			(* HostCmds.OpenCopyOf *)
			v := Views.Old(Views.dontAsk, loc, name, conv);
			IF v # NIL THEN
				IF v.context # NIL THEN
					v := Views.CopyOf(v.context(Documents.Context).ThisDoc(), Views.deep);
					Stores.InitDomain(v)
				ELSE v := Views.CopyOf(v, Views.deep)
				END;
				Views.Open(v, NIL, "", conv)
			ELSE
				Dialog.ShowParamMsg("#System:FailedToOpen", name, "", "")
			END
		END
	END Do;
	
	PROCEDURE OpenCopyOfDialog*; (* for replace HostCmds.OpenCopyOf *)
		VAR loc: Files.Locator; name: Files.Name; conv: Converters.Converter; v: Views.View;
			action: OpenCopyAction;
	BEGIN
		loc := NIL;
		IF Dialog.platform = Dialog.windows THEN
			(* HostCmds.OpenCopyOf *)
			v := Views.Old(Views.ask, loc, name, conv);
			IF v # NIL THEN
				IF v.context # NIL THEN
					v := Views.CopyOf(v.context(Documents.Context).ThisDoc(), Views.deep);
					Stores.InitDomain(v)
				ELSE v := Views.CopyOf(v, Views.deep)
				END;
				Views.Open(v, NIL, "", conv)
			END
		ELSE
			NEW(action);
			action.loc := loc;
			action.title := "#Std:Open As Copy...";
			Dialog.GetIntLocName("", loc, action);
		END
	END OpenCopyOfDialog;
	
	
	PROCEDURE (a: SaveAction) Do;
	VAR title: Views.Title; loc: Files.Locator; name: Files.Name; v: Views.View;
			conv: Converters.Converter; len, i: INTEGER; w: Windows.Window; 
			rename: BOOLEAN;
	BEGIN
		IF ~a.cancel THEN
			name := a.name;
			IF (a.type # "") & (a.type # "*") THEN
				Strings.Find(name, ".", 0, i);
				IF i = -1 THEN
					name := name + "." + a.type
				END
			END;
			IF a.converterName # "" THEN
				conv := Converters.list;
				WHILE (conv # NIL) & (conv.exp # a.converterName) DO
					conv := conv.next
				END
			END;
			IF conv = NIL THEN
				IF (a.type = "") OR (a.type = "*") THEN
					SaveConvByName(name, conv)
				ELSE
					SaveConvByType(a.type$, conv)
				END
			END;
			IF conv = NIL THEN
				conv := Converters.list
			END;
			loc := a.loc;
			w := a.w;
			v := a.v;
			rename := a.rename;
			
			Dialog.ShowStatus("#System:Saving");
			Converters.Export(loc, name, conv, v);
			IF loc.res = 0 THEN
				IF w.seq.Dirty() THEN
					IF param.clearSeqOnSave THEN
						(* clear sequencer *)
						w.seq.BeginModification(Sequencers.notUndoable, NIL);
						w.seq.EndModification(Sequencers.notUndoable, NIL);
					END;
					w.seq.SetDirty(FALSE)
				END;
				IF rename THEN
					len := MIN(LEN(name$), LEN(Views.Title) - 1);
					Strings.Extract(name, 0, len, title);
					IF len = LEN(Views.Title) - 1 THEN
						title[len - 1] := "."; title[len - 2] := "."; title[len - 3] := "."
					END;
					w.SetTitle(title); w.SetSpec(loc, name, conv)
				END;
				w.SetTitle(title); w.SetSpec(loc, name, conv);
				Dialog.ShowStatus("#System:Saved")
			ELSE
				Dialog.ShowStatus("#System:Failed")
			END;
			Kernel.Cleanup
		END
	END Do;
	
	
	PROCEDURE SaveWindow (w: Windows.Window; rename: BOOLEAN);
		VAR title: Views.Title; loc: Files.Locator; name: Files.Name; v: Views.View;
			conv: Converters.Converter; len: INTEGER;
			action: SaveAction;
	BEGIN
		IF (w # NIL) & (rename OR ~(Windows.neverDirty IN w.flags)) THEN
			v := w.doc.OriginalView();
			loc := w.loc; name := w.name$; conv := w.conv;
			IF name = "" THEN Dialog.MapString("#System:untitled", name) END;
			IF (loc = NIL) OR (loc.res = 77) OR (conv # NIL) & (conv.exp = "") THEN 
				rename := TRUE 
			END;
			IF (Dialog.platform = Dialog.windows) OR ~rename THEN
				IF rename THEN specHook.GetExtSpec(v, loc, name, conv) END;
				IF loc # NIL THEN
					Dialog.ShowStatus("#System:Saving");
					Converters.Export(loc, name, conv, v);
					IF loc.res = 0 THEN
						IF w.seq.Dirty() THEN
							IF param.clearSeqOnSave THEN
								(* clear sequencer *)
								w.seq.BeginModification(Sequencers.notUndoable, NIL);
								w.seq.EndModification(Sequencers.notUndoable, NIL);
							END;
							w.seq.SetDirty(FALSE)
						END;
						IF rename THEN
							len := MIN(LEN(name$), LEN(Views.Title) - 1);
							Strings.Extract(name, 0, len, title);
							IF len = LEN(Views.Title) - 1 THEN
								title[len - 1] := "."; title[len - 2] := "."; title[len - 3] := "."
							END;
							w.SetTitle(title); w.SetSpec(loc, name, conv)
						END;
						Dialog.ShowStatus("#System:Saved")
					ELSE
						Dialog.ShowStatus("#System:Failed")
					END
				END;
				Kernel.Cleanup
			ELSE
				NEW(action);
				IF conv # NIL THEN
					action.selectConverter := conv.exp
				ELSE
					action.selectConverter := Converters.list.exp
				END;
				action.v := v;
				action.w := w;
				action.rename := rename;
				IF rename THEN
					action.title := "#Std:Save As..."
				ELSE
					action.title := "#Std:Save..."
				END;
				Dialog.GetExtLocName(name, "", loc, action);
			END
		END
	END SaveWindow;
	
	PROCEDURE SaveDialog*; (* to replace HostCmds.Save *)
	(** save document shown in target window under old name **)
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(Controllers.targetPath);
		IF w # NIL THEN SaveWindow(w, FALSE) END
	END SaveDialog;

	PROCEDURE SaveAsDialog*; (* to replace HostCmds.SaveAs *)
	(** save document shown in target window under new name **)
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(Controllers.targetPath);
		IF w # NIL THEN SaveWindow(w, TRUE) END
	END SaveAsDialog;
	
	PROCEDURE (a: SaveAsAction) Do;
	VAR loc: Files.Locator; name: Files.Name; v: Views.View; conv: Converters.Converter;
		i: INTEGER;
	BEGIN
		IF ~a.cancel THEN
			name := a.name;
			IF (a.type # "") & (a.type # "*") THEN
				Strings.Find(name, ".", 0, i);
				IF i = -1 THEN
					name := name + "." + a.type
				END
			END;
			IF a.converterName # "" THEN
				conv := Converters.list;
				WHILE (conv # NIL) & (conv.exp # a.converterName) DO
					conv := conv.next
				END
			END;
			IF conv = NIL THEN
				IF (a.type = "") OR (a.type = "*") THEN
					SaveConvByName(name, conv)
				ELSE
					SaveConvByType(a.type$, conv)
				END
			END;
			IF conv = NIL THEN
				conv := Converters.list
			END;
			loc := a.loc;
			v := a.v;
			Dialog.ShowStatus("#System:Saving");
			Converters.Export(loc, name, conv, v);
			IF loc.res = 0 THEN Dialog.ShowStatus("#System:Saved")
			ELSE Dialog.ShowStatus("#System:Failed")
			END;
			Kernel.Cleanup
		END
	END Do;

	PROCEDURE SaveCopyAsDialog*; (* to replace HostCmds.SaveCopyAs *)
	(** save copy of document shown in target window under new name **)
		VAR w: Windows.Window; loc: Files.Locator; name: Files.Name; v: Views.View;
			conv: Converters.Converter;
			action: SaveAsAction;
	BEGIN
		w := Windows.dir.Focus(Controllers.targetPath);
		IF w # NIL THEN
			v := w.doc.OriginalView();
			loc := w.loc; name := w.name$; conv := w.conv;
			IF name = "" THEN Dialog.MapString("#System:untitled", name) END;
			IF Dialog.platform = Dialog.windows THEN
				specHook.GetExtSpec(v, loc, name, conv);
				IF loc # NIL THEN
					Dialog.ShowStatus("#System:Saving");
					Converters.Export(loc, name, conv, v);
					IF loc.res = 0 THEN Dialog.ShowStatus("#System:Saved")
					ELSE Dialog.ShowStatus("#System:Failed")
					END
				END;
				Kernel.Cleanup
			ELSE
				NEW(action);
				IF conv # NIL THEN
					action.selectConverter := conv.exp
				ELSE
					action.selectConverter := Converters.list.exp
				END;
				action.v := v;
				action.title := "#Std:Save Copy As...";
				Dialog.GetExtLocName(name, "", loc, action)
			END
		END
	END SaveCopyAsDialog;
	
	PROCEDURE GuardedClose* (w: Windows.Window; OUT cancelled, deferred: BOOLEAN);
	(* cancelled: the user cancelled closing of w process (possibly by clicking Cancel button); if closing w was part of closing a sequence of windows, the closing of the sequence is thus also cancelled deferred: the closing of w was deferred (possibly by asking the user a non-modal question *)
		VAR msg: Sequencers.CloseMsg; action: INTEGER;
	BEGIN
		ASSERT(w # NIL, 20); cancelled := TRUE; deferred := FALSE;
		IF w.sub THEN Windows.dir.Close(w)
		ELSE
			msg.sticky := FALSE; w.seq.Notify(msg);
			IF msg.sticky THEN cancelled := TRUE; deferred := FALSE
			ELSE
				IF w.seq.Dirty() & ~(Windows.neverDirty IN w.flags) THEN
					(* here you actually involve a dirty close dialog hook *)
					IF param.proc # NIL THEN
						param.proc(w, action)
					ELSE
						action := default
					END;
					ASSERT(stdDirtyHook # NIL, 38);
					IF action = default THEN stdDirtyHook(w, action) END;
					IF action = defer THEN cancelled := FALSE; deferred := TRUE
					ELSIF action = save THEN deferred := FALSE;
						SaveWindow(w, FALSE);	(* if saving is canceled, document remains dirty *)
						IF w.seq.Dirty() THEN cancelled := TRUE
						ELSE Windows.dir.Close(w); cancelled := FALSE
						END
					ELSIF action = cancel THEN
						cancelled := TRUE; deferred := FALSE
					ELSIF action = close THEN
						cancelled := FALSE; deferred := FALSE; Windows.dir.Close(w)
					END
				ELSE cancelled := FALSE; deferred := FALSE; Windows.dir.Close(w)
				END
			END;
			Kernel.Cleanup
		END
	END GuardedClose;
	
	PROCEDURE CloseThis* (w: Windows.Window);
		VAR cancelled_, deferred_: BOOLEAN;
	BEGIN GuardedClose(w, cancelled_, deferred_)
	END CloseThis;

	PROCEDURE CloseTopDialog*;	(** close topmost window *)
		VAR w: Windows.Window; cancelled_, deferred_: BOOLEAN;
	BEGIN
		w := Windows.dir.First();
		IF w # NIL THEN GuardedClose(w, cancelled_, deferred_) END
	END CloseTopDialog;
	
	PROCEDURE Exit*;
		VAR prev, w: Windows.Window; deferred, cancelled: BOOLEAN;
	BEGIN
		cancelled := FALSE; deferred := FALSE; prev := NIL;
		REPEAT
			IF prev = NIL THEN w := Windows.dir.First()
			ELSE w := Windows.dir.Next(prev)
			END;
			IF w # NIL THEN 
				GuardedClose(w, cancelled, deferred);
				IF w.frame # NIL (* not closed *) THEN prev := w ELSE prev := NIL END
			END
		UNTIL (w = NIL) OR cancelled OR deferred;
		IF (Windows.dir.First() = NIL) & ~cancelled & ~deferred & (exitHook # NIL) THEN
			exitHook
		END
	END Exit;
	
	PROCEDURE Open*;
		VAR i: INTEGER; v: Views.View;
	BEGIN
		i := Views.Available();
		IF i > 0 THEN Views.Fetch(v); Views.OpenView(v)
		ELSE Dialog.ShowMsg("#System:DepositExpected")
		END
	END Open;

	PROCEDURE PasteView*;
		VAR i: INTEGER; v: Views.View;
	BEGIN
		i := Views.Available();
		IF i > 0 THEN
			Views.Fetch(v);
			Controllers.PasteView(v, Views.undefined, Views.undefined, FALSE)
		ELSE Dialog.ShowMsg("#System:DepositExpected")
		END
	END PasteView;
	
	
	
	(* file menu commands *)
	
	PROCEDURE New*;
		VAR res: INTEGER;
	BEGIN
		Dialog.Call(allocator, " ", res)
	END New;


	(* edit menu commands *)
	
	PROCEDURE Undo*;
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(Controllers.frontPath);
		IF w # NIL THEN
			w.seq.Undo;
			IF ~param.clearSeqOnSave THEN w.seq.SetDirty(TRUE) END
		END
	END Undo;

	PROCEDURE Redo*;
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(Controllers.frontPath);
		IF w # NIL THEN
			w.seq.Redo;
			IF ~param.clearSeqOnSave THEN w.seq.SetDirty(TRUE) END
		END
	END Redo;

	PROCEDURE CopyProp*;
	BEGIN
		Properties.CollectProp(prop)
	END CopyProp;
	
	PROCEDURE PasteProp*;
	BEGIN
		Properties.EmitProp(NIL, prop)
	END PasteProp;
	
	PROCEDURE Clear*;
	(** remove the selection of the current focus **)
		VAR msg: Controllers.EditMsg;
	BEGIN
		msg.op := Controllers.cut; msg.view := NIL;
		msg.clipboard := FALSE;
		Controllers.Forward(msg)
	END Clear;

	PROCEDURE SelectAll*;
	(** select whole content of current focus **)
		VAR msg: Controllers.SelectMsg;
	BEGIN
		msg.set := TRUE; Controllers.Forward(msg)
	END SelectAll;

	PROCEDURE DeselectAll*;
	(** select whole content of current focus **)
		VAR msg: Controllers.SelectMsg;
	BEGIN
		msg.set := FALSE; Controllers.Forward(msg)
	END DeselectAll;

	PROCEDURE SelectDocument*;
	(** select whole document **)
		VAR w: Windows.Window; c: Containers.Controller;
	BEGIN
		w := Windows.dir.Focus(Controllers.path);
		IF w # NIL THEN
			c := w.doc.ThisController();
			IF (c # NIL) & ~(Containers.noSelection IN c.opts) & (c.Singleton() = NIL) THEN
				c.SetSingleton(w.doc.ThisView())
			END
		END
	END SelectDocument;

	PROCEDURE SelectNextView*;
		VAR c: Containers.Controller; v: Views.View;
	BEGIN
		c := Containers.Focus();
		IF (c # NIL) & ~(Containers.noSelection IN c.opts) THEN
			IF c.HasSelection() THEN v := c.Singleton() ELSE v := NIL END;
			IF v = NIL THEN
				c.GetFirstView(Containers.any, v)
			ELSE
				c.GetNextView(Containers.any, v);
				IF v = NIL THEN c.GetFirstView(Containers.any, v) END
			END;
			c.SelectAll(FALSE);
			IF v # NIL THEN c.SetSingleton(v) END
		ELSE Dialog.ShowMsg("#Dev:NoTargetFocusFound")
		END
	END SelectNextView;

	
	(** font menu commands **)

	PROCEDURE Font* (typeface: Fonts.Typeface);
	(** set the selection to the given font family **)
		VAR p: Properties.StdProp;
	BEGIN
		NEW(p); p.valid := {Properties.typeface}; p.typeface := typeface;
		Properties.EmitProp(NIL, p)
	END Font;

	PROCEDURE DefaultFont*;
	(** set the selection to the default font family **)
		VAR p: Properties.StdProp;
	BEGIN
		NEW(p); p.valid := {Properties.typeface}; p.typeface := Fonts.default;
		Properties.EmitProp(NIL, p)
	END DefaultFont;


	(** attributes menu commands **)

	PROCEDURE Plain*;
	(** reset the font attribute "weight" and all font style attributes of the selection **)
		VAR p: Properties.StdProp;
	BEGIN
		NEW(p); p.valid := {Properties.style, Properties.weight};
		p.style.val := {}; p.style.mask := {Fonts.italic, Fonts.underline, Fonts.strikeout};
		p.weight := Fonts.normal;
		Properties.EmitProp(NIL, p)
	END Plain;

	PROCEDURE Bold*;
	(** change the font attribute "weight" in the selection;
	if the selection has a homogeneously bold weight: toggle to normal, else force to bold **)
		VAR p, p0: Properties.StdProp;
	BEGIN
		Properties.CollectStdProp(p0);
		NEW(p); p.valid := {Properties.weight};
		IF (Properties.weight IN p0.valid) & (p0.weight # Fonts.normal) THEN
			p.weight := Fonts.normal
		ELSE p.weight := Fonts.bold
		END;
		Properties.EmitProp(NIL, p)
	END Bold;

	PROCEDURE Italic*;
	(** change the font style attribute "italic" in the selection;
	if the selection is homogeneous wrt this attribute: toggle, else force to italic **)
		VAR p, p0: Properties.StdProp;
	BEGIN
		Properties.CollectStdProp(p0);
		NEW(p); p.valid := {Properties.style}; p.style.mask := {Fonts.italic};
		IF (Properties.style IN p0.valid) & (Fonts.italic IN p0.style.val) THEN
			p.style.val := {}
		ELSE p.style.val := {Fonts.italic}
		END;
		Properties.EmitProp(NIL, p)
	END Italic;

	PROCEDURE Underline*;
	(** change the font style attribute "underline" in the selection;
	if the selection is homogeneous wrt this attribute: toggle, else force to underline **)
		VAR p, p0: Properties.StdProp;
	BEGIN
		Properties.CollectStdProp(p0);
		NEW(p); p.valid := {Properties.style}; p.style.mask := {Fonts.underline};
		IF (Properties.style IN p0.valid) & (Fonts.underline IN p0.style.val) THEN
			p.style.val := {}
		ELSE p.style.val := {Fonts.underline}
		END;
		Properties.EmitProp(NIL, p)
	END Underline;

	PROCEDURE Strikeout*;
	(** change the font style attribute "strikeout" in the selection,
	without changing other attributes;
	if the selection is homogeneous wrt this attribute: toggle,
	else force to strikeout **)
		VAR p, p0: Properties.StdProp;
	BEGIN
		Properties.CollectStdProp(p0);
		NEW(p); p.valid := {Properties.style}; p.style.mask := {Fonts.strikeout};
		IF (Properties.style IN p0.valid) & (Fonts.strikeout IN p0.style.val) THEN
			p.style.val := {}
		ELSE p.style.val := {Fonts.strikeout}
		END;
		Properties.EmitProp(NIL, p)
	END Strikeout;

	PROCEDURE Size* (size: INTEGER);
	(** set the selection to the given font size **)
		VAR p: Properties.StdProp;
	BEGIN
		NEW(p); p.valid := {Properties.size};
		p.size := size * Ports.point;
		Properties.EmitProp(NIL, p)
	END Size;

	PROCEDURE SetSize*;
		VAR p: Properties.StdProp;
	BEGIN
		IF (0 <= size.size) & (size.size < 32768) THEN
			NEW(p); p.valid := {Properties.size};
			p.size := size.size * Fonts.point;
			Properties.EmitProp(NIL, p)
		ELSE
			Dialog.ShowMsg(illegalSizeKey)
		END
	END SetSize;

	PROCEDURE InitSizeDialog*;
		VAR p: Properties.StdProp;
	BEGIN
		Properties.CollectStdProp(p);
		IF Properties.size IN p.valid THEN size.size := p.size DIV Fonts.point END
	END InitSizeDialog;

	PROCEDURE Color* (color: Ports.Color);
	(** set the color attributes of the selection **)
		VAR p: Properties.StdProp;
	BEGIN
		NEW(p); p.valid := {Properties.color};
		p.color.val := color;
		Properties.EmitProp(NIL, p)
	END Color;

	PROCEDURE UpdateAll*;	(* for HostCmds.Toggle *)
		VAR w: Windows.Window; pw, ph: INTEGER; dirty: BOOLEAN; msg: Models.UpdateMsg;
	BEGIN
		w := Windows.dir.First();
		WHILE w # NIL DO
			IF ~w.sub THEN
				dirty := w.seq.Dirty();
				Models.Domaincast(w.doc.Domain(), msg);
				IF ~dirty THEN w.seq.SetDirty(FALSE) END	(* not perfect: "undoable dirt" ... *)
			END;
			w.port.GetSize(pw, ph); (* no HostWindows  *)
			w.Restore(0, 0, pw, ph);
			w := Windows.dir.Next(w)
		END
	END UpdateAll;

	PROCEDURE RestoreAll*;
		VAR w: Windows.Window; pw, ph: INTEGER;
	BEGIN
		w := Windows.dir.First();
		WHILE w # NIL DO
			w.port.GetSize(pw, ph);
			w.Restore(0, 0, pw, ph);
			w := Windows.dir.Next(w)
		END
	END RestoreAll;
	
	
	(** document layout dialog **)
	
	PROCEDURE SetLayout*;
		VAR opts: SET; l, t, r, b, r0, b0: INTEGER; c: Containers.Controller; script: Stores.Operation;
	BEGIN
		c := layout.doc.ThisController();
		opts := c.opts - {Documents.pageWidth..Documents.winHeight};
		IF layout.wType = page THEN INCL(opts, Documents.pageWidth)
		ELSIF layout.wType = window THEN INCL(opts, Documents.winWidth)
		END;
		IF layout.hType = page THEN INCL(opts, Documents.pageHeight)
		ELSIF layout.hType = window THEN INCL(opts, Documents.winHeight)
		END;
		layout.doc.PollRect(l, t, r, b); r0 := r; b0 := b;
		IF layout.wType = fix THEN r := l + SHORT(ENTIER(layout.width * layout.u)) END;
		IF layout.hType = fix THEN b := t + SHORT(ENTIER(layout.height * layout.u)) END;
		IF (opts # c.opts) OR (r # r0) OR (b # b0) THEN
			Views.BeginScript(layout.doc, "#System:ChangeLayout", script);
			c.SetOpts(opts);
			layout.doc.SetRect(l, t, r, b);
			Views.EndScript(layout.doc, script)
		END
	END SetLayout;
	
	PROCEDURE InitLayoutDialog*;
	(* guard: WindowGuard *)
		VAR w: Windows.Window; c: Containers.Controller; l, t, r, b: INTEGER;
	BEGIN
		w := Windows.dir.First();
		IF w # NIL THEN
			layout.doc := w.doc;
			c := w.doc.ThisController();
			IF Documents.pageWidth IN c.opts THEN layout.wType := page
			ELSIF Documents.winWidth IN c.opts THEN layout.wType := window
			ELSE layout.wType := fix
			END;
			IF Documents.pageHeight IN c.opts THEN layout.hType := page
			ELSIF Documents.winHeight IN c.opts THEN layout.hType := window
			ELSE layout.hType := fix
			END;
			IF Dialog.metricSystem THEN layout.u := Ports.mm * 10 ELSE layout.u := Ports.inch END;
			w.doc.PollRect(l, t, r, b);
			layout.width := (r - l) DIV (layout.u DIV 100) / 100;
			layout.height := (b - t) DIV (layout.u DIV 100) / 100
		END
	END InitLayoutDialog;
	
	PROCEDURE WidthGuard* (VAR par: Dialog.Par);
	BEGIN
		IF layout.wType # fix THEN par.readOnly := TRUE END
	END WidthGuard;
	
	PROCEDURE HeightGuard* (VAR par: Dialog.Par);
	BEGIN
		IF layout.hType # fix THEN par.readOnly := TRUE END
	END HeightGuard;
	
	PROCEDURE TypeNotifier* (op_, from_, to_: INTEGER);
		VAR w, h, l, t, r, b: INTEGER; d: BOOLEAN;
	BEGIN
		layout.doc.PollRect(l, t, r, b);
		IF layout.wType = page THEN
			layout.doc.PollPage(w, h, l, t, r, b, d)
		ELSIF layout.wType = window THEN
			layout.doc.context.GetSize(w, h); r := w - l
		END;
		layout.width := (r - l) DIV (layout.u DIV 100) / 100;
		layout.doc.PollRect(l, t, r, b);
		IF layout.hType = page THEN
			layout.doc.PollPage(w, h, l, t, r, b, d)
		ELSIF layout.hType = window THEN
			layout.doc.context.GetSize(w, h); b := h - t
		END;
		layout.height := (b - t) DIV (layout.u DIV 100) / 100;
		Dialog.Update(layout)
	END TypeNotifier;
	

	(** window menu command **)

	PROCEDURE NewWindow*;
	(** guard ModelViewGuard **)
		VAR win: Windows.Window; doc: Documents.Document; v: Views.View; title: Views.Title;
			seq: ANYPTR; clean: BOOLEAN;
	BEGIN
		win := Windows.dir.Focus(Controllers.frontPath);
		IF win # NIL THEN
			v := win.doc.ThisView();
			IF v.Domain() # NIL THEN seq := v.Domain().GetSequencer() ELSE seq := NIL END;
			clean := (seq # NIL) & ~seq(Sequencers.Sequencer).Dirty();
			doc := win.doc.DocCopyOf(v);
			(* Stores.InitDomain(doc, v.Domain()); *)
			ASSERT(doc.Domain() = v.Domain(), 100);
			win.GetTitle(title);
			Windows.dir.OpenSubWindow(Windows.dir.New(), doc, win.flags, title);
			IF clean THEN seq(Sequencers.Sequencer).SetDirty(FALSE) END
		END
	END NewWindow;
	
	
	(* properties *)
	
	PROCEDURE GetCmd (name: ARRAY OF CHAR; OUT cmd: ARRAY OF CHAR);
		VAR i, j: INTEGER; ch, lch: CHAR; key: ARRAY 256 OF CHAR;
	BEGIN
		i := 0; ch := name[0]; key[0] := "#"; j := 1;
		REPEAT
			key[j] := ch; INC(j); lch := ch; INC(i); ch := name[i]
		UNTIL (ch = 0X) OR (ch = ".") OR Strings.IsUpper(ch) & ~Strings.IsUpper(lch);
		IF ch = "." THEN
			key := "#System:" + name
		ELSE
			key[j] := ":"; INC(j); key[j] := 0X; j := 0;
			WHILE ch # 0X DO name[j] := ch; INC(i); INC(j); ch := name[i] END;
			name[j] := 0X; key := key + name
		END;
		Dialog.MapString(key, cmd);
		IF cmd = name THEN cmd := "" END
	END GetCmd;
	
	PROCEDURE SearchCmd (call: BOOLEAN; OUT found: BOOLEAN);
		VAR p: Properties.Property; std: BOOLEAN; v: Views.View;
			cmd: ARRAY 256 OF CHAR; pos, res: INTEGER;
	BEGIN
		Controllers.SetCurrentPath(Controllers.targetPath);
		v := Containers.FocusSingleton(); found := FALSE;
		IF v # NIL THEN
			Services.GetTypeName(v, cmd);
			GetCmd(cmd, cmd);
			IF cmd # "" THEN found := TRUE;
				IF call THEN Dialog.Call(cmd, "", res) END
			END
		END;
		std := FALSE;
		Properties.CollectProp(p);
		WHILE p # NIL DO
			IF p IS Properties.StdProp THEN std := TRUE
			ELSE
				Services.GetTypeName(p, cmd);
				GetCmd(cmd, cmd);
				IF cmd # "" THEN found := TRUE;
					IF call THEN Dialog.Call(cmd, "", res) END
				ELSE
					Services.GetTypeName(p, cmd);
					Strings.Find(cmd, "Desc", LEN(cmd$)-4, pos);
					IF LEN(cmd$)-4 = pos THEN
						cmd[pos] := 0X; GetCmd(cmd, cmd);
						IF cmd # "" THEN found := TRUE;
							IF call THEN Dialog.Call(cmd, "", res) END
						END
					END
				END
			END;
			p := p.next
		END;
		IF std & ~found THEN
			Dialog.MapString("#Std:Properties.StdProp", cmd);
			IF cmd # "Properties.StdProp" THEN found := TRUE;
				IF call THEN Dialog.Call(cmd, "", res) END
			END
		END;
		IF ~found THEN
			Dialog.MapString("#System:ShowProp", cmd);
			IF cmd # "ShowProp" THEN found := TRUE;
				IF call THEN Dialog.Call(cmd, "", res) END
			END
		END;
		Controllers.ResetCurrentPath
	END SearchCmd;
	
	PROCEDURE ShowProp*;
		VAR found: BOOLEAN;
	BEGIN
		SearchCmd(TRUE, found)
	END ShowProp;
	
	PROCEDURE ShowPropGuard* (VAR par: Dialog.Par);
		VAR found: BOOLEAN;
	BEGIN
		SearchCmd(FALSE, found);
		IF ~found THEN par.disabled := TRUE END
	END ShowPropGuard;
	
	
	(* container commands *)
	
	PROCEDURE ActFocus (): Containers.Controller;
		VAR c: Containers.Controller; v: Views.View;
	BEGIN
		c := Containers.Focus();
		IF c # NIL THEN
			v := c.ThisView();
			IF v IS Documents.Document THEN
				v := v(Documents.Document).ThisView();
				IF v IS Containers.View THEN
					c := v(Containers.View).ThisController()
				ELSE c := NIL
				END
			END
		END;
		RETURN c
	END ActFocus;

	PROCEDURE ToggleNoFocus*;
		VAR c: Containers.Controller; v: Views.View;
	BEGIN
		c := ActFocus();
		IF c # NIL THEN
			v := c.ThisView();
			IF ~((v IS Documents.Document) OR (Containers.noSelection IN c.opts)) THEN
				IF Containers.noFocus IN c.opts THEN
					c.SetOpts(c.opts - {Containers.noFocus})
				ELSE
					c.SetOpts(c.opts + {Containers.noFocus})
				END
			END
		END
	END ToggleNoFocus;

	PROCEDURE OpenAsAuxDialog*;
	(** create a new sub-window onto the focus view shown in the top window, mask mode **)
		VAR win: Windows.Window; doc: Documents.Document; v, u: Views.View; title: Views.Title;
			c: Containers.Controller;
	BEGIN
		v := Controllers.FocusView();
		IF (v # NIL) & (v IS Containers.View) & ~(v IS Documents.Document) THEN
			win := Windows.dir.Focus(Controllers.frontPath); ASSERT(win # NIL, 100);
			doc := win.doc.DocCopyOf(v);
			u := doc.ThisView();
			c := u(Containers.View).ThisController();
			c.SetOpts(c.opts - {Containers.noFocus} + {Containers.noCaret, Containers.noSelection});
			IF v # win.doc.ThisView() THEN
				c := doc.ThisController();
				c.SetOpts(c.opts - {Documents.pageWidth, Documents.pageHeight}
										+ {Documents.winWidth, Documents.winHeight})
			END;
			(* Stores.InitDomain(doc, v.Domain()); already done in DocCopyOf *)
			win.GetTitle(title);
			Windows.dir.OpenSubWindow(Windows.dir.New(), doc, {Windows.isAux, Windows.neverDirty, Windows.noResize, Windows.noHScroll, Windows.noVScroll}, title)
		ELSE Dialog.Beep
		END
	END OpenAsAuxDialog;

	PROCEDURE OpenAsToolDialog*;
	(** create a new sub-window onto the focus view shown in the top window, mask mode **)
		VAR win: Windows.Window; doc: Documents.Document; v, u: Views.View; title: Views.Title;
			c: Containers.Controller;
	BEGIN
		v := Controllers.FocusView();
		IF (v # NIL) & (v IS Containers.View) & ~(v IS Documents.Document) THEN
			win := Windows.dir.Focus(Controllers.frontPath); ASSERT(win # NIL, 100);
			doc := win.doc.DocCopyOf(v);
			u := doc.ThisView();
			c := u(Containers.View).ThisController();
			c.SetOpts(c.opts - {Containers.noFocus} + {Containers.noCaret, Containers.noSelection});
			IF v # win.doc.ThisView() THEN
				c := doc.ThisController();
				c.SetOpts(c.opts - {Documents.pageWidth, Documents.pageHeight}
										+ {Documents.winWidth, Documents.winHeight})
			END;
			(* Stores.InitDomain(doc, v.Domain()); already done in DocCopyOf *)
			win.GetTitle(title);
			Windows.dir.OpenSubWindow(Windows.dir.New(), doc, {Windows.isTool, Windows.neverDirty, Windows.noResize, Windows.noHScroll, Windows.noVScroll}, title)
		ELSE Dialog.Beep
		END
	END OpenAsToolDialog;

	PROCEDURE RecalcFocusSize*;
		VAR c: Containers.Controller; v: Views.View; bounds: Properties.BoundsPref;
	BEGIN
		c := Containers.Focus();
		IF c # NIL THEN
			v := c.ThisView();
			bounds.w := Views.undefined; bounds.h := Views.undefined;
			Views.HandlePropMsg(v, bounds);
			v.context.SetSize(bounds.w, bounds.h)
		END
	END RecalcFocusSize;

	PROCEDURE RecalcAllSizes*;
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.First();
		WHILE w # NIL DO
			StdDialog.RecalcView(w.doc.ThisView());
			w := Windows.dir.Next(w)
		END
	END RecalcAllSizes;
	
	PROCEDURE SetMode(opts: SET);
		VAR 
			c: Containers.Controller; v: Views.View; 
			gm: Containers.GetOpts; sm: Containers.SetOpts;
			w: Windows.Window;
	BEGIN
		c := Containers.Focus();
		gm.valid := {};
		IF (c # NIL) & (c.Singleton() # NIL) THEN
			v := c.Singleton();
			Views.HandlePropMsg(v, gm);
		END;
		IF gm.valid = {} THEN
			w := Windows.dir.Focus(Controllers.path);
			IF (w # NIL) & (w.doc.ThisView() IS Containers.View) THEN v := w.doc.ThisView() ELSE v := NIL END
		END;
		IF v # NIL THEN
			sm.valid := {Containers.noSelection, Containers.noFocus, Containers.noCaret};
			sm.opts := opts;
			Views.HandlePropMsg(v, sm);
		END;
	END SetMode;
	
	PROCEDURE GetMode(OUT found: BOOLEAN; OUT opts: SET);
		VAR c: Containers.Controller; gm: Containers.GetOpts; w: Windows.Window;
	BEGIN
		c := Containers.Focus();
		gm.valid := {};
		IF (c # NIL) & (c.Singleton() # NIL) THEN
			Views.HandlePropMsg(c.Singleton(), gm);
		END;
		IF gm.valid = {}  THEN
			w := Windows.dir.Focus(Controllers.path);
			IF (w # NIL) & (w.doc.ThisView() IS Containers.View) THEN
				Views.HandlePropMsg(w.doc.ThisView(), gm);
			END
		END;
		found := gm.valid # {};
		opts := gm.opts
	END GetMode;
	
	PROCEDURE SetMaskMode*;
	(* Guard: SetMaskGuard *)
	BEGIN
		SetMode({Containers.noSelection, Containers.noCaret})
	END SetMaskMode;

	PROCEDURE SetEditMode*;
	(* Guard: SetEditGuard *)
	BEGIN
		SetMode({})
	END SetEditMode;

	PROCEDURE SetLayoutMode*;
	(* Guard: SetLayoutGuard *)
	BEGIN
		SetMode({Containers.noFocus})
	END SetLayoutMode;

	PROCEDURE SetBrowserMode*;
	(* Guard: SetBrowserGuard *)
	BEGIN
		SetMode({Containers.noCaret})
	END SetBrowserMode;

	(* standard guards *)
	
	PROCEDURE ToggleNoFocusGuard* (VAR par: Dialog.Par);
		VAR c: Containers.Controller; v: Views.View;
	BEGIN
		c := ActFocus();
		IF c # NIL THEN
			v := c.ThisView();
			IF ~((v IS Documents.Document) OR (Containers.noSelection IN c.opts)) THEN
				IF Containers.noFocus IN c.opts THEN par.label := "#System:AllowFocus"
				ELSE par.label := "#System:PreventFocus"
				END
			ELSE par.disabled := TRUE
			END
		ELSE par.disabled := TRUE
		END
	END ToggleNoFocusGuard;

	PROCEDURE ReadOnlyGuard* (VAR par: Dialog.Par);
	BEGIN
		par.readOnly := TRUE
	END ReadOnlyGuard;
	
	PROCEDURE WindowGuard* (VAR par: Dialog.Par);
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.First();
		IF w = NIL THEN par.disabled := TRUE END
	END WindowGuard;

	PROCEDURE ModelViewGuard* (VAR par: Dialog.Par);
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(Controllers.frontPath);
		par.disabled := (w = NIL) OR (w.doc.ThisView().ThisModel() = NIL)
	END ModelViewGuard;

	PROCEDURE SetMaskModeGuard* (VAR par: Dialog.Par);
		CONST mode = {Containers.noSelection, Containers.noFocus, Containers.noCaret};
		VAR opts: SET; found: BOOLEAN;
	BEGIN
		GetMode(found, opts);
		IF found THEN
			par.checked := opts * mode = {Containers.noSelection, Containers.noCaret}
		ELSE
			par.disabled := TRUE
		END
	END SetMaskModeGuard;

	PROCEDURE SetEditModeGuard* (VAR par: Dialog.Par);
		CONST mode = {Containers.noSelection, Containers.noFocus, Containers.noCaret};
		VAR opts: SET; found: BOOLEAN;
	BEGIN
		GetMode(found, opts);
		IF found THEN
			par.checked := opts * mode = {}
		ELSE
			par.disabled := TRUE
		END
	END SetEditModeGuard;

	PROCEDURE SetLayoutModeGuard* (VAR par: Dialog.Par);
		CONST mode = {Containers.noSelection, Containers.noFocus, Containers.noCaret};
		VAR opts: SET; found: BOOLEAN;
	BEGIN
		GetMode(found, opts);
		IF found THEN
			par.checked := opts * mode = {Containers.noFocus}
		ELSE
			par.disabled := TRUE
		END
	END SetLayoutModeGuard;

	PROCEDURE SetBrowserModeGuard* (VAR par: Dialog.Par);
		CONST mode = {Containers.noSelection, Containers.noFocus, Containers.noCaret};
		VAR opts: SET; found: BOOLEAN;
	BEGIN
		GetMode(found, opts);
		IF found THEN
			par.checked := opts * mode = {Containers.noCaret}
		ELSE
			par.disabled := TRUE
		END
	END SetBrowserModeGuard;

	PROCEDURE SelectionGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ops.valid * {Controllers.cut, Controllers.copy} = {} THEN par.disabled := TRUE END
	END SelectionGuard;

	PROCEDURE SingletonGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ops.singleton = NIL THEN par.disabled := TRUE END
	END SingletonGuard;
	
	PROCEDURE SelectAllGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ~ops.selectable THEN par.disabled := TRUE END
	END SelectAllGuard;

	PROCEDURE CaretGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ops.valid * {Controllers.pasteChar .. Controllers.paste} = {} THEN par.disabled := TRUE END
	END CaretGuard;

	PROCEDURE PasteCharGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ~(Controllers.pasteChar IN ops.valid) THEN par.disabled := TRUE END
	END PasteCharGuard;

	PROCEDURE PasteLCharGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ~(Controllers.pasteChar IN ops.valid) THEN par.disabled := TRUE END
	END PasteLCharGuard;

	PROCEDURE PasteViewGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ~(Controllers.paste IN ops.valid) THEN par.disabled := TRUE END
	END PasteViewGuard;
	
	PROCEDURE ContainerGuard* (VAR par: Dialog.Par);
	BEGIN
		IF Containers.Focus() = NIL THEN par.disabled := TRUE END
	END ContainerGuard;
	
	PROCEDURE UndoGuard* (VAR par: Dialog.Par);
		VAR f: Windows.Window; opName: Stores.OpName;
	BEGIN
		Dialog.MapString("#System:Undo", par.label);
		f := Windows.dir.Focus(Controllers.frontPath);
		IF (f # NIL) & f.seq.CanUndo() THEN
			f.seq.GetUndoName(opName);
			Dialog.MapString(opName, opName);
			Append(par.label, " ");
			Append(par.label, opName)
		ELSE
			par.disabled := TRUE
		END
	END UndoGuard;

	PROCEDURE RedoGuard* (VAR par: Dialog.Par);
		VAR f: Windows.Window; opName: Stores.OpName;
	BEGIN
		Dialog.MapString("#System:Redo", par.label);
		f := Windows.dir.Focus(Controllers.frontPath);
		IF (f # NIL) & f.seq.CanRedo() THEN
			f.seq.GetRedoName(opName);
			Dialog.MapString(opName, opName);
			Append(par.label, " ");
			Append(par.label, opName)
		ELSE
			par.disabled := TRUE
		END
	END RedoGuard;
	
	PROCEDURE PlainGuard* (VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF props.known * {Properties.style, Properties.weight} # {} THEN
			par.checked := (Properties.style IN props.valid)
				& (props.style.val = {}) & ({Fonts.italic, Fonts.underline, Fonts.strikeout} - props.style.mask = {})
				& (Properties.weight IN props.valid) & (props.weight = Fonts.normal)
		ELSE
			par.disabled := TRUE
		END
	END PlainGuard;

	PROCEDURE BoldGuard* (VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF Properties.weight IN props.known THEN
			par.checked := (Properties.weight IN props.valid) & (props.weight = Fonts.bold)
		ELSE
			par.disabled := TRUE
		END
	END BoldGuard;

	PROCEDURE ItalicGuard* (VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF Properties.style IN props.known THEN
			par.checked := (Properties.style IN props.valid) & (Fonts.italic IN props.style.val)
		ELSE
			par.disabled := TRUE
		END
	END ItalicGuard;

	PROCEDURE UnderlineGuard* (VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF Properties.style IN props.known THEN
			par.checked := (Properties.style IN props.valid) & (Fonts.underline IN props.style.val)
		ELSE
			par.disabled := TRUE
		END
	END UnderlineGuard;

	PROCEDURE StrikeoutGuard* (VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF Properties.style IN props.known THEN
			par.checked := (Properties.style IN props.valid) & (Fonts.strikeout IN props.style.val)
		ELSE
			par.disabled := TRUE
		END
	END StrikeoutGuard;

	PROCEDURE SizeGuard* (size: INTEGER; VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF Properties.size IN props.known THEN
			par.checked := (Properties.size IN props.valid) & (size = props.size DIV Ports.point)
		ELSE
			par.disabled := TRUE
		END
	END SizeGuard;

	PROCEDURE ColorGuard* (color: INTEGER; VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF Properties.color IN props.known THEN
			par.checked := (Properties.color IN props.valid) & (color = props.color.val)
		ELSE
			par.disabled := TRUE
		END
	END ColorGuard;
	
	PROCEDURE DefaultFontGuard* (VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF Properties.typeface IN props.known THEN
			par.checked := (Properties.typeface IN props.valid) & (props.typeface = Fonts.default)
		ELSE
			par.disabled := TRUE
		END
	END DefaultFontGuard;

	PROCEDURE TypefaceGuard* (VAR par: Dialog.Par);
		VAR props: Properties.StdProp;
	BEGIN
		props := StdProp();
		IF ~(Properties.typeface IN props.known) THEN par.disabled := TRUE END
	END TypefaceGuard;
	
	
	PROCEDURE SaveGuard* (VAR par: Dialog.Par);
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(Controllers.targetPath);
		IF (w = NIL) OR (Windows.neverDirty IN w.flags) OR ~w.seq.Dirty() THEN
			par.disabled := TRUE
		END
	END SaveGuard;
	
	(* printing *)
	
	PROCEDURE PrintGuard* (VAR par: Dialog.Par);
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(Controllers.targetPath);
		IF (w = NIL) OR (Printers.dir = NIL) OR ~Printers.dir.Available() THEN 
			par.disabled := TRUE 
		END
	END PrintGuard;

	PROCEDURE PrinterGuard* (VAR par: Dialog.Par);
	BEGIN
		IF ~Printers.dir.Available() THEN par.disabled := TRUE END
	END PrinterGuard;
	
	PROCEDURE HasSel (w: Windows.Window): BOOLEAN;
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		ops.type := ""; ops.singleton := NIL; ops.selectable := FALSE; ops.valid := {};
		w.ForwardCtrlMsg(ops);
		RETURN ops.singleton # NIL
	END HasSel;

	PROCEDURE PrintSel (w: Windows.Window; from_, to_, copies_: INTEGER);
		VAR wt, title: Views.Title; i: INTEGER; edit: Controllers.EditMsg;
	BEGIN
		edit.op := Controllers.copy; edit.view := NIL;
		edit.clipboard := FALSE; w.ForwardCtrlMsg(edit);
		ASSERT(edit.view # NIL, 100);
		w.GetTitle(wt); title := "[";
		i := 1; WHILE (wt[i - 1] # 0X) & (i < LEN(title)) DO title[i] := wt[i - 1]; INC(i) END;
		IF i >= LEN(title) - 1 THEN i := LEN(title) - 2 END;
		title[i] := "]"; title[i + 1] := 0X;
		Printing.PrintView(edit.view, (*edit.w, edit.h,*) Printing.NewDefaultPar(title))
	END PrintSel;

	PROCEDURE PrintDoc (w: Windows.Window; from, to, copies: INTEGER);
		VAR pw, ph, l, t, r, b: INTEGER; decorate: BOOLEAN;
			page: Printing.PageInfo; header, footer: Printing.Banner;
	BEGIN
		w.doc.PollPage(pw, ph, l, t, r, b, decorate);
		page.first := 1; page.from := from; page.to := to; page.alternate := FALSE;
		w.GetTitle(page.title);
		IF decorate THEN
			header.gap := 5 * Ports.mm; header.right := "&d&;&p";
		ELSE
			header.gap := 0; header.right := "";
		END;
		footer.gap := 0; footer.right := "";
		Printing.PrintView(w.doc, Printing.NewPar(page, header, footer, copies));
	END PrintDoc;

	PROCEDURE PrintThis (w: Windows.Window; from, to, copies: INTEGER; sel: BOOLEAN);
	BEGIN
		IF copies > 0 THEN
			IF sel THEN
				PrintSel(w, from, to, copies)
			ELSE
				PrintDoc(w, from, to, copies)
			END
		END
	END PrintThis;

	PROCEDURE Print*;
	(** print the front window's document **)
		VAR win: Windows.Window;
			from, to, copies: INTEGER; selection: BOOLEAN;
	BEGIN
		IF Printers.dir.Available() THEN
			win := Windows.dir.Focus(Controllers.targetPath);
			IF win # NIL THEN
				WHILE win.sub DO win := win.link END;
				selection := FALSE;
				StdDialog.PrintDialog(HasSel(win), from, to, copies, selection);
				PrintThis(win, from, to, copies, selection)
			END;
			Kernel.Cleanup
		END
	END Print;


	(* clipboard *)
	
	PROCEDURE Cut*;
	(** move the focus document's selection into the clipboard **)
		VAR msg: Controllers.EditMsg;
	BEGIN
		msg.op := Controllers.cut;
		msg.clipboard := TRUE;
		msg.view := NIL; msg.w := Views.undefined; msg.h := Views.undefined;
		Controllers.Forward(msg);
		IF msg.view # NIL THEN
			clipboardHook.Register(msg.view, msg.w, msg.h, msg.isSingle)
		END
	END Cut;

	PROCEDURE Copy*;
	(** move a copy of the focus document's selection into the clipboard **)
		VAR msg: Controllers.EditMsg;
	BEGIN
		msg.op := Controllers.copy;
		msg.clipboard := TRUE;
		msg.view := NIL; msg.w := Views.undefined; msg.h := Views.undefined;
		Controllers.Forward(msg);
		IF msg.view # NIL THEN
			clipboardHook.Register(msg.view, msg.w, msg.h, msg.isSingle)
		END
	END Copy;

	PROCEDURE Paste*;
	BEGIN
		IF clipboardHook # NIL THEN clipboardHook.Paste END;
	END Paste;
	
	PROCEDURE ObjectMenu*;
		VAR res_: INTEGER;
	BEGIN
		Dialog.Call("StdMenus.OpenAux", "", res_)
	END ObjectMenu;
	
	PROCEDURE ObjectMenuGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		IF objectMenuGuard # NIL THEN
			objectMenuGuard(par)
		ELSE
			Controllers.PollOps(ops);
    	IF (ops.singleton # NIL) & (ops.singleton.ThisModel() # NIL) THEN
				par.disabled := FALSE;
				par.label := "#Std:Open object"
			ELSE
				par.disabled := TRUE
			END
		END
	END ObjectMenuGuard;
	
	PROCEDURE CutGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ~(Controllers.cut IN ops.valid) THEN par.disabled := TRUE END
	END CutGuard;
	
	PROCEDURE CopyGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ~(Controllers.copy IN ops.valid) THEN par.disabled := TRUE END
	END CopyGuard;
	
	PROCEDURE PasteGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ~(Controllers.paste IN ops.valid)
			(* OR ~HostClipboard.ConvertibleTo(ops.pasteType) TODO *)
			THEN par.disabled := TRUE END
	END PasteGuard;
	
	PROCEDURE CutGuardHidden* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		par.label := '-' (* StdMenus.hidden*);
		Controllers.PollOps(ops);
		IF ~(Controllers.cut IN ops.valid) THEN par.disabled := TRUE END
	END CutGuardHidden;
	
	PROCEDURE CopyGuardHidden* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		par.label := '-' (* StdMenus.hidden*);
		Controllers.PollOps(ops);
		IF ~(Controllers.copy IN ops.valid) THEN par.disabled := TRUE END
	END CopyGuardHidden;
	
	PROCEDURE PasteGuardHidden* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		par.label := '-' (* StdMenus.hidden*);
		Controllers.PollOps(ops);
		IF ~(Controllers.paste IN ops.valid)
			(* OR ~HostClipboard.ConvertibleTo(ops.pasteType) TODO *)
			THEN par.disabled := TRUE END
	END PasteGuardHidden;

	PROCEDURE PasteSizedView* (widthPoints, heightPoints: INTEGER);
		VAR i: INTEGER; v: Views.View;
	BEGIN
		ASSERT(0 < widthPoints, 20); ASSERT(0 < heightPoints, 21);
		i := Views.Available();
		IF i > 0 THEN
			Views.Fetch(v);
			ASSERT(v.context = NIL, 22);
			Controllers.PasteView(v, widthPoints * Ports.point, heightPoints * Ports.point, FALSE)
		ELSE
			Dialog.ShowMsg("#System:DepositExpected")
		END
	END PasteSizedView;

	(* standard notifiers *)

	PROCEDURE DefaultOnDoubleClick* (op, from, to_: INTEGER);
		VAR  msg: Controllers.EditMsg; c: Containers.Controller;
	BEGIN
		IF (op = Dialog.pressed) & (from = 1) THEN
			Controllers.SetCurrentPath(Controllers.frontPath);
			c := Containers.Focus();
			Controllers.ResetCurrentPath;
			IF {Containers.noSelection, Containers.noCaret} - c.opts = {} THEN
				msg.op := Controllers.pasteChar;
				msg.char := 0DX; msg.modifiers := {};
				Controllers.ForwardVia(Controllers.frontPath, msg)
			END
		END
	END DefaultOnDoubleClick;

	PROCEDURE ParamInit (par: ANYPTR);
		VAR val: RECORD (Meta.Value) hook: DirtyHook END; it: Meta.Item; ok: BOOLEAN;
	BEGIN
		WITH par: Param DO
			par.proc := NIL;
			Meta.LookupPath(par.dirtyHook, it);
			IF it.Valid() THEN
				it.GetVal(val, ok);
				IF ok THEN par.proc := val.hook END
			END
		END
	END ParamInit;

	PROCEDURE ParamInitDefaults (par: ANYPTR);
	BEGIN
		WITH par: Param DO 
			par.dirtyHook := "";
			par.proc := NIL; 
			par.clearSeqOnSave := FALSE;
			par.Init := ParamInit; par.InitDefaults := ParamInitDefaults
		END
	END ParamInitDefaults;

	PROCEDURE Init;
	BEGIN
		allocator := defaultAllocator;
		propEra := -1;
		NEW(param); ParamInitDefaults(param)
	END Init;

BEGIN
	Init
END StdCmds.







Closing windows and exiting BlackBox
technical note by Anton Dmitriev

StdCmds.CloseTopDialog
	StdCmds.GuardedClose(w...)
		w.seq.Notify(msg:Sequencers.CloseMsg)
			StdTiles.SeqNotifier.Notify(msg)
				StdTiles.CloseNestedWindows(w)
					∀u∈w: StdCmds.GuardedClose(u)
		IF ~msg.sticky THEN
			DirtyHook(w, action)	(* presents a modal or non-modal dirty close confirmation *)
			CASE action OF default | cancel | save | close | defer
			END
		END
		Kernel.Cleanup

File->Close => StdCmds.CloseTopDialog

Window ⨂ =>
	LinBackends.DeleteHandler.Do
		StdWindows.guardedCloseHook -> StdCmds.CloseThis
			StdCmds.GuardedClose

So, for this to work, 
1) the tyler window's sequencer has to be given a notifier. This is done in StdTiles.New.
2) StdWindows.guardedCloseHook has to be set. This is done in LinInit

File->Exit => StdCmds.Exit

The main event loop for dialog (event-driven) applications is now in Dialog.Loop. It's started in Lin(Win)Init with Dialog.Start, which is given two procedures: one to let the platform process an event, another to validate views.
The loop ends when there's no windows AND exit without windows has been enabled thru Dialog.RequestExit(Dialog.exitWithoutWindows). Windowless applications should enable the loop to operate without windows by Dialog.RequestExit(~Dialog.exitWithouWindows): this way they can sit in the background and wait for a timer event or another platform event, and, if/when necessary, open new windows. Dialog.RequestExit can be done at any time.
Another application kind is batch applications (or command-line applications). They do not use the Dialog.Loop (thru Dialog.Start) at all, which is clear in LinIntInit, for example.

To enable legacy MDI applications to exit, StdCmds provides VAR exitHook, which is called at the end of StdCmds.Exit in case all windows were successfully closed. Applications using the new event loop do not need to provide this hook.

StdCmds.Exit
	close all windows
	exitHook -> MdiMenus.AllowExit
		MdiMenus.exit := TRUE

WinMenus.Loop
	WHILE WinApi.GetMessage() # 0 DO
		IF exit THEN Exit
			WinApi.SendMessage(WinWindows.main, WM_CLOSE)
				WinMenus.ApplWinHandler
					CASE WM_CLOSE: 
						WinMenus.Quit
							IF ~WinMenus.exit THEN
								StdCmds.Exit
							END
						WinApi.DefWndProc
							sends WM_QUIT to WinWindows.main
					END
		END
	END;
	(* WinApi.GetMessage() = 0 AFTER WM_QUIT *)
	Kernel.Quit

File->Exit => StdCmds.Exit

Click Main ⨂ => sends WM_CLOSE to WinWindows.mainWin (see above) => StdCmds.Exit

MDI ⨂ => 
	WinWindows.DocWinHandler
	CASE WM_CLOSE
		WinApi.SendMessage(WinWindows.main, WM_COMMAND iClose)
			WinMenus.ApplWinHandler
				CASE WM_COMMAND
					WinMenus.MenuCommand
						CASE iClose
							StdCmds.CloseTopDialog

