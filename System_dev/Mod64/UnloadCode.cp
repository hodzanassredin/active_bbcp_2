MODULE System_devUnloadCode;
(** Компутон для проверки выгрузки кода у исполнителей (этап 10).

	Версия 1: y = x * x. Тест System_devUnload перекомпилирует этот модуль
	в версию 2 (y = x * x + 1000), выгружает код у исполнителей и
	проверяет, что следующий Push исполняется новым кодом.

	Тип Square не экспортируется: снаружи видны только New и Y, поэтому
	переопределения Do/Externalize сохраняют видимость базовых методов. *)

	IMPORT AsyncComputon, Stores;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;

	PROCEDURE New* (x: INTEGER): AsyncComputon.Computon;
		VAR c: Square;
	BEGIN
		NEW(c); c.x := x; c.y := -1;
		RETURN c
	END New;

	PROCEDURE Y* (c: AsyncComputon.Computon): INTEGER;
		VAR y: INTEGER;
	BEGIN
		y := -1;
		WITH c: Square DO y := c.y END;
		RETURN y
	END Y;

	PROCEDURE (c: Square) Do;
	BEGIN
		c.y := c.x * c.x
	END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(0);
		wr.WriteInt(c.x); wr.WriteInt(c.y)
	END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, version);
		rd.ReadInt(c.x); rd.ReadInt(c.y)
	END Internalize;

END System_devUnloadCode.
