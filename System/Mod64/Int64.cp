MODULE Int64;
(** PURPOSE "Умножение LONGINT по модулю 2^64 на amd64"; *)

	(* Пара — System/Mod/Int64.cp (i386). Здесь умножение LONGINT
	   аппаратное (imul) и заворачивается само. *)

	(** Младшие 64 бита произведения x * y. *)
	PROCEDURE Mul* (x, y: LONGINT): LONGINT;
	BEGIN
		RETURN x * y
	END Mul;

END Int64.
