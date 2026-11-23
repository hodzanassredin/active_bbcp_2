MODULE DevSubTool;
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

	IMPORT Strings, Files, Fonts, Views, TextModels, TextMappers, TextViews,
		StdCmds, Dialog, DevCommanders;

	CONST
		textCmds* = 0; formCmds* = 1; otherCmds = 2;
		noModelView* = 3; modelView* = 4; complexView* = 5;
		wrapper = 6; specialContainer = 7; generalContainer = 8;

	VAR
		create*: RECORD
			subsystem*: ARRAY 16 OF CHAR;
			kind*: INTEGER;
			Create*: PROCEDURE
		END;

	PROCEDURE TranslateText (t: TextModels.Model; s: ARRAY OF CHAR);
		VAR r: TextModels.Reader; w: TextModels.Writer; from, to: INTEGER; i: INTEGER; ch: CHAR;
	BEGIN
		r := t.NewReader(NIL); w := t.NewWriter(NIL);
		r.ReadChar(ch);
		WHILE ~r.eot DO
			WHILE ~r.eot & ~(Fonts.strikeout IN r.attr.font.style) DO
				r.ReadChar(ch);
			END;
			IF ~r.eot THEN
				from := r.Pos() - 1;
				WHILE ~r.eot & (Fonts.strikeout IN r.attr.font.style) DO
					r.ReadChar(ch);
				END;
				IF ~r.eot THEN
					to := r.Pos() - 1;
					t.Delete(from, to);
					w.SetPos(from);
					w.SetAttr(TextModels.NewStyle(r.attr, r.attr.font.style - {Fonts.strikeout}));
					i := 0; ch := s[0]; WHILE ch # 0X DO w.WriteChar(ch); INC(i); INC(from); ch := s[i] END;
					r.SetPos(from); r.ReadChar(ch);
				END
			END
		END
	END TranslateText;

	PROCEDURE TranslateFile (floc: Files.Locator; fname: Files.Name; tloc: Files.Locator; tname: Files.Name;
											string: ARRAY OF CHAR; VAR res: INTEGER);
		VAR v: Views.View;
	BEGIN
		v := Views.OldView(floc, fname);
		IF v # NIL THEN
			IF v IS TextViews.View THEN
				TranslateText(v(TextViews.View).ThisModel(), string);
				tloc.res := 76;	(* don't ask whether directory should be created *)
				Views.RegisterView(v, tloc, tname);
				res := tloc.res
			ELSE res := 101
			END
		ELSE res := 100
		END
	END TranslateFile;

	PROCEDURE TranslateSubsystem (kind: INTEGER; string: ARRAY OF CHAR);
		VAR loc, new: Files.Locator; t: TextModels.Model; res: INTEGER; f: TextMappers.Formatter;
			v: Views.View;

		PROCEDURE Message (sub, dir, old: ARRAY OF CHAR);
			VAR dummy: INTEGER;
		BEGIN
			IF res = 0 THEN
				f.WriteString(sub); f.WriteString(dir); f.WriteLn
			ELSE
				Dialog.GetOK("#Dev:CannotTranslate", old, "", "", {Dialog.ok}, dummy)
			END
		END Message;

	BEGIN
		t := TextModels.dir.New(); f.ConnectTo(t);
		loc := Files.dir.This("Dev"); loc := loc.This("Rsrc"); loc := loc.This("New");
		new := Files.dir.This(string); new := new.This("Mod");
		IF kind = textCmds THEN
			TranslateFile(loc, "Cmds0", new, "Cmds", string, res);
			Message(string, "Cmds", "Cmds0")
		ELSIF kind = formCmds THEN
			TranslateFile(loc, "Cmds1", new, "Cmds", string, res);
			Message(string, "Cmds", "Cmds1")
		ELSIF kind = otherCmds THEN
			TranslateFile(loc, "Cmds2", new, "Cmds", string, res);
			Message(string, "Cmds", "Cmds2")
		ELSIF kind = noModelView THEN
			TranslateFile(loc, "Views3", new, "Views", string, res);
			Message(string, "Views", "Views3");
			f.WriteLn;
			f.WriteView(DevCommanders.dir.New());
			f.WriteString(' "'); f.WriteString(string); f.WriteString('Views.Deposit; StdCmds.Open"'); f.WriteLn
		ELSIF kind = modelView THEN
			TranslateFile(loc, "Views4", new, "Views", string, res);
			Message(string, "Views", "Views4");
			f.WriteLn;
			f.WriteView(DevCommanders.dir.New());
			f.WriteString(' "'); f.WriteString(string); f.WriteString('Views.Deposit; StdCmds.Open"'); f.WriteLn
		ELSIF kind = complexView THEN
			TranslateFile(loc, "Models5", new, "Models", string, res);
			Message(string, "Models", "Models5");
			TranslateFile(loc, "Views5", new, "Views", string, res);
			Message(string, "Views", "Views5");
			TranslateFile(loc, "Cmds5", new, "Cmds", string, res);
			Message(string, "Cmds", "Cmds5");
			f.WriteLn;
			f.WriteView(DevCommanders.dir.New());
			f.WriteString(' "'); f.WriteString(string); f.WriteString('Views.Deposit; StdCmds.Open"'); f.WriteLn
		ELSIF kind = wrapper THEN
			TranslateFile(loc, "Views6", new, "Views", string, res);
			Message(string, "Views", "Views6");
			f.WriteLn;
			f.WriteView(DevCommanders.dir.New());
			f.WriteString(' "'); f.WriteString(string); f.WriteString('Views.Deposit; StdCmds.Open"'); f.WriteLn
		ELSIF kind = specialContainer THEN
			TranslateFile(loc, "Views7", new, "Views", string, res);
			Message(string, "Views", "Views7");
			f.WriteLn;
			f.WriteView(DevCommanders.dir.New());
			f.WriteString(' "'); f.WriteString(string); f.WriteString('Views.Deposit; StdCmds.Open"'); f.WriteLn
		ELSIF kind = generalContainer THEN
			TranslateFile(loc, "Models8", new, "Models", string, res);
			Message(string, "Models", "Models8");
			TranslateFile(loc, "Views8", new, "Views", string, res);
			Message(string, "Views", "Views8");
			TranslateFile(loc, "Controllers8", new, "Controllers", string, res);
			Message(string, "Controllers", "Controllers8");
			TranslateFile(loc, "Cmds8", new, "Cmds", string, res);
			Message(string, "Cmds", "Cmds8");
			f.WriteLn;
			f.WriteView(DevCommanders.dir.New());
			f.WriteString(' "'); f.WriteString(string); f.WriteString('Views.Deposit; StdCmds.Open"'); f.WriteLn
		ELSE HALT(20)
		END;
		v := TextViews.dir.New(t);
		Views.Open(v, new, "List", NIL);
		IF t.Length() > 0 THEN Views.RegisterView(v, new, "List") END
	END TranslateSubsystem;

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

	PROCEDURE Create;
		VAR res: INTEGER;
	BEGIN
		IF (LEN(create.subsystem$) < 3) THEN
			Dialog.GetOK("#Dev:PrefixTooShort", "", "", "", {Dialog.ok, Dialog.cancel}, res);
			IF res = Dialog.cancel THEN RETURN END
		END ;
		IF ~Strings.IsUpper(create.subsystem[0]) THEN
			Dialog.GetOK("#Dev:NotUppercase", "", "", "", {Dialog.ok, Dialog.cancel}, res);
			IF res = Dialog.cancel THEN RETURN END
		END ;
		IF (create.kind >= textCmds) & (create.kind <= generalContainer) THEN
			IF SyntaxOK(create.subsystem) THEN
				StdCmds.CloseDialog;
				TranslateSubsystem(create.kind, create.subsystem);
				create.subsystem := ""
			ELSE Dialog.GetOK("#Dev:IllegalSyntax", "", "", "", {Dialog.ok}, res)
			END
		ELSE Dialog.GetOK("#Dev:IllegalKind", "", "", "", {Dialog.ok}, res)
		END
	END Create;

BEGIN
	create.Create := Create; create.kind := textCmds
END DevSubTool.