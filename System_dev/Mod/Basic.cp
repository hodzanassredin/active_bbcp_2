MODULE System_devBasic;
(** Базовый тест ядра и сборщика: объекты на главном стеке переживают
	Kernel.Collect, финализаторы вызываются, повторное использование памяти
	после сборки не портит живые объекты. Порт идей ObxProbe8/ObxProbe10. *)

	IMPORT Console, Kernel, System_devSupport;

	TYPE Obj = POINTER TO RECORD x: INTEGER END;

	VAR keep: ARRAY 64 OF Obj; finCount: INTEGER;

	PROCEDURE (o: Obj) FINALIZE;
	BEGIN INC(finCount) END FINALIZE;

	PROCEDURE Do*;
		VAR i: INTEGER; o: Obj;
	BEGIN
		System_devSupport.Begin("Basic");
		finCount := 0;
		FOR i := 0 TO 63 DO keep[i] := NIL END;

		FOR i := 0 TO 2047 DO
			NEW(o); o.x := i;
			IF i MOD 32 = 0 THEN keep[i DIV 32] := o END
		END;
		Kernel.Collect; Kernel.Collect;

		FOR i := 0 TO 63 DO
			System_devSupport.Check(keep[i] # NIL, "kept object became NIL");
			IF keep[i] # NIL THEN
				System_devSupport.Check(keep[i].x = i * 32, "kept object corrupted")
			END
		END;
		System_devSupport.Check(finCount > 0, "no finalizer ran for garbage");

		(* вторая волна: память освобождённых объектов переиспользуется *)
		FOR i := 0 TO 2047 DO NEW(o); o.x := i + 100000 END;
		Kernel.Collect;
		FOR i := 0 TO 63 DO
			IF keep[i] # NIL THEN
				System_devSupport.Check(keep[i].x = i * 32, "kept object corrupted after reuse")
			END
		END;
		System_devSupport.End
	END Do;

END System_devBasic.
