MODULE DemoGroup;
(** Отмена группы задач — аналог context/errgroup в Go.

	Перенесено из статьи Sameer Ajmani "Go Concurrency Patterns:
	Pipelines and Cancellation" (Go Blog, 2014), раздел про отмену
	через context, и из документации x/sync/errgroup: первая ошибка
	отменяет остальных участников, родитель ждёт всех.
	Источники:
	  https://go.dev/blog/pipelines
	  https://pkg.go.dev/golang.org/x/sync/errgroup
	В Go это context.WithCancel + errgroup.Group; здесь — Tasks.Group:
	StartIn регистрирует участника, трап помечает группу failed и
	CancelGroup отменяет остальных, родитель (владелец группы) ждёт
	опустошения в TaskBoot.

	Барьер — монитор-объект Barrier: рабочий увеличивает счётчик и ждёт
	флага «пришли все». Все четыре рабочих доходят до барьера Arrive;
	затем третий трапает. Трап ловит Kernel.Try в TaskBoot, поэтому
	зритель трапов не нужен. Итог детерминирован: Entered = 8,
	Completed = 0, группа failed, один рабочий трапнул, трое отменены,
	в живых 0. *)

	IMPORT Tasks, DemoSupport;

	CONST
		n = 4;
		badWorker = 3;
		allArrived = 0;		(* флаг Ready: барьер собран *)

	TYPE
		Barrier* = POINTER TO RECORD (Tasks.Object)
			arrived: INTEGER
		END;
		ArriveOp = RECORD (Tasks.Op) END;

		Worker = POINTER TO RECORD (Tasks.Job) barrier: Barrier; id: INTEGER END;
		(* Родитель — задача без пропуска: пример показывает ожидание
		   опустошения группы в TaskBoot. *)
		Parent = POINTER TO RECORD (Tasks.Task) END;

	VAR
		Workers*: ARRAY n OF Worker;
		Entered*: INTEGER;	(* входы в тело до барьера *)
		Cancelled*: INTEGER;	(* отменены группой *)
		Completed*: INTEGER;	(* дошли до конца *)
		Errors*: INTEGER;	(* рабочих сообщили об ошибке (трап) *)
		FailedWorker*: INTEGER;	(* трапнувших *)
		LastGroup*: Tasks.Group;
		GroupIsFailed*: BOOLEAN;	(* Tasks.GroupFailed *)
		GroupAlive*: INTEGER;	(* Tasks.GroupCount *)
		Failed*: BOOLEAN;	(* неожиданный сбой прогона *)

	PROCEDURE NilCrash;
		VAR p: Worker;
	BEGIN
		p := NIL; p.id := 1
	END NilCrash;

	PROCEDURE (b: Barrier) Ready* (): SET;
	BEGIN
		IF b.arrived >= n THEN RETURN {allArrived} ELSE RETURN {} END
	END Ready;

	PROCEDURE (b: Barrier) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: ArriveOp DO
			INC(b.arrived);
			IF ~b.Await(ctx, {allArrived}) THEN RETURN END
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE Arrive* (b: Barrier; VAR ctx: Tasks.Ctx);
	(** Барьер: все n рабочих должны дойти до этой точки. *)
		VAR op: ArriveOp;
	BEGIN
		b.Do(ctx, op)
	END Arrive;

	PROCEDURE (w: Worker) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		INC(Entered); Tasks.Sleep(1);	(* первый круг *)
		INC(Entered); Arrive(w.barrier, ctx);	(* все четверо во втором круге *)
		IF w.id = badWorker THEN
			INC(Errors);
			NilCrash		(* ошибка: группа failed, остальных отменят *)
		END;
		Tasks.Sleep(1);			(* сюда отмена будит остальных *)
		Tasks.Sleep(1);
		INC(Completed)
	END Body;

	PROCEDURE (p: Parent) Run;
		VAR g: Tasks.Group; b: Barrier; i: INTEGER;
	BEGIN
		g := Tasks.NewGroup();
		LastGroup := g;
		NEW(b);
		FOR i := 0 TO n - 1 DO
			NEW(Workers[i]); Workers[i].barrier := b; Workers[i].id := i + 1;
			Tasks.StartIn(Workers[i], g)
		END
		(* Run возвращается; TaskBoot родителя ждёт опустошения группы *)
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: трап одного участника отменяет группу. *)
		VAR p: Parent; i, st: INTEGER; ok: BOOLEAN;
	BEGIN
		Entered := 0; Cancelled := 0; Completed := 0; Errors := 0;
		FailedWorker := 0;
		LastGroup := NIL; GroupIsFailed := FALSE; GroupAlive := -1;
		Failed := FALSE;

		NEW(p); Tasks.Start(p);
		ok := DemoSupport.Pump(10000);

		GroupIsFailed := Tasks.GroupFailed(LastGroup);
		GroupAlive := Tasks.GroupCount(LastGroup);
		FOR i := 0 TO n - 1 DO
			st := Tasks.State(Workers[i]);
			IF st = Tasks.Cancelled THEN
				INC(Cancelled)
			ELSIF Tasks.Failed(Workers[i]) THEN
				INC(FailedWorker)
			END	(* дошедших до конца считает сам рабочий (Completed) *)
		END;
		Failed := ~ok OR ~GroupIsFailed OR (FailedWorker # 1) OR (GroupAlive # 0);
		DemoSupport.LogInt("DemoGroup: entered=", Entered);
		DemoSupport.LogInt(" completed=", Completed);
		DemoSupport.LogInt(" cancelled=", Cancelled);
		DemoSupport.LogInt(" failedWorker=", FailedWorker);
		IF GroupIsFailed THEN
			DemoSupport.LogInt(" groupFailed=", 1)
		ELSE
			DemoSupport.LogInt(" groupFailed=", 0)
		END;
		DemoSupport.LogInt(" alive=", GroupAlive);
		IF Failed THEN DemoSupport.Log(" UNEXPECTED") END;
		DemoSupport.LogLn
	END Run;

END DemoGroup.
