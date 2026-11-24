MODULE ActorRun;
(** Исполнители ящиков: пул задач и собственные задачи акторов.

	Пул — n задач, общая очередь ящиков с работой (кольцо run). Свободный
	исполнитель ждёт в Tasks.Park; Schedule будит его Unpark. Будить
	можно только задачу в этом ожидании: задачу, ждущую внутри Receive
	(монитор, канал), Unpark сбил бы с ожидания. Поэтому у исполнителя
	флаг idle, поднятый ровно на время ожидания.

	Первый исполнитель вместо Park спит tick мс. Unpark, сделанный вне
	задачи (команда, действие Services), не переносит срок планировщика,
	уже назначенный на будущее; без спящей задачи с близким сроком
	разбуженный исполнитель ждал бы этого срока. Спящий первый исполнитель
	ограничивает задержку tick мс и избавляет планировщик от сообщения о
	взаимной блокировке, когда все исполнители свободны.

	Ящик с флагом dedicated обслуживает своя задача Own; она живёт, пока
	актор не остановлен.

	Инвариант: каждый ящик в run стоит у исполнителя (Pending) и лежит в
	run не более одного раза; idle => исполнитель в Tasks.Sleep цикла. *)

	IMPORT Tasks, ActorCore;

	CONST
		throughput = 100;	(* писем одного ящика за вызов Process *)
		tick = 10;	(* мс сна первого исполнителя *)
		yieldEvery = 64;	(* ящиков между уступками *)
		maxExec = 64;

	TYPE
		Pool = POINTER TO RECORD (ActorCore.Dispatcher) END;
		Exec = POINTER TO RECORD (Tasks.Task) idle: BOOLEAN END;
		Own = POINTER TO RECORD (Tasks.Task) c: ActorCore.Context; idle: BOOLEAN END;

	VAR
		execs: ARRAY maxExec OF Exec;
		n-: INTEGER;	(** число исполнителей пула; 0 — пул остановлен *)
		run: POINTER TO ARRAY OF ActorCore.Context;
		head, count: INTEGER;
		pool: Pool;

	PROCEDURE PushRun (c: ActorCore.Context);
		VAR new: POINTER TO ARRAY OF ActorCore.Context; i, len: INTEGER;
	BEGIN
		len := LEN(run^);
		IF count = len THEN
			NEW(new, 2 * len);
			FOR i := 0 TO len - 1 DO new[i] := run[(head + i) MOD len] END;
			run := new; head := 0; len := 2 * len
		END;
		run[(head + count) MOD len] := c; INC(count)
	END PushRun;

	PROCEDURE PopRun (): ActorCore.Context;
		VAR c: ActorCore.Context;
	BEGIN
		c := run[head]; run[head] := NIL;
		head := (head + 1) MOD LEN(run^); DEC(count);
		RETURN c
	END PopRun;

	PROCEDURE (e: Exec) Run;
		VAR c: ActorCore.Context; k: INTEGER;
	BEGIN
		k := 0;
		LOOP
			IF count > 0 THEN
				c := PopRun();
				IF ActorCore.Process(c, throughput) THEN PushRun(c) END;
				INC(k);
				IF k MOD yieldEvery = 0 THEN Tasks.Yield END
			ELSE
				e.idle := TRUE;
				IF e = execs[0] THEN Tasks.Sleep(tick) ELSE Tasks.Park END;
				e.idle := FALSE
			END
		END
	END Run;

	PROCEDURE (t: Own) Run;
		VAR c: ActorCore.Context;
	BEGIN
		c := t.c;
		LOOP
			IF c.Pending() THEN
				IF ActorCore.Process(c, throughput) THEN Tasks.Yield END
			ELSIF c.IsStopped() THEN EXIT
			ELSE
				t.idle := TRUE; Tasks.Park; t.idle := FALSE
			END
		END
	END Run;

	PROCEDURE (p: Pool) Schedule (c: ActorCore.Context);
		VAR i: INTEGER; o: Own;
	BEGIN
		IF c.dedicated THEN
			IF (c.host = NIL) OR (Tasks.State(c.host(Own)) IN {Tasks.Finished, Tasks.Cancelled}) THEN
				NEW(o); o.c := c; c.host := o; Tasks.Start(o)
			ELSE
				o := c.host(Own);
				IF o.idle THEN o.idle := FALSE; Tasks.Unpark(o) END
			END
		ELSE
			PushRun(c);
			i := 0;
			WHILE (i < n) & ~execs[i].idle DO INC(i) END;
			IF i < n THEN execs[i].idle := FALSE; Tasks.Unpark(execs[i]) END
		END
	END Schedule;

	PROCEDURE Start* (k: INTEGER);
	(** Пред: 0 < k <= 64 (трап 20); пул остановлен (21).
		Пост: пул из k исполнителей установлен в ActorCore; ящики, ждавшие
		в очереди, обслуживаются. *)
		VAR i: INTEGER;
	BEGIN
		ASSERT((k > 0) & (k <= maxExec), 20); ASSERT(n = 0, 21);
		FOR i := 0 TO k - 1 DO NEW(execs[i]); execs[i].idle := FALSE; Tasks.Start(execs[i]) END;
		n := k;
		ActorCore.SetDispatcher(pool)
	END Start;

	PROCEDURE Stop*;
	(** Пост: задачи пула отменены (трап 127 в их точке переключения);
		ящики с работой ждут следующего Start. Собственные задачи акторов
		не затронуты. *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO execs[i].idle := FALSE; Tasks.Cancel(execs[i]); execs[i] := NIL END;
		n := 0
	END Stop;

BEGIN
	NEW(run, 64); NEW(pool)
END ActorRun.
