MODULE Test64InHeap;
(** Шаг 2а: корректность InHeap с индексом кластеров.

	Проверяем адреса внутри кластеров, на границах и вне; адрес стека;
	исчезновение кластера из индекса после его освобождения сборщиком.
	Индекс виден через Kernel.ClusterBounds/ClusterIndexSize/
	ClusterIndexCheck, сам InHeap — через Kernel.InHeapAddr.

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
		VAR i, n, n0: INTEGER; base, size: LONGINT;
			key: LONGINT; local: INTEGER; ok: BOOLEAN;
	BEGIN
		Test64Support.Begin("InHeap");

		Test64Support.Check(Kernel.ClusterIndexCheck(), "cluster index invariant");
		Test64Support.Check(Kernel.ClusterIndexSize() >= 1, "index is not empty");
		Test64Support.Check(Kernel.ClusterIndexSize() = Kernel.Clusters(), "index size = cluster count");

		(* Границы каждого кластера: [base+24, base+size) *)
		n := Kernel.ClusterIndexSize();
		i := 0;
		WHILE i < n DO
			ok := Kernel.ClusterBounds(i, base, size);
			Test64Support.Check(ok, "ClusterBounds");
			IF ok THEN
				Test64Support.Check(size > 24, "cluster size > header");
				Test64Support.Check(Kernel.InHeapAddr(base + 24), "base+24 inside");
				Test64Support.Check(Kernel.InHeapAddr(base + size - 1), "base+size-1 inside");
				Test64Support.Check(~Kernel.InHeapAddr(base), "base outside");
				Test64Support.Check(~Kernel.InHeapAddr(base + 23), "base+23 outside");
				Test64Support.Check(~Kernel.InHeapAddr(base + size), "base+size outside")
			END;
			INC(i)
		END;

		(* Вне кучи: ноль, отрицательное, адрес на стеке *)
		Test64Support.Check(~Kernel.InHeapAddr(0), "0 outside");
		Test64Support.Check(~Kernel.InHeapAddr(-1), "-1 outside");
		local := 1;
		Test64Support.Check(~Kernel.InHeapAddr(SYSTEM.VAL(LONGINT, SYSTEM.ADR(local))), "stack address outside");

		(* Большой объект: указатель в куче, затем освобождение кластера *)
		n0 := Kernel.Clusters();
		key := MakeBigKey();
		Test64Support.Check(key # 0, "big allocation failed");
		IF key # 0 THEN
			Test64Support.Check(Kernel.InHeapAddr(key - mask), "big object address inside");
			Test64Support.Check(Kernel.InHeapAddr((key - mask) - 8), "big object tag inside");
			Test64Support.Check(Kernel.Clusters() > n0, "big object got its own cluster");
			Test64Support.Check(Kernel.ClusterIndexCheck(), "index invariant after insert");

			Kernel.Collect;
			Test64Support.Check(~Kernel.InHeapAddr(key - mask), "address inside freed cluster");
			Test64Support.Check(Kernel.Clusters() < n0 + 1, "cluster was deallocated");
			Test64Support.Check(Kernel.ClusterIndexCheck(), "index invariant after remove");
			Test64Support.Check(Kernel.ClusterIndexSize() = Kernel.Clusters(), "index size after remove")
		END;

		(* Повторные добавление/удаление: индекс не должен «залипать» *)
		i := 0;
		WHILE i < 3 DO
			key := MakeBigKey();
			IF key # 0 THEN
				Kernel.Collect;
				Test64Support.Check(~Kernel.InHeapAddr(key - mask), "repeat: freed cluster outside");
				Test64Support.Check(Kernel.ClusterIndexCheck(), "repeat: index invariant")
			END;
			INC(i)
		END;

		Test64Support.End
	END Do;

END Test64InHeap.
