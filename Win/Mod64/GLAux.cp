MODULE WinGLAux ["Glaux.dll"];
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

	IMPORT SYSTEM, WinGL;

	CONST (* macros *)
		AUX_RGB* = 0;
		AUX_RGBA* = 0;
		AUX_INDEX* = 1;
		AUX_SINGLE* = 0;
		AUX_DOUBLE* = 2;
		AUX_DIRECT* = 0;
		AUX_INDIRECT* = 4;
		AUX_ACCUM* = 8;
		AUX_ALPHA* = 16;
		AUX_DEPTH24* = 32;
		AUX_STENCIL* = 64;
		AUX_AUX* = 128;
		AUX_DEPTH16* = 256;
		AUX_FIXED_332_PAL* = 512;
		AUX_DEPTH* = 256;
		AUX_EXPOSE* = 1;
		AUX_CONFIG* = 2;
		AUX_DRAW* = 4;
		AUX_KEYEVENT* = 8;
		AUX_MOUSEDOWN* = 16;
		AUX_MOUSEUP* = 32;
		AUX_MOUSELOC* = 64;
		AUX_WINDOWX* = 0;
		AUX_WINDOWY* = 1;
		AUX_MOUSEX* = 0;
		AUX_MOUSEY* = 1;
		AUX_MOUSESTATUS* = 3;
		AUX_KEY* = 0;
		AUX_KEYSTATUS* = 1;
		AUX_LEFTBUTTON* = 1;
		AUX_RIGHTBUTTON* = 2;
		AUX_MIDDLEBUTTON* = 4;
		AUX_SHIFT* = 1;
		AUX_CONTROL* = 2;
		AUX_RETURN* = 13;
		AUX_ESCAPE* = 27;
		AUX_SPACE* = 32;
		AUX_LEFT* = 37;
		AUX_UP* = 38;
		AUX_RIGHT* = 39;
		AUX_DOWN* = 40;
		AUX_A* = 65;
		AUX_B* = 66;
		AUX_C* = 67;
		AUX_D* = 68;
		AUX_E* = 69;
		AUX_F* = 70;
		AUX_G* = 71;
		AUX_H* = 72;
		AUX_I* = 73;
		AUX_J* = 74;
		AUX_K* = 75;
		AUX_L* = 76;
		AUX_M* = 77;
		AUX_N* = 78;
		AUX_O* = 79;
		AUX_P* = 80;
		AUX_Q* = 81;
		AUX_R* = 82;
		AUX_S* = 83;
		AUX_T* = 84;
		AUX_U* = 85;
		AUX_V* = 86;
		AUX_W* = 87;
		AUX_X* = 88;
		AUX_Y* = 89;
		AUX_Z* = 90;
		AUX_a* = 97;
		AUX_b* = 98;
		AUX_c* = 99;
		AUX_d* = 100;
		AUX_e* = 101;
		AUX_f* = 102;
		AUX_g* = 103;
		AUX_h* = 104;
		AUX_i* = 105;
		AUX_j* = 106;
		AUX_k* = 107;
		AUX_l* = 108;
		AUX_m* = 109;
		AUX_n* = 110;
		AUX_o* = 111;
		AUX_p* = 112;
		AUX_q* = 113;
		AUX_r* = 114;
		AUX_s* = 115;
		AUX_t* = 116;
		AUX_u* = 117;
		AUX_v* = 118;
		AUX_w* = 119;
		AUX_x* = 120;
		AUX_y* = 121;
		AUX_z* = 122;
		AUX_0* = 48;
		AUX_1* = 49;
		AUX_2* = 50;
		AUX_3* = 51;
		AUX_4* = 52;
		AUX_5* = 53;
		AUX_6* = 54;
		AUX_7* = 55;
		AUX_8* = 56;
		AUX_9* = 57;
		AUX_FD* = 1;
		AUX_COLORMAP* = 3;
		AUX_GREYSCALEMAP* = 4;
		AUX_FOGMAP* = 5;
		AUX_ONECOLOR* = 6;
		AUX_BLACK* = 0;
		AUX_RED* = 13;
		AUX_GREEN* = 14;
		AUX_YELLOW* = 15;
		AUX_BLUE* = 16;
		AUX_MAGENTA* = 17;
		AUX_CYAN* = 18;
		AUX_WHITE* = 19;

	CONST (* enumerations *)
		AUX_USE_ID* = 1;
		AUX_EXACT_MATCH* = 2;
		AUX_MINIMUM_CRITERIA* = 3;

	TYPE
		PtrSTR* = POINTER TO ARRAY [untagged] OF SHORTCHAR;
		PtrWSTR* = POINTER TO ARRAY [untagged] OF CHAR;
		HWND* = SYSTEM.ADDRESS;
		HDC* = SYSTEM.ADDRESS;
		HGLRC* = SYSTEM.ADDRESS;
		AUX_EVENTREC* = RECORD [align8]
			event*: WinGL.GLint;
			data*: ARRAY [untagged] 4 OF WinGL.GLint;
		END;
		PtrAUX_EVENTREC* = POINTER TO AUX_EVENTREC;
		AUX_RGBImageRec* = RECORD [align8]
			sizeX*: WinGL.GLint;
			sizeY*: WinGL.GLint;
			data*: PtrSTR;
		END;
		PtrAUX_RGBImageRec* = POINTER TO AUX_RGBImageRec;
		AUXMAINPROC* = PROCEDURE [ms64] ();
		AUXEXPOSEPROC* = PROCEDURE [ms64] (p0: INTEGER; p1: INTEGER);
		AUXRESHAPEPROC* = PROCEDURE [ms64] (p0: WinGL.GLsizei; p1: WinGL.GLsizei);
		AUXIDLEPROC* = PROCEDURE [ms64] ();
		AUXKEYPROC* = PROCEDURE [ms64] ();
		AUXMOUSEPROC* = PROCEDURE [ms64] (VAR [nil] p0: AUX_EVENTREC);

	PROCEDURE [ms64] auxInitDisplayMode* (p0: WinGL.GLenum);

	PROCEDURE [ms64] auxInitPosition* (p0: INTEGER; p1: INTEGER; p2: INTEGER; p3: INTEGER);

	PROCEDURE [ms64] auxInitWindow* ["auxInitWindowA"] (p0: PtrSTR): WinGL.GLenum;

	PROCEDURE [ms64] auxInitWindowA* (p0: PtrSTR): WinGL.GLenum;

	PROCEDURE [ms64] auxInitWindowW* (p0: PtrWSTR): WinGL.GLenum;

	PROCEDURE [ms64] auxCloseWindow* ();

	PROCEDURE [ms64] auxQuit* ();

	PROCEDURE [ms64] auxSwapBuffers* ();

	PROCEDURE [ms64] auxMainLoop* (p0: AUXMAINPROC);

	PROCEDURE [ms64] auxExposeFunc* (p0: AUXEXPOSEPROC);

	PROCEDURE [ms64] auxReshapeFunc* (p0: AUXRESHAPEPROC);

	PROCEDURE [ms64] auxIdleFunc* (p0: AUXIDLEPROC);

	PROCEDURE [ms64] auxKeyFunc* (p0: INTEGER; p1: AUXKEYPROC);

	PROCEDURE [ms64] auxMouseFunc* (p0: INTEGER; p1: INTEGER; p2: AUXMOUSEPROC);

	PROCEDURE [ms64] auxGetColorMapSize* (): INTEGER;

	PROCEDURE [ms64] auxGetMouseLoc* (VAR [nil] p0: INTEGER; VAR [nil] p1: INTEGER);

	PROCEDURE [ms64] auxSetOneColor* (p0: INTEGER; p1: SHORTREAL; p2: SHORTREAL; p3: SHORTREAL);

	PROCEDURE [ms64] auxSetFogRamp* (p0: INTEGER; p1: INTEGER);

	PROCEDURE [ms64] auxSetGreyRamp* ();

	PROCEDURE [ms64] auxSetRGBMap* (p0: INTEGER; VAR [nil] p1: SHORTREAL);

	PROCEDURE [ms64] auxRGBImageLoad* ["auxRGBImageLoadA"] (p0: PtrSTR): PtrAUX_RGBImageRec;

	PROCEDURE [ms64] auxRGBImageLoadA* (p0: PtrSTR): PtrAUX_RGBImageRec;

	PROCEDURE [ms64] auxRGBImageLoadW* (p0: PtrWSTR): PtrAUX_RGBImageRec;

	PROCEDURE [ms64] auxDIBImageLoad* ["auxDIBImageLoadA"] (p0: PtrSTR): PtrAUX_RGBImageRec;

	PROCEDURE [ms64] auxDIBImageLoadA* (p0: PtrSTR): PtrAUX_RGBImageRec;

	PROCEDURE [ms64] auxDIBImageLoadW* (p0: PtrWSTR): PtrAUX_RGBImageRec;

	PROCEDURE [ms64] auxCreateFont* ();

	PROCEDURE [ms64] auxDrawStr* ["auxDrawStrA"] (p0: PtrSTR);

	PROCEDURE [ms64] auxDrawStrA* (p0: PtrSTR);

	PROCEDURE [ms64] auxDrawStrW* (p0: PtrWSTR);

	PROCEDURE [ms64] auxWireSphere* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidSphere* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireCube* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidCube* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireBox* (p0: WinGL.GLdouble; p1: WinGL.GLdouble; p2: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidBox* (p0: WinGL.GLdouble; p1: WinGL.GLdouble; p2: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireTorus* (p0: WinGL.GLdouble; p1: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidTorus* (p0: WinGL.GLdouble; p1: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireCylinder* (p0: WinGL.GLdouble; p1: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidCylinder* (p0: WinGL.GLdouble; p1: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireIcosahedron* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidIcosahedron* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireOctahedron* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidOctahedron* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireTetrahedron* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidTetrahedron* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireDodecahedron* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidDodecahedron* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireCone* (p0: WinGL.GLdouble; p1: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidCone* (p0: WinGL.GLdouble; p1: WinGL.GLdouble);

	PROCEDURE [ms64] auxWireTeapot* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxSolidTeapot* (p0: WinGL.GLdouble);

	PROCEDURE [ms64] auxGetHWND* (): HWND;

	PROCEDURE [ms64] auxGetHDC* (): HDC;

	PROCEDURE [ms64] auxGetHGLRC* (): HGLRC;

	PROCEDURE [ms64] auxInitDisplayModePolicy* (p0: WinGL.GLenum);

	PROCEDURE [ms64] auxInitDisplayModeID* (p0: WinGL.GLint): WinGL.GLenum;

	PROCEDURE [ms64] auxGetDisplayModePolicy* (): WinGL.GLenum;

	PROCEDURE [ms64] auxGetDisplayModeID* (): WinGL.GLint;

	PROCEDURE [ms64] auxGetDisplayMode* (): WinGL.GLenum;

END WinGLAux.
