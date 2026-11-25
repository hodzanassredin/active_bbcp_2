MODULE ObxCodec;
(** Интерфейс разборщика потока байтов: одна единица за вызов.

	Разборщик получает окно буфера `buf` от `beg` длиной `len` и отвечает
	одним из трёх:
	- `more` — единица не целая, ничего не съедено, `used = 0`;
	- `done` — единица разобрана, съедено `used` байт, описание единицы
	  (смещение и длина среза того же буфера) лежит в полях разборщика;
	- `error` — поток испорчен.
	Разборщик помнит, докуда уже искал разделитель, поэтому повторный
	вызов с большим окном не начинает поиск заново. *)

	CONST
		done* = 0;
		more* = 1;
		error* = 2;

	TYPE
		Decoder* = ABSTRACT RECORD END;

	PROCEDURE (VAR d: Decoder) Decode* (IN buf: ARRAY OF BYTE;
			beg, len: INTEGER; OUT used, res: INTEGER), NEW, ABSTRACT;
	(** Пред: beg >= 0, len >= 0, beg + len <= LEN(buf).
		Пост: res = more — used = 0; res = done — used > 0 или единица
		пуста; res = error — поток испорчен. *)

END ObxCodec.
