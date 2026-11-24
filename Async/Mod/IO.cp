MODULE AsyncIO;
(** Ввод-вывод задач: «прочитать или дождаться», «записать или
	дождаться» поверх CommStreams.

	Поток CommStreams неблокирующий: ReadBytes/WriteBytes возвращают 0,
	если данных/места нет. Задача регистрирует дескриптор в Poller и
	Park; главный цикл (LinConsole.WaitInput) опрашивает stdin и
	зарегистрированные дескрипторы одним poll и будит задачу. Процессор
	в покое не занят: poll спит до события или тайм-аута цикла.

	Поток без дескриптора (Handle = -1) обслуживается повторными
	попытками с Tasks.Sleep(1) — медленно, но не блокирует цикл. *)

	IMPORT S := SYSTEM, Kernel, Tasks, Poller, CommStreams;

	TYPE
		Waiter = POINTER TO RECORD (Poller.Waiter)
			task: Tasks.Task
		END;

	PROCEDURE (w: Waiter) Ready (events: SET);
	BEGIN
		Tasks.Unpark(w.task)
	END Ready;

	PROCEDURE WaitFd* (fd: INTEGER; events: SET);
	(** Дождаться готовности дескриптора (одноразовая регистрация). *)
		VAR w: Waiter;
	BEGIN
		IF ~Tasks.InTask() THEN ASSERT(FALSE, 20) END;
		IF fd < 0 THEN Tasks.Sleep(1); RETURN END;
		NEW(w); w.task := Tasks.Current();
		Poller.Add(fd, events, w);
		Tasks.Park;
		Poller.Remove(w)
	END WaitFd;

	PROCEDURE Poll* (timeout: LONGINT);
	(** Прогнать ожидания (для тестового насоса); в консоли это делает
		Console.WaitInput. *)
	BEGIN
		IF Poller.Wait(timeout) THEN END
	END Poll;

	PROCEDURE Read* (s: CommStreams.Stream; VAR x: ARRAY OF BYTE; beg, len: INTEGER; OUT read: INTEGER);
	(** Читать хотя бы байт; 0 — конец/обрыв. *)
		VAR fd: INTEGER;
	BEGIN
		LOOP
			s.ReadBytes(x, beg, len, read);
			IF read > 0 THEN RETURN END;
			IF ~s.IsConnected() THEN RETURN END;
			fd := s.Handle();
			IF fd < 0 THEN Tasks.Sleep(1) ELSE WaitFd(fd, Poller.In) END
		END
	END Read;

	PROCEDURE Write* (s: CommStreams.Stream; IN x: ARRAY OF BYTE; beg, len: INTEGER; OUT written: INTEGER);
	(** Записать хотя бы байт; 0 — обрыв. *)
		VAR fd: INTEGER;
	BEGIN
		LOOP
			s.WriteBytes(x, beg, len, written);
			IF written > 0 THEN RETURN END;
			IF ~s.IsConnected() THEN RETURN END;
			fd := s.Handle();
			IF fd < 0 THEN Tasks.Sleep(1) ELSE WaitFd(fd, Poller.Out) END
		END
	END Write;

	PROCEDURE Accept* (l: CommStreams.Listener; OUT s: CommStreams.Stream; timeout: LONGINT): BOOLEAN;
	(** Принять соединение; timeout < 0 — ждать бесконечно. *)
		VAR t0: LONGINT; fd: INTEGER;
	BEGIN
		s := NIL; t0 := Kernel.Time();
		LOOP
			l.Accept(s);
			IF s # NIL THEN RETURN TRUE END;
			IF (timeout >= 0) & (Kernel.Time() - t0 >= timeout) THEN RETURN FALSE END;
			fd := l.Handle();
			IF fd < 0 THEN Tasks.Sleep(1) ELSE WaitFd(fd, Poller.In) END
		END
	END Accept;

END AsyncIO.
