MODULE DevReferences;
(**
	project	= "BlackBox 2.0, diffs vs 1.7.2 are marked by green"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)
	
	IMPORT
		Strings, Files, Fonts, Models, Views, Dialog, StdDialog, StdApi,
		Converters, DevCPS, Librarian, 
		TextModels, TextMappers, TextViews, TextControllers;
	
	CONST
		(* additional scanner types *)
		import = 100; module = 101; semicolon = 102; becomes  = 103; comEnd = 104;


	PROCEDURE SpecialFont (r: TextModels.Reader): BOOLEAN;
		CONST max = 14 * Fonts.point;
	BEGIN
		RETURN (r.attr.font.weight > Fonts.normal) & (r.attr.font.size <= max)
	END SpecialFont;
	
	PROCEDURE SearchIdent (
		t: TextModels.Model; ident: TextMappers.String; VAR beg, end: INTEGER; bold: BOOLEAN
	);
		VAR r: TextModels.Reader; found: BOOLEAN; ch: CHAR; len, i: INTEGER;
	BEGIN
		end := 0;
		IF ident # "" THEN
			len := 0; WHILE ident[len] # 0X DO INC(len) END;
			r := t.NewReader(NIL); found := FALSE;
			beg := 0; r.SetPos(0); r.ReadChar(ch);
			WHILE ~r.eot & ~found DO
				(* search legal start symbol *)
				WHILE ~r.eot & ~Strings.IsIdentStart(ch) DO
					INC(beg); r.ReadChar(ch)
				END;
				i := 0;
				WHILE ~r.eot & (~bold OR SpecialFont(r)) & (i # len) & (ch = ident[i]) DO
					r.ReadChar(ch); INC(i)
				END;
				found := (i = len) & ~Strings.IsIdent(ch);
				IF ~r.eot & ~found THEN	(* skip rest of identifier *)
					beg := r.Pos() - 1;
					WHILE ~r.eot & Strings.IsIdent(ch) DO
						INC(beg); r.ReadChar(ch)
					END
				END
			END;
			IF found THEN end := r.Pos() - 1 END
		END
	END SearchIdent;

	PROCEDURE ShowText* (module, ident: TextMappers.String; category: Files.Name);
		VAR loc: Files.Locator; sub, name: Files.Name; conv: Converters.Converter;
			v: Views.View; m: Models.Model; t: TextModels.Model; beg, end: INTEGER;
	BEGIN
		IF category = "Docu" THEN
			Librarian.lib.SplitName(module, sub, name);
			Files.dir.GetFileName(name, Files.docType, name);
			StdApi.OpenBrowser(sub + "/Docu/" + name, module, v)
		ELSE
			StdDialog.GetSubLoc(module, category, loc, name);
			conv := NIL;
			v := Views.Old(Views.dontAsk, loc, name, conv);
			IF v # NIL THEN Views.Open(v, loc, name, conv) END
		END;
		IF v # NIL THEN
			m := v.ThisModel();
			IF (m # NIL) & (m IS TextModels.Model) THEN
				t := m(TextModels.Model);
				SearchIdent(t, ident, beg, end, TRUE);
				IF end <= 0 THEN SearchIdent(t, ident, beg, end, FALSE) END;
				IF end > 0 THEN
					TextViews.ShowRange(t, beg, end, TextViews.any);
					TextControllers.SetSelection(t, beg, end)
				ELSIF ident # "" THEN Dialog.Beep
				END
			ELSE Dialog.ShowMsg("#System:WrongSelection")
			END
		ELSE Dialog.ShowParamMsg("#System:FileNotFound", name, "", "")
		END
	END ShowText;

	PROCEDURE Scan (VAR s: TextMappers.Scanner);
		VAR sym: BYTE; 
	BEGIN
		s.Scan;
		IF s.type = TextMappers.string THEN
			IF DevCPS.checkKeyword # NIL THEN
				sym := -1;
				DevCPS.checkKeyword(s.string, sym);
				IF sym = DevCPS.import THEN
					s.type := import
				ELSIF sym = DevCPS.module THEN
					s.type := module
				END
			ELSE
				IF s.string = "IMPORT" THEN s.type := import
				ELSIF s.string = "MODULE" THEN s.type := module
				END
			END
		ELSIF s.type = TextMappers.char THEN
			IF s.char = ";" THEN s.type := semicolon
			ELSIF s.char = ":" THEN
				IF s.rider.char = "=" THEN s.rider.Read; s.type := becomes END
			ELSIF s.char = "(" THEN
				IF s.rider.char = "*" THEN
					s.rider.Read;
					REPEAT Scan(s) UNTIL (s.type = TextMappers.eot) OR (s.type = comEnd);
					Scan(s)
				END
			ELSIF s.char = "*" THEN
				IF s.rider.char = ")" THEN s.rider.Read; s.type := comEnd END
			END
		END
	END Scan;

	PROCEDURE ResolveImportAlias* (VAR mod: TextMappers.String; t: TextModels.Model);
		VAR s: TextMappers.Scanner;
	BEGIN
		s.ConnectTo(t); s.SetPos(0); Scan(s);
		IF s.type = module THEN
			Scan(s);
			WHILE (s.type # TextMappers.eot) & (s.type # import) DO Scan(s) END;
			WHILE (s.type # TextMappers.eot) & (s.type # semicolon)
					& ((s.type # TextMappers.string) OR (s.string # mod)) DO Scan(s) END;
			IF s.type = TextMappers.string THEN
				Scan(s);
				IF s.type = becomes THEN
					Scan(s);
					IF s.type = TextMappers.string THEN
						mod := s.string$
					END
				END
			END
		END
	END ResolveImportAlias;

	PROCEDURE Show* (category: Files.Name);
		VAR c: TextControllers.Controller; s: TextMappers.Scanner; beg, end: INTEGER;
			module, ident: TextMappers.String;
	BEGIN
		c := TextControllers.Focus();
		IF c # NIL THEN
			IF c.HasSelection() THEN
				c.GetSelection(beg, end);
				s.ConnectTo(c.text); s.SetOpts({7});	(* acceptUnderscores *)
				s.SetPos(beg); s.Scan;
				IF s.type = TextMappers.string THEN
					module := s.string; ident := "";
					ResolveImportAlias(module, c.text);
					s.Scan;
					IF (s.type = TextMappers.char) & (s.char = ".") THEN
						s.Scan;
						IF s.type = TextMappers.string THEN ident := s.string END
					END;
					ShowText(module, ident, category)
				ELSE Dialog.ShowMsg("#System:WrongSelection")
				END
			ELSE Dialog.ShowMsg("#System:NoSelection")
			END
		ELSE Dialog.ShowMsg("#System:NoFocus")
		END
	END Show;

	PROCEDURE ShowSource*;
	BEGIN
		Show("Mod")
	END ShowSource;

	PROCEDURE ShowDocu*;
	BEGIN
		Show("Docu")
	END ShowDocu;

END DevReferences.
