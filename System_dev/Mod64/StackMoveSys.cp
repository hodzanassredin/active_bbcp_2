MODULE System_devStackMoveSys;
(** Пара к System_devStackMoveCode: тот же рекурсивный спуск, но модуль импортирует SYSTEM.
	По правилу модулей его кадры непереносимы: стек задачи закрепляется, и при нехватке
	проверка в прологе даёт трап 137, а не перенос. *)

	IMPORT S := SYSTEM, Tasks;

	TYPE
		SysT* = POINTER TO RECORD (Tasks.Task) got*: INTEGER END;

	PROCEDURE Rec (n: INTEGER): INTEGER;
		VAR pad: ARRAY 8 OF INTEGER; a: S.ADDRESS;
	BEGIN
		pad[n MOD 8] := 1; a := S.ADR(pad);
		IF n = 0 THEN RETURN 1 END;
		RETURN Rec(n - 1) + pad[n MOD 8]
	END Rec;

	PROCEDURE (t: SysT) Run*;
	BEGIN
		t.got := Rec(300)
	END Run;

END System_devStackMoveSys.
