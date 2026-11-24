MODULE KvLin;
(** Файл базы Kv в Linux: KvStore.Store поверх дескриптора ОС.

	Файл открывается прямо через libc (open, lseek64, read, write, fsync),
	мимо модуля Files: Files.File не отдаёт дескриптор, а File.Flush только
	передаёт буферы ядру ОС и не ждёт носителя. Sync зовёт fsync: после
	него записанные страницы переживают выключение питания.

	Вызовы синхронные и блокируют всю систему задач на время ввода-вывода:
	внутри них нет точек переключения. *)

	IMPORT SYSTEM, Utf, Libc := LinLibc, KvPages, KvStore;

	CONST
		size = KvPages.size;

	TYPE
		Store = POINTER TO RECORD (KvStore.Store)
			fd: INTEGER;	(* -1 — закрыт *)
			n: INTEGER	(* страниц в файле *)
		END;

	PROCEDURE Seek (s: Store; id: INTEGER): BOOLEAN;
		VAR pos: LONGINT;
	BEGIN
		pos := LONG(id) * size;
		RETURN Libc.lseek64(s.fd, pos, Libc.SEEK_SET) = pos
	END Seek;

	PROCEDURE (s: Store) Read* (id: INTEGER; VAR buf: ARRAY OF BYTE; beg: INTEGER; OUT res: INTEGER);
		VAR got, n: INTEGER;
	BEGIN
		ASSERT(s.fd >= 0, 20); ASSERT(id >= 0, 21); ASSERT(LEN(buf) >= beg + size, 22);
		res := KvStore.ioError;
		IF (id < s.n) & Seek(s, id) THEN
			got := 0; n := 1;
			WHILE (got < size) & (n > 0) DO
				n := SYSTEM.VAL(INTEGER, Libc.read(s.fd, SYSTEM.ADR(buf[beg + got]), size - got));
				IF n > 0 THEN INC(got, n) END
			END;
			IF got = size THEN res := KvStore.ok END
		END
	END Read;

	PROCEDURE (s: Store) Write* (id: INTEGER; IN buf: ARRAY OF BYTE; beg: INTEGER; OUT res: INTEGER);
		VAR put, n: INTEGER;
	BEGIN
		ASSERT(s.fd >= 0, 20); ASSERT(id >= 0, 21); ASSERT(LEN(buf) >= beg + size, 22);
		res := KvStore.ioError;
		IF Seek(s, id) THEN
			put := 0; n := 1;
			WHILE (put < size) & (n > 0) DO
				n := SYSTEM.VAL(INTEGER, Libc.write(s.fd, SYSTEM.ADR(buf[beg + put]), size - put));
				IF n > 0 THEN INC(put, n) END
			END;
			IF put = size THEN
				res := KvStore.ok;
				IF id >= s.n THEN s.n := id + 1 END
			END
		END
	END Write;

	PROCEDURE (s: Store) Sync* (OUT res: INTEGER);
	BEGIN
		ASSERT(s.fd >= 0, 20);
		IF Libc.fsync(s.fd) = 0 THEN res := KvStore.ok ELSE res := KvStore.ioError END
	END Sync;

	PROCEDURE (s: Store) Pages* (): INTEGER;
	BEGIN
		RETURN s.n
	END Pages;

	PROCEDURE (s: Store) Close*;
		VAR r: INTEGER;
	BEGIN
		IF s.fd >= 0 THEN r := Libc.close(s.fd); s.fd := -1 END
	END Close;

	PROCEDURE (s: Store) FINALIZE-;
	BEGIN
		s.Close
	END FINALIZE;

	PROCEDURE Open* (IN path: ARRAY OF CHAR; OUT st: KvStore.Store; OUT res: INTEGER);
	(** Открыть или создать файл path (права 0600).
		Пост: res = KvStore.ok — st открыт; иначе st = NIL, res = ioError. *)
		VAR s: Store; name: ARRAY 1024 OF SHORTCHAR; r: INTEGER; len: LONGINT;
	BEGIN
		st := NIL; res := KvStore.ioError;
		Utf.StringToUtf8(path, name, r);
		IF r = 0 THEN
			NEW(s);
			s.fd := Libc.open(name, Libc.O_RDWR + Libc.O_CREAT + Libc.O_CLOEXEC, {7, 8});
			IF s.fd >= 0 THEN
				len := Libc.lseek64(s.fd, 0, Libc.SEEK_END);
				IF len >= 0 THEN
					s.n := SHORT(len DIV size); st := s; res := KvStore.ok
				ELSE s.Close
				END
			END
		END
	END Open;

	PROCEDURE Delete* (IN path: ARRAY OF CHAR);
	(** Удалить файл path; отсутствие файла не ошибка. *)
		VAR name: ARRAY 1024 OF SHORTCHAR; r: INTEGER;
	BEGIN
		Utf.StringToUtf8(path, name, r);
		IF r = 0 THEN r := Libc.remove(name) END
	END Delete;

END KvLin.
