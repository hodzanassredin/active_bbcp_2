StdRasters

DEFINITION StdRasters;

	IMPORT Files, Ports, Stores, Models, Views;

	CONST
		bilinear = 1;
		deep = TRUE;
		nearest = 0;
		noSwap = FALSE;
		reference = TRUE;
		shallow = FALSE;
		swap = TRUE;

	TYPE
		Directory = POINTER TO ABSTRACT RECORD 
			type-: Files.Type;
			(d: Directory) HandlesFType (IN type: Files.Type): BOOLEAN, NEW, ABSTRACT;
			(d: Directory) New (): Raster, NEW, ABSTRACT
		END;

		Image = POINTER TO RECORD 
			width, height, dpiX, dpiY: INTEGER;
			data: POINTER TO RasterData
		END;

		ImageLoader = POINTER TO ABSTRACT RECORD 
			(ldr: ImageLoader) Detect (rd: Files.Reader): BOOLEAN, NEW, ABSTRACT;
			(ldr: ImageLoader) GetInfo (rd: Files.Reader): Image, NEW, ABSTRACT;
			(ldr: ImageLoader) Load (rd: Files.Reader): Image, NEW, ABSTRACT
		END;

		Model = POINTER TO RECORD (Models.Model)
			raster-: Raster;
			type-: Files.Type;
			(m: Model) CopyFrom- (source: Stores.Store);
			(m: Model) Externalize- (VAR wr: Stores.Writer);
			(m: Model) Internalize- (VAR rd: Stores.Reader)
		END;

		Raster = POINTER TO ABSTRACT RECORD (Ports.Raster)
			rd-: Files.Reader;
			beg-, end-: INTEGER;
			(r: Raster) InitFileRange-, NEW, EMPTY
		END;

		View = POINTER TO RECORD (Views.View)
			(v: View) CopyFromModelView- (source: Views.View; model: Models.Model);
			(v: View) Externalize- (VAR wr: Stores.Writer);
			(v: View) HandlePropMsg- (VAR msg: Views.PropMessage);
			(v: View) Internalize- (VAR rd: Stores.Reader);
			(v: View) Restore (f: Views.Frame; _l, _t, _r, _b: INTEGER)
		END;

		RasterData = ARRAY OF Ports.Color;

	PROCEDURE AddDirectory (d: Directory; type: Files.Type; loader: ImageLoader);
	PROCEDURE CanHandle (IN ftype: Files.Type): BOOLEAN;
	PROCEDURE Import (f: Files.File; OUT s: Stores.Store);
	PROCEDURE InitFromFileRange (r: Raster; rd: Files.Reader; beg, end: INTEGER; copy: BOOLEAN);
	PROCEDURE Load (r: Raster): Image;
	PROCEDURE New (type: Files.Type): Raster;
	PROCEDURE NewModel (r: Raster; IN type: Files.Type): Model;
	PROCEDURE NewModelFromSpec (loc: Files.Locator; IN fname: Files.Name; reference: BOOLEAN): Model;
	PROCEDURE NewView (m: Model): View;
	PROCEDURE Scale (IN src: RasterData; srcw, srch, sx, sy, sw, sh: INTEGER;
		VAR dst: RasterData; dstw, dsth, dx, dy, dw, dh: INTEGER; swapColor: BOOLEAN; q: INTEGER);
	PROCEDURE ScaleBlend (IN src: RasterData; srcw, srch, sx, sy, sw, sh: INTEGER;
		VAR dst: RasterData; dstw, dsth, dx, dy, dw, dh: INTEGER; swapColor: BOOLEAN; q: INTEGER);

END StdRasters.


TYPE
Image 


Model <> Raster 1:1				Model  <<> Raster   N:1

If rasters are immutable, N:1 will do. But if they are not - which they may very well be not immutable, shall we desire to add any sophistication - then M:1 is no good.

Raster has a storage presentation (most likely, a packed one), and a memory presentation. The latter is actually of no interest to, well, any parts of the system except for where rasters are actually drawn or edited. Thus, when a raster is to be copied, this refers only to the storage presentation.

As a storage presentation, a file range will suffice. And it doesn't have to reside in memory! Thus, when a Raster is copied, a new file (range) for writing has to be provided. Until the copied Raster is externalized (at which point it is provided with an actual Stores.Writer), it's file range can be... well, actually, it can reside in a temporary file!!! Which is a) reasonable and b) very quick to do and c) could be replaced with in-memory files, shall there be a need for that!!! Whohoa! And there is no need to mess with file names, due to the fantastic BB file notion! Awesome!