MODULE LinGtk4Bootstrap;

	IMPORT S := SYSTEM, Dl := LinDl, Fenv := LinFenv;

	TYPE Initialize = PROCEDURE [ccall16];

	VAR handle,xml: Dl.HANDLE; init: Initialize; address: S.ADDRESS; environment: Fenv.Environment; result: INTEGER;

BEGIN
	(* Load native constructors before any GTK4 interface is bound. Their worker
	   threads must inherit masked FPU exceptions, even before gtk_init_check. *)
	result := Fenv.feholdexcept(environment);
	xml := Dl.dlopen("libxml2.so.2",Dl.RTLD_LAZY+Dl.RTLD_GLOBAL);
	IF xml # 0 THEN
		address := Dl.dlsym(xml,"xmlInitParser");
		IF address # 0 THEN init := S.VAL(Initialize,address); init() END
	END;
	handle := Dl.dlopen("libgtk-4.so.1",Dl.RTLD_LAZY+Dl.RTLD_GLOBAL);
	result := Fenv.fesetenv(environment);
	ASSERT(handle # 0)
END LinGtk4Bootstrap.
