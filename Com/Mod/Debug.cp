MODULE ComDebug;
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
		SYSTEM, COM, WinApi, WinOle,
		Kernel, Strings, Dialog, Fonts, Ports, Stores, Models, Views, Properties, Containers,
		Documents, Windows,
		TextModels, TextRulers, TextViews, TextControllers, TextMappers,
		DevDebug, DevHeapSpy, StdLog, StdLinks;
	
	
	TYPE
		Block = POINTER TO RECORD [untagged]
			tag: Kernel.Type;
			size: INTEGER;		(* size of free blocks *)
			ref: INTEGER;
			unk: INTEGER
		END;
		Cluster = POINTER TO RECORD [untagged]
			size: INTEGER;	(* total size *)
			next: Cluster;
		END;


	VAR
		all: BOOLEAN;
		
	
	PROCEDURE NewRuler (): TextRulers.Ruler;
		CONST mm = Ports.mm;
		VAR p: TextRulers.Prop;
	BEGIN
		NEW(p);
		p.valid := {TextRulers.right, TextRulers.tabs, TextRulers.opts};
		p.opts.val := {TextRulers.rightFixed}; p.opts.mask := p.opts.val;
		p.right := 130 * mm;
		p.tabs.len := 4;
		p.tabs.tab[0].stop := 15 * mm; p.tabs.tab[1].stop := 70 * mm;
		p.tabs.tab[2].stop := 85 * mm; p.tabs.tab[3].stop := 95 * mm;
		RETURN TextRulers.dir.NewFromProp(p)
	END NewRuler;

	PROCEDURE OpenViewer (t: TextModels.Model; title: Views.Title; ruler:TextRulers.Ruler);
		VAR v: TextViews.View; c: Containers.Controller;
	BEGIN
		Dialog.MapString(title, title);
		v := TextViews.dir.New(t);
		IF ruler # NIL THEN v.SetDefaults(ruler, TextViews.dir.defAttr) END;
		c := v.ThisController();
		IF c # NIL THEN
			c.SetOpts(c.opts - {Containers.noFocus, Containers.noSelection} + {Containers.noCaret})
		END;
		Views.OpenAux(v, title)
	END OpenViewer;
	
	PROCEDURE OpenInfoViewer (t: TextModels.Model; title: Views.Title);
		VAR v: TextViews.View; c: Containers.Controller; p: Properties.BoundsPref;
	BEGIN
		Dialog.MapString(title, title);
		v := TextViews.dir.New(t);
		c := v.ThisController();
		IF c # NIL THEN
			c.SetOpts(c.opts - {Containers.noFocus, Containers.noSelection} + {Containers.noCaret})
		END;
		p.w := Views.undefined; p.h := Views.undefined; Views.HandlePropMsg(v, p);
		Views.OpenAux(Documents.dir.New(v, p.w, p.h), title)
	END OpenInfoViewer;
	
	
	PROCEDURE Next (b: Block): Block;	(* next block in same cluster *)
		VAR size: INTEGER; tag: Kernel.Type;
	BEGIN
		tag := SYSTEM.VAL(Kernel.Type, SYSTEM.VAL(INTEGER, b.tag) DIV 4 * 4);
		size := tag.size + 4;
		IF ODD(SYSTEM.VAL(INTEGER, b.tag) DIV 2) THEN size := b.size - SYSTEM.ADR(b.size) + size END;
		size := (size + 15) DIV 16 * 16;
		RETURN SYSTEM.VAL(Block, SYSTEM.VAL(INTEGER, b) + size)
	END Next;

	PROCEDURE ShowInterfaces (VAR out: TextMappers.Formatter);
		VAR adr, end: INTEGER; modName, name, anchor: Kernel.Name;
			a0, a1: TextModels.Attributes; cluster: Cluster; blk: Block;
	BEGIN
		out.WriteString("Referenced Interface Records:");
		out.WriteTab; out.WriteTab;
		a0 := out.rider.attr;
		out.rider.SetAttr(TextModels.NewStyle(out.rider.attr, {Fonts.underline}));
		out.rider.SetAttr(TextModels.NewColor(out.rider.attr, Ports.blue));
		a1 := out.rider.attr;
		out.WriteView(StdLinks.dir.NewLink("ComDebug.ToggleAllInterfaces"));
		IF all THEN out.WriteString("New") ELSE out.WriteString("All") END;
		out.WriteView(StdLinks.dir.NewLink(""));
		out.rider.SetAttr(a0);
		out.WriteTab;
		out.rider.SetAttr(a1);
		out.WriteView(StdLinks.dir.NewLink("ComDebug.UpdateInterfaceRecords"));
		out.WriteString("Update");
		out.WriteView(StdLinks.dir.NewLink(""));
		out.rider.SetAttr(a0);
		out.WriteLn;
		out.WriteLn;
		cluster := SYSTEM.VAL(Cluster, Kernel.Root());
		WHILE cluster # NIL DO
			blk := SYSTEM.VAL(Block, SYSTEM.VAL(INTEGER, cluster) + 12);
			end := SYSTEM.VAL(INTEGER, blk) + (cluster.size - 12) DIV 16 * 16;
			WHILE SYSTEM.VAL(INTEGER, blk) < end DO
				IF ~(1 IN SYSTEM.VAL(SET, blk.tag))
					& (SYSTEM.VAL(INTEGER, blk.tag) # SYSTEM.ADR(blk.size))
					& (blk.tag.base[0] = NIL)
				THEN
					Kernel.GetModName(blk.tag.mod, modName);
					IF (all OR (modName # "HostMechanisms")
							& (modName # "OleServer")
							& (modName # "OleClient")
							& (modName # "OleStorage")
							& (modName # "OleData")) THEN
						adr := SYSTEM.ADR(blk.size);
						out.WriteString("ref: ");
						out.WriteInt(blk.ref); out.WriteTab;
						out.WriteString(modName); out.WriteChar(".");
						IF (blk.tag.id DIV 256 # 0) & (blk.tag.mod.refcnt >= 0) THEN
							Kernel.GetTypeName(blk.tag, name); out.WriteString(name)
						ELSE
							out.WriteString("RECORD"); 
						END;
						out.WriteTab;
						out.WriteChar("["); out.WriteIntForm(adr, TextMappers.hexadecimal, 9, "0", TextMappers.showBase);
						out.WriteChar("]"); out.WriteChar(" ");
						out.WriteView(DevDebug.HeapRefView(adr, "Interface"));
						DevHeapSpy.GetAnchor(adr, anchor);
						IF anchor # "" THEN
							out.WriteTab; out.WriteTab; out.WriteChar("(");
							out.WriteString(anchor); out.WriteChar(")")
						END;
						out.WriteLn
					END;
				END;
				blk := Next(blk)
			END;
			cluster := cluster.next
		END
	END ShowInterfaces;
	
	PROCEDURE ShowInterfaceRecords*;
		VAR out: TextMappers.Formatter;
	BEGIN
		out.ConnectTo(TextModels.CloneOf(StdLog.buf));
		ShowInterfaces(out);
		OpenViewer(out.rider.Base(), "Interfaces", NewRuler());
		out.ConnectTo(NIL)
	END ShowInterfaceRecords;
	
	PROCEDURE UpdateInterfaceRecords*;
		VAR t, t0: TextModels.Model; out: TextMappers.Formatter;
	BEGIN
		t0 := TextViews.FocusText();
		Models.BeginModification(Models.notUndoable, t0);
		t0.Delete(0, t0.Length());	(* removes old object references from text *)
		Views.Update(TextViews.Focus(), Views.rebuildFrames);
		Windows.dir.Update(Windows.dir.First());	(* remove frame references *)
		Kernel.Collect;
		t := TextModels.CloneOf(t0); (*Stores.InitDomain(t, t0.domain);*) Stores.Join(t, t0);
		out.ConnectTo(t);
		ShowInterfaces(out);
		t0.Insert(0, t, 0, t.Length());
		Models.EndModification(Models.notUndoable, t0);
		out.ConnectTo(NIL)
	END UpdateInterfaceRecords;
	
	PROCEDURE ToggleAllInterfaces*;
	BEGIN
		all := ~all;
		UpdateInterfaceRecords
	END ToggleAllInterfaces;
	
	PROCEDURE ShowError*;
		VAR res: INTEGER; c: TextControllers.Controller; r: TextModels.Reader; f: TextMappers.Formatter;
			beg, end, i: INTEGER; str: ARRAY 1024 OF CHAR; ch: CHAR; s: ARRAY 64 OF CHAR;
	BEGIN
		c := TextControllers.Focus();
		IF (c # NIL) & c.HasSelection() THEN
			c.GetSelection(beg, end);
			r := c.text.NewReader(NIL);
			r.SetPos(beg); i := 0; r.ReadChar(ch);
			WHILE (beg + i < end) & (i < LEN(s) - 1) & (ch >= " ") DO s[i] := ch; INC(i); r.ReadChar(ch) END;
			s[i] := 0X;
			Strings.StringToInt(s, i, res);
			IF res = 0 THEN
				f.ConnectTo(TextModels.CloneOf(StdLog.buf));
				f.WriteString("Error Code: ");
				f.WriteIntForm(i, TextMappers.hexadecimal, 9, "0", TRUE);
				f.WriteLn;
				f.WriteString("(Facility: ");
				CASE i DIV 10000H MOD 2000H OF
				| 0: f.WriteString("NULL, ")
				| 1: f.WriteString("RPC, ")
				| 2: f.WriteString("DISPATCH, ")
				| 3: f.WriteString("STORAGE, ")
				| 4: f.WriteString("ITF, ")
				| 7: f.WriteString("WIN32, ")
				| 8: f.WriteString("WINDOWS, ")
				| 10: f.WriteString("CONTROL, ")
				ELSE f.WriteString("unknown, ")
				END;
				f.WriteString("Severity: ");
				IF i < 0 THEN f.WriteString("Error, ") ELSE f.WriteString("Success, ") END;
				f.WriteString("Code: ");
				f.WriteInt(i MOD 10000H);
				f.WriteChar(")"); f.WriteLn;
				f.WriteString("Description:");
				f.WriteLn;
				i := WinApi.FormatMessageW({12}, 0, i, 0, str, LEN(str), NIL);
				IF i > 0 THEN
					REPEAT DEC(i) UNTIL (i < 0) OR (str[i] >= " ");
					str[i + 1] := 0X;
				ELSE str := ""
				END;
				f.WriteString(str);
				f.WriteLn;
				OpenInfoViewer(f.rider.Base(), "Show Error");
				f.ConnectTo(NIL)
			END
		END
	END ShowError;
	
	PROCEDURE Hex (VAR f: TextMappers.Formatter; x, n: INTEGER);
	BEGIN
		IF n > 1 THEN Hex(f, x DIV 16, n - 1) END;
		x := x MOD 16;
		IF x >= 10 THEN f.WriteChar(CHR(x + ORD("A") - 10))
		ELSE f.WriteChar(CHR(x + ORD("0")))
		END
	END Hex;

	PROCEDURE NewGuid*;
		VAR f: TextMappers.Formatter; g: COM.GUID; res: COM.RESULT; n: INTEGER;
	BEGIN
		f.ConnectTo(TextModels.CloneOf(StdLog.buf)); n := 10;
		WHILE n > 0 DO
			res := WinOle.CoCreateGuid(g);
			f.WriteChar("{");
			Hex(f, g[2] MOD 256 + 256 * g[3] MOD 256, 4);
			Hex(f, g[0] MOD 256 + 256 * g[1] MOD 256, 4);
			f.WriteChar("-");
			Hex(f, g[4] MOD 256 + 256 * g[5] MOD 256, 4);
			f.WriteChar("-");
			Hex(f, g[6] MOD 256 + 256 * g[7] MOD 256, 4);
			f.WriteChar("-");
			Hex(f, g[8] MOD 256, 2);
			Hex(f, g[9] MOD 256, 2);
			f.WriteChar("-");
			Hex(f, g[10] MOD 256, 2);
			Hex(f, g[11] MOD 256, 2);
			Hex(f, g[12] MOD 256, 2);
			Hex(f, g[13] MOD 256, 2);
			Hex(f, g[14] MOD 256, 2);
			Hex(f, g[15] MOD 256, 2);
			f.WriteChar("}");
			f.WriteLn; DEC(n)
		END;
		OpenInfoViewer(f.rider.Base(), "Guids");
		f.ConnectTo(NIL)
	END NewGuid;

END ComDebug.
