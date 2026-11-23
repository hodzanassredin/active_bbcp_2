MODULE LinConsole;

	(*
		A. V. Shiryaev, 2012.10, 2016.11
		Console implementation for Linux, OpenBSD, FreeBSD and NetBSD
		I. A. Denisov, 2020.01
		Change iconv to Strings UTF-8
	*)

	IMPORT S := SYSTEM, Console, Utf, Libc := LinLibc;

	TYPE
		Cons = POINTER TO RECORD (Console.Console) END;

	VAR
		cons: Cons;
		input: Libc.PtrFILE;

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
		   несколько строк вперёд и poll врал бы. *)
		VAR p: Libc.pollfd; res: INTEGER;
	BEGIN
		p.fd := Libc.fileno(input);
		p.events := 1;	(* POLLIN *)
		p.revents := 0;
		res := Libc.poll(S.ADR(p), 1, SHORT(timeout));
		RETURN res > 0
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
	BEGIN
		input := Libc.fdopen(Libc.STDIN_FILENO, "rb");
		ASSERT(input # Libc.NULL, 100);
		(* без буфера: poll(fd) должен видеть ровно то, что ещё не прочитано *)
		IF Libc.setvbuf(input, 0, 2, 0) # 0 THEN	(* _IONBF = 2 *)
			(* не фатально: цикл просто будет реже исполнять действия *)
		END;
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