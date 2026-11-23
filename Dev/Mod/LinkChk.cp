MODULE DevLinkChk;
(**
	project	= "BlackBox 2.0"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		Strings, Dialog, Files, Fonts, Ports, Converters, Views, Containers,
		TextModels, TextMappers, TextViews, TextControllers, StdLinks, StdCmds;

	CONST
		oneSubsystem* = 0; globalSubsystem* = 1; allSubsystems* = 2;
		linkCommand = "DevLinkChk.Open('";

	TYPE
		Iterator = POINTER TO IteratorDesc;
		IteratorDesc = RECORD
			root: Files.Locator;
			locs: Files.LocInfo;
			files: Files.FileInfo
		END;

	VAR
		par*: RECORD
			scope*: INTEGER;	(* IN {oneSubsystem, globalSubsystem, allSubsystems} *)
			subsystem*: ARRAY 16 OF CHAR;
			legal: BOOLEAN	(* legal => correct syntax for a subsystem name # "" *)
		END;
		default, link: TextModels.Attributes;

	PROCEDURE MakeDocName (VAR name: Files.Name);
	BEGIN
		Files.dir.GetFileName(name, Files.docType, name);
	END MakeDocName;

	PROCEDURE New (root: Files.Locator): Iterator;
		VAR i: Iterator;
	BEGIN
		NEW(i); i.root := root; i.locs := Files.dir.LocList(root); i.files := Files.dir.FileList(root);
		(* could sort by name *)
		RETURN i
	END New;

	PROCEDURE ReadLoc (i: Iterator; VAR loc: Files.Locator; VAR name: Files.Name);
	BEGIN
		IF i.locs # NIL THEN
			loc := i.root.This(i.locs.name); ASSERT(loc.res = 0, 60);
			name := i.locs.name$;
			i.locs := i.locs.next
		ELSE
			loc := NIL; name := ""
		END
	END ReadLoc;

	PROCEDURE ReadFile (i: Iterator; OUT name: Files.Name);
	BEGIN
		IF i.files # NIL THEN
			name := i.files.name$;
			i.files := i.files.next
		ELSE
			name := ""
		END
	END ReadFile;

	(* similar to StdInterpreter.CallHook.Call but without calling the commands*)
	PROCEDURE Stale (IN proc: ARRAY OF CHAR; text: TextModels.Model): BOOLEAN;
	
		TYPE
			Ident = Dialog.String;
			
		CONST
			 ident = 0; dot = 1; semicolon = 2; eot = 3;
			lparen = 4; rparen = 5; quote = 6; comma = 7; int = 8;
			
		VAR
			i, type, res: INTEGER; ch: CHAR; id: Ident; x: INTEGER;
			par0: POINTER TO Dialog.String; numPar: INTEGER;
			focusLoc: Files.Locator; focusFile: Files.Name;
		
		PROCEDURE Error;
		BEGIN res := 1
		END Error;
		
		PROCEDURE Init (VAR s: ARRAY OF CHAR);
		BEGIN s := ""
		END Init;

		PROCEDURE GetCh;
		BEGIN
			IF i < LEN(proc) THEN ch := proc[i]; INC(i) ELSE ch := 0X END
		END GetCh;

		PROCEDURE Scan;
			VAR j: INTEGER; num: ARRAY 32 OF CHAR; r: INTEGER;
		BEGIN
			IF res = 0 THEN
				WHILE (ch # 0X) & (ch <= " ") DO GetCh END;
				IF ch = 0X THEN
					type := eot
				ELSIF ch = "." THEN
					type := dot; GetCh
				ELSIF ch = ";" THEN
					type := semicolon; GetCh
				ELSIF ch = "(" THEN
					type := lparen; GetCh
				ELSIF ch = ")" THEN
					type := rparen; GetCh
				ELSIF ch = "'" THEN
					type := quote; GetCh
				ELSIF ch = "," THEN
					type := comma; GetCh
				ELSIF (ch >= "0") & (ch <= "9") OR (ch = "-") THEN
					type := int; j := 0;
					REPEAT
						num[j] := ch; INC(j); GetCh
					UNTIL (ch < "0") OR (ch > "9") & (ch < "A") OR (ch > "H");
					num[j] := 0X; Strings.StringToInt(num, x, r)
				ELSIF Strings.IsIdentStart(ch) THEN
					type := ident;
					id[0] := ch; j := 1; GetCh;
					WHILE (ch # 0X) & (i < LEN(proc)) & Strings.IsIdent(ch) DO
						id[j] := ch; INC(j); GetCh
					END;
					id[j] := 0X
				ELSE Error
				END
			END
		END Scan;
		
		PROCEDURE String (VAR s: ARRAY OF CHAR);
			VAR j: INTEGER;
		BEGIN
			IF type = quote THEN
				j := 0;
				WHILE (ch # 0X) & (ch # "'") & (j < LEN(s) - 1) DO
					s[j] := ch; INC(j); GetCh
				END;
				s[j] := 0X;
				IF ch = "'" THEN
					GetCh; Scan
				ELSE Error
				END
			ELSE Error
			END
		END String;

		PROCEDURE ParamList ();
			VAR sv: Ident;
		BEGIN
			numPar := 0; par0 := NIL;
			IF type = lparen THEN Scan;
				WHILE (type # rparen) & (res = 0) DO
					IF type = quote THEN
						String(sv);
						IF numPar = 0 THEN NEW(par0); par0^ := sv$ END;
						INC(numPar)
					ELSIF type IN {int, ident} THEN (* non string params can be ignored here *)
						Scan
					ELSE Error
					END;
					IF type = comma THEN Scan
					ELSIF type # rparen THEN Error
					END
				END;
				Scan
			END
		END ParamList;

		PROCEDURE CheckLink (IN m, p: ARRAY OF CHAR);
			VAR loc: Files.Locator; i, j: INTEGER; ch: CHAR; fn: Files.Name;
				rd: TextModels.Reader; v: Views.View; ident: Ident; lnk: StdLinks.Target;
		BEGIN
			IF m = "StdCmds" THEN
				IF (p = "OpenBrowser") OR (p = "OpenDoc") OR (p = "OpenAuxDialog") 
				OR (p = "OpenToolDialog") OR (p = "OpenAux") THEN
					IF (p = "OpenDoc") & (numPar = 1) OR (p # "OpenDoc") & (numPar = 2) THEN
						loc := Files.dir.This(""); i := 0;
						REPEAT
							j := 0; ch := par0[i];
							WHILE (ch # "/") & (ch # "\") & (ch # 0X) DO
								fn[j] := ch; INC(j); INC(i); ch := par0[i]
							END;
							fn[j] := 0X;
							IF ch # 0X THEN loc := loc.This(fn); INC(i) 
							END
						UNTIL ch = 0X;
						MakeDocName(fn);
						focusLoc := loc;
						focusFile := fn;
						IF Files.dir.Old(loc, fn, Files.shared) = NIL THEN Error (* file not found *) END
					ELSE Error
					END
				END
			ELSIF m = "StdLinks" THEN
				IF p = "ShowTarget" THEN
					IF numPar = 1 THEN
						IF focusLoc # NIL THEN v := Views.OldView(focusLoc, focusFile);
							IF (v # NIL) & (v IS TextViews.View) THEN
								rd := v(TextViews.View).ThisModel().NewReader(NIL)
							ELSE
								Error; RETURN
							END
						ELSE rd := text.NewReader(NIL)
						END;
						rd.ReadView(v);
						WHILE ~rd.eot DO
							IF v IS StdLinks.Target THEN
								lnk := v(StdLinks.Target);
								IF lnk.leftSide THEN lnk.GetIdent(ident);
									IF ident = par0$ THEN RETURN (* target found *) END
								END
							END;
							rd.ReadView(v)
						END
					END;
					Error
				END
			END
		END CheckLink;

		PROCEDURE Command;
			VAR left, right: Ident;
		BEGIN
			Init(left); Init(right);
			left := id; Scan;
			IF type = dot THEN	(* Oberon command *)
				Scan;
				IF type = ident THEN
					right := id; Scan; ParamList();
					IF res = 0 THEN CheckLink(left, right) END
				ELSE Error
				END
			ELSE Error
			END
		END Command;

	BEGIN
		res := 0; i := 0; type := 0; GetCh; Init(id); x := 0; focusLoc := NIL; focusFile := "";
		Scan;
		IF type = ident THEN
			Command;
			WHILE (type = semicolon) & (res = 0) DO Scan; Command END;
			IF type # eot THEN Error END
		ELSE Error
		END;
		RETURN res # 0
	END Stale;

	PROCEDURE GetCmd (IN path, file: Files.Name; pos: INTEGER; OUT cmd: ARRAY OF CHAR);
		VAR p, bug0, bug1, bug2: ARRAY 128 OF CHAR;
	BEGIN
		Strings.IntToString(pos, p);
		bug0 :=  linkCommand + path + "', '";
		bug1 :=  bug0 + file + "', ";
		bug2 := bug1 + p + ")";
		cmd := bug2$
	END GetCmd;

	PROCEDURE CheckDoc (IN path, file: Files.Name; t: TextModels.Model;
			VAR f: TextMappers.Formatter; tabs: INTEGER; check: BOOLEAN; VAR hit: BOOLEAN);
		VAR r: TextModels.Reader; v: Views.View; cmd, cmd0: ARRAY 1024 OF CHAR;
			i: INTEGER;
	BEGIN
		r := t.NewReader(NIL);
		r.ReadView(v);
		WHILE v # NIL DO
			WITH v: StdLinks.Link DO
				IF v.leftSide THEN
					v.GetCmd(cmd);
					IF (cmd # "") & (~check OR Stale(cmd, t)) THEN
						hit := TRUE;
						i := 0; WHILE i # tabs DO f.WriteTab; INC(i) END;
						GetCmd(path, file, r.Pos() - 1, cmd0);
						f.WriteView(StdLinks.dir.NewLink(cmd0));
						f.rider.SetAttr(link);
						f.WriteString(cmd);
						f.rider.SetAttr(default);
						f.WriteView(StdLinks.dir.NewLink(""));
						f.WriteLn
					END
				END
			ELSE
			END;
			r.ReadView(v)
		END
	END CheckDoc;

	PROCEDURE CheckLoc (
		IN path: Files.Name; loc: Files.Locator; name: Files.Name;
		VAR f: TextMappers.Formatter; check: BOOLEAN; OUT hit: BOOLEAN
	);
		VAR i: Iterator; fname: Files.Name; v: Views.View; conv: Converters.Converter;
			label, label0: INTEGER; hit0: BOOLEAN;
	BEGIN
		label := f.Pos(); hit := FALSE;
		loc := loc.This(name);
		i := New(loc);
		ReadFile(i, fname);
		IF fname # "" THEN f.WriteTab; f.WriteString(name); f.WriteLn END;
		hit := FALSE;
		WHILE fname # "" DO
			label0 := f.Pos(); hit0 := FALSE;
			MakeDocName(fname);
			v := Views.Old(Views.dontAsk, loc, fname, conv);
			IF (v # NIL) & (v IS TextViews.View) THEN
				f.WriteTab; f.WriteTab; f.WriteString(fname); f.WriteLn;
				CheckDoc(path, fname, v(TextViews.View).ThisModel(), f, 3, check, hit0);
				IF ~hit0 THEN f.SetPos(label0) END;
				hit := hit OR hit0
			END;
			ReadFile(i, fname)
		END;
		IF ~hit THEN f.SetPos(label) END
	END CheckLoc;

	PROCEDURE Equal (IN a, b: ARRAY OF CHAR): BOOLEAN;
		VAR al, bl: Files.Name;
	BEGIN
		IF a = b THEN RETURN TRUE
		ELSE Strings.ToLower(a, al); Strings.ToLower(b, bl); RETURN al = bl
		END
	END Equal;

	PROCEDURE Check* (subsystem: ARRAY OF CHAR; scope: INTEGER; check: BOOLEAN);
		VAR i: Iterator; root, loc: Files.Locator; name: Files.Name; hit0, hit1: BOOLEAN;
			out: TextModels.Model; f: TextMappers.Formatter; label: INTEGER; title: Views.Title;
			v: TextViews.View; c: Containers.Controller; path: Files.Name;
	BEGIN
		out := TextModels.dir.New(); f.ConnectTo(out);
		IF check THEN
			Dialog.MapString("#Dev:InconsistentLinks", title)
		ELSE
			Dialog.MapString("#Dev:Links", title)
		END;
		v := TextViews.dir.New(out);
		(* set Browser mode: *)
		c := v.ThisController();
		c.SetOpts(c.opts + {Containers.noCaret} - {Containers.noSelection, Containers.noFocus});
		Views.OpenAux(v, title);
		default := TextModels.dir.attr;
		link := TextModels.NewColor(default, Ports.blue);
		link := TextModels.NewStyle(link, {Fonts.underline});
		root := Files.dir.This("");
		IF scope IN {globalSubsystem, allSubsystems} THEN
			label := f.Pos();
			Dialog.ShowStatus(".");
			f.WriteString("."); f.WriteLn;
			path := "Docu";
			CheckLoc(path, root, "Docu", f, check, hit0);
			path := "Mod";
			CheckLoc(path, root, "Mod", f, check, hit1);
			IF ~hit0 & ~hit1 THEN f.SetPos(label) END
		END;
		i := New(root);
		ReadLoc(i, loc, name);
		WHILE loc # NIL DO
			IF (scope = allSubsystems) OR (scope = oneSubsystem) & Equal(name, subsystem) THEN
				label := f.Pos();
				Dialog.ShowStatus(name);
				f.WriteString(name); f.WriteLn;
				path := name + "/" + "Docu";
				CheckLoc(path, loc, "Docu", f, check, hit0);
				path := name + "/" + "Mod";
				CheckLoc(path, loc, "Mod", f, check, hit1);
				IF ~hit0 & ~hit1 THEN f.SetPos(label) END
			END;
			ReadLoc(i, loc, name)
		END;
		out.Delete(f.Pos(), out.Length());
		Dialog.ShowStatus(""); default := NIL; link := NIL
	END Check;

	PROCEDURE PathToLoc (IN path: ARRAY OF CHAR; OUT loc: Files.Locator);
		VAR i, j: INTEGER; ch: CHAR; name: ARRAY 256 OF CHAR;
	BEGIN
		loc := Files.dir.This("");
		IF path # "" THEN
			i := 0; j := 0;
			REPEAT
				ch := path[i]; INC(i);
				IF (ch = "/") OR (ch = 0X) THEN name[j] := 0X; j := 0; loc := loc.This(name)
				ELSE name[j] := ch; INC(j)
				END
			UNTIL (ch = 0X) OR (loc.res # 0)
		END
	END PathToLoc;

	PROCEDURE Open* (path, file: ARRAY OF CHAR; pos: INTEGER);
		VAR loc: Files.Locator; v: Views.View; bug: Files.Name; c: TextControllers.Controller;
	BEGIN
		ASSERT(file # "", 20); ASSERT(pos >= 0, 21);
		PathToLoc(path, loc); ASSERT(loc # NIL, 22);
		bug := file$;
		v := Views.OldView(loc, bug);
		IF v # NIL THEN
			WITH v: TextViews.View DO
				(* v.DisplayMarks(TextViews.show); *)
				Views.Open(v, loc, bug, NIL);
				c := v.ThisController()(TextControllers.Controller);
				c.SetCaret(pos);
				v.ShowRange(pos, pos + 1, TextViews.focusOnly);
			ELSE
				HALT(23)
			END
		END
	END Open;

	PROCEDURE ListLinks*;
	(** Guard: CommandGuard **)
	BEGIN
		StdCmds.CloseDialog; Check(par.subsystem, par.scope, FALSE)
	END ListLinks;

	PROCEDURE CheckLinks*;
	(** Guard: CommandGuard **)
	BEGIN
		StdCmds.CloseDialog; Check(par.subsystem, par.scope, TRUE)
	END CheckLinks;

	PROCEDURE SubsystemGuard* (VAR p: Dialog.Par);
	BEGIN
		p.readOnly := par.scope # oneSubsystem
	END SubsystemGuard;

	PROCEDURE CommandGuard* (VAR p: Dialog.Par);
	BEGIN
		p.disabled := (par.scope = oneSubsystem) & ~par.legal
	END CommandGuard;

	PROCEDURE SyntaxOK(IN s: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER; ch: CHAR;
	BEGIN
		i := 0; ch := s[0];
		IF ~Strings.IsIdentStart(ch) THEN RETURN FALSE END ;
		WHILE Strings.IsUpper(ch) DO INC(i); ch := s[i] END ;
		IF ch = 0X THEN RETURN FALSE END ;
		WHILE Strings.IsIdent(ch) & ~Strings.IsUpper(ch) DO INC(i); ch := s[i] END ;
		RETURN ch = 0X
	END SyntaxOK;

	PROCEDURE SubsystemNotifier* (op, from, to: INTEGER);
	BEGIN
		IF par.scope = oneSubsystem THEN
			par.legal := SyntaxOK(par.subsystem)
		ELSE
			par.subsystem := ""; par.legal := FALSE
		END
	END SubsystemNotifier;

END DevLinkChk.
