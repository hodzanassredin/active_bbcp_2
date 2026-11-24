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

	Все четыре рабочих доходят до барьера Arrive; затем третий трапает.
	Трап ловит Kernel.Try в TaskBoot, поэтому зритель трапов не нужен.
	Итог детерминирован: Entered = 8, Completed = 0, группа failed,
	один рабочий трапнул, трое отменены, в живых 0. *)

	IMPORT ACTIVE, Tasks, DemoSupport;

	CONST
		n = 4;
		badWorker = 3;

	TYPE
		Worker = POINTER TO RECORD (Tasks.Task) id: INTEGER END;
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
		arrived: INTEGER;	(* барьер под монитором модуля *)

	PROCEDURE NilCrash;
		VAR p: Worker;
	BEGIN
		p := NIL; p.id := 1
	END NilCrash;

	PROCEDURE [exclusive] Arrive*;
	(** Барьер: все n рабочих должны дойти до этой точки. *)
	BEGIN
		INC(arrived);
		ACTIVE.AWAIT(arrived >= n)
	END Arrive;

	PROCEDURE (w: Worker) Run;
	BEGIN
		INC(Entered); Tasks.Sleep(1);	(* первый круг *)
		INC(Entered); Arrive;		(* все четверо во втором круге *)
		IF w.id = badWorker THEN
			INC(Errors);
			NilCrash		(* ошибка: группа failed, остальных отменят *)
		END;
		Tasks.Sleep(1);			(* сюда отмена будит остальных *)
		Tasks.Sleep(1);
		INC(Completed)
	END Run;

	PROCEDURE (p: Parent) Run;
		VAR g: Tasks.Group; i: INTEGER;
	BEGIN
		g := Tasks.NewGroup();
		LastGroup := g;
		FOR i := 0 TO n - 1 DO
			NEW(Workers[i]); Workers[i].id := i + 1;
			Tasks.StartIn(Workers[i], g)
		END
		(* Run возвращается; TaskBoot родителя ждёт опустошения группы *)
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: трап одного участника отменяет группу. *)
		VAR p: Parent; i, st: INTEGER; ok: BOOLEAN;
	BEGIN
		Entered := 0; Cancelled := 0; Completed := 0; Errors := 0;
		FailedWorker := 0; arrived := 0;
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
			ELSIF st = Tasks.Finished THEN
				INC(Completed)
			END
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
