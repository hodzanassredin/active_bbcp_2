MODULE ActorRun;
(** Исполнители ящиков: пул задач и собственные задачи акторов.

	Пул — n задач, общая очередь ящиков с работой (кольцо run). Свободный
	исполнитель ждёт в Tasks.ParkExternal: письмо может прийти с главного
	стека, поэтому, пока пул жив, планировщик не объявляет тупик. Schedule
	будит исполнителя Unpark. Будить
	можно только задачу в этом ожидании: задачу, ждущую внутри Receive
	(монитор, канал), Unpark сбил бы с ожидания. Поэтому свободный
	исполнитель стоит в списке idle ровно на время ожидания.

	Ожидание в обработчике занимает исполнителя. Чтобы готовые ящики не
	ждали, пул растёт: Schedule, положив ящик в run, берёт исполнителя из
	idle, иначе из sleep, иначе назначает проверку роста через growMs.
	Проверка видит, что все исполнители ушли в ожидание внутри
	обработчиков (нет idle и sleep, задача каждого не Ready и не Running),
	и запускает исполнителей по числу готовых ящиков, пока n < limit. Если
	же кто-то работает, он сам возьмёт ящик, и проверка откладывается.
	Предел достигнут — ящик ждёт, limitHits считает случаи.
	Исполнитель сверх начального числа base не ждёт в idle, а спит
	shrinkMs в списке sleep; проснувшись по сроку без работы, он
	завершается. Списки врезные, взять и вернуть — O(1), на письмо нет ни
	обхода, ни выделения.

	Ящик с флагом dedicated обслуживает своя задача Own; она живёт, пока
	актор не остановлен. Отменённую Own заменяет новая при следующей
	работе ящика: отмена задачи актора — не отказ и не остановка.

	Shutdown останавливает все акторы и завершает задачи пула без отмены.

	Инвариант: каждый ящик в run стоит у исполнителя (Pending) и лежит в
	run не более одного раза; исполнитель в idle или sleep ждёт только в
	своём цикле; execs[0..n-1] без пропусков. *)

	IMPORT Services, Tasks, ActorCore;

	CONST
		throughput = 100;	(* писем одного ящика за вызов Process *)
		yieldEvery = 64;	(* ящиков между уступками *)
		defaultLimit = 1024;	(* стек задачи 256 КиБ => 256 МиБ адресов *)
		shrinkMs = 1000;	(* простой лишнего исполнителя до ухода *)
		growMs = 1;	(* задержка проверки роста: обработчик успевает уйти в ожидание *)
		shutdownMs = 5000;	(* предел ожидания остановки акторов *)

	TYPE
		Pool = POINTER TO RECORD (ActorCore.Dispatcher) END;
		GrowCheck = POINTER TO RECORD (Services.Action) END;
		Exec = POINTER TO RECORD (Tasks.Task)
			quit, extra: BOOLEAN;	(* extra — сверх числа из Start *)
			inIdle, inSleep: BOOLEAN;
			idlePrev, idleNext: Exec;
			sleepPrev, sleepNext: Exec;
			slot: INTEGER	(* место в execs; -1 — завершён *)
		END;
		Own = POINTER TO RECORD (Tasks.Task) c: ActorCore.Context; idle: BOOLEAN END;

	VAR
		execs: POINTER TO ARRAY OF Exec;	(* запас растёт вместе с пулом *)
		n-: INTEGER;	(** число исполнителей пула; 0 — пул остановлен *)
		base: INTEGER;	(* начальное число из Start *)
		limit-: INTEGER;	(** предел числа исполнителей *)
		limitHits-: LONGINT;	(** раз готовый ящик ждал из-за предела (с Start) *)
		grown-: LONGINT;	(** создано исполнителей сверх начального числа (с Start) *)
		shrunk-: LONGINT;	(** завершено лишних исполнителей (с Start) *)
		idleHead: Exec; idleCount: INTEGER;	(* свободные, ждут в ParkExternal *)
		sleepHead: Exec; sleepCount: INTEGER;	(* лишние, спят до ухода *)
		growCheck: GrowCheck; armed, stopping: BOOLEAN;	(* отложенная проверка роста *)
		run: POINTER TO ARRAY OF ActorCore.Context;
		head, count: INTEGER;
		pool: Pool;

	(* --- списки свободных и спящих --------------------------------------- *)

	PROCEDURE PushIdle (e: Exec);
	BEGIN
		e.idlePrev := NIL; e.idleNext := idleHead;
		IF idleHead # NIL THEN idleHead.idlePrev := e END;
		idleHead := e; e.inIdle := TRUE; INC(idleCount)
	END PushIdle;

	PROCEDURE RemoveIdle (e: Exec);
	BEGIN
		IF e.inIdle THEN
			IF e.idlePrev # NIL THEN e.idlePrev.idleNext := e.idleNext ELSE idleHead := e.idleNext END;
			IF e.idleNext # NIL THEN e.idleNext.idlePrev := e.idlePrev END;
			e.idlePrev := NIL; e.idleNext := NIL; e.inIdle := FALSE; DEC(idleCount)
		END
	END RemoveIdle;

	PROCEDURE TakeIdle (): Exec;
		VAR e: Exec;
	BEGIN
		e := idleHead;
		IF e # NIL THEN RemoveIdle(e) END;
		RETURN e
	END TakeIdle;

	PROCEDURE PushSleep (e: Exec);
	BEGIN
		e.sleepPrev := NIL; e.sleepNext := sleepHead;
		IF sleepHead # NIL THEN sleepHead.sleepPrev := e END;
		sleepHead := e; e.inSleep := TRUE; INC(sleepCount)
	END PushSleep;

	PROCEDURE RemoveSleep (e: Exec);
	BEGIN
		IF e.inSleep THEN
			IF e.sleepPrev # NIL THEN e.sleepPrev.sleepNext := e.sleepNext ELSE sleepHead := e.sleepNext END;
			IF e.sleepNext # NIL THEN e.sleepNext.sleepPrev := e.sleepPrev END;
			e.sleepPrev := NIL; e.sleepNext := NIL; e.inSleep := FALSE; DEC(sleepCount)
		END
	END RemoveSleep;

	PROCEDURE TakeSleep (): Exec;
		VAR e: Exec;
	BEGIN
		e := sleepHead;
		IF e # NIL THEN RemoveSleep(e) END;
		RETURN e
	END TakeSleep;

	(* --- очередь ящиков --------------------------------------------------- *)

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

	(* --- состав пула ------------------------------------------------------ *)

	PROCEDURE EnsureCap (need: INTEGER);
	(* Пост: в execs места не меньше need; живые исполнители сохранены. *)
		VAR a: POINTER TO ARRAY OF Exec; len, i: INTEGER;
	BEGIN
		IF execs = NIL THEN len := 0 ELSE len := LEN(execs^) END;
		IF need > len THEN
			IF len = 0 THEN len := 8 END;
			WHILE len < need DO len := 2 * len END;
			NEW(a, len);
			FOR i := 0 TO n - 1 DO a[i] := execs[i] END;
			execs := a
		END
	END EnsureCap;

	PROCEDURE Grow;
	(* Пост: n + 1 исполнитель, если есть место; иначе limitHits + 1. *)
		VAR e: Exec;
	BEGIN
		IF n >= limit THEN INC(limitHits); RETURN END;
		EnsureCap(n + 1);
		NEW(e); e.quit := FALSE; e.extra := TRUE;
		e.inIdle := FALSE; e.inSleep := FALSE;
		e.idlePrev := NIL; e.idleNext := NIL; e.sleepPrev := NIL; e.sleepNext := NIL;
		e.slot := n; execs[n] := e; INC(n); INC(grown);
		Tasks.Start(e)
	END Grow;

	PROCEDURE Retire (e: Exec);
	(* Пост: e завершён и убран из execs; последний исполнитель занял его место. *)
		VAR i: INTEGER;
	BEGIN
		i := e.slot;
		IF (i >= 0) & (i < n) & (execs[i] = e) THEN
			DEC(n);
			IF i # n THEN execs[i] := execs[n]; execs[i].slot := i END;
			execs[n] := NIL
		END;
		e.slot := -1; e.extra := FALSE; INC(shrunk)
	END Retire;

	PROCEDURE Arm;
	(* Пост: проверка роста назначена на growMs вперёд (не больше одной). *)
	BEGIN
		IF ~armed THEN
			armed := TRUE;
			IF growCheck = NIL THEN NEW(growCheck) END;
			Services.DoLater(growCheck, Services.Ticks() + growMs)
		END
	END Arm;

	PROCEDURE (g: GrowCheck) Do;
	(* Готовые ящики ждут, только если все исполнители ушли в ожидание
	   внутри обработчиков: свободных и спящих нет, а состояние каждой
	   задачи — Sleeping, Waiting или Parked. Работающий или готовый
	   исполнитель сам возьмёт ящик, поэтому рост откладывается. *)
		VAR i, k, add: INTEGER; waiting: BOOLEAN;
	BEGIN
		armed := FALSE;
		IF stopping OR (n = 0) OR (count = 0) THEN RETURN END;
		IF (idleCount > 0) OR (sleepCount > 0) THEN RETURN END;
		waiting := TRUE; i := 0;
		WHILE (i < n) & waiting DO
			IF Tasks.State(execs[i]) IN {Tasks.New, Tasks.Ready, Tasks.Running} THEN waiting := FALSE END;
			INC(i)
		END;
		IF ~waiting THEN Arm; RETURN END;
		IF n >= limit THEN INC(limitHits); RETURN END;
		add := count;
		IF add > limit - n THEN add := limit - n END;
		k := 0;
		WHILE k < add DO Grow; INC(k) END;
		IF count > 0 THEN
			IF n < limit THEN Arm ELSE INC(limitHits) END
		END
	END Do;

	(* --- задачи ----------------------------------------------------------- *)

	PROCEDURE (e: Exec) Run;
		VAR c: ActorCore.Context; k: INTEGER; t0: LONGINT;
	BEGIN
		k := 0;
		LOOP
			IF count > 0 THEN
				c := PopRun();
				IF pool.Process(c, throughput) THEN PushRun(c) END;
				INC(k);
				IF k MOD yieldEvery = 0 THEN Tasks.Yield END
			ELSIF e.quit THEN
				RemoveIdle(e); RemoveSleep(e); EXIT
			ELSIF e.extra THEN
				t0 := Services.Ticks();
				PushSleep(e); Tasks.Sleep(shrinkMs); RemoveSleep(e);
				IF (count = 0) & (Services.Ticks() - t0 >= shrinkMs) & ~e.quit & (n > base) THEN
					Retire(e); EXIT
				END
			ELSE
				IF ~e.inIdle THEN PushIdle(e) END;
				Tasks.ParkExternal; RemoveIdle(e)
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
		VAR o: Own; h: ANYPTR; e: Exec;
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
			IF n > 0 THEN	(* пул остановлен — ящик ждёт следующего Start *)
				e := TakeIdle();
				IF e = NIL THEN e := TakeSleep() END;
				IF e # NIL THEN Tasks.Unpark(e)
				ELSIF n < limit THEN Arm
				ELSE INC(limitHits)
				END
			END
		END
	END Schedule;

	PROCEDURE Start* (k: INTEGER);
	(** Пред: 0 < k <= limit (трап 20); пул остановлен (21).
		Пост: пул из k исполнителей установлен в ActorCore; ящики, ждавшие
		в очереди, обслуживаются; счётчики роста обнулены. *)
		VAR i: INTEGER;
	BEGIN
		ASSERT((k > 0) & (k <= limit), 20); ASSERT(n = 0, 21);
		EnsureCap(k);
		idleHead := NIL; idleCount := 0; sleepHead := NIL; sleepCount := 0;
		armed := FALSE; stopping := FALSE;
		limitHits := 0; grown := 0; shrunk := 0;
		FOR i := 0 TO k - 1 DO
			NEW(execs[i]); execs[i].quit := FALSE; execs[i].extra := FALSE;
			execs[i].inIdle := FALSE; execs[i].inSleep := FALSE;
			execs[i].idlePrev := NIL; execs[i].idleNext := NIL;
			execs[i].sleepPrev := NIL; execs[i].sleepNext := NIL;
			execs[i].slot := i;
			Tasks.Start(execs[i])
		END;
		n := k; base := k;
		ActorCore.SetDispatcher(pool)
	END Start;

	PROCEDURE SetLimit* (k: INTEGER);
	(** Пред: k > 0 (трап 20). Пост: предел числа исполнителей k; выше
		k пул не растёт, лишние уходят при простое. *)
	BEGIN
		ASSERT(k > 0, 20);
		limit := k
	END SetLimit;

	PROCEDURE Stop*;
	(** Пост: задачи пула отменены (трап 127 в их точке переключения);
		ящики с работой ждут следующего Start. Собственные задачи акторов
		не затронуты. *)
		VAR i: INTEGER;
	BEGIN
		idleHead := NIL; idleCount := 0; sleepHead := NIL; sleepCount := 0;
		armed := FALSE; stopping := FALSE;
		FOR i := 0 TO n - 1 DO
			execs[i].quit := TRUE;
			execs[i].inIdle := FALSE; execs[i].inSleep := FALSE;
			Tasks.Cancel(execs[i]); execs[i] := NIL
		END;
		n := 0; base := 0
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
		stopping := TRUE;
		IF ActorCore.dispatcher = pool THEN
			pool.StopRoots;
			t0 := Services.Ticks();
			WHILE (pool.Live() > 0) & (Services.Ticks() - t0 < shutdownMs) DO Wait END
		END;
		FOR i := 0 TO n - 1 DO
			execs[i].quit := TRUE;
			IF execs[i].inIdle OR execs[i].inSleep THEN Tasks.Unpark(execs[i]) END
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
	limit := defaultLimit;
	NEW(run, 64); NEW(pool)
END ActorRun.
