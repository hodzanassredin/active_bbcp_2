MODULE CheckWinApi64;

	IMPORT S := SYSTEM, Kernel, WinKernel, A := WinApi, Strings;

	VAR failed, callbacks: INTEGER;

	PROCEDURE Check (ok: BOOLEAN; code: INTEGER);
	BEGIN IF ~ok THEN Kernel.Quit(code) END END Check;

	PROCEDURE Layout (IN name: ARRAY OF CHAR; actual, expected: INTEGER);
		VAR text, number: ARRAY 128 OF CHAR;
	BEGIN
		IF actual # expected THEN
			Strings.IntToString(actual, number); text := name+" size "+number;
			WinKernel.WkWrite(S.ADR(text), LEN(text$)); INC(failed)
		END
	END Layout;

	PROCEDURE [ms64] Enumerate (wnd: A.HWND; data: A.LPARAM): A.BOOL;
	BEGIN
		Check(data = 100000001L, 141); INC(callbacks); RETURN 0
	END Enumerate;

	PROCEDURE Run;
		VAR system: A.SYSTEM_INFO; exception: A.EXCEPTION_RECORD; menu: A.MENUITEMINFOW;
			pe: A.IMAGE_OPTIONAL_HEADER64; imports: A.IMAGE_IMPORT_DESCRIPTOR;
			shell: A.SHFILEOPSTRUCTW; app: A.APPBARDATA; icon: A.NOTIFYICONDATAW;
			heap, mem, bytes: S.ADDRESS; result, input, copy: INTEGER;
			storage: ARRAY SIZE(A.MEMORY_BASIC_INFORMATION)+15 OF BYTE;
			info: A.PtrMEMORY_BASIC_INFORMATION; text: ARRAY 128 OF CHAR;
	BEGIN
		Layout("SYSTEM_INFO", SIZE(A.SYSTEM_INFO), 48);
		Layout("MEMORYSTATUS", SIZE(A.MEMORYSTATUS), 56);
		Layout("MEMORY_BASIC_INFORMATION", SIZE(A.MEMORY_BASIC_INFORMATION), 48);
		Layout("EXCEPTION_RECORD", SIZE(A.EXCEPTION_RECORD), 152);
		Layout("EXCEPTION_POINTERS", SIZE(A.EXCEPTION_POINTERS), 16);
		Layout("COPYDATASTRUCT", SIZE(A.COPYDATASTRUCT), 24);
		Layout("MEASUREITEMSTRUCT", SIZE(A.MEASUREITEMSTRUCT), 32);
		Layout("DRAWITEMSTRUCT", SIZE(A.DRAWITEMSTRUCT), 64);
		Layout("DELETEITEMSTRUCT", SIZE(A.DELETEITEMSTRUCT), 32);
		Layout("COMPAREITEMSTRUCT", SIZE(A.COMPAREITEMSTRUCT), 56);
		Layout("NMHDR", SIZE(A.NMHDR), 24);
		Layout("MENUITEMINFOA", SIZE(A.MENUITEMINFOA), 80);
		Layout("MENUITEMINFOW", SIZE(A.MENUITEMINFOW), 80);
		Layout("SHFILEOPSTRUCTW", SIZE(A.SHFILEOPSTRUCTW), 56);
		Layout("SHELLEXECUTEINFOA", SIZE(A.SHELLEXECUTEINFOA), 112);
		Layout("APPBARDATA", SIZE(A.APPBARDATA), 48);
		Layout("PRINTER_INFO_2W", SIZE(A.PRINTER_INFO_2W), 136);
		Layout("TOKEN_PRIVILEGES", SIZE(A.TOKEN_PRIVILEGES), 16);
		Layout("TOKEN_GROUPS", SIZE(A.TOKEN_GROUPS), 24);
		Layout("CRITICAL_SECTION", SIZE(A.RTL_CRITICAL_SECTION), 40);
		Layout("CRITICAL_SECTION_DEBUG", SIZE(A.RTL_CRITICAL_SECTION_DEBUG), 48);
		Layout("IMAGE_OPTIONAL_HEADER32", SIZE(A.IMAGE_OPTIONAL_HEADER32), 224);
		Layout("IMAGE_OPTIONAL_HEADER64", SIZE(A.IMAGE_OPTIONAL_HEADER64), 240);
		Layout("IMAGE_NT_HEADERS32", SIZE(A.IMAGE_NT_HEADERS32), 248);
		Layout("IMAGE_NT_HEADERS64", SIZE(A.IMAGE_NT_HEADERS64), 264);
		Layout("IMAGE_TLS_DIRECTORY", SIZE(A.IMAGE_TLS_DIRECTORY), 40);
		Layout("IMAGE_EXPORT_DIRECTORY", SIZE(A.IMAGE_EXPORT_DIRECTORY), 40);
		Layout("IMAGE_IMPORT_DESCRIPTOR", SIZE(A.IMAGE_IMPORT_DESCRIPTOR), 20);
		Layout("IMAGE_RUNTIME_FUNCTION_ENTRY", SIZE(A.IMAGE_RUNTIME_FUNCTION_ENTRY), 12);
		Layout("IMAGE_LOAD_CONFIG_DIRECTORY", SIZE(A.IMAGE_LOAD_CONFIG_DIRECTORY), 112);
		Layout("NOTIFYICONDATAA", SIZE(A.NOTIFYICONDATAA), 528);
		Layout("NOTIFYICONDATAW", SIZE(A.NOTIFYICONDATAW), 976);
		Check(failed = 0, 140);
		Check(S.ADR(system.dwActiveProcessorMask)-S.ADR(system) = 24, 155);
		Check(S.ADR(exception.ExceptionInformation)-S.ADR(exception) = 32, 156);
		Check(S.ADR(menu.dwItemData)-S.ADR(menu) = 48, 157);
		Check(S.ADR(menu.hbmpItem)-S.ADR(menu) = 72, 158);
		Check(S.ADR(pe.ImageBase)-S.ADR(pe) = 24, 159);
		Check(S.ADR(imports.FirstThunk)-S.ADR(imports) = 16, 160);
		Check(S.ADR(shell.pFrom)-S.ADR(shell) = 16, 161);
		Check(S.ADR(app.lParam)-S.ADR(app) = 40, 162);
		Check(S.ADR(icon.guidItem)-S.ADR(icon) = 952, 163);
		heap := A.HeapCreate({}, 65536, 0); Check(heap # 0, 142);
		mem := A.HeapAlloc(heap, 0, 128); Check(mem # 0, 143);
		mem := A.HeapReAlloc(heap, {}, mem, 256); Check(mem # 0, 144);
		Check(A.HeapSize(heap, {}, mem) >= 256, 145);
		result := A.HeapFree(heap, 0, mem); Check(result # 0, 146);
		result := A.HeapDestroy(heap); Check(result # 0, 147);
		mem := A.LocalAlloc({}, 128); Check(mem # 0, 148);
		Check(A.LocalSize(mem) >= 128, 149); mem := A.LocalFree(mem); Check(mem = 0, 150);
		input := 123456; copy := 0; bytes := -1;
		result := A.ReadProcessMemory(A.GetCurrentProcess(), S.ADR(input), S.ADR(copy), SIZE(INTEGER), bytes);
		Check((result # 0) & (bytes = SIZE(INTEGER)) & (copy = input), 151);
		input := 654321; bytes := -1;
		result := A.WriteProcessMemory(A.GetCurrentProcess(), S.ADR(copy), S.ADR(input), SIZE(INTEGER), bytes);
		Check((result # 0) & (bytes = SIZE(INTEGER)) & (copy = input), 152);
		info := S.VAL(A.PtrMEMORY_BASIC_INFORMATION, (S.ADR(storage)+15) DIV 16*16);
		bytes := A.VirtualQueryEx(A.GetCurrentProcess(), S.ADR(input), info^, SIZE(A.MEMORY_BASIC_INFORMATION));
		Check((bytes = SIZE(A.MEMORY_BASIC_INFORMATION)) & (info.RegionSize > 0), 153);
		result := A.EnumWindows(Enumerate, 100000001L); Check(callbacks > 0, 154);
		text := "WinApi x64 full coverage, layouts and native calls OK";
		WinKernel.WkWrite(S.ADR(text), LEN(text$)); Kernel.Quit(0)
	END Run;

BEGIN
	Run
END CheckWinApi64.
