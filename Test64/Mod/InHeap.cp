MODULE Test64InHeap;
(** Шаг 2а: корректность индекса кластеров после того, как из ядра убраны
	отладочные запросы InHeapAddr/ClusterBounds/ClusterIndexSize/Clusters.
	Наружу оставлена одна проверка инварианта — Kernel.ClusterIndexCheck:
	индекс строго возрастает и совпадает по составу с цепочкой root.

	Наблюдаемые следствия индекса проверяются через публичный счётчик
	Kernel.Allocated: большой объект получает отдельный кластер (allocated
	растёт), после Collect освобождённый кластер уходит из кучи (allocated
	падает), инвариант индекса держится после вставки и удаления.

	Адрес большого объекта нельзя держать в стеке во время Collect
	(консервативный ScanStack принял бы число за указатель и сохранил
	объект), поэтому он возвращается из отдельной процедуры уже
	замаскированным (a + mask уводит число за диапазон кучи). *)

	IMPORT SYSTEM, Kernel, Test64Support;

	CONST
		mask = 4611686018427387904;	(* 2^62: адреса кучи Linux x64 ниже 2^47 *)
		bigSize = 1024 * 1024;		(* INTEGER'ов, 4 МБ — отдельный кластер *)

	TYPE
		Big = POINTER TO ARRAY bigSize OF INTEGER;

	(** Выделить большой объект отдельным кластером и вернуть его адрес
		+ mask. Объект больше не удерживается: в кадре Do не остаётся
		незамаскированного адреса. *)
	PROCEDURE MakeBigKey (): LONGINT;
		VAR p: Big; a: LONGINT;
	BEGIN
		NEW(p);
		IF p = NIL THEN RETURN 0 END;
		a := SYSTEM.VAL(LONGINT, SYSTEM.ADR(p[0]));
		p := NIL;
		RETURN a + mask
	END MakeBigKey;

	PROCEDURE Do*;
		VAR i: INTEGER; key, a0, a1: LONGINT;
	BEGIN
		Test64Support.Begin("InHeap");

		Test64Support.Check(Kernel.ClusterIndexCheck(), "cluster index invariant");

		(* Большой объект: указатель в куче, затем освобождение кластера *)
		a0 := Kernel.Allocated();
		key := MakeBigKey();
		Test64Support.Check(key # 0, "big allocation failed");
		IF key # 0 THEN
			a1 := Kernel.Allocated();
			Test64Support.Check(a1 > a0, "big object got its own cluster");
			Test64Support.Check(Kernel.ClusterIndexCheck(), "index invariant after insert");

			Kernel.Collect;
			Test64Support.Check(Kernel.Allocated() < a1, "cluster was deallocated");
			Test64Support.Check(Kernel.ClusterIndexCheck(), "index invariant after remove")
		END;

		(* Повторные добавление/удаление: индекс не должен «залипать» *)
		i := 0;
		WHILE i < 3 DO
			key := MakeBigKey();
			IF key # 0 THEN
				Kernel.Collect;
				Test64Support.Check(Kernel.ClusterIndexCheck(), "repeat: index invariant")
			END;
			INC(i)
		END;

		Test64Support.End
	END Do;

END Test64InHeap.
