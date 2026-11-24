MODULE SystemTestsBasic;
(** Базовый тест ядра и сборщика: объекты на главном стеке переживают
	Kernel.Collect, финализаторы вызываются, повторное использование памяти
	после сборки не портит живые объекты. Порт идей ObxProbe8/ObxProbe10. *)

	IMPORT Console, Kernel, SystemTestsSupport;

	TYPE Obj = POINTER TO RECORD x: INTEGER END;

	VAR keep: ARRAY 64 OF Obj; finCount: INTEGER;

	PROCEDURE (o: Obj) FINALIZE;
	BEGIN INC(finCount) END FINALIZE;

	PROCEDURE Do*;
		VAR i: INTEGER; o: Obj;
	BEGIN
		SystemTestsSupport.Begin("Basic");
		finCount := 0;
		FOR i := 0 TO 63 DO keep[i] := NIL END;

		FOR i := 0 TO 2047 DO
			NEW(o); o.x := i;
			IF i MOD 32 = 0 THEN keep[i DIV 32] := o END
		END;
		Kernel.Collect; Kernel.Collect;

		FOR i := 0 TO 63 DO
			SystemTestsSupport.Check(keep[i] # NIL, "kept object became NIL");
			IF keep[i] # NIL THEN
				SystemTestsSupport.Check(keep[i].x = i * 32, "kept object corrupted")
			END
		END;
		SystemTestsSupport.Check(finCount > 0, "no finalizer ran for garbage");

		(* вторая волна: память освобождённых объектов переиспользуется *)
		FOR i := 0 TO 2047 DO NEW(o); o.x := i + 100000 END;
		Kernel.Collect;
		FOR i := 0 TO 63 DO
			IF keep[i] # NIL THEN
				SystemTestsSupport.Check(keep[i].x = i * 32, "kept object corrupted after reuse")
			END
		END;
		SystemTestsSupport.End
	END Do;

END SystemTestsBasic.
