MODULE WinOleDlg ["OLEDLG.dll"];
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

	IMPORT SYSTEM, WinDlg, WinOle, COM, WinApi;

	CONST (* macros *)
		IDC_OLEUIHELP* = 99;
		IDC_IO_CREATENEW* = 2100;
		IDC_IO_CREATEFROMFILE* = 2101;
		IDC_IO_LINKFILE* = 2102;
		IDC_IO_OBJECTTYPELIST* = 2103;
		IDC_IO_DISPLAYASICON* = 2104;
		IDC_IO_CHANGEICON* = 2105;
		IDC_IO_FILE* = 2106;
		IDC_IO_FILEDISPLAY* = 2107;
		IDC_IO_RESULTIMAGE* = 2108;
		IDC_IO_RESULTTEXT* = 2109;
		IDC_IO_ICONDISPLAY* = 2110;
		IDC_IO_OBJECTTYPETEXT* = 2111;
		IDC_IO_FILETEXT* = 2112;
		IDC_IO_FILETYPE* = 2113;
		IDC_IO_INSERTCONTROL* = 2114;
		IDC_IO_ADDCONTROL* = 2115;
		IDC_IO_CONTROLTYPELIST* = 2116;
		IDC_PS_PASTE* = 500;
		IDC_PS_PASTELINK* = 501;
		IDC_PS_SOURCETEXT* = 502;
		IDC_PS_PASTELIST* = 503;
		IDC_PS_PASTELINKLIST* = 504;
		IDC_PS_DISPLAYLIST* = 505;
		IDC_PS_DISPLAYASICON* = 506;
		IDC_PS_ICONDISPLAY* = 507;
		IDC_PS_CHANGEICON* = 508;
		IDC_PS_RESULTIMAGE* = 509;
		IDC_PS_RESULTTEXT* = 510;
		IDC_CI_GROUP* = 120;
		IDC_CI_CURRENT* = 121;
		IDC_CI_CURRENTICON* = 122;
		IDC_CI_DEFAULT* = 123;
		IDC_CI_DEFAULTICON* = 124;
		IDC_CI_FROMFILE* = 125;
		IDC_CI_FROMFILEEDIT* = 126;
		IDC_CI_ICONLIST* = 127;
		IDC_CI_LABEL* = 128;
		IDC_CI_LABELEDIT* = 129;
		IDC_CI_BROWSE* = 130;
		IDC_CI_ICONDISPLAY* = 131;
		IDC_CV_OBJECTTYPE* = 150;
		IDC_CV_DISPLAYASICON* = 152;
		IDC_CV_CHANGEICON* = 153;
		IDC_CV_ACTIVATELIST* = 154;
		IDC_CV_CONVERTTO* = 155;
		IDC_CV_ACTIVATEAS* = 156;
		IDC_CV_RESULTTEXT* = 157;
		IDC_CV_CONVERTLIST* = 158;
		IDC_CV_ICONDISPLAY* = 165;
		IDC_EL_CHANGESOURCE* = 201;
		IDC_EL_AUTOMATIC* = 202;
		IDC_EL_CANCELLINK* = 209;
		IDC_EL_UPDATENOW* = 210;
		IDC_EL_OPENSOURCE* = 211;
		IDC_EL_MANUAL* = 212;
		IDC_EL_LINKSOURCE* = 216;
		IDC_EL_LINKTYPE* = 217;
		IDC_EL_LINKSLISTBOX* = 206;
		IDC_EL_COL1* = 220;
		IDC_EL_COL2* = 221;
		IDC_EL_COL3* = 222;
		IDC_BZ_RETRY* = 600;
		IDC_BZ_ICON* = 601;
		IDC_BZ_MESSAGE1* = 602;
		IDC_BZ_SWITCHTO* = 604;
		IDC_UL_METER* = 1029;
		IDC_UL_STOP* = 1030;
		IDC_UL_PERCENT* = 1031;
		IDC_UL_PROGRESS* = 1032;
		IDC_PU_LINKS* = 900;
		IDC_PU_TEXT* = 901;
		IDC_PU_CONVERT* = 902;
		IDC_PU_ICON* = 908;
		IDC_GP_OBJECTNAME* = 1009;
		IDC_GP_OBJECTTYPE* = 1010;
		IDC_GP_OBJECTSIZE* = 1011;
		IDC_GP_CONVERT* = 1013;
		IDC_GP_OBJECTICON* = 1014;
		IDC_GP_OBJECTLOCATION* = 1022;
		IDC_VP_PERCENT* = 1000;
		IDC_VP_CHANGEICON* = 1001;
		IDC_VP_EDITABLE* = 1002;
		IDC_VP_ASICON* = 1003;
		IDC_VP_RELATIVE* = 1005;
		IDC_VP_SPIN* = 1006;
		IDC_VP_SCALETXT* = 1034;
		IDC_VP_ICONDISPLAY* = 1021;
		IDC_VP_RESULTIMAGE* = 1033;
		IDC_LP_OPENSOURCE* = 1006;
		IDC_LP_UPDATENOW* = 1007;
		IDC_LP_BREAKLINK* = 1008;
		IDC_LP_LINKSOURCE* = 1012;
		IDC_LP_CHANGESOURCE* = 1015;
		IDC_LP_AUTOMATIC* = 1016;
		IDC_LP_MANUAL* = 1017;
		IDC_LP_DATE* = 1018;
		IDC_LP_TIME* = 1019;
		IDD_INSERTOBJECT* = 1000;
		IDD_CHANGEICON* = 1001;
		IDD_CONVERT* = 1002;
		IDD_PASTESPECIAL* = 1003;
		IDD_EDITLINKS* = 1004;
		IDD_BUSY* = 1006;
		IDD_UPDATELINKS* = 1007;
		IDD_CHANGESOURCE* = 1009;
		IDD_INSERTFILEBROWSE* = 1010;
		IDD_CHANGEICONBROWSE* = 1011;
		IDD_CONVERTONLY* = 1012;
		IDD_CHANGESOURCE4* = 1013;
		IDD_GNRLPROPS* = 1100;
		IDD_VIEWPROPS* = 1101;
		IDD_LINKPROPS* = 1102;
		IDD_CONVERT4* = 1103;
		IDD_CONVERTONLY4* = 1104;
		IDD_EDITLINKS4* = 1105;
		IDD_GNRLPROPS4* = 1106;
		IDD_LINKPROPS4* = 1107;
		IDD_PASTESPECIAL4* = 1108;
		IDD_CANNOTUPDATELINK* = 1008;
		IDD_LINKSOURCEUNAVAILABLE* = 1020;
		IDD_SERVERNOTFOUND* = 1023;
		IDD_OUTOFMEMORY* = 1024;
		IDD_SERVERNOTREGW* = 1021;
		IDD_LINKTYPECHANGEDW* = 1022;
		IDD_SERVERNOTREGA* = 1025;
		IDD_LINKTYPECHANGEDA* = 1026;
		IDD_SERVERNOTREG* = 1025;
		IDD_LINKTYPECHANGED* = 1026;
		OLESTDDELIM* = "\";
		SZOLEUI_MSG_HELP* = "OLEUI_MSG_HELP";
		SZOLEUI_MSG_ENDDIALOG* = "OLEUI_MSG_ENDDIALOG";
		SZOLEUI_MSG_BROWSE* = "OLEUI_MSG_BROWSE";
		SZOLEUI_MSG_CHANGEICON* = "OLEUI_MSG_CHANGEICON";
		SZOLEUI_MSG_CLOSEBUSYDIALOG* = "OLEUI_MSG_CLOSEBUSYDIALOG";
		SZOLEUI_MSG_CONVERT* = "OLEUI_MSG_CONVERT";
		SZOLEUI_MSG_CHANGESOURCE* = "OLEUI_MSG_CHANGESOURCE";
		SZOLEUI_MSG_ADDCONTROL* = "OLEUI_MSG_ADDCONTROL";
		SZOLEUI_MSG_BROWSE_OFN* = "OLEUI_MSG_BROWSE_OFN";
		ID_BROWSE_CHANGEICON* = 1;
		ID_BROWSE_INSERTFILE* = 2;
		ID_BROWSE_ADDCONTROL* = 3;
		ID_BROWSE_CHANGESOURCE* = 4;
		OLEUI_FALSE* = 0;
		OLEUI_SUCCESS* = 1;
		OLEUI_OK* = 1;
		OLEUI_CANCEL* = 2;
		OLEUI_ERR_STANDARDMIN* = 100;
		OLEUI_ERR_OLEMEMALLOC* = 100;
		OLEUI_ERR_STRUCTURENULL* = 101;
		OLEUI_ERR_STRUCTUREINVALID* = 102;
		OLEUI_ERR_CBSTRUCTINCORRECT* = 103;
		OLEUI_ERR_HWNDOWNERINVALID* = 104;
		OLEUI_ERR_LPSZCAPTIONINVALID* = 105;
		OLEUI_ERR_LPFNHOOKINVALID* = 106;
		OLEUI_ERR_HINSTANCEINVALID* = 107;
		OLEUI_ERR_LPSZTEMPLATEINVALID* = 108;
		OLEUI_ERR_HRESOURCEINVALID* = 109;
		OLEUI_ERR_FINDTEMPLATEFAILURE* = 110;
		OLEUI_ERR_LOADTEMPLATEFAILURE* = 111;
		OLEUI_ERR_DIALOGFAILURE* = 112;
		OLEUI_ERR_LOCALMEMALLOC* = 113;
		OLEUI_ERR_GLOBALMEMALLOC* = 114;
		OLEUI_ERR_LOADSTRING* = 115;
		OLEUI_ERR_STANDARDMAX* = 116;
		IOF_SHOWHELP* = {0};
		IOF_SELECTCREATENEW* = {1};
		IOF_SELECTCREATEFROMFILE* = {2};
		IOF_CHECKLINK* = {3};
		IOF_CHECKDISPLAYASICON* = {4};
		IOF_CREATENEWOBJECT* = {5};
		IOF_CREATEFILEOBJECT* = {6};
		IOF_CREATELINKOBJECT* = {7};
		IOF_DISABLELINK* = {8};
		IOF_VERIFYSERVERSEXIST* = {9};
		IOF_DISABLEDISPLAYASICON* = {10};
		IOF_HIDECHANGEICON* = {11};
		IOF_SHOWINSERTCONTROL* = {12};
		IOF_SELECTCREATECONTROL* = {13};
		OLEUI_IOERR_LPSZFILEINVALID* = 116;
		OLEUI_IOERR_LPSZLABELINVALID* = 117;
		OLEUI_IOERR_HICONINVALID* = 118;
		OLEUI_IOERR_LPFORMATETCINVALID* = 119;
		OLEUI_IOERR_PPVOBJINVALID* = 120;
		OLEUI_IOERR_LPIOLECLIENTSITEINVALID* = 121;
		OLEUI_IOERR_LPISTORAGEINVALID* = 122;
		OLEUI_IOERR_SCODEHASERROR* = 123;
		OLEUI_IOERR_LPCLSIDEXCLUDEINVALID* = 124;
		OLEUI_IOERR_CCHFILEINVALID* = 125;
		PS_MAXLINKTYPES* = 8;
		PSF_SHOWHELP* = {0};
		PSF_SELECTPASTE* = {1};
		PSF_SELECTPASTELINK* = {2};
		PSF_CHECKDISPLAYASICON* = {3};
		PSF_DISABLEDISPLAYASICON* = {4};
		PSF_HIDECHANGEICON* = {5};
		PSF_STAYONCLIPBOARDCHANGE* = {6};
		PSF_NOREFRESHDATAOBJECT* = {7};
		OLEUI_IOERR_SRCDATAOBJECTINVALID* = 116;
		OLEUI_IOERR_ARRPASTEENTRIESINVALID* = 117;
		OLEUI_IOERR_ARRLINKTYPESINVALID* = 118;
		OLEUI_PSERR_CLIPBOARDCHANGED* = 119;
		OLEUI_PSERR_GETCLIPBOARDFAILED* = 120;
		OLEUI_ELERR_LINKCNTRNULL* = 116;
		OLEUI_ELERR_LINKCNTRINVALID* = 117;
		ELF_SHOWHELP* = {0};
		ELF_DISABLEUPDATENOW* = {1};
		ELF_DISABLEOPENSOURCE* = {2};
		ELF_DISABLECHANGESOURCE* = {3};
		ELF_DISABLECANCELLINK* = {4};
		CIF_SHOWHELP* = {0};
		CIF_SELECTCURRENT* = {1};
		CIF_SELECTDEFAULT* = {2};
		CIF_SELECTFROMFILE* = {3};
		CIF_USEICONEXE* = {4};
		OLEUI_CIERR_MUSTHAVECLSID* = 116;
		OLEUI_CIERR_MUSTHAVECURRENTMETAFILE* = 117;
		OLEUI_CIERR_SZICONEXEINVALID* = 118;
		PROP_HWND_CHGICONDLG* = "HWND_CIDLG";
		CF_SHOWHELPBUTTON* = {0};
		CF_SETCONVERTDEFAULT* = {1};
		CF_SETACTIVATEDEFAULT* = {2};
		CF_SELECTCONVERTTO* = {3};
		CF_SELECTACTIVATEAS* = {4};
		CF_DISABLEDISPLAYASICON* = {5};
		CF_DISABLEACTIVATEAS* = {6};
		CF_HIDECHANGEICON* = {7};
		CF_CONVERTONLY* = {8};
		OLEUI_CTERR_CLASSIDINVALID* = 117;
		OLEUI_CTERR_DVASPECTINVALID* = 118;
		OLEUI_CTERR_CBFORMATINVALID* = 119;
		OLEUI_CTERR_HMETAPICTINVALID* = 120;
		OLEUI_CTERR_STRINGINVALID* = 121;
		BZ_DISABLECANCELBUTTON* = {0};
		BZ_DISABLESWITCHTOBUTTON* = {1};
		BZ_DISABLERETRYBUTTON* = {2};
		BZ_NOTRESPONDINGDIALOG* = {3};
		OLEUI_BZERR_HTASKINVALID* = 116;
		OLEUI_BZ_SWITCHTOSELECTED* = 117;
		OLEUI_BZ_RETRYSELECTED* = 118;
		OLEUI_BZ_CALLUNBLOCKED* = 119;
		CSF_SHOWHELP* = {0};
		CSF_VALIDSOURCE* = {1};
		CSF_ONLYGETSOURCE* = {2};
		CSF_EXPLORER* = {3};
		OLEUI_CSERR_LINKCNTRNULL* = 116;
		OLEUI_CSERR_LINKCNTRINVALID* = 117;
		OLEUI_CSERR_FROMNOTNULL* = 118;
		OLEUI_CSERR_TONOTNULL* = 119;
		OLEUI_CSERR_SOURCENULL* = 120;
		OLEUI_CSERR_SOURCEINVALID* = 121;
		OLEUI_CSERR_SOURCEPARSERROR* = 122;
		OLEUI_CSERR_SOURCEPARSEERROR* = 122;
		VPF_SELECTRELATIVE* = {0};
		VPF_DISABLERELATIVE* = {1};
		VPF_DISABLESCALE* = {2};
		OPF_OBJECTISLINK* = {0};
		OPF_NOFILLDEFAULT* = {1};
		OPF_SHOWHELP* = {2};
		OPF_DISABLECONVERT* = {3};
		OLEUI_OPERR_SUBPROPNULL* = 116;
		OLEUI_OPERR_SUBPROPINVALID* = 117;
		OLEUI_OPERR_PROPSHEETNULL* = 118;
		OLEUI_OPERR_PROPSHEETINVALID* = 119;
		OLEUI_OPERR_SUPPROP* = 120;
		OLEUI_OPERR_PROPSINVALID* = 121;
		OLEUI_OPERR_PAGESINCORRECT* = 122;
		OLEUI_OPERR_INVALIDPAGES* = 123;
		OLEUI_OPERR_NOTSUPPORTED* = 124;
		OLEUI_OPERR_DLGPROCNOTNULL* = 125;
		OLEUI_OPERR_LPARAMNOTZERO* = 126;
		OLEUI_GPERR_STRINGINVALID* = 127;
		OLEUI_GPERR_CLASSIDINVALID* = 128;
		OLEUI_GPERR_LPCLSIDEXCLUDEINVALID* = 129;
		OLEUI_GPERR_CBFORMATINVALID* = 130;
		OLEUI_VPERR_METAPICTINVALID* = 131;
		OLEUI_VPERR_DVASPECTINVALID* = 132;
		OLEUI_LPERR_LINKCNTRNULL* = 133;
		OLEUI_LPERR_LINKCNTRINVALID* = 134;
		OLEUI_OPERR_PROPERTYSHEET* = 135;
		OLEUI_OPERR_OBJINFOINVALID* = 136;
		OLEUI_OPERR_LINKINFOINVALID* = 137;
		OLEUI_QUERY_GETCLASSID* = 65280;
		OLEUI_QUERY_LINKBROKEN* = 65281;

	CONST (* enumerations *)
		OLEUIPASTE_ENABLEICON* = {11};
		OLEUIPASTE_PASTEONLY* = {};
		OLEUIPASTE_PASTE* = {9};
		OLEUIPASTE_LINKANYTYPE* = {10};
		OLEUIPASTE_LINKTYPE1* = {0};
		OLEUIPASTE_LINKTYPE2* = {1};
		OLEUIPASTE_LINKTYPE3* = {2};
		OLEUIPASTE_LINKTYPE4* = {3};
		OLEUIPASTE_LINKTYPE5* = {4};
		OLEUIPASTE_LINKTYPE6* = {5};
		OLEUIPASTE_LINKTYPE7* = {6};
		OLEUIPASTE_LINKTYPE8* = {7};

	TYPE
		FNOLEUIHOOK* = PROCEDURE [ms64] (p0: WinApi.HWND; p1: INTEGER; p2: WinApi.WPARAM; p3: WinApi.LPARAM): INTEGER;
		OLEUIINSERTOBJECTW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrWSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrWSTR;
			hResource*: WinApi.HRSRC;
			clsid*: COM.GUID;
			lpszFile*: WinApi.PtrWSTR;
			cchFile*: INTEGER;
			cClsidExclude*: INTEGER;
			lpClsidExclude*: POINTER TO (*?*) ARRAY [untagged] OF COM.GUID;
			iid*: COM.GUID;
			oleRender*: INTEGER;
			lpFormatEtc*: WinOle.PtrFORMATETC;
			lpIOleClientSite*: WinOle.IOleClientSite;
			lpIStorage*: WinOle.IStorage;
			ppvObj*: INTEGER;
			sc*: COM.RESULT;
			hMetaPict*: WinApi.HGLOBAL;
		END;
		PtrOLEUIINSERTOBJECTW* = POINTER TO OLEUIINSERTOBJECTW;
		OLEUIINSERTOBJECTA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrSTR;
			hResource*: WinApi.HRSRC;
			clsid*: COM.GUID;
			lpszFile*: WinApi.PtrSTR;
			cchFile*: INTEGER;
			cClsidExclude*: INTEGER;
			lpClsidExclude*: POINTER TO (*?*) ARRAY [untagged] OF COM.GUID;
			iid*: COM.GUID;
			oleRender*: INTEGER;
			lpFormatEtc*: WinOle.PtrFORMATETC;
			lpIOleClientSite*: WinOle.IOleClientSite;
			lpIStorage*: WinOle.IStorage;
			ppvObj*: INTEGER;
			sc*: COM.RESULT;
			hMetaPict*: WinApi.HGLOBAL;
		END;
		PtrOLEUIINSERTOBJECTA* = POINTER TO OLEUIINSERTOBJECTA;
		tagOLEUIINSERTOBJECT* = OLEUIINSERTOBJECTA; (*m*)
		PtrtagOLEUIINSERTOBJECT* = PtrOLEUIINSERTOBJECTA;
		OLEUIINSERTOBJECT* = OLEUIINSERTOBJECTA; (*m*)
		PtrOLEUIINSERTOBJECT* = PtrOLEUIINSERTOBJECTA;
		OLEUIPASTEFLAG* = INTEGER;
		OLEUIPASTEENTRYW* = RECORD [align8]
			fmtetc*: WinOle.FORMATETC;
			lpstrFormatName*: WinApi.PtrWSTR;
			lpstrResultText*: WinApi.PtrWSTR;
			dwFlags*: SET;
			dwScratchSpace*: INTEGER;
		END;
		PtrOLEUIPASTEENTRYW* = POINTER TO OLEUIPASTEENTRYW;
		OLEUIPASTEENTRYA* = RECORD [align8]
			fmtetc*: WinOle.FORMATETC;
			lpstrFormatName*: WinApi.PtrSTR;
			lpstrResultText*: WinApi.PtrSTR;
			dwFlags*: SET;
			dwScratchSpace*: INTEGER;
		END;
		PtrOLEUIPASTEENTRYA* = POINTER TO OLEUIPASTEENTRYA;
		tagOLEUIPASTEENTRY* = OLEUIPASTEENTRYA; (*m*)
		PtrtagOLEUIPASTEENTRY* = PtrOLEUIPASTEENTRYA;
		OLEUIPASTEENTRY* = OLEUIPASTEENTRYA; (*m*)
		PtrOLEUIPASTEENTRY* = PtrOLEUIPASTEENTRYA;
		OLEUIPASTESPECIALW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrWSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrWSTR;
			hResource*: WinApi.HRSRC;
			lpSrcDataObj*: WinOle.IDataObject;
			arrPasteEntries*: PtrOLEUIPASTEENTRYW;
			cPasteEntries*: INTEGER;
			arrLinkTypes*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			cLinkTypes*: INTEGER;
			cClsidExclude*: INTEGER;
			lpClsidExclude*: POINTER TO (*?*) ARRAY [untagged] OF COM.GUID;
			nSelectedIndex*: INTEGER;
			fLink*: WinApi.BOOL;
			hMetaPict*: WinApi.HGLOBAL;
			sizel*: WinApi.SIZE;
		END;
		PtrOLEUIPASTESPECIALW* = POINTER TO OLEUIPASTESPECIALW;
		OLEUIPASTESPECIALA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrSTR;
			hResource*: WinApi.HRSRC;
			lpSrcDataObj*: WinOle.IDataObject;
			arrPasteEntries*: PtrOLEUIPASTEENTRYA;
			cPasteEntries*: INTEGER;
			arrLinkTypes*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			cLinkTypes*: INTEGER;
			cClsidExclude*: INTEGER;
			lpClsidExclude*: POINTER TO (*?*) ARRAY [untagged] OF COM.GUID;
			nSelectedIndex*: INTEGER;
			fLink*: WinApi.BOOL;
			hMetaPict*: WinApi.HGLOBAL;
			sizel*: WinApi.SIZE;
		END;
		PtrOLEUIPASTESPECIALA* = POINTER TO OLEUIPASTESPECIALA;
		tagOLEUIPASTESPECIAL* = OLEUIPASTESPECIALA; (*m*)
		PtrtagOLEUIPASTESPECIAL* = PtrOLEUIPASTESPECIALA;
		OLEUIPASTESPECIAL* = OLEUIPASTESPECIALA; (*m*)
		PtrOLEUIPASTESPECIAL* = PtrOLEUIPASTESPECIALA;
		IOleUILinkContainerW* = POINTER TO ABSTRACT RECORD [interface] (COM.IUnknown)
		END;
		IOleUILinkContainerA* = POINTER TO ABSTRACT RECORD [interface] (COM.IUnknown)
		END;
		IOleUILinkContainer* = IOleUILinkContainerA;
		OLEUIEDITLINKSW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrWSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrWSTR;
			hResource*: WinApi.HRSRC;
			lpOleUILinkContainer*: IOleUILinkContainerW;
		END;
		PtrOLEUIEDITLINKSW* = POINTER TO OLEUIEDITLINKSW;
		OLEUIEDITLINKSA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrSTR;
			hResource*: WinApi.HRSRC;
			lpOleUILinkContainer*: IOleUILinkContainerA;
		END;
		PtrOLEUIEDITLINKSA* = POINTER TO OLEUIEDITLINKSA;
		tagOLEUIEDITLINKS* = OLEUIEDITLINKSA; (*m*)
		PtrtagOLEUIEDITLINKS* = PtrOLEUIEDITLINKSA;
		OLEUIEDITLINKS* = OLEUIEDITLINKSA; (*m*)
		PtrOLEUIEDITLINKS* = PtrOLEUIEDITLINKSA;
		OLEUICHANGEICONW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrWSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrWSTR;
			hResource*: WinApi.HRSRC;
			hMetaPict*: WinApi.HGLOBAL;
			clsid*: COM.GUID;
			szIconExe*: ARRAY [untagged] 260 OF CHAR;
			cchIconExe*: INTEGER;
		END;
		PtrOLEUICHANGEICONW* = POINTER TO OLEUICHANGEICONW;
		OLEUICHANGEICONA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrSTR;
			hResource*: WinApi.HRSRC;
			hMetaPict*: WinApi.HGLOBAL;
			clsid*: COM.GUID;
			szIconExe*: ARRAY [untagged] 260 OF SHORTCHAR;
			cchIconExe*: INTEGER;
		END;
		PtrOLEUICHANGEICONA* = POINTER TO OLEUICHANGEICONA;
		tagOLEUICHANGEICON* = OLEUICHANGEICONA; (*m*)
		PtrtagOLEUICHANGEICON* = PtrOLEUICHANGEICONA;
		OLEUICHANGEICON* = OLEUICHANGEICONA; (*m*)
		PtrOLEUICHANGEICON* = PtrOLEUICHANGEICONA;
		OLEUICONVERTW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrWSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrWSTR;
			hResource*: WinApi.HRSRC;
			clsid*: COM.GUID;
			clsidConvertDefault*: COM.GUID;
			clsidActivateDefault*: COM.GUID;
			clsidNew*: COM.GUID;
			dvAspect*: SET;
			wFormat*: SHORTINT;
			fIsLinkedObject*: WinApi.BOOL;
			hMetaPict*: WinApi.HGLOBAL;
			lpszUserType*: WinApi.PtrWSTR;
			fObjectsIconChanged*: WinApi.BOOL;
			lpszDefLabel*: WinApi.PtrWSTR;
			cClsidExclude*: INTEGER;
			lpClsidExclude*: POINTER TO (*?*) ARRAY [untagged] OF COM.GUID;
		END;
		PtrOLEUICONVERTW* = POINTER TO OLEUICONVERTW;
		OLEUICONVERTA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrSTR;
			hResource*: WinApi.HRSRC;
			clsid*: COM.GUID;
			clsidConvertDefault*: COM.GUID;
			clsidActivateDefault*: COM.GUID;
			clsidNew*: COM.GUID;
			dvAspect*: SET;
			wFormat*: SHORTINT;
			fIsLinkedObject*: WinApi.BOOL;
			hMetaPict*: WinApi.HGLOBAL;
			lpszUserType*: WinApi.PtrSTR;
			fObjectsIconChanged*: WinApi.BOOL;
			lpszDefLabel*: WinApi.PtrSTR;
			cClsidExclude*: INTEGER;
			lpClsidExclude*: POINTER TO (*?*) ARRAY [untagged] OF COM.GUID;
		END;
		PtrOLEUICONVERTA* = POINTER TO OLEUICONVERTA;
		tagOLEUICONVERT* = OLEUICONVERTA; (*m*)
		PtrtagOLEUICONVERT* = PtrOLEUICONVERTA;
		OLEUICONVERT* = OLEUICONVERTA; (*m*)
		PtrOLEUICONVERT* = PtrOLEUICONVERTA;
		OLEUIBUSYW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrWSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrWSTR;
			hResource*: WinApi.HRSRC;
			hTask*: WinApi.HTASK;
			lphWndDialog*: POINTER TO (*?*) ARRAY [untagged] OF WinApi.HWND;
		END;
		PtrOLEUIBUSYW* = POINTER TO OLEUIBUSYW;
		OLEUIBUSYA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrSTR;
			hResource*: WinApi.HRSRC;
			hTask*: WinApi.HTASK;
			lphWndDialog*: POINTER TO (*?*) ARRAY [untagged] OF WinApi.HWND;
		END;
		PtrOLEUIBUSYA* = POINTER TO OLEUIBUSYA;
		tagOLEUIBUSY* = OLEUIBUSYA; (*m*)
		PtrtagOLEUIBUSY* = PtrOLEUIBUSYA;
		OLEUIBUSY* = OLEUIBUSYA; (*m*)
		PtrOLEUIBUSY* = PtrOLEUIBUSYA;
		OLEUICHANGESOURCEW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrWSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrWSTR;
			hResource*: WinApi.HRSRC;
			lpOFN*: WinDlg.PtrOPENFILENAMEW;
			dwReserved1*: ARRAY [untagged] 4 OF INTEGER;
			lpOleUILinkContainer*: IOleUILinkContainerW;
			dwLink*: INTEGER;
			lpszDisplayName*: WinApi.PtrWSTR;
			nFileLength*: INTEGER;
			lpszFrom*: WinApi.PtrWSTR;
			lpszTo*: WinApi.PtrWSTR;
		END;
		PtrOLEUICHANGESOURCEW* = POINTER TO OLEUICHANGESOURCEW;
		OLEUICHANGESOURCEA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			hWndOwner*: WinApi.HWND;
			lpszCaption*: WinApi.PtrSTR;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			hInstance*: WinApi.HINSTANCE;
			lpszTemplate*: WinApi.PtrSTR;
			hResource*: WinApi.HRSRC;
			lpOFN*: WinDlg.PtrOPENFILENAMEA;
			dwReserved1*: ARRAY [untagged] 4 OF INTEGER;
			lpOleUILinkContainer*: IOleUILinkContainerA;
			dwLink*: INTEGER;
			lpszDisplayName*: WinApi.PtrSTR;
			nFileLength*: INTEGER;
			lpszFrom*: WinApi.PtrSTR;
			lpszTo*: WinApi.PtrSTR;
		END;
		PtrOLEUICHANGESOURCEA* = POINTER TO OLEUICHANGESOURCEA;
		tagOLEUICHANGESOURCE* = OLEUICHANGESOURCEA; (*m*)
		PtrtagOLEUICHANGESOURCE* = PtrOLEUICHANGESOURCEA;
		OLEUICHANGESOURCE* = OLEUICHANGESOURCEA; (*m*)
		PtrOLEUICHANGESOURCE* = PtrOLEUICHANGESOURCEA;
		IOleUIObjInfoW* = POINTER TO ABSTRACT RECORD [interface] (COM.IUnknown)
		END;
		IOleUIObjInfoA* = POINTER TO ABSTRACT RECORD [interface] (COM.IUnknown)
		END;
		IOleUIObjInfo* = IOleUIObjInfoA;
		IOleUILinkInfoW* = POINTER TO ABSTRACT RECORD [interface] (IOleUILinkContainerW)
		END;
		IOleUILinkInfoA* = POINTER TO ABSTRACT RECORD [interface] (IOleUILinkContainerA)
		END;
		IOleUILinkInfo* = IOleUILinkInfoA;
		PtrOLEUIOBJECTPROPSW* = POINTER TO OLEUIOBJECTPROPSW;
		OLEUIGNRLPROPSW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			dwReserved1*: ARRAY [untagged] 2 OF INTEGER;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			dwReserved2*: ARRAY [untagged] 3 OF INTEGER;
			lpOP*: PtrOLEUIOBJECTPROPSW;
		END;
		PtrOLEUIGNRLPROPSW* = POINTER TO OLEUIGNRLPROPSW;
		PtrOLEUIOBJECTPROPSA* = POINTER TO OLEUIOBJECTPROPSA;
		OLEUIGNRLPROPSA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			dwReserved1*: ARRAY [untagged] 2 OF INTEGER;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			dwReserved2*: ARRAY [untagged] 3 OF INTEGER;
			lpOP*: PtrOLEUIOBJECTPROPSA;
		END;
		PtrOLEUIGNRLPROPSA* = POINTER TO OLEUIGNRLPROPSA;
		tagOLEUIGNRLPROPS* = OLEUIGNRLPROPSA; (*m*)
		PtrtagOLEUIGNRLPROPS* = PtrOLEUIGNRLPROPSA;
		OLEUIGNRLPROPS* = OLEUIGNRLPROPSA; (*m*)
		PtrOLEUIGNRLPROPS* = PtrOLEUIGNRLPROPSA;
		OLEUIVIEWPROPSW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			dwReserved1*: ARRAY [untagged] 2 OF INTEGER;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			dwReserved2*: ARRAY [untagged] 3 OF INTEGER;
			lpOP*: PtrOLEUIOBJECTPROPSW;
			nScaleMin*: INTEGER;
			nScaleMax*: INTEGER;
		END;
		PtrOLEUIVIEWPROPSW* = POINTER TO OLEUIVIEWPROPSW;
		OLEUIVIEWPROPSA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			dwReserved1*: ARRAY [untagged] 2 OF INTEGER;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			dwReserved2*: ARRAY [untagged] 3 OF INTEGER;
			lpOP*: PtrOLEUIOBJECTPROPSA;
			nScaleMin*: INTEGER;
			nScaleMax*: INTEGER;
		END;
		PtrOLEUIVIEWPROPSA* = POINTER TO OLEUIVIEWPROPSA;
		tagOLEUIVIEWPROPS* = OLEUIVIEWPROPSA; (*m*)
		PtrtagOLEUIVIEWPROPS* = PtrOLEUIVIEWPROPSA;
		OLEUIVIEWPROPS* = OLEUIVIEWPROPSA; (*m*)
		PtrOLEUIVIEWPROPS* = PtrOLEUIVIEWPROPSA;
		OLEUILINKPROPSW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			dwReserved1*: ARRAY [untagged] 2 OF INTEGER;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			dwReserved2*: ARRAY [untagged] 3 OF INTEGER;
			lpOP*: PtrOLEUIOBJECTPROPSW;
		END;
		PtrOLEUILINKPROPSW* = POINTER TO OLEUILINKPROPSW;
		OLEUILINKPROPSA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			dwReserved1*: ARRAY [untagged] 2 OF INTEGER;
			lpfnHook*: FNOLEUIHOOK;
			lCustData*: WinApi.LPARAM;
			dwReserved2*: ARRAY [untagged] 3 OF INTEGER;
			lpOP*: PtrOLEUIOBJECTPROPSA;
		END;
		PtrOLEUILINKPROPSA* = POINTER TO OLEUILINKPROPSA;
		tagOLEUILINKPROPS* = OLEUILINKPROPSA; (*m*)
		PtrtagOLEUILINKPROPS* = PtrOLEUILINKPROPSA;
		OLEUILINKPROPS* = OLEUILINKPROPSA; (*m*)
		PtrOLEUILINKPROPS* = PtrOLEUILINKPROPSA;
		OLEUIOBJECTPROPSW* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			lpPS*: WinApi.PtrPROPSHEETHEADERW;
			dwObject*: INTEGER;
			lpObjInfo*: IOleUIObjInfoW;
			dwLink*: INTEGER;
			lpLinkInfo*: IOleUILinkInfoW;
			lpGP*: PtrOLEUIGNRLPROPSW;
			lpVP*: PtrOLEUIVIEWPROPSW;
			lpLP*: PtrOLEUILINKPROPSW;
		END;
		OLEUIOBJECTPROPSA* = RECORD [align8]
			cbStruct*: INTEGER;
			dwFlags*: SET;
			lpPS*: WinApi.PtrPROPSHEETHEADERA;
			dwObject*: INTEGER;
			lpObjInfo*: IOleUIObjInfoA;
			dwLink*: INTEGER;
			lpLinkInfo*: IOleUILinkInfoA;
			lpGP*: PtrOLEUIGNRLPROPSA;
			lpVP*: PtrOLEUIVIEWPROPSA;
			lpLP*: PtrOLEUILINKPROPSA;
		END;
		tagOLEUIOBJECTPROPS* = OLEUIOBJECTPROPSA; (*m*)
		PtrtagOLEUIOBJECTPROPS* = PtrOLEUIOBJECTPROPSA;
		OLEUIOBJECTPROPS* = OLEUIOBJECTPROPSA; (*m*)
		PtrOLEUIOBJECTPROPS* = PtrOLEUIOBJECTPROPSA;

	PROCEDURE [ms64] (this: IOleUILinkContainerW) GetNextLink* (dwLink: INTEGER): INTEGER, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerW) SetLinkUpdateOptions* (dwLink: INTEGER; dwUpdateOpt: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerW) GetLinkUpdateOptions* (dwLink: INTEGER; VAR [nil] lpdwUpdateOpt: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerW) SetLinkSource* (dwLink: INTEGER; lpszDisplayName: WinApi.PtrWSTR; lenFileName: INTEGER; VAR [nil] pchEaten: INTEGER; fValidateSource: WinApi.BOOL): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerW) GetLinkSource* (dwLink: INTEGER; VAR [nil] lplpszDisplayName: WinApi.PtrWSTR; VAR [nil] lplenFileName: INTEGER; VAR [nil] lplpszFullLinkType: WinApi.PtrWSTR; VAR [nil] lplpszShortLinkType: WinApi.PtrWSTR; VAR [nil] lpfSourceAvailable: WinApi.BOOL; VAR [nil] lpfIsSelected: WinApi.BOOL): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerW) OpenLinkSource* (dwLink: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerW) UpdateLink* (dwLink: INTEGER; fErrorMessage: WinApi.BOOL; fReserved: WinApi.BOOL): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerW) CancelLink* (dwLink: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerA) GetNextLink* (dwLink: INTEGER): INTEGER, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerA) SetLinkUpdateOptions* (dwLink: INTEGER; dwUpdateOpt: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerA) GetLinkUpdateOptions* (dwLink: INTEGER; VAR [nil] lpdwUpdateOpt: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerA) SetLinkSource* (dwLink: INTEGER; lpszDisplayName: WinApi.PtrSTR; lenFileName: INTEGER; VAR [nil] pchEaten: INTEGER; fValidateSource: WinApi.BOOL): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerA) GetLinkSource* (dwLink: INTEGER; VAR [nil] lplpszDisplayName: WinApi.PtrSTR; VAR [nil] lplenFileName: INTEGER; VAR [nil] lplpszFullLinkType: WinApi.PtrSTR; VAR [nil] lplpszShortLinkType: WinApi.PtrSTR; VAR [nil] lpfSourceAvailable: WinApi.BOOL; VAR [nil] lpfIsSelected: WinApi.BOOL): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerA) OpenLinkSource* (dwLink: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerA) UpdateLink* (dwLink: INTEGER; fErrorMessage: WinApi.BOOL; fReserved: WinApi.BOOL): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkContainerA) CancelLink* (dwLink: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoW) GetObjectInfo* (dwObject: INTEGER; VAR [nil] lpdwObjSize: INTEGER; VAR [nil] lplpszLabel: WinApi.PtrWSTR; VAR [nil] lplpszType: WinApi.PtrWSTR; VAR [nil] lplpszShortType: WinApi.PtrWSTR; VAR [nil] lplpszLocation: WinApi.PtrWSTR): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoW) GetConvertInfo* (dwObject: INTEGER; VAR [nil] lpClassID: COM.GUID; VAR [nil] lpwFormat: SHORTINT; VAR [nil] lpConvertDefaultClassID: COM.GUID; VAR [nil] lplpClsidExclude: POINTER TO (*?*) ARRAY [untagged] OF COM.GUID; VAR [nil] lpcClsidExclude: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoW) ConvertObject* (dwObject: INTEGER; IN [nil] clsidNew: COM.GUID): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoW) GetViewInfo* (dwObject: INTEGER; VAR [nil] phMetaPict: WinApi.HGLOBAL; VAR [nil] pdvAspect: INTEGER; VAR [nil] pnCurrentScale: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoW) SetViewInfo* (dwObject: INTEGER; hMetaPict: WinApi.HGLOBAL; dvAspect: SET; nCurrentScale: INTEGER; bRelativeToOrig: WinApi.BOOL): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoA) GetObjectInfo* (dwObject: INTEGER; VAR [nil] lpdwObjSize: INTEGER; VAR [nil] lplpszLabel: WinApi.PtrSTR; VAR [nil] lplpszType: WinApi.PtrSTR; VAR [nil] lplpszShortType: WinApi.PtrSTR; VAR [nil] lplpszLocation: WinApi.PtrSTR): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoA) GetConvertInfo* (dwObject: INTEGER; VAR [nil] lpClassID: COM.GUID; VAR [nil] lpwFormat: SHORTINT; VAR [nil] lpConvertDefaultClassID: COM.GUID; VAR [nil] lplpClsidExclude: POINTER TO (*?*) ARRAY [untagged] OF COM.GUID; VAR [nil] lpcClsidExclude: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoA) ConvertObject* (dwObject: INTEGER; IN [nil] clsidNew: COM.GUID): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoA) GetViewInfo* (dwObject: INTEGER; VAR [nil] phMetaPict: WinApi.HGLOBAL; VAR [nil] pdvAspect: INTEGER; VAR [nil] pnCurrentScale: INTEGER): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUIObjInfoA) SetViewInfo* (dwObject: INTEGER; hMetaPict: WinApi.HGLOBAL; dvAspect: SET; nCurrentScale: INTEGER; bRelativeToOrig: WinApi.BOOL): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkInfoW) GetLastUpdate* (dwLink: INTEGER; VAR [nil] lpLastUpdate: WinApi.FILETIME): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] (this: IOleUILinkInfoA) GetLastUpdate* (dwLink: INTEGER; VAR [nil] lpLastUpdate: WinApi.FILETIME): COM.RESULT, NEW, ABSTRACT;

	PROCEDURE [ms64] OleUIAddVerbMenuW* (lpOleObj: WinOle.IOleObject; lpszShortType: WinApi.PtrWSTR; hMenu: WinApi.HMENU; uPos: INTEGER; uIDVerbMin: INTEGER; uIDVerbMax: INTEGER; bAddConvert: WinApi.BOOL; idConvert: INTEGER; VAR [nil] lphMenu: WinApi.HMENU): WinApi.BOOL;

	PROCEDURE [ms64] OleUIAddVerbMenuA* (lpOleObj: WinOle.IOleObject; lpszShortType: WinApi.PtrSTR; hMenu: WinApi.HMENU; uPos: INTEGER; uIDVerbMin: INTEGER; uIDVerbMax: INTEGER; bAddConvert: WinApi.BOOL; idConvert: INTEGER; VAR [nil] lphMenu: WinApi.HMENU): WinApi.BOOL;

	PROCEDURE [ms64] OleUIAddVerbMenu* ["OleUIAddVerbMenuA"] (lpOleObj: WinOle.IOleObject; lpszShortType: WinApi.PtrSTR; hMenu: WinApi.HMENU; uPos: INTEGER; uIDVerbMin: INTEGER; uIDVerbMax: INTEGER; bAddConvert: WinApi.BOOL; idConvert: INTEGER; VAR [nil] lphMenu: WinApi.HMENU): WinApi.BOOL;

	PROCEDURE [ms64] OleUIInsertObjectW* (VAR [nil] p0: OLEUIINSERTOBJECTW): INTEGER;

	PROCEDURE [ms64] OleUIInsertObjectA* (VAR [nil] p0: OLEUIINSERTOBJECTA): INTEGER;

	PROCEDURE [ms64] OleUIInsertObject* ["OleUIInsertObjectA"] (VAR [nil] p0: OLEUIINSERTOBJECTA): INTEGER;

	PROCEDURE [ms64] OleUIPasteSpecialW* (VAR [nil] p0: OLEUIPASTESPECIALW): INTEGER;

	PROCEDURE [ms64] OleUIPasteSpecialA* (VAR [nil] p0: OLEUIPASTESPECIALA): INTEGER;

	PROCEDURE [ms64] OleUIPasteSpecial* ["OleUIPasteSpecialA"] (VAR [nil] p0: OLEUIPASTESPECIALA): INTEGER;

	PROCEDURE [ms64] OleUIEditLinksW* (VAR [nil] p0: OLEUIEDITLINKSW): INTEGER;

	PROCEDURE [ms64] OleUIEditLinksA* (VAR [nil] p0: OLEUIEDITLINKSA): INTEGER;

	PROCEDURE [ms64] OleUIEditLinks* ["OleUIEditLinksA"] (VAR [nil] p0: OLEUIEDITLINKSA): INTEGER;

	PROCEDURE [ms64] OleUIChangeIconW* (VAR [nil] p0: OLEUICHANGEICONW): INTEGER;

	PROCEDURE [ms64] OleUIChangeIconA* (VAR [nil] p0: OLEUICHANGEICONA): INTEGER;

	PROCEDURE [ms64] OleUIChangeIcon* ["OleUIChangeIconA"] (VAR [nil] p0: OLEUICHANGEICONA): INTEGER;

	PROCEDURE [ms64] OleUIConvertW* (VAR [nil] p0: OLEUICONVERTW): INTEGER;

	PROCEDURE [ms64] OleUIConvertA* (VAR [nil] p0: OLEUICONVERTA): INTEGER;

	PROCEDURE [ms64] OleUIConvert* ["OleUIConvertA"] (VAR [nil] p0: OLEUICONVERTA): INTEGER;

	PROCEDURE [ms64] OleUICanConvertOrActivateAs* (IN [nil] rClsid: COM.GUID; fIsLinkedObject: WinApi.BOOL; wFormat: SHORTINT): WinApi.BOOL;

	PROCEDURE [ms64] OleUIBusyW* (VAR [nil] p0: OLEUIBUSYW): INTEGER;

	PROCEDURE [ms64] OleUIBusyA* (VAR [nil] p0: OLEUIBUSYA): INTEGER;

	PROCEDURE [ms64] OleUIBusy* ["OleUIBusyA"] (VAR [nil] p0: OLEUIBUSYA): INTEGER;

	PROCEDURE [ms64] OleUIChangeSourceW* (VAR [nil] p0: OLEUICHANGESOURCEW): INTEGER;

	PROCEDURE [ms64] OleUIChangeSourceA* (VAR [nil] p0: OLEUICHANGESOURCEA): INTEGER;

	PROCEDURE [ms64] OleUIChangeSource* ["OleUIChangeSourceA"] (VAR [nil] p0: OLEUICHANGESOURCEA): INTEGER;

	PROCEDURE [ms64] OleUIObjectPropertiesW* (VAR [nil] p0: OLEUIOBJECTPROPSW): INTEGER;

	PROCEDURE [ms64] OleUIObjectPropertiesA* (VAR [nil] p0: OLEUIOBJECTPROPSA): INTEGER;

	PROCEDURE [ms64] OleUIObjectProperties* ["OleUIObjectPropertiesA"] (VAR [nil] p0: OLEUIOBJECTPROPSA): INTEGER;

	PROCEDURE [ms64] OleUIPromptUserW* (nTemplate: INTEGER; hwndParent: WinApi.HWND): INTEGER;

	PROCEDURE [ms64] OleUIPromptUserA* (nTemplate: INTEGER; hwndParent: WinApi.HWND): INTEGER;

	PROCEDURE [ms64] OleUIPromptUser* ["OleUIPromptUserA"] (nTemplate: INTEGER; hwndParent: WinApi.HWND): INTEGER;

	PROCEDURE [ms64] OleUIUpdateLinksW* (lpOleUILinkCntr: IOleUILinkContainerW; hwndParent: WinApi.HWND; lpszTitle: WinApi.PtrWSTR; cLinks: INTEGER): WinApi.BOOL;

	PROCEDURE [ms64] OleUIUpdateLinksA* (lpOleUILinkCntr: IOleUILinkContainerA; hwndParent: WinApi.HWND; lpszTitle: WinApi.PtrSTR; cLinks: INTEGER): WinApi.BOOL;

	PROCEDURE [ms64] OleUIUpdateLinks* ["OleUIUpdateLinksA"] (lpOleUILinkCntr: IOleUILinkContainerA; hwndParent: WinApi.HWND; lpszTitle: WinApi.PtrSTR; cLinks: INTEGER): WinApi.BOOL;

END WinOleDlg.
