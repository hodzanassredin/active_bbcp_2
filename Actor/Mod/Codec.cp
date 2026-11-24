MODULE ActorCodec;
(** Кадр события и снимка: Stores.Store <-> байты.

	Кодек сменный, по образцу Files.Directory: абстрактный тип, переменные
	dir и stdDir, процедура SetDir. Стандартный кодек пишет объект тем же
	механизмом, каким BlackBox пишет документы: Stores.Writer.WriteStore —
	имя типа и цепочка базовых типов, затем поля из Externalize. Читает
	Stores.Reader.ReadStore: тип ищется по имени, модуль загружается.

	Версии. Externalize пишет номер версии (WriteVersion), Internalize
	читает его (ReadVersion(min, max, v)) и разбирает поля по v. Новая
	версия типа поднимает max и читает старые кадры по старой раскладке.
	Кадр с версией вне [min, max] и кадр неизвестного типа Stores читает
	как «чужой» объект (Stores.Alien); Decode отвечает unknown, не трапует. *)

	IMPORT Files, Stores, ActorJournal;

	CONST
		(** res *)
		ok* = 0;
		unknown* = 1;	(** тип не найден или версия вне допустимых *)

	TYPE
		Codec* = POINTER TO ABSTRACT RECORD END;

		Std = POINTER TO RECORD (Codec) END;

	VAR
		dir-, stdDir-: Codec;

	PROCEDURE (c: Codec) Encode* (s: Stores.Store; OUT data: ActorJournal.Bytes; OUT len: INTEGER), NEW, ABSTRACT;
	(** Пред: s # NIL — 20. Пост: data[0 .. len-1] — кадр s. *)

	PROCEDURE (c: Codec) Decode* (IN data: ARRAY OF BYTE; len: INTEGER; OUT s: Stores.Store;
		OUT res: INTEGER), NEW, ABSTRACT;
	(** Пред: 0 < len <= LEN(data) — 20.
		Пост: res = ok — s — новый объект из кадра; res = unknown — s = NIL. *)

	PROCEDURE (c: Std) Encode (s: Stores.Store; OUT data: ActorJournal.Bytes; OUT len: INTEGER);
		VAR f: Files.File; wr: Stores.Writer; r: Files.Reader;
	BEGIN
		ASSERT(s # NIL, 20);
		f := Files.dir.Temp(); wr.ConnectTo(f); wr.WriteStore(s);
		len := f.Length(); NEW(data, MAX(len, 1));
		r := f.NewReader(NIL); r.SetPos(0); r.ReadBytes(data, 0, len);
		f.Close
	END Encode;

	PROCEDURE (c: Std) Decode (IN data: ARRAY OF BYTE; len: INTEGER; OUT s: Stores.Store; OUT res: INTEGER);
		VAR f: Files.File; rd: Stores.Reader; w: Files.Writer;
	BEGIN
		ASSERT((len > 0) & (len <= LEN(data)), 20);
		f := Files.dir.Temp(); w := f.NewWriter(NIL); w.WriteBytes(data, 0, len);
		rd.ConnectTo(f); rd.SetPos(0); rd.ReadStore(s);
		IF rd.cancelled OR (s = NIL) OR (s IS Stores.Alien) THEN s := NIL; res := unknown ELSE res := ok END;
		f.Close
	END Decode;

	PROCEDURE SetDir* (c: Codec);
	(** Пред: c # NIL — 20. Пост: dir = c; первый установленный кодек остаётся в stdDir. *)
	BEGIN
		ASSERT(c # NIL, 20);
		dir := c;
		IF stdDir = NIL THEN stdDir := c END
	END SetDir;

	PROCEDURE Init;
		VAR c: Std;
	BEGIN
		NEW(c); SetDir(c)
	END Init;

BEGIN
	Init
END ActorCodec.
