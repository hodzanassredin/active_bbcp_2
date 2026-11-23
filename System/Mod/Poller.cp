MODULE Poller;
(** Реестр готовности файловых дескрипторов (этап 7).

	Задачи ждут готовности сокета/файла, а ждёт их главный цикл: платформа
	(LinConsole) перекрывает хук и опрашивает stdin вместе с
	зарегистрированными дескрипторами. Регистрация — одноразовая: когда
	дескриптор готов, хук снимает запись и зовёт Waiter.Ready; ожидающий
	снова регистрируется, если готовности не хватило.

	Без платформенного хука Wait ведёт себя как прежний Console.WaitInput:
	сразу возвращает TRUE, цикл не ждёт. *)

	CONST
		(** события, как у poll(2) **)
		In* = {1};
		Out* = {2};
		Err* = {3};
		Hup* = {4};

	TYPE
		Waiter* = POINTER TO ABSTRACT RECORD END;
		Hook* = POINTER TO ABSTRACT RECORD END;

	VAR
		hook: Hook;

	(* абстрактные методы *)

	PROCEDURE (w: Waiter) Ready* (events: SET), NEW, ABSTRACT;
	(** Вызывается на главном стеке, когда дескриптор готов. *)

	PROCEDURE (h: Hook) Add* (fd: INTEGER; events: SET; w: Waiter), NEW, ABSTRACT;

	PROCEDURE (h: Hook) Remove* (w: Waiter), NEW, ABSTRACT;

	PROCEDURE (h: Hook) Wait* (timeout: LONGINT; console: BOOLEAN): BOOLEAN, NEW, ABSTRACT;
	(** Ждать не дольше timeout мс. console = TRUE — учитывать stdin
		консоли и вернуть TRUE, если готов он. *)

	PROCEDURE SetHook* (h: Hook);
	BEGIN
		hook := h
	END SetHook;

	PROCEDURE Add* (fd: INTEGER; events: SET; w: Waiter);
	BEGIN
		IF hook # NIL THEN hook.Add(fd, events, w) END
	END Add;

	PROCEDURE Remove* (w: Waiter);
	BEGIN
		IF hook # NIL THEN hook.Remove(w) END
	END Remove;

	PROCEDURE Wait* (timeout: LONGINT): BOOLEAN;
	(** Ждать готовности зарегистрированных дескрипторов (без stdin). *)
	BEGIN
		IF hook # NIL THEN RETURN hook.Wait(timeout, FALSE) ELSE RETURN TRUE END
	END Wait;

	PROCEDURE WaitConsole* (timeout: LONGINT): BOOLEAN;
	(** Ждать stdin консоли или зарегистрированных дескрипторов. *)
	BEGIN
		IF hook # NIL THEN RETURN hook.Wait(timeout, TRUE) ELSE RETURN TRUE END
	END WaitConsole;

END Poller.
