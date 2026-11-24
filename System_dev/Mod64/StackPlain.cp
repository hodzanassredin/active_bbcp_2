MODULE System_devStackPlain;
(** Задача, чей метод Run собран без ключа % (модуль компилируется без
	опций): для System_devStackDirs. Плотный поставщик стеков такой
	задаче стек не даёт — она получает стек со сторожем. *)

	IMPORT Tasks;

	TYPE
		Plain* = POINTER TO EXTENSIBLE RECORD (Tasks.Task)
			n*: INTEGER
		END;

	PROCEDURE (t: Plain) Run*, EXTENSIBLE;
	BEGIN
		INC(t.n); Tasks.Yield; INC(t.n)
	END Run;

END System_devStackPlain.
