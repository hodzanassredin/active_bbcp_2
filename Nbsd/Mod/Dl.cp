MODULE NbsdDl ["libc.so.12"];

	(*
		Alexander Shiryaev, 2025.10

		NetBSD 10.1
		i386
	*)

	CONST
		NULL* = 0H;

		(* from NetBSD 10.1 /usr/include/dlfcn.h *)
			RTLD_LAZY* = 1;
			RTLD_NOW* = 2;

			RTLD_GLOBAL* = 100H;
			RTLD_LOCAL* = 200H;

	TYPE
		PtrVoid* = INTEGER;
		HANDLE* = PtrVoid;
		PtrSTR* = POINTER TO ARRAY [untagged] OF SHORTCHAR;

	PROCEDURE [ccall16] dlopen* (file: PtrSTR; mode: INTEGER): HANDLE;
	PROCEDURE [ccall16] dlclose* (handle: HANDLE): INTEGER;
	PROCEDURE [ccall16] dlsym* (handle: HANDLE; symbol: PtrSTR): HANDLE;

	PROCEDURE [ccall16] dlerror* (): PtrSTR;

END NbsdDl.
