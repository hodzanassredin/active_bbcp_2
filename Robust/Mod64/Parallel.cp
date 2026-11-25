MODULE RobustParallel;
(** Фасад над AsyncComputon с опубликованным интерфейсом RobustParallel
	И. Денисова (доклад «Блэкбокс для надмолекулярного моделирования»,
	День Оберона 2017, с. 19).

	Definition из доклада:

		DEFINITION RobustParallel; IMPORT Stores; CONST tag = 1;
		TYPE Computon = POINTER TO ABSTRACT RECORD (Stores.Store)
			(c: Computon) Do, NEW, ABSTRACT END;
		VAR rank-, size-: INTEGER;
		PROCEDURE LogMsg (a: ARRAY OF CHAR);
		PROCEDURE Pull (rank, tag: INTEGER): Computon;
		PROCEDURE Push (store: Computon; rank, tag: INTEGER);
		PROCEDURE SetHook (h: Hook);
		PROCEDURE Stop;
		PROCEDURE Try (rank, tag: INTEGER): Computon;
		PROCEDURE Unload (module: ARRAY OF CHAR);
		PROCEDURE Wait;
		END RobustParallel.

	Ранги как в MPI: 0 — управляющий процесс (здесь он же клиент),
	1..size-1 — исполнители, то есть процессы-исполнители
	AsyncComputon. Тип Hook на слайде не приведён; взят
	AsyncComputon.Hook (Failed(rank, tag, err)).

	Инициализации в определении нет: число процессов задаёт внешняя
	переменная ROBUST_SIZE (размер мира MPI, по умолчанию 1 — только
	управляющий). В процессе-исполнителе фасад свой пул не запускает. *)

	IMPORT Dialog, Strings, AsyncComputon;

	CONST
		tag* = 1;

	TYPE
		Computon* = AsyncComputon.Computon;
		Hook* = AsyncComputon.Hook;

	VAR
		rank-, size-: INTEGER;

	PROCEDURE LogMsg* (a: ARRAY OF CHAR);
	BEGIN
		AsyncComputon.LogMsg(a)
	END LogMsg;

	PROCEDURE MapRank (r: INTEGER): INTEGER;
	(** Ранг MPI 1..size-1 → индекс исполнителя 0..size-2. *)
	BEGIN
		ASSERT((r >= 1) & (r < size), 20);
		RETURN r - 1
	END MapRank;

	PROCEDURE Push* (store: Computon; rank, tag: INTEGER);
	(** Отправить компутон исполнителю с рангом MPI rank (1..size-1). *)
	BEGIN
		AsyncComputon.Push(store, MapRank(rank), tag)
	END Push;

	PROCEDURE Pull* (rank, tag: INTEGER): Computon;
	(** Дождаться результата от исполнителя rank; точка ожидания. *)
	BEGIN
		RETURN AsyncComputon.Pull(MapRank(rank), tag)
	END Pull;

	PROCEDURE Try* (rank, tag: INTEGER): Computon;
	(** Не ждать: готовый результат или NIL. *)
		VAR c: Computon; ok: BOOLEAN;
	BEGIN
		ok := AsyncComputon.Try(MapRank(rank), tag, c);
		IF ok THEN RETURN c ELSE RETURN NIL END
	END Try;

	PROCEDURE SetHook* (h: Hook);
	BEGIN
		AsyncComputon.SetHook(h)
	END SetHook;

	PROCEDURE Wait*;
	BEGIN
		AsyncComputon.Wait
	END Wait;

	PROCEDURE Stop*;
	BEGIN
		AsyncComputon.Stop;
		size := 1
	END Stop;

	PROCEDURE Unload* (module: ARRAY OF CHAR);
	(** Выгрузить код модуля у исполнителей. В определении нет
		результата; отказ виден по LastError у AsyncComputon и журналу. *)
		VAR ok: BOOLEAN;
	BEGIN
		ok := AsyncComputon.Unload(module);
		IF ~ok THEN LogMsg("RobustParallel.Unload: executor refused") END
	END Unload;

	PROCEDURE Init;
		VAR val: ARRAY 64 OF CHAR; res, n: INTEGER;
	BEGIN
		rank := 0; size := 1;
		(* Процесс-исполнитель: фасад не запускает вложенный пул. *)
		Dialog.GetPar("ASYNC_IN", val, res);
		IF res = 0 THEN RETURN END;
		Dialog.GetPar("ASYNC_OUT", val, res);
		IF res = 0 THEN RETURN END;
		Dialog.GetPar("ASYNC_TCP", val, res);
		IF res = 0 THEN RETURN END;
		Dialog.GetPar("ROBUST_SIZE", val, res);
		IF res = 0 THEN
			Strings.StringToInt(val, n, res);
			IF (res = 0) & (n > 1) THEN
				AsyncComputon.Start(n - 1);
				size := AsyncComputon.Size() + 1
			END
		END
	END Init;

BEGIN
	Init
CLOSE
	AsyncComputon.Stop
END RobustParallel.
