MODULE System_devStackPlain;
(** Задача, чей метод Run собран без ключа % (модуль компилируется без
	опций): для System_devStackDirs. Плотный поставщик стеков такой
	задаче стек не даёт — она получает стек со сторожем. *)

	IMPORT S := SYSTEM, Tasks;

	TYPE
		Plain* = POINTER TO EXTENSIBLE RECORD (Tasks.Task)
			n*: INTEGER
		END;

	VAR
		lowest*: S.ADDRESS;	(* самый низкий адрес локала Burn в последнем вызове *)

	PROCEDURE Burn* (n: INTEGER): INTEGER;
	(** Код без проверки в прологе: n вложенных кадров по ~140 байт. *)
		VAR a: ARRAY 16 OF INTEGER;
	BEGIN
		IF S.ADR(a) < lowest THEN lowest := S.ADR(a) END;
		a[n MOD 16] := n;
		IF n <= 0 THEN RETURN 0 END;
		RETURN Burn(n - 1) + a[n MOD 16]
	END Burn;

	PROCEDURE (t: Plain) Run*, EXTENSIBLE;
	BEGIN
		INC(t.n); Tasks.Yield; INC(t.n)
	END Run;

END System_devStackPlain.
