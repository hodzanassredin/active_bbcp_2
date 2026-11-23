MODULE LinConsole;

	(*
		A. V. Shiryaev, 2012.10, 2016.11
		Console implementation for Linux, OpenBSD, FreeBSD and NetBSD
		I. A. Denisov, 2020.01
		Change iconv to Strings UTF-8
	*)

	IMPORT S := SYSTEM, Console, Utf, Poller, Libc := LinLibc;

	CONST
		maxWaiters = 4096;
		(* struct pollfd *)
		pollIn = 1; pollOut = 4; pollErr = 8; pollHup = 16;

	TYPE
		Cons = POINTER TO RECORD (Console.Console) END;

		Entry = RECORD
			fd: INTEGER;
			events: SET;
			w: Poller.Waiter;
			used: BOOLEAN
		END;

		PollHook = POINTER TO RECORD (Poller.Hook) END;

	VAR
		cons: Cons;
		input: Libc.PtrFILE;
		entries: ARRAY maxWaiters OF Entry;
		fds: ARRAY maxWaiters + 1 OF Libc.pollfd;
		stdinFd: INTEGER;

	(* --- хук Poller ------------------------------------------------------- *)

	PROCEDURE MapEvents (revents: INTEGER): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF ODD(revents DIV pollIn) THEN INCL(s, 1) END;
		IF ODD(revents DIV pollOut) THEN INCL(s, 2) END;
		IF ODD(revents DIV pollErr) THEN INCL(s, 3) END;
		IF ODD(revents DIV pollHup) THEN INCL(s, 4) END;
		RETURN s
	END MapEvents;

	PROCEDURE (h: PollHook) Add (fd: INTEGER; events: SET; w: Poller.Waiter);
		VAR i: INTEGER; found: BOOLEAN;
	BEGIN
		i := 0; found := FALSE;
		WHILE (i < maxWaiters) & ~found DO
			IF ~entries[i].used THEN
				entries[i].fd := fd; entries[i].events := events;
				entries[i].w := w; entries[i].used := TRUE; found := TRUE
			END;
			INC(i)
		END;
		IF ~found THEN
			(* переполнение: разбудить сразу, ожидающий повторит попытку *)
			w.Ready(events)
		END
	END Add;

	PROCEDURE (h: PollHook) Remove (w: Poller.Waiter);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO maxWaiters - 1 DO
			IF entries[i].used & (entries[i].w = w) THEN entries[i].used := FALSE END
		END
	END Remove;

	PROCEDURE (h: PollHook) Wait (timeout: LONGINT; console: BOOLEAN): BOOLEAN;
		(* Снять готовые записи до вызова Ready: он может зарегистрироваться
		   снова и переписать таблицу. *)
		VAR i, n, res, nidx: INTEGER; stdinReady: BOOLEAN;
			idx: ARRAY maxWaiters OF INTEGER; ev: ARRAY maxWaiters OF SET;
	BEGIN
		n := 0; stdinReady := FALSE;
		IF console THEN
			fds[0].fd := stdinFd; fds[0].events := SHORT(pollIn); fds[0].revents := 0;
			n := 1
		END;
		nidx := 0;
		FOR i := 0 TO maxWaiters - 1 DO
			IF entries[i].used THEN
				fds[n].fd := entries[i].fd;
				fds[n].events := 0;
				IF 1 IN entries[i].events THEN fds[n].events := SHORT(pollIn) END;
				IF 2 IN entries[i].events THEN fds[n].events := SHORT(fds[n].events + pollOut) END;
				fds[n].revents := 0;
				idx[nidx] := i; INC(nidx);
				INC(n)
			END
		END;
		res := Libc.poll(S.ADR(fds[0]), n, SHORT(timeout));
		IF res > 0 THEN
			(* прежнее поведение: любой событие stdin (в т.ч. HUP) — это ввод *)
			IF console & (fds[0].revents # 0) THEN stdinReady := TRUE END;
			FOR i := 0 TO nidx - 1 DO
				ev[i] := MapEvents(fds[i + n - nidx].revents)
			END;
			FOR i := 0 TO nidx - 1 DO
				IF ev[i] # {} THEN
					entries[idx[i]].used := FALSE
				END
			END;
			FOR i := 0 TO nidx - 1 DO
				IF ev[i] # {} THEN
					entries[idx[i]].w.Ready(ev[i])
				END
			END
		END;
		RETURN stdinReady
	END Wait;

	PROCEDURE (cons: Cons) ReadLn (OUT s: ARRAY OF CHAR);
		CONST
			maxLineLen = 2048 - 1; (* without null terminating shortchar *)
		VAR
			str: Libc.PtrSTR;
			ss: ARRAY maxLineLen + 1 OF SHORTCHAR;
			res: INTEGER;
	BEGIN
		ss[LEN(ss)-1] := 0X;
		str := Libc.fgets(ss, LEN(ss), input);
		IF (str # NIL) & (ss[LEN(ss)-1] = 0X) THEN
			ASSERT(LEN(ss$) < LEN(s), 20);
			Utf.Utf8ToString(ss, s, res);
			ASSERT(res = 0, 60)
		ELSE s[0] := 0X END
	END ReadLn;

	PROCEDURE (cons: Cons) WaitInput (timeout: LONGINT): BOOLEAN;
		(* Есть ли ввод? poll на stdin даёт циклу возможность исполнять
		   действия, пока пользователь не ввёл строку. stdin переведён в
		   небуферизованный режим (setvbuf), иначе stdio прочитал бы
		   несколько строк вперёд и poll врал бы. В тот же poll попадают
		   дескрипторы, зарегистрированные через Poller (этап 7). *)
	BEGIN
		RETURN Poller.WaitConsole(timeout)
	END WaitInput;

	PROCEDURE Printf (IN s: ARRAY OF CHAR; len: INTEGER);
		VAR res, i, n: INTEGER; tmp: ARRAY 64 OF CHAR;
			ss: ARRAY 128 OF SHORTCHAR;
	BEGIN
		i := 0;
		WHILE i < len DO
			n := 0;
			WHILE (i < len) & (n < LEN(tmp) - 1) DO
				tmp[n] := s[i];
				INC(n); INC(i)
			END;
			tmp[n] := 0X;
			Utf.StringToUtf8(tmp, ss, res);
			IF res # 0 THEN
				(* fallback ascii conversion *)
				ss := SHORT(tmp)
			END;
			res := Libc.printfs("%s", ss);
			res := Libc.fflush(Libc.NULL)
		END
	END Printf;

	PROCEDURE (cons: Cons) WriteChar (c: CHAR);
		VAR s: ARRAY 1 OF CHAR;
	BEGIN
		s[0] := c;
		Printf(s, 1)
	END WriteChar;

	PROCEDURE (cons: Cons) WriteStr (IN text: ARRAY OF CHAR);
	BEGIN
		Printf(text, LEN(text$))
	END WriteStr;

	PROCEDURE (cons: Cons) WriteLn;
	BEGIN
		Printf(0AX, 1)
	END WriteLn;

	PROCEDURE Init;
		VAR hook: PollHook; i: INTEGER;
	BEGIN
		input := Libc.fdopen(Libc.STDIN_FILENO, "rb");
		ASSERT(input # Libc.NULL, 100);
		(* без буфера: poll(fd) должен видеть ровно то, что ещё не прочитано *)
		IF Libc.setvbuf(input, 0, 2, 0) # 0 THEN	(* _IONBF = 2 *)
			(* не фатально: цикл просто будет реже исполнять действия *)
		END;
		FOR i := 0 TO maxWaiters - 1 DO entries[i].used := FALSE END;
		stdinFd := Libc.fileno(input);
		NEW(hook); Poller.SetHook(hook);
		NEW(cons);
		Console.SetConsole(cons)
	END Init;

	PROCEDURE Close;
		VAR res: INTEGER;
	BEGIN
		res := Libc.fclose(input)
	END Close;

BEGIN
	Init
CLOSE
	Close
END LinConsole.