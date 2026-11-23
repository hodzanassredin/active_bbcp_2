MODULE DevCommanders;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems, Alexander Iljin"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		Kernel, Strings, Fonts, Ports, Stores, Models, Views, Controllers, Properties, Dialog, Controls,
		TextModels, TextSetters, TextMappers, Services, StdLog;

	CONST
		(* additional Scan types *)
		ident = 19; qualident = 20; execMark = 21;

		point = Ports.point;

		minVersion = 0; maxVersion = 0; maxStdVersion = 0;


	TYPE
		View* = POINTER TO ABSTRACT RECORD (Views.View)
		END;
		EndView* = POINTER TO ABSTRACT RECORD (Views.View)
		END;

		Par* = POINTER TO RECORD
			text*: TextModels.Model;
			beg*, end*: INTEGER
		END;

		Directory* = POINTER TO ABSTRACT RECORD END;


		StdView = POINTER TO RECORD (View) END;
		StdEndView = POINTER TO RECORD (EndView) END;

		StdDirectory = POINTER TO RECORD (Directory) END;

		Scanner = RECORD
			s: TextMappers.Scanner;
			ident: ARRAY LEN(Kernel.Name) OF CHAR;
			qualident: ARRAY LEN(Kernel.Name) * 2 - 1 OF CHAR
		END;
		
		TrapCleaner = POINTER TO RECORD (Kernel.TrapCleaner) END;

	VAR
		par*: Par;
		dir-, stdDir-: Directory;
		
		cleaner: TrapCleaner;
		cleanerInstalled: BOOLEAN;


	(** Cleaner **)

	PROCEDURE (c: TrapCleaner) Cleanup;
	BEGIN
		par := NIL;
		cleanerInstalled := FALSE;
	END Cleanup;
	
	(** View **)

	PROCEDURE (v: View) Externalize- (VAR wr: Stores.Writer), EXTENSIBLE;
	BEGIN
		v.Externalize^(wr);
		wr.WriteVersion(maxVersion);
		wr.WriteXInt(execMark)
	END Externalize;

	PROCEDURE (v: View) Internalize- (VAR rd: Stores.Reader), EXTENSIBLE;
		VAR thisVersion, type: INTEGER;
	BEGIN
		v.Internalize^(rd);
		IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(minVersion, maxVersion, thisVersion);
		IF rd.cancelled THEN RETURN END;
		rd.ReadXInt(type)
	END Internalize;


	(** Directory **)

	PROCEDURE (d: Directory) New* (): View, NEW, ABSTRACT;
	PROCEDURE (d: Directory) NewEnd* (): EndView, NEW, ABSTRACT;


	(* auxilliary procedures *)

	PROCEDURE IsIdent (IN s: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER; ch: CHAR;
	BEGIN
		ch := s[0]; i := 1;
		IF Strings.IsIdentStart(ch) THEN
			REPEAT
				ch := s[i]; INC(i)
			UNTIL ~Strings.IsIdent(ch);
			RETURN (ch = 0X) & (i <= LEN(Kernel.Name))
		ELSE
			RETURN FALSE
		END
	END IsIdent;

	PROCEDURE Scan (VAR s: Scanner);
		VAR done: BOOLEAN;
	BEGIN
		s.s.Scan;
		IF (s.s.type = TextMappers.view) THEN
			IF Properties.ThisType(s.s.view, "DevCommanders.View") # NIL THEN s.s.type := execMark END
		ELSIF (s.s.type = TextMappers.string) & TextMappers.IsQualIdent(s.s.string) THEN
			s.s.type := qualident; s.qualident := s.s.string$
		ELSIF (s.s.type = TextMappers.string) & IsIdent(s.s.string) THEN
			s.ident := s.s.string$;
			TextMappers.ScanQualIdent(s.s, s.qualident, done);
			IF done THEN s.s.type := qualident ELSE s.s.type := ident END
		END
	END Scan;

	PROCEDURE ScanLongString(rd: TextModels.Reader; OUT cmd: POINTER TO ARRAY OF CHAR);
		VAR  ch: CHAR; first, last, i: INTEGER;
	BEGIN
		rd.ReadChar(ch);
		WHILE ~rd.eot & (ch <= " ") DO rd.ReadChar(ch) END;
		IF ~rd.eot & (ch = '"') THEN (* starts with a double quote *)
			first := rd.Pos(); rd.ReadChar(ch);
			WHILE ~rd.eot & (ch # '"') DO rd.ReadChar(ch) END;
			IF ~rd.eot THEN (* ends with a double quote *)
				last := rd.Pos(); NEW(cmd, last - first);
				rd.SetPos(first); rd.ReadChar(ch); i := 0;
				WHILE ch # '"' DO cmd[i] := ch; INC(i); rd.ReadChar(ch) END;
				cmd[i] := 0X; rd.Read
			END
		END
	END ScanLongString;

	PROCEDURE GetParExtend (rd: TextModels.Reader; OUT end: INTEGER);
		VAR v: Views.View;
	BEGIN
		IF rd.view # NIL THEN v := rd.view ELSE rd.ReadView(v) END;
		WHILE ~rd.eot 
				& (Properties.ThisType(v, "DevCommanders.View") = NIL) 
				& (Properties.ThisType(v, "DevCommanders.EndView") = NIL)  DO
			rd.ReadView(v)
		END;
		end := rd.Pos(); IF ~rd.eot THEN DEC(end) END
	END GetParExtend;

	PROCEDURE Unload (IN cmd: Dialog.String);
		VAR modname: Kernel.Name; str: Dialog.String; i: INTEGER; ch: CHAR; mod: Kernel.Module;
	BEGIN
		i := 0; ch := cmd[0];
		WHILE (ch # 0X) & (ch # ".") DO modname[i] := ch; INC(i); ch := cmd[i] END;
		modname[i] := 0X;
		mod := Kernel.ThisLoadedMod(modname);
		IF mod # NIL THEN
			Kernel.UnloadMod(mod);
			IF mod.refcnt < 0 THEN
				Dialog.MapParamString("#Dev:Unloaded", modname, "", "", str);
				StdLog.String(str); StdLog.Ln;
				Controls.Relink
			ELSE
				Dialog.ShowParamMsg("#Dev:UnloadingFailed", modname, "", "")
			END
		END
	END Unload;

	PROCEDURE Execute (t: TextModels.Model; pos: INTEGER; VAR end: INTEGER; unload: BOOLEAN);
		VAR s: Scanner; beg, res: INTEGER; cmd: POINTER TO ARRAY OF CHAR;
	BEGIN
		cmd := NIL;
		s.s.ConnectTo(t); s.s.SetPos(pos); s.s.SetOpts({TextMappers.returnViews, TextMappers.maskViews});
		Scan(s); ASSERT(s.s.type = execMark, 100);
		Scan(s);
		IF s.s.type = qualident THEN
			NEW(cmd, LEN(s.qualident$) + 1); cmd^ := s.qualident$
		ELSIF s.s.type = TextMappers.string THEN
			NEW(cmd, LEN(s.s.string$) + 1); cmd^ := s.s.string$
		ELSIF s.s.type = TextMappers.invalid THEN
			s.s.SetPos(pos + 1);
			ScanLongString(s.s.rider, cmd);
		END;
		IF cmd # NIL THEN
			beg := s.s.Pos(); IF ~s.s.rider.eot THEN DEC(beg) END; GetParExtend(s.s.rider, end);
			ASSERT(~cleanerInstalled, 101);
			Kernel.PushTrapCleaner(cleaner); cleanerInstalled := TRUE;
			NEW(par); par.text := t; par.beg := beg; par.end := end;
			IF unload THEN Unload(cmd$) END;
			Dialog.Call(cmd, " ",  res);
			par := NIL;
			Kernel.PopTrapCleaner(cleaner); cleanerInstalled := FALSE;
		ELSE
			Dialog.ShowStatus("#System:SyntaxError")
		END
	END Execute;

	PROCEDURE Track (v: View; f: Views.Frame; x, y: INTEGER; buttons: SET);
		VAR c: Models.Context; w, h, end: INTEGER; isDown, in, in0: BOOLEAN; m: SET;
	BEGIN
		c := v.context; c.GetSize(w, h); in0 := FALSE; in := TRUE;
		REPEAT
			IF in # in0 THEN
				f.MarkRect(0, 0, w, h, Ports.fill, Ports.invert, Ports.show); in0 := in
			END;
			f.Input(x, y, m, isDown);
			in := (0 <= x) & (x < w) & (0 <= y) & (y < h)
		UNTIL ~isDown;
		IF in0 THEN
			f.MarkRect(0, 0, w, h, Ports.fill, Ports.invert, Ports.hide);
			WITH c:TextModels.Context DO
				Execute(c.ThisModel(), c.Pos(), end,Controllers.modify IN buttons)
			ELSE Dialog.Beep
			END
		END
	END Track;

	(* StdView *)

	PROCEDURE (v: StdView) Externalize (VAR wr: Stores.Writer);
	BEGIN
		v.Externalize^(wr);
		wr.WriteVersion(maxStdVersion)
	END Externalize;

	PROCEDURE (v: StdView) Internalize (VAR rd: Stores.Reader);
		VAR thisVersion: INTEGER;
	BEGIN
		v.Internalize^(rd);
		IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(minVersion, maxStdVersion, thisVersion)
	END Internalize;

	PROCEDURE (v: StdView) Restore (f: Views.Frame; l, t, r, b: INTEGER);
		CONST u = point;
		VAR c: Models.Context; a: TextModels.Attributes; font: Fonts.Font; color: Ports.Color;
			size, d, w, asc, dsc, fw: INTEGER; s: ARRAY 2 OF CHAR;
	BEGIN
		ASSERT(v.context # NIL, 20);
		c := v.context;
		WITH c: TextModels.Context DO a := c.Attr(); font := a.font; color := a.color
		ELSE font := Fonts.dir.Default(); color := Ports.defaultColor
		END;
		font.GetBounds(asc, dsc, fw);
		size := asc + dsc; d := size DIV 2;
		f.DrawOval(u, 0, u + size, size, Ports.fill, color);
		s := "!";
		w := font.StringWidth(s);
		f.DrawString(u + d - w DIV 2, size - dsc, Ports.background, s, font)
	END Restore;

	PROCEDURE (v: StdView) HandleCtrlMsg (f: Views.Frame; VAR msg: Controllers.Message;
																		VAR focus: Views.View);
	BEGIN
		WITH msg: Controllers.TrackMsg DO
			Track(v, f, msg.x, msg.y, msg.modifiers)
		| msg: Controllers.PollCursorMsg DO
			msg.cursor := Ports.refCursor
		ELSE
		END
	END HandleCtrlMsg;

	PROCEDURE (v: StdView) HandlePropMsg (VAR msg: Properties.Message);
		VAR c: Models.Context; a: TextModels.Attributes; font: Fonts.Font; asc, dsc, fw: INTEGER;
	BEGIN
		WITH msg: Properties.Preference DO
			WITH msg: Properties.SizePref DO
				c := v.context;
				IF (c # NIL) & (c IS TextModels.Context) THEN
					a := c(TextModels.Context).Attr(); font := a.font
				ELSE font := Fonts.dir.Default()
				END;
				font.GetBounds(asc, dsc, fw);
				msg.h := asc + dsc; msg.w := msg.h + 2 * point
			| msg: Properties.ResizePref DO
				msg.fixed := TRUE
			| msg: Properties.FocusPref DO
				msg.hotFocus := TRUE
			| msg: TextSetters.Pref DO
				c := v.context;
				IF (c # NIL) & (c IS TextModels.Context) THEN
					a := c(TextModels.Context).Attr(); font := a.font
				ELSE font := Fonts.dir.Default()
				END;
				font.GetBounds(asc, msg.dsc, fw)
			| msg: Properties.TypePref DO
				IF Services.Is(v, msg.type) THEN msg.view := v END
			ELSE
			END
		ELSE
		END
	END HandlePropMsg;
	
	
	(* StdEndView *)

	PROCEDURE (v: StdEndView) Restore (f: Views.Frame; l, t, r, b: INTEGER);
		CONST u = point;
		VAR c: Models.Context; a: TextModels.Attributes; font: Fonts.Font; color: Ports.Color;
			size, asc, dsc, fw: INTEGER;
			points: ARRAY 3 OF Ports.Point;
	BEGIN
		ASSERT(v.context # NIL, 20);
		c := v.context;
		WITH c: TextModels.Context DO a := c.Attr(); font := a.font; color := a.color
		ELSE font := Fonts.dir.Default(); color := Ports.defaultColor
		END;
		font.GetBounds(asc, dsc, fw);
		size := asc + dsc;
		points[0].x := 0; points[0].y := size;
		points[1].x := u + (size DIV 2); points[1].y := size DIV 2;
		points[2].x := u + (size DIV 2); points[2].y := size;     
		f.DrawPath(points, 3, Ports.fill, color, Ports.closedPoly)
	END Restore;
	
	PROCEDURE (v: StdEndView) HandlePropMsg (VAR msg: Properties.Message);
		VAR c: Models.Context; a: TextModels.Attributes; font: Fonts.Font; asc, dsc, fw: INTEGER;
	BEGIN
		WITH msg: Properties.Preference DO
			WITH msg: Properties.SizePref DO
				c := v.context;
				IF (c # NIL) & (c IS TextModels.Context) THEN
					a := c(TextModels.Context).Attr(); font := a.font
				ELSE font := Fonts.dir.Default()
				END;
				font.GetBounds(asc, dsc, fw);
				msg.h := asc + dsc; msg.w := (msg.h + 2 * point) DIV 2
			| msg: Properties.ResizePref DO
				msg.fixed := TRUE
			| msg: Properties.FocusPref DO
				msg.hotFocus := TRUE
			| msg: TextSetters.Pref DO
				c := v.context;
				IF (c # NIL) & (c IS TextModels.Context) THEN
					a := c(TextModels.Context).Attr(); font := a.font
				ELSE font := Fonts.dir.Default()
				END;
				font.GetBounds(asc, msg.dsc, fw)
			| msg: Properties.TypePref DO
				IF Services.Is(v, msg.type) THEN msg.view := v END
			ELSE
			END
		ELSE
		END
	END HandlePropMsg;

	(* StdDirectory *)

	PROCEDURE (d: StdDirectory) New (): View;
		VAR v: StdView;
	BEGIN
		NEW(v); RETURN v
	END New;
	
	PROCEDURE (d: StdDirectory) NewEnd (): EndView;
		VAR v: StdEndView;
	BEGIN
		NEW(v); RETURN v
	END NewEnd;

	PROCEDURE Deposit*;
	BEGIN
		Views.Deposit(dir.New())
	END Deposit;

	PROCEDURE DepositEnd*;
	BEGIN
		Views.Deposit(dir.NewEnd())
	END DepositEnd;

	PROCEDURE SetDir* (d: Directory);
	BEGIN
		dir := d
	END SetDir;

	PROCEDURE Init;
		VAR d: StdDirectory;
	BEGIN
		NEW(d); dir := d; stdDir := d;
		NEW(cleaner); cleanerInstalled := FALSE;
	END Init;

BEGIN
	Init
END DevCommanders.
