MODULE KvStore;
(** Файловый слой хранилища Kv: массив страниц, который можно читать,
	писать и синхронизировать. Устройство Kv следует BoltDB/LMDB
	(Kv/Docu/ru/Kv.md).

	Store ничего не знает о дереве: он хранит страницы по номерам. Записанная
	страница видна чтению сразу, но переживает выключение питания только
	после Sync. Порядок, в котором несинхронизированные записи доходят до
	носителя, не задан: часть из них может дойти, часть нет, страница может
	дойти наполовину. На этом допущении построен порядок фиксации в KvDb.

	Реализации: KvLin — файл ОС (Linux), KvFault — память со сбоями для
	тестов. Все операции синхронные: внутри них нет точек переключения. *)

	IMPORT KvPages;

	CONST
		ok* = 0;	(** операция выполнена *)
		ioError* = 1;	(** ошибка ввода-вывода или устройство отказало *)
		corrupt* = 2;	(** содержимое не похоже на базу Kv *)

	TYPE
		Store* = POINTER TO ABSTRACT RECORD END;

	PROCEDURE (s: Store) Read* (id: INTEGER; VAR buf: ARRAY OF BYTE; beg: INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
	(** Пред: id >= 0; LEN(buf) >= beg + KvPages.size.
		Пост: res = ok — buf[beg .. beg+size) содержит страницу id;
		страница за концом — res = ioError. *)

	PROCEDURE (s: Store) Write* (id: INTEGER; IN buf: ARRAY OF BYTE; beg: INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
	(** Пред: id >= 0; LEN(buf) >= beg + KvPages.size.
		Пост: res = ok — страница id равна buf[beg .. beg+size) для чтения;
		файл при необходимости удлиняется. *)

	PROCEDURE (s: Store) Sync* (OUT res: INTEGER), NEW, ABSTRACT;
	(** Пост: res = ok — все записанные страницы переживут выключение питания. *)

	PROCEDURE (s: Store) Pages* (): INTEGER, NEW, ABSTRACT;
	(** Пост: число страниц в хранилище. *)

	PROCEDURE (s: Store) Close*, NEW, ABSTRACT;
	(** Пост: ресурсы ОС освобождены; несинхронизированное может пропасть. *)

	PROCEDURE PageSize* (): INTEGER;
	BEGIN
		RETURN KvPages.size
	END PageSize;

END KvStore.
