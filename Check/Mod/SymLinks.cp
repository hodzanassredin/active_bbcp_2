MODULE CheckSymLinks;

	IMPORT Files, Dialog, Log, Kernel;

	PROCEDURE Do1*;
	BEGIN
		Dialog.RunExternal("cd Check/Extra && rm Loc Sym -rf && mkdir Loc && ln -s Loc Sym")
	END Do1;

	PROCEDURE Do2*;
	VAR f: Files.File; wr: Files.Writer; res: INTEGER; i: LONGINT;
	BEGIN
		f := Files.dir.New(Files.dir.This("Check/Extra/Loc"), Files.shared);
		wr := f.NewWriter(NIL);
		i := 0;
		WHILE i < 1024 DO
			wr.WriteByte(ORD("A"));
			INC(i);
		END;
		f.Register("file",  "txt", Files.dontAsk, res);
		f.Close;
		Kernel.Collect;
	END Do2;
	
	PROCEDURE Do3*;
	VAR f: Files.File; wr: Files.Writer; res: INTEGER; i: LONGINT;
	BEGIN
		f := Files.dir.New(Files.dir.This("Check/Extra/Sym"), Files.shared);
		wr := f.NewWriter(NIL);
		i := 0;
		WHILE i < 1024 DO
			wr.WriteByte(ORD("B"));
			INC(i);
		END;
		f.Register("file",  "txt", Files.dontAsk, res);
		f.Close;
		Kernel.Collect;
	END Do3;

END CheckSymLinks.

CheckSymLinks.Do1
CheckSymLinks.Do2
CheckSymLinks.Do3
