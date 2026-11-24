MODULE System_devStackMoveSys;
(** Пара к System_devStackMoveCode: тот же рекурсивный спуск, но модуль импортирует SYSTEM.
	По правилу модулей его кадры непереносимы: стек задачи закрепляется, и при нехватке
	проверка в прологе даёт трап 137, а не перенос. *)

	IMPORT S := SYSTEM, Kernel, Tasks, C := System_devStackMoveCode;

	TYPE
		SysT* = POINTER TO RECORD (Tasks.Task) got*: INTEGER END;
		(** Kernel.Try внутри задачи: перенос внутри Try, затем трап там же. Обработчику нужен
			тип SYSTEM.ADDRESS, поэтому он здесь; тест вносит модуль в доверенные. *)
		TryT* = POINTER TO RECORD (Tasks.Task) stage*, after*: INTEGER END;

	VAR stage: INTEGER;

	PROCEDURE Rec (n: INTEGER): INTEGER;
		VAR pad: ARRAY 8 OF INTEGER; a: S.ADDRESS;
	BEGIN
		pad[n MOD 8] := 1; a := S.ADR(pad);
		IF n = 0 THEN RETURN 1 END;
		RETURN Rec(n - 1) + pad[n MOD 8]
	END Rec;

	PROCEDURE Body (a, b, c: S.ADDRESS);
	BEGIN
		stage := 1;
		IF C.Deep(C.depth) = C.depth + 1 THEN stage := 2 END;
		HALT(43)
	END Body;

	PROCEDURE (t: TryT) Run*;
	BEGIN
		stage := 0;
		Kernel.Try(Body, 0, 0, 0);
		t.stage := stage; t.after := C.Deep(C.depth DIV 2)
	END Run;

	PROCEDURE (t: SysT) Run*;
	BEGIN
		t.got := Rec(300)
	END Run;

END System_devStackMoveSys.
