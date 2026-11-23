MODULE CheckRastersPng;

	IMPORT  Log, Views, Files, Stores, Ports, StdRasters;

	PROCEDURE Copy*;
	VAR v: Views.View;
	BEGIN
		v := Views.OldView(Files.dir.This("System/Rsrc"), "About.odc");
		IF v = NIL THEN
			Log.String("Cannot open"); Log.Ln
		ELSE
			Views.RegisterView(v, Files.dir.This(""), "AboutCopy.odc")
		END
	END Copy;

	PROCEDURE Deposit* (IN path, name: ARRAY OF CHAR);
		VAR f: Files.File; s: Stores.Store;
	BEGIN
		f := Files.dir.Old(Files.dir.This(path), name$, Files.shared);
		IF f = NIL THEN
			Log.String("File not found"); Log.Ln
		ELSIF ~StdRasters.CanHandle(f.type) THEN 
			Log.String("Cannot handle file of type "); Log.String(f.type); Log.Ln
		ELSE StdRasters.Import(f, s); IF s # NIL THEN Views.Deposit(s(Views.View)) END
		END
	END Deposit;

	PROCEDURE RGBColor (color: Ports.Color; OUT red, green, blue: INTEGER);
	BEGIN
		blue := (color DIV 0FFFFH) MOD 0FFH;
		green := (color DIV 0FFH) MOD 0FFH;
		red := color MOD 0FFH;
	END RGBColor;

	PROCEDURE Count* (IN path, name: ARRAY OF CHAR);
		VAR rModel: StdRasters.Model; image: StdRasters.Image;
			i, j, r, g, b, a, counter: INTEGER;
	BEGIN
		rModel := StdRasters.NewModelFromSpec(Files.dir.This(path), name$, FALSE);
		IF rModel = NIL THEN
			Log.String("File not found"); Log.Ln
		ELSE
			image := StdRasters.Load(rModel.raster);
			counter := 0;
			FOR i := 0 TO image.height - 1 DO
				FOR j := 0 TO image.width - 1 DO
					RGBColor(image.data[i * image.width + j], r, g, b);
					IF (r = 0) & (g = 0) & (b = 0) THEN
						INC(counter)
					END
				END
			END;
			Log.Int((100 * counter) DIV (image.height * image.width)); Log.String("% no color"); Log.Ln	
		END
	END Count;

END CheckRastersPng.


"CheckRastersPng.Copy"

"CheckRastersPng.Deposit('Check/Rsrc', 'Logo.png'); StdCmds.Open"

"CheckRastersPng.Count('Check/Rsrc', 'Logo.png')"