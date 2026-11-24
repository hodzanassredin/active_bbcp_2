MODULE DemoGroup;
(** Отмена группы задач — аналог context/errgroup в Go.

	Перенесено из статьи Sameer Ajmani "Go Concurrency Patterns:
	Pipelines and Cancellation" (Go Blog, 2014), раздел про отмену
	через context, и из документации x/sync/errgroup: первая ошибка
	отменяет остальных участников, родитель ждёт всех.
	Источники:
	  https://go.dev/blog/pipelines
	  https://pkg.go.dev/golang.org/x/sync/errgroup
	В Go это context.WithCancel + errgroup.Group; здесь — Tasks.Group
	для владения и ожидания и общий флаг stopped в роли ctx.Done().
	Отмена кооперативная: каждый рабочий проверяет флаг в своей точке
	ожидания. Трап в задаче в консольном хосте фатален (нет trapViewer),
	поэтому трап-отмена группы здесь не демонстрируется — её проверяет
	Test64Tasks.

	Все четыре рабочих доходят до барьера Arrive; затем третий ставит
	флаг stopped. Итог детерминирован: ошибка одна, все четверо
	завершились, Completed = 0, в живых 0. *)

	IMPORT ACTIVE, Tasks, DemoSupport;

	CONST
		n = 4;
		badWorker = 3;

	TYPE
		Worker = POINTER TO RECORD (Tasks.Task) id: INTEGER END;
		Parent = POINTER TO RECORD (Tasks.Task) END;

	VAR
		Workers*: ARRAY n OF Worker;
		Entered*: INTEGER;	(* входы в тело до отмены *)
		Cancelled*: INTEGER;	(* вышли по флагу отмены *)
		Completed*: INTEGER;	(* дошли до конца *)
		Errors*: INTEGER;	(* рабочих сообщили об ошибке *)
		Stopped*: BOOLEAN;	(* ctx.Done(): отмена запрошена *)
		LastGroup*: Tasks.Group;
		GroupAlive*: INTEGER;
		Failed*: BOOLEAN;	(* трап в задаче *)
		arrived: INTEGER;	(* барьер под монитором модуля *)

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
			Stopped := TRUE		(* отмена для остальных *)
		END;
		WHILE ~Stopped DO
			INC(Entered); Tasks.Sleep(1)
		END;
		INC(Cancelled)
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
	(** Демонстрация: ошибка одного рабочего отменяет группу. *)
		VAR p: Parent; i: INTEGER; ok: BOOLEAN;
	BEGIN
		Entered := 0; Cancelled := 0; Completed := 0; Errors := 0;
		Stopped := FALSE; arrived := 0;
		LastGroup := NIL; GroupAlive := -1; Failed := FALSE;
		NEW(p); Tasks.Start(p);
		ok := DemoSupport.Pump(10000);
		GroupAlive := Tasks.GroupCount(LastGroup);
		Failed := ~ok;
		FOR i := 0 TO n - 1 DO
			IF Tasks.Failed(Workers[i]) THEN Failed := TRUE END
		END;
		DemoSupport.LogInt("DemoGroup: entered=", Entered);
		DemoSupport.LogInt(" completed=", Completed);
		DemoSupport.LogInt(" cancelled=", Cancelled);
		DemoSupport.LogInt(" errors=", Errors);
		DemoSupport.LogInt(" alive=", GroupAlive);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoGroup.
