MODULE PaketController;
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

	IMPORT M := PaketModel, Files, Dialog, PaketFeed, PaketFiles, PaketReader, PaketView, StdCoder, StdLog, StdTextConv, Stores, TextMappers, TextModels, TextViews, StdMenuTool;

	VAR
		packages: M.PackageList;
		download: M.PackageList;
		downloading: M.Package;
		compile: M.PackageList;
		
	PROCEDURE FillListSilent(f: Files.File; url: ARRAY OF CHAR);
		VAR s: Stores.Store; sc: TextMappers.Scanner; str: Dialog.String;
	BEGIN
		Dialog.MapString("#Paket:DownloadComplete", str);
		StdLog.String(str); StdLog.Ln;
		StdTextConv.ImportUtf8(f, s);
		sc.ConnectTo(s(TextViews.View).ThisModel());
		sc.SetOpts(sc.opts + {TextMappers.returnCtrlChars});
		sc.SetPos(0);
		sc.Scan;
		packages := PaketReader.ReadPackages(sc,  - 1);
	END FillListSilent;

	PROCEDURE FillList (f: Files.File; url: ARRAY OF CHAR);
		VAR s: Stores.Store; sc: TextMappers.Scanner;
	BEGIN
		FillListSilent(f, url);
		PaketView.Open(packages);
	END FillList;
	
	PROCEDURE DownloadFeed* ();
	BEGIN
		PaketFeed.DownloadFeed(FillList);
	END DownloadFeed;
	
	PROCEDURE DownloadFeedSilent* ();
	BEGIN
		PaketFeed.DownloadFeed(FillListSilent);
	END DownloadFeedSilent;

	PROCEDURE ^ InstallPackage (f: Files.File; url: ARRAY OF CHAR);

	PROCEDURE Download* (packetUrl: TextMappers.String);
	BEGIN
		downloading := M.FindPackageBy(packages, M.urlTag, packetUrl + "");
		ASSERT(downloading # NIL);
		PaketFeed.DownloadExtension(packetUrl, InstallPackage)
	END Download;

	PROCEDURE Compile* ();
		VAR p: M.Package;
	BEGIN
		(* TODO Sort compile *)
		p := M.PopFromList(compile);
		WHILE p # NIL DO
			PaketFiles.CompilePackage(p);
			p := M.PopFromList(compile);
		END;
	END Compile;

	PROCEDURE InstallPackage (f: Files.File; url: ARRAY OF CHAR);
		VAR text: TextModels.Model; s: Stores.Store; ask: BOOLEAN;
			deps: M.PackageList; str: Dialog.String;
	BEGIN
		Dialog.MapString("#Paket:ExtractingPackage", str);
		StdLog.String(str + " " + downloading.data[M.nameTag]); StdLog.Ln;
		StdTextConv.ImportText(f, s);
		text := s(TextViews.View).ThisModel();
		ask := StdCoder.askOption;
		StdCoder.askOption := FALSE;
		StdCoder.DecodeAllFromText(text, 0, FALSE);
		StdCoder.askOption := ask;
		PaketFiles.SetDependencies(packages, downloading);
		downloading.isInstalled := TRUE;
		PaketFiles.OpenQuickStart(downloading);
		M.AddToList(compile, downloading, M.nameTag);
		deps := downloading.depends;

		WHILE deps # NIL DO
			IF ~deps.package.isInstalled THEN
				M.AddToList(download, deps.package, M.nameTag);
			END;
			deps := deps.next;
		END;

		downloading := M.PopFromList(download);
		IF downloading # NIL THEN
			Download(downloading.data[M.urlTag]);
		ELSE
			Compile();
			PaketView.Open(packages);
		END;
		
		StdMenuTool.UpdateAllMenus;

	END InstallPackage;

END PaketController.

PaketController.DownloadFeed


