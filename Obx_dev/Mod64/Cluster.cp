MODULE Obx_devCluster;
(** Тесты счёта и координатора ObxCluster без сети.

	Проверяются границы Fits, снимок и восстановление списка номеров
	(все 128 мест) и остатка, повтор уже применённой операции. Сеть,
	пассивация и повторы шага проверяются сценарием obxcluster. *)

	IMPORT UnitAssert, ActorCore, ObxCluster;

	PROCEDURE NewMoved (op: LONGINT; sum: INTEGER; ok: BOOLEAN): ObxCluster.Moved;
		VAR e: ObxCluster.Moved;
	BEGIN
		NEW(e); e.op := op; e.sum := sum; e.ok := ok; RETURN e
	END NewMoved;

	PROCEDURE TestFits*;
	(** Границы допуска входят: списание до нуля и зачисление ровно до
		предела проходят, на единицу дальше — нет; возврат проходит всегда. *)
	BEGIN
		UnitAssert.True(ObxCluster.Fits(1000, 0, -1000), "debit to zero fits");
		UnitAssert.True(~ObxCluster.Fits(1000, 0, -1001), "debit below zero");
		UnitAssert.True(ObxCluster.Fits(0, 0, 0), "zero debit fits");
		UnitAssert.True(ObxCluster.Fits(1000, 1, 300), "credit to limit fits");
		UnitAssert.True(~ObxCluster.Fits(1000, 1, 301), "credit above limit");
		UnitAssert.True(ObxCluster.Fits(0, 2, 500), "refund fits below zero");
		UnitAssert.True(ObxCluster.Fits(1300, 2, 500), "refund fits above limit")
	END TestFits;

	PROCEDURE TestAccountRestore*;
	(** Снимок восстанавливает остаток, номера и итоги операций; повтор
		восстановленного номера не двигает деньги и не добавляет событие. *)
		VAR x: ActorCore.Actor; a, b: ObxCluster.Account; i: INTEGER;
	BEGIN
		x := ObxCluster.NewAccount(); a := x(ObxCluster.Account);
		a.Apply(NewMoved(0, -100, TRUE));
		a.Apply(NewMoved(1, 50, FALSE));
		a.Apply(NewMoved(2, 30, TRUE));
		UnitAssert.EqualInt(ObxCluster.initial - 70, a.balance, "apply balance");
		UnitAssert.EqualInt(3, a.n, "apply count");
		x := ObxCluster.NewAccount(); b := x(ObxCluster.Account);
		b.Restore(a.Snapshot());
		UnitAssert.EqualInt(a.balance, b.balance, "restore balance");
		UnitAssert.EqualInt(a.n, b.n, "restore count");
		FOR i := 0 TO a.n - 1 DO
			UnitAssert.True(a.ops[i] = b.ops[i], "restore op number");
			UnitAssert.True(a.oks[i] = b.oks[i], "restore op result")
		END;
		b.Apply(NewMoved(0, -100, TRUE));	(* повтор уже записанной операции *)
		UnitAssert.EqualInt(1, b.dups, "repeat counted");
		UnitAssert.EqualInt(a.n, b.n, "repeat adds no event")
	END TestAccountRestore;

	PROCEDURE TestAccountFull*;
	(** Счёт помнит все 128 номеров; снимок сохраняет их целиком, повтор
		любого из них учтён, а новый номер при полном списке — порча. *)
		VAR x: ActorCore.Actor; a, b: ObxCluster.Account; i, cap: INTEGER;
	BEGIN
		x := ObxCluster.NewAccount(); a := x(ObxCluster.Account);
		cap := LEN(a.ops);
		FOR i := 0 TO cap - 1 DO a.Apply(NewMoved(i * 3, 1, FALSE)) END;
		UnitAssert.EqualInt(cap, a.n, "all slots used");
		UnitAssert.EqualInt(ObxCluster.initial, a.balance, "rejected moves no money");
		x := ObxCluster.NewAccount(); b := x(ObxCluster.Account);
		b.Restore(a.Snapshot());
		UnitAssert.EqualInt(cap, b.n, "restore all numbers");
		UnitAssert.EqualInt(a.balance, b.balance, "restore full balance");
		FOR i := 0 TO cap - 1 DO
			UnitAssert.True(a.ops[i] = b.ops[i], "restore full op");
			UnitAssert.True(a.oks[i] = b.oks[i], "restore full result")
		END;
		b.Apply(NewMoved(3, 1, FALSE));	(* повтор середины списка *)
		UnitAssert.EqualInt(1, b.dups, "repeat of stored number");
		UnitAssert.EqualInt(cap, b.n, "repeat no slot");
		b.Apply(NewMoved(cap * 3, 1, FALSE));	(* новый номер при полном списке *)
		UnitAssert.EqualInt(2, b.dups, "full list counts");
		UnitAssert.EqualInt(cap, b.n, "full list no slot");
		UnitAssert.EqualInt(a.balance, b.balance, "full list no money")
	END TestAccountFull;

	PROCEDURE TestCoordRestore*;
	(** Снимок координатора сохраняет шаг: номер, счета, сумму и состояние. *)
		VAR x: ActorCore.Actor; k, q: ObxCluster.Coord; e: ObxCluster.Step;
	BEGIN
		x := ObxCluster.NewCoord(); k := x(ObxCluster.Coord);
		NEW(e); e.id := 7; e.from := 1; e.to := 2; e.sum := 100;
		e.state := ObxCluster.withdrawn;
		k.Apply(e);
		UnitAssert.True(k.started, "coord started");
		UnitAssert.EqualInt(ObxCluster.withdrawn, k.state, "coord state");
		x := ObxCluster.NewCoord(); q := x(ObxCluster.Coord);
		q.Restore(k.Snapshot());
		UnitAssert.True(q.started, "restore started");
		UnitAssert.EqualInt(7, q.id, "restore id");
		UnitAssert.EqualInt(1, q.from, "restore from");
		UnitAssert.EqualInt(2, q.to, "restore to");
		UnitAssert.EqualInt(100, q.sum, "restore sum");
		UnitAssert.EqualInt(ObxCluster.withdrawn, q.state, "restore state")
	END TestCoordRestore;

END Obx_devCluster.
