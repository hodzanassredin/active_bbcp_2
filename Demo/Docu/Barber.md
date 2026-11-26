# Обзор на примерах: DemoBarber

Спящий парикмахер.

Парикмахер спит, пока нет клиентов; клиент садится ждать, если есть свободный стул, и уходит, если
стульев нет. Задача Дейкстры показывает, как монитор согласует несколько условий. Пример взят из
курса ETH *System Construction* (монитор «Barber Shop»,
<https://lec.inf.ethz.ch/syscon/2019/download/syscon06.pdf>); семантика `AWAIT` — P. Reali,
*Active Oberon Language Report*, §3.3.

```
MODULE DemoBarber;
(** Спящий парикмахер (Sleeping Barber).

	Перенесено из мониторного решения классической задачи Дейкстры
	(монитор Barber Shop: barber / chair / open / exit, условные
	переменные barberAvailable, chairOccupied, doorOpen, clientLeft;
	см. лекцию System Construction, ETH Zurich, и Reali, Active Oberon
	Language Report, 2004, §3.3: AWAIT — та же условная переменная,
	но условие произвольное, а signal заменён перепроверкой условий
	уходящим из монитора).

	Перевод: каждое "while cond do wait" стало ожиданием флага Ready у
	монитора-объекта Shop; Enter, Next и Finish — операции над ним.
	Семафоры и signal не используются.

	Состояние: barbers — свободен ли парикмахер; waiting — посетители
	на стульях; serving — посетитель уже забрал парикмахера; open —
	стрижка окончена, посетитель ещё не ушёл.

	Команда DemoBarber.Run: 6 посетителей и 3 стула, парикмахер
	начинает после них. Итог детерминирован: 3 подстрижено, 3 ушло. *)

	IMPORT Tasks, DemoSupport;

	CONST
		capacity = 3;	(* стулья в приёмной *)
		customers = 6;
		cuts = 3;	(* парикмахер успевает подстричь всех, кто сел *)

		(* флаги Ready *)
		barberAvailable = 0;	(* barbers > 0 *)
		servingPositive = 1;	(* serving > 0 *)
		openPositive = 2;	(* open > 0 *)
		openZero = 3;		(* open = 0 *)

	TYPE
		EnterOp = RECORD (Tasks.Op) id: INTEGER; got: BOOLEAN END;
		NextOp = RECORD (Tasks.Op) END;
		FinishOp = RECORD (Tasks.Op) END;

		Shop* = POINTER TO RECORD (Tasks.Object)
			barbers: INTEGER;	(* 1 — парикмахер свободен *)
			waiting: INTEGER;	(* посетители на стульях *)
			serving: INTEGER;	(* забрал парикмахера и сел в кресло *)
			open: INTEGER;		(* стрижка окончена, ждём ухода *)
			served*, turnedAway*: INTEGER
		END;

		Customer = POINTER TO RECORD (Tasks.Job)
			shop: Shop; id: INTEGER; got: BOOLEAN
		END;

		Barber = POINTER TO RECORD (Tasks.Job) shop: Shop END;

	VAR
		Served*: INTEGER;
		TurnedAway*: INTEGER;
		Failed*: BOOLEAN;

	PROCEDURE (s: Shop) Ready* (): SET;
		VAR r: SET;
	BEGIN
		r := {};
		IF s.barbers > 0 THEN INCL(r, barberAvailable) END;
		IF s.serving > 0 THEN INCL(r, servingPositive) END;
		IF s.open > 0 THEN INCL(r, openPositive) END;
		IF s.open = 0 THEN INCL(r, openZero) END;
		RETURN r
	END Ready;

	PROCEDURE (s: Shop) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: EnterOp DO
			(* Посетитель входит; FALSE — стульев нет, уходит. *)
			IF s.waiting >= capacity THEN
				INC(s.turnedAway);
				op.got := FALSE; RETURN
			END;
			INC(s.waiting);
			(* ждём свободного парикмахера *)
			IF ~s.Await(ctx, {barberAvailable}) THEN op.got := FALSE; RETURN END;
			DEC(s.barbers);			(* забрали парикмахера *)
			DEC(s.waiting);			(* ушли со стула в кресло *)
			INC(s.serving);
			(* ждём конца стрижки *)
			IF ~s.Await(ctx, {openPositive}) THEN op.got := FALSE; RETURN END;
			DEC(s.open);			(* уходим, парикмахер может работать дальше *)
			op.got := TRUE
		| op: NextOp DO
			(* Парикмахер свободен и ждёт посетителя. *)
			INC(s.barbers);
			IF ~s.Await(ctx, {servingPositive}) THEN RETURN END;
			DEC(s.serving)
		| op: FinishOp DO
			(* Стрижка окончена; ждать, пока посетитель уйдёт. *)
			INC(s.served);
			INC(s.open);
			IF ~s.Await(ctx, {openZero}) THEN RETURN END
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (c: Customer) Body- (VAR ctx: Tasks.Ctx);
		VAR op: EnterOp;
	BEGIN
		op.id := c.id; op.got := FALSE;
		c.shop.Do(ctx, op);
		c.got := op.got
	END Body;

	PROCEDURE (b: Barber) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; next: NextOp; finish: FinishOp;
	BEGIN
		FOR i := 1 TO cuts DO
			b.shop.Do(ctx, next);
			Tasks.Sleep(1);		(* стрижка *)
			b.shop.Do(ctx, finish)
		END
	END Body;

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
```

Модуль собирает `tools/build64.sh` (исходник — `Demo/Mod/Barber.cp`; подсистема `Demo` — только для
amd64). В окне BlackBox команду исполняет коммандер:

```
   DemoBarber.Run
```

В консоли:

```sh
echo 'DemoBarber.Run' | tools/run64.sh
```

В журнале появится:

```
DemoBarber: served=3 turnedAway=3
```

## Что произошло

В курсе монитор написан на условных переменных `barberAvailable`, `chairOccupied`, `doorOpen` и
`clientLeft`. Каждый цикл `while cond do wait` здесь стал ожиданием флага `Ready` в `Await`;
монитор — объект `Shop` с операциями `EnterOp`, `NextOp` и `FinishOp`. Состояние: `barbers` — парикмахер свободен (1) или занят
(0); `waiting` — клиенты на стульях; `serving` — клиент занял парикмахера; `open` — стрижка
закончена, но клиент ещё не ушёл.

Шесть клиентов приходят раньше парикмахера: первые три занимают стулья, остальные трое уходят.
Затем парикмахер обслуживает троих сидящих.

## Тест

`Demo_devExamples.TestProcBarber` проверяет 3 обслуженных, 3 отказа, отсутствие трапов и тупика.

В этом примере мы увидели, как условные переменные и сигналы классического монитора
заменяются флагами `Ready` и ожиданием `Await`.
