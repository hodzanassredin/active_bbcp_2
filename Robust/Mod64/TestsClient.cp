MODULE RobustTestsClient;
(** Клиент RobustParallel в стиле MPI: rank 0 раздаёт работу, остальные
	ранги считают. Написан только против опубликованного определения
	RobustParallel и Stores — как программа Ивана Денисова.

	Формула доклада: инициализация (тело RobustParallel по ROBUST_SIZE) →
	[[отправка/приём]n, ожидание и расчёт]n → финализация (Stop).
	В нашей модели «остальные считают» — это Do компутона в отдельном
	процессе-исполнителе; программа на других рангах не исполняется. *)

	IMPORT Stores, RobustParallel;

	CONST
		tagBase = 100;

	TYPE
		Chunk = POINTER TO RECORD (RobustParallel.Computon)
			a, b, sum: INTEGER
		END;

	PROCEDURE (c: Chunk) Do;
		VAR i: INTEGER;
	BEGIN
		c.sum := 0;
		FOR i := c.a TO c.b DO INC(c.sum, i) END
	END Do;

	PROCEDURE (c: Chunk) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(0);
		wr.WriteInt(c.a); wr.WriteInt(c.b); wr.WriteInt(c.sum)
	END Externalize;

	PROCEDURE (c: Chunk) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, version);
		rd.ReadInt(c.a); rd.ReadInt(c.b); rd.ReadInt(c.sum)
	END Internalize;

	PROCEDURE Sum* (n: INTEGER): INTEGER;
	(** Сумма 1..n, разложенная по рангам 1..size-1. *)
		VAR i, parts, per, lo, hi, total: INTEGER;
			c: Chunk; r: RobustParallel.Computon;
	BEGIN
		IF n < 0 THEN RETURN -1 END;
		IF RobustParallel.rank = 0 THEN
			parts := RobustParallel.size - 1;
			IF parts <= 0 THEN
				(* Мир из одного ранга: считаем на месте, как MPI на size=1. *)
				NEW(c); c.a := 1; c.b := n; c.sum := 0; c.Do;
				RETURN c.sum
			END;
			per := (n + parts - 1) DIV parts;
			lo := 1;
			(* раздача *)
			FOR i := 1 TO parts DO
				hi := lo + per - 1;
				IF hi > n THEN hi := n END;
				NEW(c); c.a := lo; c.b := hi; c.sum := 0;
				RobustParallel.Push(c, i, tagBase + i);
				lo := hi + 1
			END;
			(* ожидание и расчёт *)
			total := 0;
			FOR i := 1 TO parts DO
				r := RobustParallel.Pull(i, tagBase + i);
				IF r = NIL THEN RETURN -2 END;
				WITH r: Chunk DO total := total + r.sum ELSE RETURN -3 END
			END;
			RETURN total
		ELSE
			(* MPI-ветвь исполнителя: в этой модели её заменяет Do. *)
			RETURN -4
		END
	END Sum;

END RobustTestsClient.
