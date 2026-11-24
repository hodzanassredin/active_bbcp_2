MODULE System_devHeap;
(** Куча из многих кластеров: 48 массивов по 8 МБ (~384 МБ), затем сборка.
	Идея из ObxProbe42 (регрессия «chain overflow» в старом порте); здесь
	проверяем, что цепочка кластеров, Mark и Sweep не портят живые большие
	объекты на x64. Нагрузка умеренная, чтобы тест шёл секунды. *)

	IMPORT Kernel, System_devSupport;

	TYPE
		Node = POINTER TO RECORD next: Node; val: INTEGER END;

	VAR
		collects: INTEGER;	(* сборок за замер: события 1 и 2 Kernel.watcher *)
		keep: Node;	(* живой список: корень — глобальная переменная *)

	PROCEDURE Watch (e: INTEGER);
	BEGIN
		IF (e = 1) OR (e = 2) THEN INC(collects) END
	END Watch;

	PROCEDURE Fill (n: INTEGER; live: BOOLEAN);
	(* Пост: выделено n узлов; live — все они в keep, иначе мусор. *)
		VAR i: INTEGER; x: Node;
	BEGIN
		FOR i := 1 TO n DO
			NEW(x); x.val := i;
			IF live THEN x.next := keep; keep := x END
		END
	END Fill;

	PROCEDURE Pace*;
	(** Правило запуска сборки: рост живых данных меньше нижнего порога
		(4 МБ) после сборки не собирает; поток мусора собирается, и куча не
		растёт пропорционально выделенному. Счёт сборок — Kernel.watcher. *)
		CONST node = 32;
		VAR oldW: PROCEDURE (e: INTEGER); oldD: BOOLEAN; used0: LONGINT;
	BEGIN
		System_devSupport.Begin("GcPace");
		oldW := Kernel.watcher; oldD := Kernel.debug;
		keep := NIL; Kernel.Collect;
		collects := 0; Kernel.watcher := Watch; Kernel.debug := TRUE;
		Fill(3 * 1024 * 1024 DIV node, TRUE);	(* 3 МБ живых *)
		Kernel.watcher := oldW; Kernel.debug := oldD;
		System_devSupport.Check(collects = 0, "growth below the floor collected");
		keep := NIL; Kernel.Collect; used0 := Kernel.Used();
		collects := 0; Kernel.watcher := Watch; Kernel.debug := TRUE;
		Fill(64 * 1024 * 1024 DIV node, FALSE);	(* 64 МБ мусора *)
		Kernel.watcher := oldW; Kernel.debug := oldD;
		System_devSupport.Check(collects >= 2, "garbage stream not collected");
		System_devSupport.Check(Kernel.Used() - used0 < 32 * 1024 * 1024, "heap grew with garbage");
		System_devSupport.End
	END Pace;

	PROCEDURE Do*;
		CONST n = 48;
		TYPE Arr = POINTER TO ARRAY 1024 * 1024 OF LONGINT;	(* 8 МБ *)
		VAR p: ARRAY n OF Arr; i: INTEGER;
	BEGIN
		System_devSupport.Begin("Heap");
		FOR i := 0 TO n - 1 DO p[i] := NIL END;
		FOR i := 0 TO n - 1 DO
			NEW(p[i]);
			System_devSupport.Check(p[i] # NIL, "big allocation failed");
			IF p[i] # NIL THEN
				p[i][0] := i;
				p[i][LEN(p[i], 0) - 1] := i
			END
		END;
		Kernel.Collect;
		FOR i := 0 TO n - 1 DO
			IF p[i] # NIL THEN
				System_devSupport.Check(p[i][0] = i, "first element corrupted");
				System_devSupport.Check(p[i][LEN(p[i], 0) - 1] = i, "last element corrupted")
			END
		END;
		FOR i := 0 TO n - 1 DO p[i] := NIL END;
		Kernel.Collect;
		System_devSupport.End
	END Do;

END System_devHeap.
