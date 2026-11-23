MODULE WinGLUtil ["Glu32.dll"];
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
		GLU_VERSION_1_1* = 1;
		GLU_VERSION_1_2* = 1;
		GLU_INVALID_ENUM* = 100900;
		GLU_INVALID_VALUE* = 100901;
		GLU_OUT_OF_MEMORY* = 100902;
		GLU_INCOMPATIBLE_GL_VERSION* = 100903;
		GLU_VERSION* = 100800;
		GLU_EXTENSIONS* = 100801;
		GLU_TRUE* = 1;
		GLU_FALSE* = 0;
		GLU_SMOOTH* = 100000;
		GLU_FLAT* = 100001;
		GLU_NONE* = 100002;
		GLU_POINT* = 100010;
		GLU_LINE* = 100011;
		GLU_FILL* = 100012;
		GLU_SILHOUETTE* = 100013;
		GLU_OUTSIDE* = 100020;
		GLU_INSIDE* = 100021;
		GLU_TESS_TOLERANCE* = 100142;
		GLU_TESS_WINDING_ODD* = 100130;
		GLU_TESS_WINDING_NONZERO* = 100131;
		GLU_TESS_WINDING_POSITIVE* = 100132;
		GLU_TESS_WINDING_NEGATIVE* = 100133;
		GLU_TESS_WINDING_ABS_GEQ_TWO* = 100134;
		GLU_TESS_BEGIN* = 100100;
		GLU_TESS_VERTEX* = 100101;
		GLU_TESS_END* = 100102;
		GLU_TESS_ERROR* = 100103;
		GLU_TESS_EDGE_FLAG* = 100104;
		GLU_TESS_COMBINE* = 100105;
		GLU_TESS_BEGIN_DATA* = 100106;
		GLU_TESS_VERTEX_DATA* = 100107;
		GLU_TESS_END_DATA* = 100108;
		GLU_TESS_ERROR_DATA* = 100109;
		GLU_TESS_EDGE_FLAG_DATA* = 100110;
		GLU_TESS_COMBINE_DATA* = 100111;
		GLU_TESS_ERROR1* = 100151;
		GLU_TESS_ERROR2* = 100152;
		GLU_TESS_ERROR3* = 100153;
		GLU_TESS_ERROR4* = 100154;
		GLU_TESS_ERROR5* = 100155;
		GLU_TESS_ERROR6* = 100156;
		GLU_TESS_ERROR7* = 100157;
		GLU_TESS_ERROR8* = 100158;
		GLU_TESS_MISSING_BEGIN_POLYGON* = 100151;
		GLU_TESS_MISSING_BEGIN_CONTOUR* = 100152;
		GLU_TESS_MISSING_END_POLYGON* = 100153;
		GLU_TESS_MISSING_END_CONTOUR* = 100154;
		GLU_TESS_COORD_TOO_LARGE* = 100155;
		GLU_TESS_NEED_COMBINE_CALLBACK* = 100156;
		GLU_AUTO_LOAD_MATRIX* = 100200;
		GLU_CULLING* = 100201;
		GLU_SAMPLING_TOLERANCE* = 100203;
		GLU_DISPLAY_MODE* = 100204;
		GLU_PARAMETRIC_TOLERANCE* = 100202;
		GLU_SAMPLING_METHOD* = 100205;
		GLU_U_STEP* = 100206;
		GLU_V_STEP* = 100207;
		GLU_PATH_LENGTH* = 100215;
		GLU_PARAMETRIC_ERROR* = 100216;
		GLU_DOMAIN_DISTANCE* = 100217;
		GLU_MAP1_TRIM_2* = 100210;
		GLU_MAP1_TRIM_3* = 100211;
		GLU_OUTLINE_POLYGON* = 100240;
		GLU_OUTLINE_PATCH* = 100241;
		GLU_NURBS_ERROR1* = 100251;
		GLU_NURBS_ERROR2* = 100252;
		GLU_NURBS_ERROR3* = 100253;
		GLU_NURBS_ERROR4* = 100254;
		GLU_NURBS_ERROR5* = 100255;
		GLU_NURBS_ERROR6* = 100256;
		GLU_NURBS_ERROR7* = 100257;
		GLU_NURBS_ERROR8* = 100258;
		GLU_NURBS_ERROR9* = 100259;
		GLU_NURBS_ERROR10* = 100260;
		GLU_NURBS_ERROR11* = 100261;
		GLU_NURBS_ERROR12* = 100262;
		GLU_NURBS_ERROR13* = 100263;
		GLU_NURBS_ERROR14* = 100264;
		GLU_NURBS_ERROR15* = 100265;
		GLU_NURBS_ERROR16* = 100266;
		GLU_NURBS_ERROR17* = 100267;
		GLU_NURBS_ERROR18* = 100268;
		GLU_NURBS_ERROR19* = 100269;
		GLU_NURBS_ERROR20* = 100270;
		GLU_NURBS_ERROR21* = 100271;
		GLU_NURBS_ERROR22* = 100272;
		GLU_NURBS_ERROR23* = 100273;
		GLU_NURBS_ERROR24* = 100274;
		GLU_NURBS_ERROR25* = 100275;
		GLU_NURBS_ERROR26* = 100276;
		GLU_NURBS_ERROR27* = 100277;
		GLU_NURBS_ERROR28* = 100278;
		GLU_NURBS_ERROR29* = 100279;
		GLU_NURBS_ERROR30* = 100280;
		GLU_NURBS_ERROR31* = 100281;
		GLU_NURBS_ERROR32* = 100282;
		GLU_NURBS_ERROR33* = 100283;
		GLU_NURBS_ERROR34* = 100284;
		GLU_NURBS_ERROR35* = 100285;
		GLU_NURBS_ERROR36* = 100286;
		GLU_NURBS_ERROR37* = 100287;
		GLU_CW* = 100120;
		GLU_CCW* = 100121;
		GLU_INTERIOR* = 100122;
		GLU_EXTERIOR* = 100123;
		GLU_UNKNOWN* = 100124;
		GLU_BEGIN* = 100100;
		GLU_VERTEX* = 100101;
		GLU_END* = 100102;
		GLU_ERROR* = 100103;
		GLU_EDGE_FLAG* = 100104;

	CONST (* enumerations *)

	TYPE
		PtrVoid* = SYSTEM.ADDRESS;
		PtrSTR* = POINTER TO ARRAY [untagged] OF SHORTCHAR;
		PtrWSTR* = POINTER TO ARRAY [untagged] OF CHAR;
		PtrGLUquadric* = POINTER TO RECORD [align8] END;
		PtrGLUtesselator* = POINTER TO RECORD [align8] END;
		PtrGLUnurbs* = POINTER TO RECORD [align8] END;
		GLUquadricErrorProc* = PROCEDURE [ms64] (p0: WinGL.GLenum);
		GLUtessBeginProc* = PROCEDURE [ms64] (p0: WinGL.GLenum);
		GLUtessEdgeFlagProc* = PROCEDURE [ms64] (p0: WinGL.GLboolean);
		GLUtessVertexProc* = PROCEDURE [ms64] (p0: PtrVoid);
		GLUtessEndProc* = PROCEDURE [ms64] ();
		GLUtessErrorProc* = PROCEDURE [ms64] (p0: WinGL.GLenum);
		GLUtessCombineProc* = PROCEDURE [ms64] (p0: ARRAY [untagged] 3 OF WinGL.GLdouble; p1: ARRAY [untagged] 4 OF PtrVoid; p2: ARRAY [untagged] 4 OF WinGL.GLfloat; VAR [nil] p3: PtrVoid);
		GLUtessBeginDataProc* = PROCEDURE [ms64] (p0: WinGL.GLenum; p1: PtrVoid);
		GLUtessEdgeFlagDataProc* = PROCEDURE [ms64] (p0: WinGL.GLboolean; p1: PtrVoid);
		GLUtessVertexDataProc* = PROCEDURE [ms64] (p0: PtrVoid; p1: PtrVoid);
		GLUtessEndDataProc* = PROCEDURE [ms64] (p0: PtrVoid);
		GLUtessErrorDataProc* = PROCEDURE [ms64] (p0: WinGL.GLenum; p1: PtrVoid);
		GLUtessCombineDataProc* = PROCEDURE [ms64] (p0: ARRAY [untagged] 3 OF WinGL.GLdouble; p1: ARRAY [untagged] 4 OF PtrVoid; p2: ARRAY [untagged] 4 OF WinGL.GLfloat; VAR [nil] p3: PtrVoid; p4: PtrVoid);
		GLUnurbsErrorProc* = PROCEDURE [ms64] (p0: WinGL.GLenum);

	PROCEDURE [ms64] gluErrorString* (errCode: WinGL.GLenum): PtrSTR;

	PROCEDURE [ms64] gluErrorUnicodeStringEXT* (errCode: WinGL.GLenum): PtrWSTR;

	PROCEDURE [ms64] gluGetString* (name: WinGL.GLenum): PtrSTR;

	PROCEDURE [ms64] gluOrtho2D* (left: WinGL.GLdouble; right: WinGL.GLdouble; bottom: WinGL.GLdouble; top: WinGL.GLdouble);

	PROCEDURE [ms64] gluPerspective* (fovy: WinGL.GLdouble; aspect: WinGL.GLdouble; zNear: WinGL.GLdouble; zFar: WinGL.GLdouble);

	PROCEDURE [ms64] gluPickMatrix* (x: WinGL.GLdouble; y: WinGL.GLdouble; width: WinGL.GLdouble; height: WinGL.GLdouble; viewport: ARRAY [untagged] 4 OF WinGL.GLint);

	PROCEDURE [ms64] gluLookAt* (eyex: WinGL.GLdouble; eyey: WinGL.GLdouble; eyez: WinGL.GLdouble; centerx: WinGL.GLdouble; centery: WinGL.GLdouble; centerz: WinGL.GLdouble; upx: WinGL.GLdouble; upy: WinGL.GLdouble; upz: WinGL.GLdouble);

	PROCEDURE [ms64] gluProject* (objx: WinGL.GLdouble; objy: WinGL.GLdouble; objz: WinGL.GLdouble; modelMatrix: ARRAY [untagged] 16 OF WinGL.GLdouble; projMatrix: ARRAY [untagged] 16 OF WinGL.GLdouble; viewport: ARRAY [untagged] 4 OF WinGL.GLint; VAR [nil] winx: WinGL.GLdouble; VAR [nil] winy: WinGL.GLdouble; VAR [nil] winz: WinGL.GLdouble): INTEGER;

	PROCEDURE [ms64] gluUnProject* (winx: WinGL.GLdouble; winy: WinGL.GLdouble; winz: WinGL.GLdouble; modelMatrix: ARRAY [untagged] 16 OF WinGL.GLdouble; projMatrix: ARRAY [untagged] 16 OF WinGL.GLdouble; viewport: ARRAY [untagged] 4 OF WinGL.GLint; VAR [nil] objx: WinGL.GLdouble; VAR [nil] objy: WinGL.GLdouble; VAR [nil] objz: WinGL.GLdouble): INTEGER;

	PROCEDURE [ms64] gluScaleImage* (format: WinGL.GLenum; widthin: WinGL.GLint; heightin: WinGL.GLint; typein: WinGL.GLenum; datain: PtrVoid; widthout: WinGL.GLint; heightout: WinGL.GLint; typeout: WinGL.GLenum; dataout: PtrVoid): INTEGER;

	PROCEDURE [ms64] gluBuild1DMipmaps* (target: WinGL.GLenum; components: WinGL.GLint; width: WinGL.GLint; format: WinGL.GLenum; type: WinGL.GLenum; data: PtrVoid): INTEGER;

	PROCEDURE [ms64] gluBuild2DMipmaps* (target: WinGL.GLenum; components: WinGL.GLint; width: WinGL.GLint; height: WinGL.GLint; format: WinGL.GLenum; type: WinGL.GLenum; data: PtrVoid): INTEGER;

	PROCEDURE [ms64] gluNewQuadric* (): PtrGLUquadric;

	PROCEDURE [ms64] gluDeleteQuadric* (state: PtrGLUquadric);

	PROCEDURE [ms64] gluQuadricNormals* (quadObject: PtrGLUquadric; normals: WinGL.GLenum);

	PROCEDURE [ms64] gluQuadricTexture* (quadObject: PtrGLUquadric; textureCoords: WinGL.GLboolean);

	PROCEDURE [ms64] gluQuadricOrientation* (quadObject: PtrGLUquadric; orientation: WinGL.GLenum);

	PROCEDURE [ms64] gluQuadricDrawStyle* (quadObject: PtrGLUquadric; drawStyle: WinGL.GLenum);

	PROCEDURE [ms64] gluCylinder* (qobj: PtrGLUquadric; baseRadius: WinGL.GLdouble; topRadius: WinGL.GLdouble; height: WinGL.GLdouble; slices: WinGL.GLint; stacks: WinGL.GLint);

	PROCEDURE [ms64] gluDisk* (qobj: PtrGLUquadric; innerRadius: WinGL.GLdouble; outerRadius: WinGL.GLdouble; slices: WinGL.GLint; loops: WinGL.GLint);

	PROCEDURE [ms64] gluPartialDisk* (qobj: PtrGLUquadric; innerRadius: WinGL.GLdouble; outerRadius: WinGL.GLdouble; slices: WinGL.GLint; loops: WinGL.GLint; startAngle: WinGL.GLdouble; sweepAngle: WinGL.GLdouble);

	PROCEDURE [ms64] gluSphere* (qobj: PtrGLUquadric; radius: WinGL.GLdouble; slices: WinGL.GLint; stacks: WinGL.GLint);

	PROCEDURE [ms64] gluQuadricCallback* (qobj: PtrGLUquadric; which: WinGL.GLenum; fn: PROCEDURE [ms64] ());

	PROCEDURE [ms64] gluNewTess* (): PtrGLUtesselator;

	PROCEDURE [ms64] gluDeleteTess* (tess: PtrGLUtesselator);

	PROCEDURE [ms64] gluTessBeginPolygon* (tess: PtrGLUtesselator; polygon_data: PtrVoid);

	PROCEDURE [ms64] gluTessBeginContour* (tess: PtrGLUtesselator);

	PROCEDURE [ms64] gluTessVertex* (tess: PtrGLUtesselator; coords: ARRAY [untagged] 3 OF WinGL.GLdouble; data: PtrVoid);

	PROCEDURE [ms64] gluTessEndContour* (tess: PtrGLUtesselator);

	PROCEDURE [ms64] gluTessEndPolygon* (tess: PtrGLUtesselator);

	PROCEDURE [ms64] gluTessProperty* (tess: PtrGLUtesselator; which: WinGL.GLenum; value: WinGL.GLdouble);

	PROCEDURE [ms64] gluTessNormal* (tess: PtrGLUtesselator; x: WinGL.GLdouble; y: WinGL.GLdouble; z: WinGL.GLdouble);

	PROCEDURE [ms64] gluTessCallback* (tess: PtrGLUtesselator; which: WinGL.GLenum; fn: PROCEDURE [ms64] ());

	PROCEDURE [ms64] gluGetTessProperty* (tess: PtrGLUtesselator; which: WinGL.GLenum; VAR [nil] value: WinGL.GLdouble);

	PROCEDURE [ms64] gluNewNurbsRenderer* (): PtrGLUnurbs;

	PROCEDURE [ms64] gluDeleteNurbsRenderer* (nobj: PtrGLUnurbs);

	PROCEDURE [ms64] gluBeginSurface* (nobj: PtrGLUnurbs);

	PROCEDURE [ms64] gluBeginCurve* (nobj: PtrGLUnurbs);

	PROCEDURE [ms64] gluEndCurve* (nobj: PtrGLUnurbs);

	PROCEDURE [ms64] gluEndSurface* (nobj: PtrGLUnurbs);

	PROCEDURE [ms64] gluBeginTrim* (nobj: PtrGLUnurbs);

	PROCEDURE [ms64] gluEndTrim* (nobj: PtrGLUnurbs);

	PROCEDURE [ms64] gluPwlCurve* (nobj: PtrGLUnurbs; count: WinGL.GLint; VAR [nil] array: WinGL.GLfloat; stride: WinGL.GLint; type: WinGL.GLenum);

	PROCEDURE [ms64] gluNurbsCurve* (nobj: PtrGLUnurbs; nknots: WinGL.GLint; VAR [nil] knot: WinGL.GLfloat; stride: WinGL.GLint; VAR [nil] ctlarray: WinGL.GLfloat; order: WinGL.GLint; type: WinGL.GLenum);

	PROCEDURE [ms64] gluNurbsSurface* (nobj: PtrGLUnurbs; sknot_count: WinGL.GLint; VAR [nil] sknot: SHORTREAL; tknot_count: WinGL.GLint; VAR [nil] tknot: WinGL.GLfloat; s_stride: WinGL.GLint; t_stride: WinGL.GLint; VAR [nil] ctlarray: WinGL.GLfloat; sorder: WinGL.GLint; torder: WinGL.GLint; type: WinGL.GLenum);

	PROCEDURE [ms64] gluLoadSamplingMatrices* (nobj: PtrGLUnurbs; modelMatrix: ARRAY [untagged] 16 OF WinGL.GLfloat; projMatrix: ARRAY [untagged] 16 OF WinGL.GLfloat; viewport: ARRAY [untagged] 4 OF WinGL.GLint);

	PROCEDURE [ms64] gluNurbsProperty* (nobj: PtrGLUnurbs; property: WinGL.GLenum; value: WinGL.GLfloat);

	PROCEDURE [ms64] gluGetNurbsProperty* (nobj: PtrGLUnurbs; property: WinGL.GLenum; VAR [nil] value: WinGL.GLfloat);

	PROCEDURE [ms64] gluNurbsCallback* (nobj: PtrGLUnurbs; which: WinGL.GLenum; fn: PROCEDURE [ms64] ());

	PROCEDURE [ms64] gluBeginPolygon* (tess: PtrGLUtesselator);

	PROCEDURE [ms64] gluNextContour* (tess: PtrGLUtesselator; type: WinGL.GLenum);

	PROCEDURE [ms64] gluEndPolygon* (tess: PtrGLUtesselator);

END WinGLUtil.
