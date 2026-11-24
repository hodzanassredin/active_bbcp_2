MODULE ActorRun;
(** Исполнители ящиков: пул задач и собственные задачи акторов.

	Пул — n задач, общая очередь ящиков с работой (кольцо run). Свободный
	исполнитель ждёт в Tasks.ParkExternal: письмо может прийти с главного
	стека, поэтому, пока пул жив, планировщик не объявляет тупик. Schedule
	будит исполнителя Unpark. Будить
	можно только задачу в этом ожидании: задачу, ждущую внутри Receive
	(монитор, канал), Unpark сбил бы с ожидания. Поэтому у исполнителя
	флаг idle, поднятый ровно на время ожидания.

	Ящик с флагом dedicated обслуживает своя задача Own; она живёт, пока
	актор не остановлен. Отменённую Own заменяет новая при следующей
	работе ящика: отмена задачи актора — не отказ и не остановка.

	Shutdown останавливает все акторы и завершает задачи пула без отмены.

	Инвариант: каждый ящик в run стоит у исполнителя (Pending) и лежит в
	run не более одного раза; idle => исполнитель в Tasks.ParkExternal цикла. *)

	IMPORT Services, Tasks, ActorCore;

	CONST
		throughput = 100;	(* писем одного ящика за вызов Process *)
		yieldEvery = 64;	(* ящиков между уступками *)
		maxExec = 64;
		shutdownMs = 5000;	(* предел ожидания остановки акторов *)

	TYPE
		Pool = POINTER TO RECORD (ActorCore.Dispatcher) END;
		Exec = POINTER TO RECORD (Tasks.Task) idle, quit: BOOLEAN END;
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
				IF pool.Process(c, throughput) THEN PushRun(c) END;
				INC(k);
				IF k MOD yieldEvery = 0 THEN Tasks.Yield END
			ELSIF e.quit THEN EXIT
			ELSE
				e.idle := TRUE; Tasks.ParkExternal; e.idle := FALSE
			END
		END
	END Run;

	PROCEDURE (t: Own) Run;
		VAR c: ActorCore.Context;
	BEGIN
		c := t.c;
		LOOP
			IF pool.Pending(c) THEN
				IF pool.Process(c, throughput) THEN Tasks.Yield END
			ELSIF pool.Stopped(c) THEN EXIT
			ELSE
				t.idle := TRUE; Tasks.ParkExternal; t.idle := FALSE
			END
		END
	END Run;

	PROCEDURE (p: Pool) Schedule (c: ActorCore.Context);
	(* Ящик dedicated: нет задачи, она завершена или это она сама
	   (отменённая задача снимает ящик перед трапом 127) — новая Own. *)
		VAR i: INTEGER; o: Own; h: ANYPTR;
	BEGIN
		IF c.dedicated THEN
			h := p.Host(c);
			IF (h = NIL) OR (h = Tasks.Current()) OR (Tasks.State(h(Own)) IN {Tasks.Finished, Tasks.Cancelled}) THEN
				NEW(o); o.c := c; p.SetHost(c, o); Tasks.Start(o)
			ELSE
				o := h(Own);
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
		FOR i := 0 TO k - 1 DO
			NEW(execs[i]); execs[i].idle := FALSE; execs[i].quit := FALSE; Tasks.Start(execs[i])
		END;
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

	PROCEDURE Wait;
	BEGIN
		IF Tasks.InTask() THEN Tasks.Sleep(1) ELSE Services.actionHook.Step END
	END Wait;

	PROCEDURE Shutdown*;
	(** Общая остановка. Пред: вызов не из задачи исполнителя (трап 20).
		Пост: каждый актор без родителя получил Stop, дети остановлены
		раньше родителей; задачи пула завершились сами, без отмены;
		собственные задачи остановленных акторов завершаются сами; n = 0.
		Актор, который не остановился за 5 с (обработчик ждёт без конца),
		остаётся, задачи пула тогда отменяются (трап 127). Ждёт в задаче
		(Tasks.Sleep) или крутит главный цикл (Services.actionHook.Step). *)
		VAR i: INTEGER; t0: LONGINT; busy: BOOLEAN; cur: Tasks.Task;
	BEGIN
		cur := Tasks.Current();
		ASSERT((cur = NIL) OR ~(cur IS Exec) & ~(cur IS Own), 20);
		IF ActorCore.dispatcher = pool THEN
			pool.StopRoots;
			t0 := Services.Ticks();
			WHILE (pool.Live() > 0) & (Services.Ticks() - t0 < shutdownMs) DO Wait END
		END;
		FOR i := 0 TO n - 1 DO
			execs[i].quit := TRUE;
			IF execs[i].idle THEN execs[i].idle := FALSE; Tasks.Unpark(execs[i]) END
		END;
		t0 := Services.Ticks();
		REPEAT
			busy := FALSE;
			FOR i := 0 TO n - 1 DO
				IF ~(Tasks.State(execs[i]) IN {Tasks.Finished, Tasks.Cancelled}) THEN busy := TRUE END
			END;
			IF busy THEN Wait END
		UNTIL ~busy OR (Services.Ticks() - t0 >= shutdownMs);
		Stop
	END Shutdown;

BEGIN
	NEW(run, 64); NEW(pool)
END ActorRun.
