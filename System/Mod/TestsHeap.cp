MODULE SystemTestsHeap;
(** Куча из многих кластеров: 48 массивов по 8 МБ (~384 МБ), затем сборка.
	Идея из ObxProbe42 (регрессия «chain overflow» в старом порте); здесь
	проверяем, что цепочка кластеров, Mark и Sweep не портят живые большие
	объекты на x64. Нагрузка умеренная, чтобы тест шёл секунды. *)

	IMPORT Kernel, SystemTestsSupport;

	PROCEDURE Do*;
		CONST n = 48;
		TYPE Arr = POINTER TO ARRAY 1024 * 1024 OF LONGINT;	(* 8 МБ *)
		VAR p: ARRAY n OF Arr; i: INTEGER;
	BEGIN
		SystemTestsSupport.Begin("Heap");
		FOR i := 0 TO n - 1 DO p[i] := NIL END;
		FOR i := 0 TO n - 1 DO
			NEW(p[i]);
			SystemTestsSupport.Check(p[i] # NIL, "big allocation failed");
			IF p[i] # NIL THEN
				p[i][0] := i;
				p[i][LEN(p[i], 0) - 1] := i
			END
		END;
		Kernel.Collect;
		FOR i := 0 TO n - 1 DO
			IF p[i] # NIL THEN
				SystemTestsSupport.Check(p[i][0] = i, "first element corrupted");
				SystemTestsSupport.Check(p[i][LEN(p[i], 0) - 1] = i, "last element corrupted")
			END
		END;
		FOR i := 0 TO n - 1 DO p[i] := NIL END;
		Kernel.Collect;
		SystemTestsSupport.End
	END Do;

END SystemTestsHeap.
