MODULE System_devInHeap;
(** Шаг 2а, чёрный ящик: индекс кластеров проверяется только через
	наблюдаемые следствия, без диагностических экспортов ядра.

	Объект размером 200 КБ больше остатка кластера (кластер — 256 КБ),
	поэтому каждый объект получает собственный кластер: куча вырастает
	на много кластеров. Пока объекты живы, единственная ссылка на них —
	глобальный массив keep (MarkGlobals), поэтому промежуточные сборки
	при нехватке свободного блока их не освобождают. Затем большая часть
	ссылок стирается: после Kernel.Collect мусорные кластеры обязаны уйти
	(Kernel.Allocated падает), а живые — остаться целыми.

	Адрес мусорного объекта нельзя оставлять на стеке: консервативный
	скан принял бы его за ссылку. Поэтому объекты кладутся в keep сразу
	при создании, а локальная копия обнуляется. Живые объекты проверяются
	один раз в конце: любой прочитанный указатель остаётся в мёртвом
	кадре и удерживает объект. *)

	IMPORT SYSTEM, Kernel, System_devSupport;

	CONST
		bigSize = 51200;		(* INTEGER'ов; INTEGER — 32 бита, 200 КБ *)
		bigBytes = bigSize * 4;		(* остаток кластера 256 КБ меньше объекта *)
		live = 50;
		total = 200;
		second = 100;

	TYPE
		Big = POINTER TO ARRAY bigSize OF INTEGER;

	VAR
		keep: ARRAY total OF Big;

	PROCEDURE MakeAt (slot, tag: INTEGER);
	(** Создать объект и положить ссылку только в keep[slot]. *)
		VAR p: Big;
	BEGIN
		NEW(p);
		IF p # NIL THEN p[0] := tag; p[1] := tag * 7 + 1 END;
		keep[slot] := p;
		p := NIL	(* не оставлять ссылку в кадре *)
	END MakeAt;

	PROCEDURE Allocate (from, n, tagBase: INTEGER): INTEGER;
		VAR i, made: INTEGER;
	BEGIN
		made := 0;
		FOR i := 0 TO n - 1 DO
			MakeAt(from + i, tagBase + i);
			IF keep[from + i] # NIL THEN INC(made) END
		END;
		RETURN made
	END Allocate;

	PROCEDURE Release (from, n: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO keep[from + i] := NIL END
	END Release;

	PROCEDURE BadLive (from, n: INTEGER): INTEGER;
	(** Испорченные объекты среди keep[from..from+n). Звать один раз
		в конце: прочитанные указатели остаются в мёртвых кадрах. *)
		VAR i, bad, tag: INTEGER;
	BEGIN
		bad := 0;
		FOR i := 0 TO n - 1 DO
			IF keep[from + i] # NIL THEN
				tag := from + i;
				IF (keep[from + i][0] = tag) & (keep[from + i][1] = tag * 7 + 1) THEN
				ELSE INC(bad)
				END
			END
		END;
		RETURN bad
	END BadLive;

	PROCEDURE Do*;
		VAR made, kept: INTEGER; a0, a1, a2, a2b, a3: LONGINT;
	BEGIN
		System_devSupport.Begin("InHeap");

		a0 := Kernel.Allocated();
		made := Allocate(0, total, 0);
		System_devSupport.Check(made = total, "all big objects were allocated");
		a1 := Kernel.Allocated();
		(* 90 %: промежуточные сборки успевают освободить старый мусор,
		   поэтому счётчик чуть ниже суммы размеров наших объектов. *)
		System_devSupport.Check(a1 - a0 >= total * bigBytes * 9 DIV 10,
			"heap grew over many clusters");

		(* Кластеры живых объектов остаются, мусорные уходят. *)
		Release(live, total - live);
		Kernel.Collect;
		a2 := Kernel.Allocated();
		System_devSupport.Check(a2 < a1, "garbage clusters were deallocated");
		System_devSupport.Check(a2 - a0 >= live * bigBytes * 9 DIV 10, "live clusters survived");
		System_devSupport.Check(a2 - a0 < total * bigBytes * 9 DIV 10,
			"garbage bytes left the heap");

		(* Второй круг: часть живых отпущена, куча снова растёт и
		   собирается — индекс перестраивается и при вставке, и при удалении. *)
		kept := live DIV 2;
		Release(1, kept);			(* нечётные слоты *)
		made := Allocate(live, second, 2000);
		System_devSupport.Check(made = second, "second batch was allocated");
		a2b := Kernel.Allocated();
		Release(live, second);
		Kernel.Collect;
		a3 := Kernel.Allocated();
		System_devSupport.Check(a3 < a2b, "second batch was collected");
		System_devSupport.Check(a3 - a0 >= kept * bigBytes * 9 DIV 10, "kept clusters survived");
		(* Порог между 25 оставшимися и 50 живыми: отпущенные кластеры ушли. *)
		System_devSupport.Check(a3 - a0 < (kept + live) DIV 2 * bigBytes,
			"released clusters were freed");
		System_devSupport.Check(BadLive(0, live) = 0, "live objects are intact");

		System_devSupport.End
	END Do;

END System_devInHeap.
