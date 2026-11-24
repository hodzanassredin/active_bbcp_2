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

		(* Файл в памяти: Stores пишет и читает через Files.File, а временный
		   файл ОС на каждый кадр дорог (дескриптор и запись в списке файлов). *)
		MemFile = POINTER TO RECORD (Files.File) b: ActorJournal.Bytes; len: INTEGER END;
		MemReader = POINTER TO RECORD (Files.Reader) f: MemFile; pos: INTEGER END;
		MemWriter = POINTER TO RECORD (Files.Writer) f: MemFile; pos: INTEGER END;

	VAR
		dir-, stdDir-: Codec;

	PROCEDURE (c: Codec) Encode* (s: Stores.Store; OUT data: ActorJournal.Bytes; OUT len: INTEGER), NEW, ABSTRACT;
	(** Пред: s # NIL — 20. Пост: data[0 .. len-1] — кадр s. *)

	PROCEDURE (c: Codec) Decode* (IN data: ARRAY OF BYTE; len: INTEGER; OUT s: Stores.Store;
		OUT res: INTEGER), NEW, ABSTRACT;
	(** Пред: 0 < len <= LEN(data) — 20.
		Пост: res = ok — s — новый объект из кадра; res = unknown — s = NIL. *)

	PROCEDURE (f: MemFile) Length (): INTEGER;
	BEGIN
		RETURN f.len
	END Length;

	PROCEDURE (f: MemFile) NewReader (old: Files.Reader): Files.Reader;
		VAR r: MemReader;
	BEGIN
		NEW(r); r.f := f; r.pos := 0; RETURN r
	END NewReader;

	PROCEDURE (f: MemFile) NewWriter (old: Files.Writer): Files.Writer;
		VAR w: MemWriter;
	BEGIN
		NEW(w); w.f := f; w.pos := f.len; RETURN w
	END NewWriter;

	PROCEDURE (f: MemFile) Flush;
	END Flush;

	PROCEDURE (f: MemFile) Register (name: Files.Name; type: Files.Type; ask: BOOLEAN; OUT res: INTEGER);
	BEGIN
		HALT(100)	(* файл в памяти не регистрируется *)
	END Register;

	PROCEDURE (f: MemFile) Close;
	END Close;

	PROCEDURE (r: MemReader) Base (): Files.File;
	BEGIN
		RETURN r.f
	END Base;

	PROCEDURE (r: MemReader) Pos (): INTEGER;
	BEGIN
		RETURN r.pos
	END Pos;

	PROCEDURE (r: MemReader) SetPos (pos: INTEGER);
	BEGIN
		ASSERT((pos >= 0) & (pos <= r.f.len), 20); r.pos := pos; r.eof := FALSE
	END SetPos;

	PROCEDURE (r: MemReader) ReadByte (OUT x: BYTE);
	BEGIN
		IF r.pos < r.f.len THEN x := r.f.b[r.pos]; INC(r.pos) ELSE x := 0; r.eof := TRUE END
	END ReadByte;

	PROCEDURE (r: MemReader) ReadBytes (VAR x: ARRAY OF BYTE; beg, len: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := beg TO beg + len - 1 DO r.ReadByte(x[i]) END
	END ReadBytes;

	PROCEDURE (w: MemWriter) Base (): Files.File;
	BEGIN
		RETURN w.f
	END Base;

	PROCEDURE (w: MemWriter) Pos (): INTEGER;
	BEGIN
		RETURN w.pos
	END Pos;

	PROCEDURE (w: MemWriter) SetPos (pos: INTEGER);
	BEGIN
		ASSERT((pos >= 0) & (pos <= w.f.len), 20); w.pos := pos
	END SetPos;

	PROCEDURE (w: MemWriter) WriteByte (x: BYTE);
		VAR f: MemFile; nb: ActorJournal.Bytes; i: INTEGER;
	BEGIN
		f := w.f;
		IF w.pos = LEN(f.b^) THEN
			NEW(nb, 2 * LEN(f.b^)); FOR i := 0 TO f.len - 1 DO nb[i] := f.b[i] END; f.b := nb
		END;
		f.b[w.pos] := x; INC(w.pos);
		IF w.pos > f.len THEN f.len := w.pos END
	END WriteByte;

	PROCEDURE (w: MemWriter) WriteBytes (IN x: ARRAY OF BYTE; beg, len: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := beg TO beg + len - 1 DO w.WriteByte(x[i]) END
	END WriteBytes;

	PROCEDURE NewFile (): MemFile;
		VAR f: MemFile;
	BEGIN
		NEW(f); NEW(f.b, 256); f.len := 0; RETURN f
	END NewFile;

	PROCEDURE (c: Std) Encode (s: Stores.Store; OUT data: ActorJournal.Bytes; OUT len: INTEGER);
		VAR f: MemFile; wr: Stores.Writer; i: INTEGER;
	BEGIN
		ASSERT(s # NIL, 20);
		f := NewFile(); wr.ConnectTo(f); wr.WriteStore(s);
		len := f.len; NEW(data, MAX(len, 1));
		FOR i := 0 TO len - 1 DO data[i] := f.b[i] END
	END Encode;

	PROCEDURE (c: Std) Decode (IN data: ARRAY OF BYTE; len: INTEGER; OUT s: Stores.Store; OUT res: INTEGER);
		VAR f: MemFile; rd: Stores.Reader; i: INTEGER;
	BEGIN
		ASSERT((len > 0) & (len <= LEN(data)), 20);
		NEW(f); NEW(f.b, len); f.len := len;
		FOR i := 0 TO len - 1 DO f.b[i] := data[i] END;
		rd.ConnectTo(f); rd.SetPos(0); rd.ReadStore(s);
		IF rd.cancelled OR (s = NIL) OR (s IS Stores.Alien) THEN s := NIL; res := unknown ELSE res := ok END
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
