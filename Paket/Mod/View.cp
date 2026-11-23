MODULE PaketView;
(**
	project	= "BlackBox 2.0, new module"
	organization	= "BlackBox Cross-Platform Team"
	contributors	= "Anton Shelin"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT M := PaketModel, Containers, Converters, Dialog, Files, Ports, StdDialog, StdLinks, StdLog, StdTextConv, TextMappers, TextModels, TextRulers, TextViews, Views, Windows;

	PROCEDURE PrintPackageList* (packages: M.PackageList);
		VAR q: M.PackageList; i: INTEGER;
	BEGIN
		q := packages;
		WHILE q # NIL DO
			FOR i := 0 TO 3 DO
				StdLog.String(q.package.data[i]);
				StdLog.String(" ");
			END;
			StdLog.Ln;
			q := q.next;
		END;
	END PrintPackageList;

	PROCEDURE WriteRuler  (VAR f:  TextMappers.Formatter);
		CONST cm = 10* Ports.mm; 	(* universal units *)
		VAR ruler: TextRulers.Ruler;
	BEGIN
		ruler := TextRulers.dir.New(NIL);
		TextRulers.AddTab(ruler, 4 * cm);
		TextRulers.AddTab(ruler, 8 * cm);
		f.WriteView(ruler)
	END WriteRuler;

	PROCEDURE RenderText (t: TextModels.Model; packages: M.PackageList);
		VAR
			c, deps: M.PackageList;
			f: TextMappers.Formatter;
			link: StdLinks.Link;
			default, blue: TextModels.Attributes;
			installString, updateString, noDescriptionString, depsString: Dialog.String;
			descriptionTag: INTEGER;

		PROCEDURE SetLanguageResources ();
		BEGIN
			IF Dialog.language = "ru" THEN
				descriptionTag := M.descriptionRuTag
			ELSE
				descriptionTag := M.descriptionEnTag
			END;
			Dialog.MapString("#Paket:Install", installString);
			Dialog.MapString("#Paket:Update", updateString);
			Dialog.MapString("#Paket:No description", noDescriptionString);
			Dialog.MapString("#Paket:Dependencies", depsString);
		END SetLanguageResources;

	BEGIN
		SetLanguageResources();
		f.ConnectTo(t);
		WriteRuler(f);
		default  :=  f.rider.attr;
		blue :=  TextModels.NewColor(default,  Ports.blue);
		c := packages;
		WHILE c # NIL DO
			f.WriteString(c.package.data[M.nameTag]);
			f.WriteTab;
			f.rider.SetAttr(blue);
			link  :=  StdLinks.dir.NewLink("PaketController.Download('" + c.package.data[M.urlTag] + "')");
			f.WriteView(link);
			(* flag isInstalled is not se here to allow full package reinstallation including all deps *)
			IF c.package.isInstalled THEN
				f.WriteString(updateString);
			ELSE
				f.WriteString(installString);
			END;
			link  :=  StdLinks.dir.NewLink("");
			f.WriteView(link);
			f.rider.SetAttr(default);
			f.WriteLn;
			IF LEN(c.package.data[descriptionTag]$) = 0 THEN
				f.WriteString(noDescriptionString);
			ELSE
				f.WriteString(c.package.data[descriptionTag]);
			END;
			IF c.package.depends # NIL THEN
				f.WriteLn;
				deps := c.package.depends;
				f.WriteString(depsString); f.WriteString(": ");
				WHILE deps # NIL DO
					f.WriteString(deps.package.data[M.nameTag]);
					f.WriteChar(' ');
					deps := deps.next;
				END;
			END;
			f.WriteLn;
			f.WriteLn;
			c := c.next;
		END
	END RenderText;

	PROCEDURE Open* (packages: M.PackageList);
		VAR loc: Files.Locator; name: Files.Name;
			title: Views.Title; t: TextModels.Model; v: Views.View;
			c: Containers.Controller; w: Windows.Window; conv: Converters.Converter;
	BEGIN
		loc := Files.dir.This("");
		Dialog.MapString("#Paket:Title", title);
		name := "packagemanager";
		w := Windows.GetBySpec(loc, name, Converters.list, {Windows.isAux});
		IF w # NIL THEN (* bring to front; avoid re-loading the document *)
			(*Windows.dir.Select(w, Windows.lazy);*)
			v := w.doc.ThisView();
			WITH v: TextViews.View DO
				t := v.ThisModel();
				t.Delete(0, t.Length());
				RenderText(t, packages);
			ELSE
			END;

		ELSE (* load document and open reusable aux window in Browser mode *)
			t := TextModels.dir.New();
			RenderText(t, packages);
			v := TextViews.dir.New(t);
			WITH v: Containers.View DO
				c := v.ThisController();
				IF c # NIL THEN
					c.SetOpts(c.opts - {Containers.noFocus, Containers.noSelection} + {Containers.noCaret})
				END
			ELSE
			END;
			title := title$;
			StdDialog.Open(v, title, loc, name, conv, FALSE, TRUE, FALSE, FALSE, FALSE)
		END
	END Open;

END PaketView.

