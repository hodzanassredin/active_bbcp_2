MODULE WinGdip ["gdiplus.dll"];
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch, blackbox.oberon.org"
	contributors	= "Oberon microsystems, Anton Dmitriev"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT SYSTEM, WinApi,WinOle;
	
	CONST
		GpDebugEventLevelFatal = 0; 
		GpDebugEventLevelWarning = 1;
		
	TYPE
		Unit* = INTEGER;
	CONST
		UnitWorld* = 0; UnitDisplay* = 1; UnitPixel* = 2; UnitPoint* = 3; UnitInch* = 4; 
		UnitDocument* = 5; UnitMillimeter* = 6;
	
	TYPE SmoothingMode* = INTEGER;
	CONST
		SmoothingModeNone* = 3; SmoothingModeAntiAlias* = 4;
	
	TYPE Status* = INTEGER;
	CONST
		Ok* = 0; GenericError* = 1; InvalidParameter* = 2; OutOfMemory* = 3; ObjectBusy* = 4; InsufficientBuffer* = 5; NotImplemented* = 6; Win32Error* = 7; WrongState* = 8; Aborted* = 9; FileNotFound* = 10; ValueOverflow* = 11; AccessDenied* = 12; UnknownImageFormat* = 13; FontFamilyNotFound* = 14; FontStyleNotFound* = 15; NotTrueTypeFont* = 16; UnsupportedGdiplusVersion* = 17; GdiplusNotInitialized* = 18; PropertyNotFound* = 19; PropertyNotSupported* = 20; ProfileNotFound* = 21;	
	
	TYPE DriverStringOptions* = INTEGER;
	CONST
		DriverStringOptionsCmapLookup* = 0; DriverStringOptionsVertical* = 1;
		DriverStringOptionsRealizedAdvance* = 2; DriverStringOptionsLimitSubpixel* = 3;
	
	CONST (* FontStyle *)
		FontStyleRegular* = {}; FontStyleBold* = { 0 }; FontStyleItalic* = { 1 }; FontStyleBoldItalic* = { 0, 1 };
		FontStyleUnderline* = { 2 }; FontStyleStrikeout* = { 3 };
	
	TYPE TextRenderingHint* = INTEGER;
	CONST
		TextRenderingHintSystemDefault* = 0; 
		TextRenderingHintSingleBitPerPixelGridFit* = 1;
		TextRenderingHintSingleBitPerPixel* = 2; TextRenderingHintAntiAliasGridFit* = 3;
		TextRenderingHintAntiAlias* = 4; TextRenderingHintClearTypeGridFit* = 5;

	TYPE FillMode* = INTEGER;
	CONST
		FillModeAlternate* = 0; FillModeWinding* = 1;
	
	TYPE LineJoin* = INTEGER;
	CONST
		LineJoinMiter* = 0; LineJoinBevel* = 1; LineJoinRound* = 2; LineJoinMiterClipped* = 3;
	
	TYPE DashStyle* = INTEGER;
	CONST
		DashStyleSolid* = 0; DashStyleDash* = 1; DashStyleDot* = 2; DashStyleDashDot* = 3;
		DashStyleDashDotDot* = 4; DashStyleCustom* = 5;
	
	TYPE PixelFormat* = SET;
	CONST
		PixelFormatAlpha* = BITS(40000H); PixelFormatGDI* = BITS(20000H);
		PixelFormatCanonical* = BITS(200000H);
		PixelFormat24bppRGB* = BITS(8) + BITS(ASH(24, 8)) + PixelFormatGDI;
		PixelFormat32bppRGB* = BITS(9) + BITS(ASH(32, 8)) + PixelFormatGDI;
		PixelFormat32bppARGB* = BITS(10) + BITS(ASH(32, 8)) + PixelFormatAlpha + PixelFormatGDI + PixelFormatCanonical;
	
	TYPE MatrixOrder* = INTEGER;
	CONST
		MatrixOrderPrepend* = 0;
		MatrixOrderAppend* = 1;
	
	TYPE CombineMode* = INTEGER;
	CONST
		CombineModeReplace* = 0; CombineModeIntersect* = 1; 
		CombineModeUnion* = 2; CombineModeXor* = 3; CombineModeExclude* = 4;
		CombineModeComplement* = 5;
		
	TYPE
		(*Gp*)Image* = POINTER TO ABSTRACT RECORD [untagged] END;
		(*Gp*)Graphics* = POINTER TO ABSTRACT RECORD [untagged] END;
		(*Gp*)Pen* = POINTER TO ABSTRACT RECORD [untagged] END;
		(*Gp*)Bitmap* = POINTER TO ABSTRACT RECORD [untagged] (Image) END;
		(*Gp*)Brush* = POINTER TO ABSTRACT RECORD [untagged] END;
		(*Gp*)SolidFill* = POINTER TO ABSTRACT RECORD [untagged] (Brush) END;
		(*Gp*)FontCollection* = POINTER TO ABSTRACT RECORD [untagged] END;
		(*Gp*)FontFamily* = POINTER TO ABSTRACT RECORD [untagged] END;
		(*Gp*)Font* = POINTER TO ABSTRACT RECORD [untagged] END;
		
		PointF* = RECORD x*, y*: SHORTREAL END;
		Point* = RECORD x*, y*: INTEGER END;
		RectF* = RECORD x*, y*, width*, height*: SHORTREAL END;
		Matrix* = POINTER TO ABSTRACT RECORD [untagged] END;
		Path* = POINTER TO ABSTRACT RECORD [untagged] END;
		
		ARGB* = INTEGER;
		ULONG_PTR = INTEGER;
		GpStatus = INTEGER;
		
		GpDebugEventLevel = INTEGER;

		GpDebugEventProc = PROCEDURE (level: GpDebugEventLevel; message: WinApi.PtrSTR);
	
		GdiplusStartupInput* = RECORD [untagged]
			GdiplusVersion*: INTEGER;             (* Must be 1  (or 2 for the Ex version)*)
			DebugEventCallback*: GpDebugEventProc; (* Ignored on free builds*)
			SuppressBackgroundThread*: WinApi.BOOL;   (* FALSE unless you're prepared to call the hook/unhook functions properly*)
			SuppressExternalCodecs*: WinApi.BOOL;       (* FALSE unless you want GDI+ only to use its internal image codecs.*)
		END;
	
		GpNotificationHookProc = PROCEDURE (OUT token: ULONG_PTR): GpStatus;
		GpNotificationUnhookProc = PROCEDURE (token: ULONG_PTR);
		
		GdiplusStartupOutput* = RECORD [untagged]	(** Output structure for GdiplusStartup() *)
			(* The following 2 fields are NULL if SuppressBackgroundThread is FALSE. Otherwise, they are functions which must be called appropriately to replace the background thread.
			These should be called on the application's main message loop - i.e. a message loop which is active for the lifetime of GDI+. "NotificationHook" should be called before starting the loop, and "NotificationUnhook" should be called after the loop ends. *) 
			NotificationHook: GpNotificationHookProc;
			NotificationUnhook: GpNotificationUnhookProc;
		END;


	PROCEDURE GdiplusStartup* (OUT token: ULONG_PTR; IN input: GdiplusStartupInput; OUT[nil] output: GdiplusStartupOutput ): GpStatus;
	(** GDI+ initialization. Must not be called from DllMain - can cause deadlock. Must be called before GDI+ API's or constructors are used. token  - may not be NULL - accepts a token to be passed in the corresponding GdiplusShutdown call. input  - may not be NULL. output - may be NULL only if input.SuppressBackgroundThread is FALSE. *)
	PROCEDURE GdiplusShutdown* (token: ULONG_PTR);
	(** GDI+ termination. Must be called before GDI+ is unloaded. Must not be called from DllMain - can cause deadlock. GDI+ API's may not be called after GdiplusShutdown. Pay careful attention to GDI+ object destructors. *)
	
	PROCEDURE LoadImageFromStream* ["GdipLoadImageFromStream"] (stream: WinOle.IStream; VAR image: Image): INTEGER;
	PROCEDURE CreateBitmapFromStream* ["GdipCreateBitmapFromStream"] (stream: WinOle.IStream; VAR bitmap: Bitmap): Status;
	PROCEDURE CreateBitmapFromScan0* ["GdipCreateBitmapFromScan0"] (width, height, stride: INTEGER; pixelFormat: PixelFormat; scan0: POINTER TO ARRAY [untagged] OF BYTE; VAR b: Bitmap): Status;
	
	PROCEDURE CreateHBITMAPFromBitmap* ["GdipCreateHBITMAPFromBitmap"] (bitmat: Bitmap; VAR hbitmap: WinApi.HBITMAP; background: ARGB): Status;
	
	PROCEDURE GetImageVerticalResolution* ["GdipGetImageVerticalResolution"] (img: Image; VAR ppi: SHORTREAL): INTEGER;
	PROCEDURE GetImageHorizontalResolution* ["GdipGetImageHorizontalResolution"] (img: Image; VAR ppi: SHORTREAL): INTEGER;
	
	PROCEDURE GetImageWidth* ["GdipGetImageWidth"] (img: Image; VAR wid: INTEGER): INTEGER;
	PROCEDURE GetImageHeight* ["GdipGetImageHeight"] (img: Image; VAR hei: INTEGER): INTEGER;

	(*PROCEDURE DrawImage* ["GdipDrawImage"] (GpGraphics*,GpImage*,REAL,REAL): INTEGER;*)
	PROCEDURE DrawImageI* ["GdipDrawImageI"] (gr: Graphics; img: Image; x, y: INTEGER): INTEGER;
	PROCEDURE DrawImageRectI* ["GdipDrawImageI"] (gr: Graphics; img: Image; x, y, wid, hei: INTEGER): INTEGER;
	PROCEDURE CreateFromHDC* ["GdipCreateFromHDC"] (dc: WinApi.HANDLE; VAR gr: Graphics): INTEGER;
	PROCEDURE DeleteGraphics* ["GdipDeleteGraphics"] (gr: Graphics): Status;
	
	PROCEDURE DisposeImage* ["GdipDisposeImage"] (img: Image): INTEGER;
	
	PROCEDURE DrawRectangleI* ["GdipDrawRectangleI"] (gr: Graphics; pen: Pen; x, y, wid, hei: INTEGER): INTEGER;
	
	PROCEDURE CreatePen1* ["GdipCreatePen1"] (col: ARGB; wid: SHORTREAL; unit: Unit; VAR pen: Pen): Status;
	(*GpStatus WINGDIPAPI GdipCreatePen2(GpBrush*,REAL,GpUnit,GpPen** );*)
	PROCEDURE DeletePen* ["GdipDeletePen"] (pen: Pen): Status;
	
	PROCEDURE DrawEllipse* ["GdipDrawEllipseI"] (gr: Graphics; pen: Pen; x, y, width, height: INTEGER): Status;
	PROCEDURE FillEllipse* ["GdipFillEllipseI"] (gr: Graphics; brush: Brush; x, y, width, height: INTEGER): Status;
	PROCEDURE FillRectangleI* ["GdipFillRectangleI"] (gr: Graphics; b: Brush; x, y, w, h: INTEGER): Status;
	PROCEDURE CreateSolidFill* ["GdipCreateSolidFill"] (col: ARGB; VAR brush: SolidFill): Status;
	PROCEDURE SetSolidFillColor* ["GdipSetSolidFillColor"] (b: Brush; col: ARGB): Status;
	
	PROCEDURE SetSmoothingMode* ["GdipSetSmoothingMode"] (gr: Graphics; mode: SmoothingMode): Status;
	PROCEDURE CreateFromHWND* ["GdipCreateFromHWND"] (hwnd: WinApi.HANDLE; VAR gr: Graphics): Status;
	PROCEDURE GetImageGraphicsContext* ["GdipGetImageGraphicsContext"] (img: Image; VAR gr: Graphics): Status;
	PROCEDURE TranslateWorldTransform* ["GdipTranslateWorldTransform"] (gr: Graphics; dx, dy: SHORTREAL; o: MatrixOrder): Status;
	PROCEDURE SetClipRectI* ["GdipSetClipRectI"] (gr: Graphics; l, t, w, h: INTEGER; m: CombineMode): Status;
	
	
	PROCEDURE CreateMatrix* ["GdipCreateMatrix"] (OUT m: Matrix): Status;
	PROCEDURE NewInstalledFontCollection* ["GdipNewInstalledFontCollection"] (OUT c: FontCollection): Status;
	PROCEDURE CreateFontFamily* ["GdipCreateFontFamily"] (IN typeface: ARRAY OF CHAR; c: FontCollection; OUT f: FontFamily): Status;
	PROCEDURE CreateFontFamilyFromName* ["GdipCreateFontFamilyFromName"] (IN name: ARRAY [untagged] OF CHAR; fontCollection: FontCollection; VAR fontFamily: FontFamily): Status;
	
	PROCEDURE CreateFont* ["GdipCreateFont"] (fontFamily: FontFamily; emSize: SHORTREAL; style: SET; unit: Unit;VAR font: Font): Status;
	PROCEDURE CreateFontFromDC* ["GdipCreateFontFromDC"] (dc: WinApi.HDC; VAR font: Font): Status;
	PROCEDURE CreateFontFromLogfontW* ["GdipCreateFontFromLogfontW"] (dc: WinApi.HDC; IN lfw: WinApi.LOGFONTW; VAR font: Font): Status;
	PROCEDURE DeleteFont* ["GdipDeleteFont"] (font: Font): Status;
	
	PROCEDURE DrawDriverString* ["GdipDrawDriverString"] (gr: Graphics; IN text: ARRAY [untagged] OF CHAR; length: INTEGER; font: Font; brush: Brush; IN [nil] positions: ARRAY [untagged] OF PointF; flags: SET; matrix: Matrix): Status;
	PROCEDURE MeasureDriverString* ["GdipMeasureDriverString"] (gr: Graphics; IN text: ARRAY [untagged] OF CHAR; length: INTEGER; font: Font; IN [nil] positions: ARRAY [untagged] OF PointF; flags: SET; matrix: Matrix; OUT r: RectF): Status;
	
	PROCEDURE SetTextRenderingHint* ["GdipSetTextRenderingHint"] (gr: Graphics; mode: TextRenderingHint): Status;
	PROCEDURE DrawLine* ["GdipDrawLineI"] (gr: Graphics; pen: Pen; x1, y1,  x2, y2: INTEGER): Status;
	PROCEDURE DrawLines* ["GdipDrawLinesI"] (gr: Graphics; pen: Pen; IN points: ARRAY [untagged] OF Point; count: INTEGER): Status;
	PROCEDURE DrawBeziers* ["GdipDrawBeziersI"] (gr: Graphics; pen: Pen; IN points: ARRAY [untagged] OF Point; count: INTEGER): Status;
	PROCEDURE DrawBezier* ["GdipDrawBezierI"] (gr: Graphics; pen: Pen; begx, begy, c1x, c1y, c2x, c2y, endx, endy: INTEGER): Status;
	PROCEDURE DrawPath* ["GdipDrawPath"] (g: Graphics; p: Pen; path: Path): Status;
	PROCEDURE FillPolygon* ["GdipFillPolygonI"] (gr: Graphics; brush: Brush; IN points: ARRAY [untagged] OF Point; count: INTEGER; fillMode: FillMode): Status;
	PROCEDURE FillPath* ["GdipFillPath"] (g: Graphics; b: Brush; p: Path): Status;
	PROCEDURE DrawPolygon* ["GdipDrawPolygonI"] (gr: Graphics; pen: Pen; IN points: ARRAY [untagged] OF Point; count: INTEGER): Status;
	
	PROCEDURE SetPenLineJoin* ["GdipSetPenLineJoin"] (pen: Pen; lineJoin: LineJoin): Status;
	PROCEDURE SetPenDashStyle* ["GdipSetPenDashStyle"] (p: Pen; s: DashStyle): Status;
	PROCEDURE SetPenColor* ["GdipSetPenColor"] (p: Pen; col: ARGB): Status;
	PROCEDURE SetPenWidth* ["GdipSetPenWidth"] (p: Pen; width: SHORTREAL): Status;
	PROCEDURE SetPenUnit* ["GdipSetPenUnit"] (p: Pen; unit: Unit): Status;
	
	PROCEDURE CreatePath* ["GdipCreatePath"] (fillMode: FillMode; OUT path: Path): Status;
	PROCEDURE DeletePath* ["GdipDeletePath"] (p: Path): Status;
	PROCEDURE DeleteBrush* ["GdipDeleteBrush"] (b: Brush): Status;
	
	PROCEDURE AddPathBeziers* ["GdipAddPathBeziersI"] (p: Path; IN points: ARRAY [untagged] OF Point; count: INTEGER): Status;
	PROCEDURE AddPathBezier* ["GdipAddPathBezierI"] (p: Path; begx, begy, c1x, c1y, c2x, c2y, endx, endy: INTEGER): Status;

END WinGdip.


https://docs.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-image-flat
