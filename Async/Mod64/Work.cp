MODULE AsyncWork;
(** Рабочие нити ОС для блокирующих вызовов: фасад пула.

	Клиентский код (Execute, Submit, ReadFileJob, WriteFileJob,
	ResolveJob) не зависит от платформы. Реализацию ставит делегат:

	- AsyncWorkLin — pthread, pipe готовности и Poller (Linux);
	- AsyncWorkWin — CreateThread, CRITICAL_SECTION, событие и действие
	  Services (Windows x64).

	Делегат ставится лениво при первом Execute или Submit: AsyncWork.Ensure
	зовёт Dialog.Call для AsyncWorkLin.Install, затем AsyncWorkWin.Install.
	Пока делегат не поставлен, Done, Result и Release трапают.

	Форматы запросов и заданий общие, поэтому записи объявлены здесь, а
	не в реализациях: рабочая нить получает указатель на них через
	[ccall]-процедуру и не трогает кучу. *)

	IMPORT S := SYSTEM, Dialog;

	CONST
		maxPath* = 256; maxBuf* = 4096; maxHost* = 128;

	TYPE
		Proc* = PROCEDURE [ccall] (arg: S.ADDRESS);

		FileReq* = RECORD [untagged]
			path*: ARRAY maxPath OF SHORTCHAR;
			buf*: ARRAY maxBuf OF SHORTCHAR;
			len*, status*: INTEGER
		END;

		AddrReq* = RECORD [untagged]
			host*: ARRAY maxHost OF SHORTCHAR;
			count*, status*: INTEGER
		END;

		FileReqPtr* = POINTER [untagged] TO FileReq;
		AddrReqPtr* = POINTER [untagged] TO AddrReq;

		(** Платформенный делегат пула. Методы зовутся с главного стека,
			кроме заданий: их исполняет рабочая нить по адресу
			AsyncWork.ReadFileJob/WriteFileJob/ResolveJob. *)
		Impl* = POINTER TO ABSTRACT RECORD END;

	VAR impl: Impl; callRes: INTEGER;

	PROCEDURE (i: Impl) Submit* (procAdr, arg: S.ADDRESS): INTEGER, NEW, ABSTRACT;
	PROCEDURE (i: Impl) Done* (id: INTEGER): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (i: Impl) Result* (id: INTEGER): INTEGER, NEW, ABSTRACT;
	PROCEDURE (i: Impl) Release* (id: INTEGER), NEW, ABSTRACT;
	PROCEDURE (i: Impl) Execute* (procAdr, arg: S.ADDRESS): INTEGER, NEW, ABSTRACT;
	PROCEDURE (i: Impl) Workers* (): INTEGER, NEW, ABSTRACT;
	PROCEDURE (i: Impl) Scan* (), NEW, ABSTRACT;
	PROCEDURE (i: Impl) ReadFileJob* (arg: S.ADDRESS), NEW, ABSTRACT;
	PROCEDURE (i: Impl) WriteFileJob* (arg: S.ADDRESS), NEW, ABSTRACT;
	PROCEDURE (i: Impl) ResolveJob* (arg: S.ADDRESS), NEW, ABSTRACT;

	PROCEDURE Install* (i: Impl);
	(** Поставить платформенную реализацию. Зовётся делегатом из его
		Install (лениво, при первом обращении к пулу). *)
	BEGIN
		impl := i
	END Install;

	PROCEDURE Ensure;
	(** Ленивый запуск пула: платформенный делегат находит фасад сам.
		Пустой errorMsg — отсутствующий на этой платформе модуль просто
		не загрузится, без сообщения в диалоге. *)
	BEGIN
		IF impl = NIL THEN
			Dialog.Call("AsyncWorkLin.Install", "", callRes);
			IF impl = NIL THEN Dialog.Call("AsyncWorkWin.Install", "", callRes) END;
			ASSERT(impl # NIL, 22)
		END
	END Ensure;

	PROCEDURE Active* (): BOOLEAN;
	BEGIN
		RETURN impl # NIL
	END Active;

	PROCEDURE Submit* (procAdr, arg: S.ADDRESS): INTEGER;
	(** Поставить задание в очередь пула; -1 — нет свободного слота. *)
	BEGIN
		Ensure;
		RETURN impl.Submit(procAdr, arg)
	END Submit;

	PROCEDURE Done* (id: INTEGER): BOOLEAN;
	BEGIN
		ASSERT(impl # NIL, 21);
		RETURN impl.Done(id)
	END Done;

	PROCEDURE Result* (id: INTEGER): INTEGER;
	BEGIN
		ASSERT(impl # NIL, 21);
		RETURN impl.Result(id)
	END Result;

	PROCEDURE Release* (id: INTEGER);
	BEGIN
		ASSERT(impl # NIL, 21);
		impl.Release(id)
	END Release;

	PROCEDURE Execute* (procAdr, arg: S.ADDRESS): INTEGER;
	(** Исполнить задание в рабочей нити и дождаться; только из задачи.
		Первый вызов лениво заводит пул. *)
	BEGIN
		Ensure;
		RETURN impl.Execute(procAdr, arg)
	END Execute;

	PROCEDURE Workers* (): INTEGER;
	BEGIN
		IF impl = NIL THEN RETURN 0 ELSE RETURN impl.Workers() END
	END Workers;

	PROCEDURE Scan*;
	(** Разобрать завершённые задания. Зовёт главный цикл платформы. *)
	BEGIN
		IF impl # NIL THEN impl.Scan END
	END Scan;

	(* --- прикладные задания (исполняются в рабочих нитях) ----------------- *)

	PROCEDURE [ccall] ReadFileJob* (arg: S.ADDRESS);
	BEGIN
		ASSERT(impl # NIL, 21);
		impl.ReadFileJob(arg)
	END ReadFileJob;

	PROCEDURE [ccall] WriteFileJob* (arg: S.ADDRESS);
	BEGIN
		ASSERT(impl # NIL, 21);
		impl.WriteFileJob(arg)
	END WriteFileJob;

	PROCEDURE [ccall] ResolveJob* (arg: S.ADDRESS);
	BEGIN
		ASSERT(impl # NIL, 21);
		impl.ResolveJob(arg)
	END ResolveJob;

END AsyncWork.
