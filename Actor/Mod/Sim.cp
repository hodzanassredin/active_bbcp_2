MODULE ActorSim;
(** Исполнитель ящиков для детерминированной проверки.

	Ящики с работой стоят в одной очереди по порядку прихода; Step берёт
	первый, обрабатывает одно пользовательское письмо (и все системные) и,
	если работа осталась, ставит ящик в конец. Своих задач у исполнителя
	нет: Step зовёт одна задача-водитель. Без реального времени и без
	нескольких задач порядок обработки определяется только порядком
	писем, поэтому прогон, где сеть и сбои берут случайность от зерна,
	повторяется шаг в шаг.

	Тайм-аут простоя и Ask живут в реальном времени: в детерминированном
	прогоне ими не пользуются, повторы идут по шагам водителя. *)

	IMPORT Tasks, ActorCore;

	TYPE
		Item = POINTER TO RECORD c: ActorCore.Context; next: Item END;

		Sim = POINTER TO RECORD (ActorCore.Dispatcher)
			head, tail: Item
		END;

	VAR
		sim: Sim;
		steps-: LONGINT;	(** обработанных ящиков с Install *)

	PROCEDURE (s: Sim) Schedule (c: ActorCore.Context);
		VAR it: Item;
	BEGIN
		NEW(it); it.c := c;
		IF s.tail = NIL THEN s.head := it ELSE s.tail.next := it END;
		s.tail := it
	END Schedule;

	PROCEDURE Install*;
	(** Пред: пул ActorRun остановлен (его исполнители не работают) — не
		проверяется. Пост: ящики обслуживает этот исполнитель; очередь пуста. *)
	BEGIN
		NEW(sim); steps := 0;
		ActorCore.SetDispatcher(sim)
	END Install;

	PROCEDURE Idle* (): BOOLEAN;
	(** Пост: ящиков с работой нет. *)
	BEGIN
		RETURN (sim = NIL) OR (sim.head = NIL)
	END Idle;

	PROCEDURE Step* (): BOOLEAN;
	(** Пред: Install; вызов из задачи — 20.
		Пост: FALSE — работы не было; TRUE — первый ящик очереди обработал
		все системные письма и не более одного пользовательского. *)
		VAR it: Item;
	BEGIN
		ASSERT(Tasks.InTask() & (sim # NIL) & (ActorCore.dispatcher = sim), 20);
		it := sim.head;
		IF it = NIL THEN RETURN FALSE END;
		sim.head := it.next; IF sim.head = NIL THEN sim.tail := NIL END;
		INC(steps);
		IF sim.Process(it.c, 1) THEN sim.Schedule(it.c) END;
		RETURN TRUE
	END Step;

	PROCEDURE Drain* (max: INTEGER): BOOLEAN;
	(** Step, пока работа есть, но не больше max раз. Пост: TRUE — работы нет. *)
	BEGIN
		WHILE (max > 0) & Step() DO DEC(max) END;
		RETURN Idle()
	END Drain;

END ActorSim.
