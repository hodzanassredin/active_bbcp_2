MODULE LinFenv ["libm.so.6"];

	TYPE
		(* Opaque storage; glibc fenv_t occupies 28 bytes on i386 and 32 on AMD64. *)
		Environment* = ARRAY [untagged] 128 OF BYTE;

	PROCEDURE [ccall16] feholdexcept* (VAR environment: Environment): INTEGER;
	PROCEDURE [ccall16] fesetenv* (VAR environment: Environment): INTEGER;

END LinFenv.
