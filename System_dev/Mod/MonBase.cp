MODULE System_devMonBase;
(** Базовые типы для System_devMonField: расширение в другом модуле.

	Counter — тип с исключающим методом: у него скрытое поле владельца,
	и расширение в другом модуле пользуется им же. Plain — тип без
	исключающих методов: поле заводит расширение. *)

	IMPORT ACTIVE, Tasks;

	TYPE
		Counter* = POINTER TO EXTENSIBLE RECORD
			n*, inside*, overlaps*: INTEGER
		END;

		Plain* = POINTER TO EXTENSIBLE RECORD
			n*: INTEGER
		END;

	PROCEDURE [exclusive] (c: Counter) Base*, NEW;
	(** Пост: c.n увеличен; пока метод исполняется, в монитор c не входит
		никто другой (иначе растёт c.overlaps). *)
	BEGIN
		INC(c.inside);
		IF c.inside > 1 THEN INC(c.overlaps) END;
		Tasks.Yield; Tasks.Yield;
		DEC(c.inside); INC(c.n)
	END Base;

END System_devMonBase.
