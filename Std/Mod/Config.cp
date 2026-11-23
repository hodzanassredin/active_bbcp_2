MODULE StdConfig;
(**
	project	= "BlackBox 2.0"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, A. Dmitriev, I. Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT Kernel, Converters, Dialog, Documents, Views, Ports, Containers, Windows, 
		StdMenus, StdMenuTool, StdTiles, StdGrids, StdWindows, StdDocuments;


	PROCEDURE Router (IN title: ARRAY OF CHAR);
	VAR 
		trapTitle: ARRAY 64 OF CHAR;
	
		PROCEDURE EndsWith (IN str, patt: ARRAY OF CHAR): BOOLEAN;
			VAR k, i: INTEGER; res: BOOLEAN;
		BEGIN
			i := LEN(patt$); k := LEN(str$);
			IF k >= i THEN
				REPEAT DEC(i); DEC(k); res := str[k] = patt[i] UNTIL ~res OR (i = 0)
			ELSE res := FALSE
			END;
			RETURN res
		END EndsWith;
		
		PROCEDURE StartsWith (IN str, patt: ARRAY OF CHAR): BOOLEAN;
			VAR i, k: INTEGER; res: BOOLEAN;
		BEGIN
			i := LEN(patt$); k := LEN(str$);
			IF k >= i THEN
				k := 0; 
				REPEAT res := patt[k] = str[k]; INC(k) UNTIL ~res OR (k = i)
			ELSE res := FALSE
			END;
			RETURN res
		END StartsWith;
	
	BEGIN
		IF title = ">>>" THEN
			(* rout sub windows by current target track *)
			IF StdTiles.targetTrack = "User" THEN
				StdTiles.SetTarget("Aux")
			ELSIF (StdTiles.targetTrack = "Aux") OR (StdTiles.targetTrack = "System") THEN
				StdTiles.SetTarget("User")
			ELSE
				StdTiles.SetTarget("")
			END
		ELSE
			(* rout new windows by title *)
			Dialog.MapString("#Dev:Trap", trapTitle);
			IF (title = "Variables") OR (title = trapTitle) OR (title = "Heap Object")
				OR EndsWith(title, " interface)") OR StartsWith(title, "<") THEN
				StdTiles.SetTarget("Aux")
			ELSIF (title = "#Dev:Log") OR (title = "Log") THEN
				StdTiles.SetTarget("System")
			ELSE
				StdTiles.SetTarget("User")
			END;
		END
	END Router;
	

	PROCEDURE CanDrop (v_: StdGrids.View; ind_: INTEGER): BOOLEAN;
	(* Forbids dropping new views into v; needed by the root and unnamed dividers *)
	BEGIN 
		RETURN FALSE
	END CanDrop;

	PROCEDURE SetupWorkspace;
		VAR
			right: StdGrids.View; root: StdGrids.View;	(* root divider *)
			w: StdWindows.Window; doc: Documents.Document; mainLine, u: Views.View;
			system, user, aux: StdGrids.View; bd: StdWindows.BackendDirectory;
			c: Containers.Controller; tmsg: StdGrids.TrackPropMsg;
			screenWidth, screenHeight: INTEGER;
	BEGIN
		root := StdGrids.NewDivider(StdGrids.horizontal);
		root.canDrop := CanDrop;
		system := StdGrids.NewStack();
		user := StdGrids.NewStack();
		aux := StdGrids.NewStack();
		
		tmsg.name := 'System'; Views.HandlePropMsg(system, tmsg);
		tmsg.name := 'Root'; Views.HandlePropMsg(root, tmsg);
		tmsg.name := 'User'; Views.HandlePropMsg(user, tmsg);
		tmsg.name := 'Aux'; Views.HandlePropMsg(aux, tmsg);
		
		right := StdGrids.NewDivider(StdGrids.horizontal); right.canDrop := CanDrop;
		StdGrids.AppendDivTile(right, user, 180 * Ports.mm, 0);
		StdGrids.AppendDivTile(right, aux, 0, 0);
		
		StdGrids.AppendDivTile(root, system, 90 * Ports.mm, 0); 
		StdGrids.AppendDivTile(root, right, 0, 0);
		
		bd := StdWindows.backendDir;
		StdWindows.SetBackendDir(StdWindows.stdBackendDir);
		
		w := StdWindows.stdDir.New()(StdWindows.Window); ASSERT(w.port # NIL);
		IF bd # StdWindows.backendDir THEN StdWindows.SetBackendDir(bd) END;

		Windows.dir.GetBounds(screenWidth, screenHeight);
		Windows.dir.l := screenWidth DIV 10;
		Windows.dir.t := screenHeight DIV 10; 
		Windows.dir.r := screenWidth - screenWidth DIV 10;
		Windows.dir.b := screenHeight - screenHeight DIV 10; 
		
		doc := Documents.dir.New(root, Views.undefined, Views.undefined);
		c := doc.ThisController();
		c.SetOpts(c.opts - {Documents.pageWidth..Documents.winHeight} + 
			{Documents.winHeight, Documents.winWidth}
		);
		Windows.dir.Open(w, doc, {Windows.isAux, Windows.neverDirty,
			Windows.noVScroll, Windows.noHScroll}, Dialog.appName$, NIL, "", NIL
		);
		
		WITH doc: StdDocuments.Document DO 
			mainLine := StdMenus.NewMainLine();
			StdDocuments.InsertBar(doc, StdDocuments.top, mainLine, NIL, TRUE, NIL);
			StdMenus.DepositStatusBar;
			Views.Fetch(u);
			IF u # NIL THEN
				StdDocuments.InsertBar(doc, StdDocuments.bottom, u, NIL, FALSE, NIL)
			END
		ELSE HALT(61)
		END;
		StdTiles.BindCloseNotifier(w);
	
	END SetupWorkspace;

	(*
	PROCEDURE SecondHead;
		VAR root, stack, ref: StdGrids.View; (* root divider *) n: INTEGER;
	BEGIN
		StdWindows.ext.GetNumHeads(n);
		IF n > 1 THEN
			root := StdGrids.NewDivider(StdGrids.horizontal); 
			root.canDrop := CanDrop;
			ref := StdGrids.NewDivider(StdGrids.vertical); 
			ref.canDrop := CanDrop;
			stack := StdGrids.NewStack();
			stack.canDrop := CanDrop;
			StdGrids.AppendDivTile(root, stack, 0, 5000); 
			StdGrids.AppendDivTile(root, ref, 0, 10000); 
			StdWindows.ext.ToHead(wb, 2);
			StdWindows.ext.Maximize(wb)
		END
	END SecondHead;
	*)

	PROCEDURE Setup*;
		VAR res: INTEGER; par: ARRAY 16 OF CHAR;
	BEGIN
		StdWindows.Init;
		StdDocuments.Install;
		Converters.Register(
			"StdDocuments.ImportDocument", "StdDocuments.ExportDocument", "", "odc", {});
		StdMenus.Install;
		
		StdMenuTool.UpdateAllMenus;
		
		StdWindows.border := 0;
		StdTiles.On;
		SetupWorkspace;
		StdTiles.SetWindowsRouter(Router);
		
		Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "utf8", {Converters.importAll});
		Converters.Register("StdTextConv.ImportText", "StdTextConv.ExportText", "TextViews.View", "txt", {});
		Converters.Register("StdTextConv.ImportUnicode", "StdTextConv.ExportUnicode", "TextViews.View", "utf", {});
		Converters.Register("StdTextConv.ImportDosText", "", "TextViews.View", "txt", {});
		Converters.Register("StdTextConv.ImportHex", "", "TextViews.View", "dat", {Converters.importAll});
		Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "xml", {});
		Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "html", {});
		Converters.Register("DevBrowser.ImportSymFile", "", "TextViews.View", "osf", {});
		Converters.Register("DevBrowser.ImportCodeFile", "", "TextViews.View", "ocf", {});
		Converters.Register("WinBitmaps.ImportBitmap", "WinBitmaps.ExportBitmap", "WinBitmaps.StdView", "bmp", {});
		Converters.Register("", "XhtmlExporter.ExportText", "TextViews.View", "html", {});
		Converters.Register("StdRasters.Import", "", "StdRasters.View", "png", {});
		
		Dialog.Call("StdLog.Open", "", res);
		
		Dialog.Call("StdFilesBrowser.SetupPlaces", "", res);
		Dialog.Call("StdColorDialog.Init", "", res);
		
		Dialog.GetPar("DEBUG", par, res);
		Kernel.debug := res = 0;
		
	END Setup;

END StdConfig.
