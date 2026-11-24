MODULE DemoBarber;
(** Спящий парикмахер (Sleeping Barber).

	Перенесено из мониторного решения классической задачи Дейкстры
	(монитор Barber Shop: barber / chair / open / exit, условные
	переменные barberAvailable, chairOccupied, doorOpen, clientLeft;
	см. лекцию System Construction, ETH Zurich, и Reali, Active Oberon
	Language Report, 2004, §3.3: AWAIT — та же условная переменная,
	но условие произвольное, а signal заменён перепроверкой условий
	уходящим из монитора).

	Перевод: каждое "while cond do wait" стало ACTIVE.AWAIT(условие);
	монитор — объект Shop, методы помечены [exclusive]. Семафоры и
	signal не используются.

	Состояние: barbers — свободен ли парикмахер; waiting — посетители
	на стульях; serving — посетитель уже забрал парикмахера; open —
	стрижка окончена, посетитель ещё не ушёл.

	Команда DemoBarber.Run: 6 посетителей и 3 стула, парикмахер
	начинает после них. Итог детерминирован: 3 подстрижено, 3 ушло. *)

	IMPORT ACTIVE, Tasks, DemoSupport;

	CONST
		capacity = 3;	(* стулья в приёмной *)
		customers = 6;
		cuts = 3;	(* парикмахер успевает подстричь всех, кто сел *)

	TYPE
		Shop* = POINTER TO RECORD
			barbers: INTEGER;	(* 1 — парикмахер свободен *)
			waiting: INTEGER;	(* посетители на стульях *)
			serving: INTEGER;	(* забрал парикмахера и сел в кресло *)
			open: INTEGER;		(* стрижка окончена, ждём ухода *)
			served*, turnedAway*: INTEGER
		END;

		Customer = POINTER TO RECORD (Tasks.Task)
			shop: Shop; id: INTEGER; got: BOOLEAN
		END;

		Barber = POINTER TO RECORD (Tasks.Task) shop: Shop END;

	VAR
		Served*: INTEGER;
		TurnedAway*: INTEGER;
		Failed*: BOOLEAN;

	PROCEDURE [exclusive] (s: Shop) Enter* (id: INTEGER): BOOLEAN, NEW;
	(** Посетитель входит; FALSE — стульев нет, уходит. *)
	BEGIN
		IF s.waiting >= capacity THEN
			INC(s.turnedAway);
			RETURN FALSE
		END;
		INC(s.waiting);
		ACTIVE.AWAIT(s.barbers > 0);	(* ждём свободного парикмахера *)
		DEC(s.barbers);			(* забрали парикмахера *)
		DEC(s.waiting);			(* ушли со стула в кресло *)
		INC(s.serving);
		ACTIVE.AWAIT(s.open > 0);	(* ждём конца стрижки *)
		DEC(s.open);			(* уходим, парикмахер может работать дальше *)
		RETURN TRUE
	END Enter;

	PROCEDURE [exclusive] (s: Shop) Next*, NEW;
	(** Парикмахер свободен и ждёт посетителя. *)
	BEGIN
		INC(s.barbers);
		ACTIVE.AWAIT(s.serving > 0);
		DEC(s.serving)
	END Next;

	PROCEDURE [exclusive] (s: Shop) Finish*, NEW;
	(** Стрижка окончена; ждать, пока посетитель уйдёт. *)
	BEGIN
		INC(s.served);
		INC(s.open);
		ACTIVE.AWAIT(s.open = 0)
	END Finish;

	PROCEDURE (c: Customer) Run;
	BEGIN
		c.got := c.shop.Enter(c.id)
	END Run;

	PROCEDURE (b: Barber) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO cuts DO
			b.shop.Next;
			Tasks.Sleep(1);		(* стрижка *)
			b.shop.Finish
		END
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: 6 посетителей, 3 стула, один парикмахер. *)
		VAR s: Shop; c: ARRAY customers OF Customer; b: Barber;
			i: INTEGER; ok: BOOLEAN;
	BEGIN
		Served := 0; TurnedAway := 0; Failed := FALSE;
		NEW(s); s.barbers := 0; s.waiting := 0; s.serving := 0; s.open := 0;
		s.served := 0; s.turnedAway := 0;
		(* все посетители приходят раньше парикмахера: первые три
		   занимают стулья, остальные уходят *)
		FOR i := 0 TO customers - 1 DO
			NEW(c[i]); c[i].shop := s; c[i].id := i + 1; Tasks.Start(c[i])
		END;
		NEW(b); b.shop := s; Tasks.Start(b);
		ok := DemoSupport.Pump(15000);
		FOR i := 0 TO customers - 1 DO
			IF Tasks.Failed(c[i]) THEN Failed := TRUE END
		END;
		IF Tasks.Failed(b) THEN Failed := TRUE END;
		Failed := Failed OR ~ok;
		Served := s.served; TurnedAway := s.turnedAway;
		DemoSupport.LogInt("DemoBarber: served=", Served);
		DemoSupport.LogInt(" turnedAway=", TurnedAway);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoBarber.
