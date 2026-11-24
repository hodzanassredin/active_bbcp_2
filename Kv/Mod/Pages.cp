MODULE KvPages;
(** Страница хранилища Kv и её кодирование в байты.

	Подсистема Kv — встроенное упорядоченное хранилище «ключ -> значение»
	с транзакциями. Устройство следует BoltDB и LMDB: B+-дерево с
	копированием при записи, один писатель, читатели по снимку, две
	мета-страницы с контрольной суммой, без журнала и без фоновой работы.
	Руководство — Kv/Docu/ru/Kv.md.

	Страница — size байт. Числа в ней записаны младшим байтом вперёд, поэтому
	файл одинаков на amd64 и i386. Заголовок страницы узла и списка свободных:

		0	kind	вид страницы (leaf, branch, free)
		2	count	число элементов (16 бит)
		4	over	число страниц продолжения за этой (узел больше страницы)

	Мета-страница (kind = meta) хранит Meta и CRC32 всех байтов до неё. *)

	IMPORT SYSTEM;

	CONST
		size* = 4096;	(** размер страницы, байт *)
		hdr* = 8;	(** размер заголовка страницы узла *)

		(** вид страницы *)
		leaf* = 1; branch* = 2; meta* = 3; free* = 4;

		magic = 4B564242H;	(* "BBVK" младшим байтом вперёд *)
		version = 1;
		metaLen = 40;	(* байтов меты до контрольной суммы *)

	TYPE
		Bytes* = POINTER TO ARRAY OF BYTE;

		Meta* = RECORD
			root*: INTEGER;	(** страница корня дерева *)
			free*: INTEGER;	(** первая страница списка свободных *)
			pages*: INTEGER;	(** страниц в файле: номера 0 .. pages-1 *)
			txid*: LONGINT	(** номер транзакции, записавшей мету *)
		END;

	VAR
		table: ARRAY 256 OF INTEGER;	(* таблица CRC32, многочлен EDB88320H *)

	(* --- числа ----------------------------------------------------------- *)

	PROCEDURE PutByte* (VAR b: ARRAY OF BYTE; pos, x: INTEGER);
	(** Пост: b[pos] — младшие 8 бит x. *)
	BEGIN
		x := x MOD 256;
		IF x > 127 THEN x := x - 256 END;
		b[pos] := SHORT(SHORT(x))
	END PutByte;

	PROCEDURE Byte* (IN b: ARRAY OF BYTE; pos: INTEGER): INTEGER;
	(** Пост: b[pos] как число 0..255. *)
	BEGIN
		RETURN b[pos] MOD 256
	END Byte;

	PROCEDURE PutShort* (VAR b: ARRAY OF BYTE; pos, x: INTEGER);
	(** Пред: 0 <= x < 10000H — 20. *)
	BEGIN
		ASSERT((x >= 0) & (x < 10000H), 20);
		PutByte(b, pos, x); PutByte(b, pos + 1, x DIV 256)
	END PutShort;

	PROCEDURE Short* (IN b: ARRAY OF BYTE; pos: INTEGER): INTEGER;
	BEGIN
		RETURN Byte(b, pos) + 256 * Byte(b, pos + 1)
	END Short;

	PROCEDURE PutInt* (VAR b: ARRAY OF BYTE; pos, x: INTEGER);
	BEGIN
		PutByte(b, pos, x); PutByte(b, pos + 1, ASH(x, -8));
		PutByte(b, pos + 2, ASH(x, -16)); PutByte(b, pos + 3, ASH(x, -24))
	END PutInt;

	PROCEDURE Int* (IN b: ARRAY OF BYTE; pos: INTEGER): INTEGER;
	BEGIN
		RETURN Byte(b, pos) + ASH(Byte(b, pos + 1), 8) + ASH(Byte(b, pos + 2), 16)
			+ ASH(b[pos + 3], 24)	(* старший байт со знаком *)
	END Int;

	PROCEDURE PutLong (VAR b: ARRAY OF BYTE; pos: INTEGER; x: LONGINT);
	BEGIN
		PutInt(b, pos, SHORT(x MOD 100000000L)); PutInt(b, pos + 4, SHORT(ASH(x, -32)))
	END PutLong;

	PROCEDURE Long (IN b: ARRAY OF BYTE; pos: INTEGER): LONGINT;
	BEGIN
		RETURN (Int(b, pos) MOD 100000000L) + ASH(LONG(Int(b, pos + 4)), 32)
	END Long;

	(* --- контрольная сумма ------------------------------------------------ *)

	PROCEDURE Crc* (IN b: ARRAY OF BYTE; beg, len: INTEGER): INTEGER;
	(** CRC32 (как в zlib) байтов b[beg .. beg+len-1]. *)
		VAR c, idx, sh: INTEGER; i: INTEGER;
	BEGIN
		c := -1;
		FOR i := beg TO beg + len - 1 DO
			idx := ORD(BITS(c MOD 256) / BITS(Byte(b, i)));
			sh := SYSTEM.LSH(c, -8);
			c := ORD(BITS(table[idx]) / BITS(sh))
		END;
		RETURN ORD(-BITS(c))
	END Crc;

	PROCEDURE InitTable;
		VAR i, k, c: INTEGER;
	BEGIN
		FOR i := 0 TO 255 DO
			c := i;
			FOR k := 0 TO 7 DO
				IF ODD(c) THEN c := ORD(BITS(SYSTEM.LSH(c, -1)) / BITS(0EDB88320H))
				ELSE c := SYSTEM.LSH(c, -1)
				END
			END;
			table[i] := c
		END
	END InitTable;

	(* --- заголовок -------------------------------------------------------- *)

	PROCEDURE PutHeader* (VAR b: ARRAY OF BYTE; kind, count, over: INTEGER);
	BEGIN
		PutByte(b, 0, kind); PutByte(b, 1, 0); PutShort(b, 2, count); PutInt(b, 4, over)
	END PutHeader;

	PROCEDURE Kind* (IN b: ARRAY OF BYTE): INTEGER;
	BEGIN
		RETURN Byte(b, 0)
	END Kind;

	PROCEDURE Count* (IN b: ARRAY OF BYTE): INTEGER;
	BEGIN
		RETURN Short(b, 2)
	END Count;

	PROCEDURE Over* (IN b: ARRAY OF BYTE): INTEGER;
	BEGIN
		RETURN Int(b, 4)
	END Over;

	(* --- мета ------------------------------------------------------------- *)

	PROCEDURE PutMeta* (VAR b: ARRAY OF BYTE; IN m: Meta);
	(** Пред: LEN(b) >= size — 20. Пост: b — мета-страница m с контрольной
		суммой; остальные байты страницы нулевые. *)
		VAR i: INTEGER;
	BEGIN
		ASSERT(LEN(b) >= size, 20);
		FOR i := 0 TO size - 1 DO b[i] := 0 END;
		PutHeader(b, meta, 0, 0);
		PutInt(b, 8, magic); PutInt(b, 12, version); PutInt(b, 16, size);
		PutInt(b, 20, m.root); PutInt(b, 24, m.free); PutInt(b, 28, m.pages);
		PutLong(b, 32, m.txid);
		PutInt(b, metaLen, Crc(b, 0, metaLen))
	END PutMeta;

	PROCEDURE GetMeta* (IN b: ARRAY OF BYTE; OUT m: Meta; OUT ok: BOOLEAN);
	(** Пост: ok — b содержит целую мету этой версии (вид, сигнатура, размер
		страницы, контрольная сумма); тогда m — она. *)
	BEGIN
		ok := (LEN(b) >= size) & (Kind(b) = meta) & (Int(b, 8) = magic)
			& (Int(b, 12) = version) & (Int(b, 16) = size)
			& (Int(b, metaLen) = Crc(b, 0, metaLen));
		IF ok THEN
			m.root := Int(b, 20); m.free := Int(b, 24); m.pages := Int(b, 28);
			m.txid := Long(b, 32);
			ok := (m.pages >= 4) & (m.root >= 2) & (m.root < m.pages)
				& (m.free >= 2) & (m.free < m.pages) & (m.txid >= 0)
		END;
		IF ~ok THEN m.root := 0; m.free := 0; m.pages := 0; m.txid := -1 END
	END GetMeta;

	(* --- порядок ключей --------------------------------------------------- *)

	PROCEDURE Compare* (IN a: ARRAY OF BYTE; alen: INTEGER; IN b: ARRAY OF BYTE; blen: INTEGER): INTEGER;
	(** Побайтное сравнение без знака a[0..alen) и b[0..blen): -1, 0 или 1.
		Короткий ключ, который служит началом длинного, меньше него. *)
		VAR i, n, x, y: INTEGER;
	BEGIN
		n := MIN(alen, blen); i := 0;
		WHILE (i < n) & (a[i] = b[i]) DO INC(i) END;
		IF i < n THEN
			x := a[i] MOD 256; y := b[i] MOD 256;
			IF x < y THEN RETURN -1 ELSE RETURN 1 END
		ELSIF alen < blen THEN RETURN -1
		ELSIF alen > blen THEN RETURN 1
		ELSE RETURN 0
		END
	END Compare;

	PROCEDURE Copy* (IN a: ARRAY OF BYTE; beg, len: INTEGER): Bytes;
	(** Пост: новый массив из len байтов a[beg..]. *)
		VAR r: Bytes; i: INTEGER;
	BEGIN
		NEW(r, len);
		FOR i := 0 TO len - 1 DO r[i] := a[beg + i] END;
		RETURN r
	END Copy;

BEGIN
	InitTable
END KvPages.
