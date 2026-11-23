MODULE WinDlg ["comdlg32.dll"];

	IMPORT S := SYSTEM, WinApi;

	TYPE
		CCHOOKPROC* = PROCEDURE [ms64] (wnd: WinApi.HWND; msg: INTEGER; w: WinApi.WPARAM; l: WinApi.LPARAM): WinApi.WPARAM;
		CHOOSECOLORW* = RECORD [align8]
			lStructSize*: INTEGER;
			hwndOwner*: WinApi.HWND;
			hInstance*: WinApi.HINSTANCE;
			rgbResult*: WinApi.COLORREF;
			lpCustColors*: S.ADDRESS;
			Flags*: SET;
			lCustData*: WinApi.LPARAM;
			lpfnHook*: CCHOOKPROC;
			lpTemplateName*: WinApi.PtrWSTR
		END;
		PtrCHOOSECOLORW* = POINTER TO CHOOSECOLORW;

		OPENFILENAMEW* = RECORD [align8]
			size*: INTEGER; owner*, instance*, filter*, customFilter*: S.ADDRESS;
			maxCustom*, filterIndex*: INTEGER;
			file*: S.ADDRESS; maxFile*: INTEGER;
			fileTitle*: S.ADDRESS; maxTitle*: INTEGER;
			initialDir*, title*: S.ADDRESS; flags*: SET;
			fileOffset*, extensionOffset*: SHORTINT;
			defaultExt*, data*, hook*, template*, reserved*: S.ADDRESS;
			reservedWord*, flagsEx*: INTEGER
		END;

		OPENFILENAMEA* = OPENFILENAMEW; (* identical native layout; string addresses carry ANSI data *)
		PtrOPENFILENAMEA* = POINTER TO OPENFILENAMEA;
		PtrOPENFILENAMEW* = POINTER TO OPENFILENAMEW;
		PRINTHOOKPROC* = PROCEDURE [ms64] (wnd: WinApi.HWND; msg: INTEGER; w: WinApi.WPARAM; l: WinApi.LPARAM): WinApi.WPARAM;
		SETUPHOOKPROC* = PRINTHOOKPROC;
		PRINTDLGW* = RECORD [align8]
			lStructSize*: INTEGER;
			hwndOwner*: WinApi.HWND;
			hDevMode*: WinApi.HGLOBAL;
			hDevNames*: WinApi.HGLOBAL;
			hDC*: WinApi.HDC;
			Flags*: SET;
			nFromPage*: SHORTINT;
			nToPage*: SHORTINT;
			nMinPage*: SHORTINT;
			nMaxPage*: SHORTINT;
			nCopies*: SHORTINT;
			hInstance*: WinApi.HINSTANCE;
			lCustData*: WinApi.LPARAM;
			lpfnPrintHook*: PRINTHOOKPROC;
			lpfnSetupHook*: SETUPHOOKPROC;
			lpPrintTemplateName*: WinApi.PtrWSTR;
			lpSetupTemplateName*: WinApi.PtrWSTR;
			hPrintTemplate*: WinApi.HGLOBAL;
			hSetupTemplate*: WinApi.HGLOBAL;
		END;
		PtrPRINTDLGW* = POINTER TO PRINTDLGW;

	PROCEDURE [ms64] GetOpenFileNameW* (VAR file: OPENFILENAMEW): INTEGER;

	PROCEDURE [ms64] GetSaveFileNameW* (VAR file: OPENFILENAMEW): INTEGER;

	PROCEDURE [ms64] CommDlgExtendedError* (): INTEGER;
	PROCEDURE [ms64] ChooseColorW* (VAR [nil] color: CHOOSECOLORW): WinApi.BOOL;
	PROCEDURE [ms64] PrintDlgW* (VAR [nil] p: PRINTDLGW): INTEGER;

END WinDlg.
