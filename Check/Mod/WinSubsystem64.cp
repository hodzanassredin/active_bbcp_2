MODULE CheckWinSubsystem64;

	IMPORT S := SYSTEM, Kernel, WinKernel, A := WinApi, D := WinDlg,
		O := WinOle, V := WinOleAut, R := WinRpc, G := WinGdip, Q := WinSql;

	PROCEDURE Check (ok: BOOLEAN; code: INTEGER);
	BEGIN
		IF ~ok THEN Kernel.Quit(code) END
	END Check;

	PROCEDURE Run;
		VAR pd: D.PRINTDLGW; dm: A.DEVMODEW; v: V.VARIANT; sa: V.SAFEARRAY;
			stub: R.MIDL_STUB_MESSAGE; stat: O.STATSTG; doc: A.DOCINFOW; token, mem: S.ADDRESS;
			input: G.GdiplusStartupInput; env: Q.HENV; result: INTEGER;
			text: ARRAY 64 OF CHAR;
	BEGIN
		(* Sizes and offsets independently checked against MinGW Windows x64 headers. *)
		Check(SIZE(A.DEVMODEW) = 220, 101);
		Check(SIZE(A.DOCINFOW) = 40, 102);
		Check(SIZE(D.PRINTDLGW) = 120, 103);
		Check(SIZE(D.OPENFILENAMEW) = 152, 104);
		Check(SIZE(V.VARIANT) = 24, 105);
		Check(SIZE(V.SAFEARRAY) = 32, 106);
		Check(SIZE(O.STATSTG) = 80, 107);
		Check(SIZE(O.FORMATETC) = 32, 108);
		Check(SIZE(O.STGMEDIUM) = 24, 109);
		Check(SIZE(A.PROPSHEETHEADERW) = 96, 110);
		Check(SIZE(A.PROPSHEETPAGEW) = 96, 111);
		Check(SIZE(R.RPC_MESSAGE) = 80, 112);
		Check(S.ADR(pd.hInstance)-S.ADR(pd) = 56, 113);
		Check(S.ADR(dm.dmFields)-S.ADR(dm) = 72, 114);
		Check(S.ADR(v.u)-S.ADR(v) = 8, 115);
		Check(S.ADR(sa.pvData)-S.ADR(sa) = 16, 116);
		Check(S.ADR(stat.clsid)-S.ADR(stat) = 56, 117);
		Check(S.ADR(doc.fwType)-S.ADR(doc) = 32, 118);
		Check(SIZE(R.MIDL_STUB_MESSAGE) = 400, 126);
		Check(SIZE(R.MIDL_STUB_DESC) = 152, 127);
		Check(S.ADR(stub.MaxCount)-S.ADR(stub) = 104, 128);
		Check(S.ADR(stub.pfnAllocate)-S.ADR(stub) = 120, 129);
		V.VariantInit(v); Check(v.vt = 0, 119);
		v.vt := 3; v.u.lVal := 123456; result := V.VariantClear(v);
		Check((result = 0) & (v.vt = 0), 120);
		mem := O.CoTaskMemAlloc(64); Check(mem # 0, 121);
		mem := O.CoTaskMemRealloc(mem, 128); Check(mem # 0, 122); O.CoTaskMemFree(mem);
		input.GdiplusVersion := 1; input.DebugEventCallback := NIL;
		input.SuppressBackgroundThread := 0; input.SuppressExternalCodecs := 0;
		result := G.GdiplusStartup(token, input, NIL); Check((result = 0) & (token # 0), 123);
		G.GdiplusShutdown(token);
		env := 0; result := Q.SQLAllocEnv(env); Check((result = 0) & (env # 0), 124);
		result := Q.SQLFreeEnv(env); Check(result = 0, 125);
		text := "Win subsystem x64 ABI and native API OK";
		WinKernel.WkWrite(S.ADR(text), LEN(text$)); Kernel.Quit(0)
	END Run;

BEGIN
	Run
END CheckWinSubsystem64.
