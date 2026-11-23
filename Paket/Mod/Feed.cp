MODULE PaketFeed;
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

	IMPORT Files, PaketHttp, Dialog, StdLog, TextMappers;

	TYPE
		Handler = PROCEDURE (f: Files.File; data: ARRAY OF CHAR);

	CONST
		feedUrl = "http://blackbox.oberon.org/publist";

	PROCEDURE DownloadFeed* (fillList: Handler);
		VAR d: PaketHttp.Downloading; str: Dialog.String;
	BEGIN
		Dialog.MapString("#Paket:GetList", str);
		StdLog.Ln; StdLog.String(str); StdLog.Ln;
		StdLog.String(feedUrl); StdLog.Ln;
		d := PaketHttp.NewDownloading(feedUrl);
		d.onFinish := fillList;
		d.silent := TRUE;
		d.Start;
	END DownloadFeed;

	PROCEDURE DownloadExtension* (packetUrl: TextMappers.String; InstallPackage: Handler);
		VAR d: PaketHttp.Downloading; str: Dialog.String;
	BEGIN
		Dialog.MapString("#Paket:GetExtension", str);
		StdLog.Ln; StdLog.String(str); StdLog.Ln;
		StdLog.String(packetUrl); StdLog.Ln;
		d := PaketHttp.NewDownloading(packetUrl);
		d.onFinish := InstallPackage;
		d.silent := TRUE;
		d.Start;
	END DownloadExtension;

END PaketFeed.


