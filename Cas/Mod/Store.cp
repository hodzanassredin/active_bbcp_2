MODULE CasStore;
(** Хранилище блоков по содержимому: абстрактный тип и реализация в памяти.

	Блок называется своим хешем (CasHash.Id), поэтому запись идемпотентна:
	тот же блок — тот же Id. Ссылка — изменяемое имя, указывающее на Id;
	CasRef меняет её, только если она указывает на ожидаемый Id. Реализация
	в каталоге файлов — CasFiles. *)

	IMPORT CasHash;

	CONST
		(** res *)
		ok* = 0;
		absent* = 1;	(** блока нет *)
		corrupt* = 2;	(** блок прочитан, но хеш не сошёлся *)
		failed* = 3;	(** ошибка среды: файл не создан, не прочитан *)

		maxRef* = 64;	(** длина имени ссылки, включая 0X *)
		buckets = 256;

	TYPE
		Block* = POINTER TO ARRAY OF BYTE;
		RefName* = ARRAY maxRef OF CHAR;

		Store* = POINTER TO ABSTRACT RECORD END;

		Item = POINTER TO RECORD
			next: Item;
			id: CasHash.Id;
			data: Block
		END;

		Ref = POINTER TO RECORD
			next: Ref;
			name: RefName;
			id: CasHash.Id
		END;

		Mem* = POINTER TO RECORD (Store)
			blocks-: INTEGER;	(** число разных блоков *)
			bytes-: INTEGER;	(** их общий размер *)
			tab: ARRAY buckets OF Item;
			refs: Ref
		END;

	PROCEDURE ValidRef* (IN name: ARRAY OF CHAR): BOOLEAN;
	(** Пост: результат = name из 1..maxRef-1 знаков A-Z a-z 0-9 _ - . *)
		VAR i: INTEGER; c: CHAR;
	BEGIN
		i := 0;
		WHILE (i < LEN(name)) & (i < maxRef) & (name[i] # 0X) DO
			c := name[i];
			IF ~(((c >= "a") & (c <= "z")) OR ((c >= "A") & (c <= "Z")) OR ((c >= "0") & (c <= "9"))
				OR (c = "_") OR (c = "-") OR (c = ".")) THEN RETURN FALSE
			END;
			INC(i)
		END;
		RETURN (i > 0) & (i < maxRef) & ((i = LEN(name)) OR (name[i] = 0X))
	END ValidRef;

	PROCEDURE (s: Store) Put* (IN data: ARRAY OF BYTE; len: INTEGER; OUT id: CasHash.Id;
		OUT res: INTEGER), NEW, ABSTRACT;
	(** Записать блок data[0 .. len-1].
		Пред: 0 < len <= LEN(data) — 20 (пустого блока нет).
		Пост: res = ok: id = SHA-256 блока, Has(id); блок, который уже был, не перезаписан.
		Пост: res = failed: id — тот же хеш, блок мог не сохраниться. *)

	PROCEDURE (s: Store) Get* (IN id: CasHash.Id; OUT b: Block; OUT res: INTEGER), NEW, ABSTRACT;
	(** Прочитать блок.
		Пост: res = ok: b # NIL, SHA-256(b) = id.
		Пост: res IN {absent, corrupt, failed}: b = NIL. *)

	PROCEDURE (s: Store) Has* (IN id: CasHash.Id): BOOLEAN, NEW, ABSTRACT;
	(** Пост: результат = блок id записан (содержимое не проверяется). *)

	PROCEDURE (s: Store) GetRef* (IN name: ARRAY OF CHAR; OUT id: CasHash.Id; OUT res: INTEGER),
		NEW, ABSTRACT;
	(** Прочитать ссылку.
		Пред: ValidRef(name) — 22.
		Пост: res = ok: id — значение ссылки; ссылки нет — id пуст (CasHash.IsNil).
		Пост: res = failed или corrupt: id пуст. *)

	PROCEDURE (s: Store) CasRef* (IN name: ARRAY OF CHAR; IN old, new: CasHash.Id): BOOLEAN,
		NEW, ABSTRACT;
	(** Сравнить и заменить: если ссылка name равна old (пустой Id — ссылки нет),
		она получает значение new.
		Пред: ValidRef(name) — 22.
		Пост: TRUE — ссылка равна new. FALSE — ссылка не равна old или запись не удалась;
		ссылка не изменилась. *)

	(* Mem *)

	PROCEDURE NewMem* (): Mem;
	(** Пост: пустое хранилище в памяти: blocks = 0, bytes = 0, ссылок нет. *)
		VAR m: Mem;
	BEGIN
		NEW(m); RETURN m
	END NewMem;

	PROCEDURE Bucket (IN id: CasHash.Id): INTEGER;
		VAR h: INTEGER;
	BEGIN
		h := id[0]; RETURN h MOD buckets
	END Bucket;

	PROCEDURE (m: Mem) Find (IN id: CasHash.Id): Item, NEW;
		VAR it: Item;
	BEGIN
		it := m.tab[Bucket(id)];
		WHILE (it # NIL) & ~CasHash.Equal(it.id, id) DO it := it.next END;
		RETURN it
	END Find;

	PROCEDURE (m: Mem) Put* (IN data: ARRAY OF BYTE; len: INTEGER; OUT id: CasHash.Id;
		OUT res: INTEGER);
		VAR it: Item; i, h: INTEGER;
	BEGIN
		ASSERT((len > 0) & (len <= LEN(data)), 20);
		CasHash.Of(data, 0, len, id);
		IF m.Find(id) = NIL THEN
			NEW(it); it.id := id; NEW(it.data, len);
			FOR i := 0 TO len - 1 DO it.data[i] := data[i] END;
			h := Bucket(id); it.next := m.tab[h]; m.tab[h] := it;
			INC(m.blocks); INC(m.bytes, len)
		END;
		res := ok
	END Put;

	PROCEDURE (m: Mem) Get* (IN id: CasHash.Id; OUT b: Block; OUT res: INTEGER);
		VAR it: Item; i: INTEGER;
	BEGIN
		it := m.Find(id);
		IF it = NIL THEN b := NIL; res := absent
		ELSE
			NEW(b, LEN(it.data));	(* копия: блок неизменяем и для того, кто его прочитал *)
			FOR i := 0 TO LEN(b) - 1 DO b[i] := it.data[i] END;
			res := ok
		END
	END Get;

	PROCEDURE (m: Mem) Has* (IN id: CasHash.Id): BOOLEAN;
	BEGIN
		RETURN m.Find(id) # NIL
	END Has;

	PROCEDURE (m: Mem) FindRef (IN name: ARRAY OF CHAR): Ref, NEW;
		VAR r: Ref;
	BEGIN
		r := m.refs;
		WHILE (r # NIL) & (r.name # name) DO r := r.next END;
		RETURN r
	END FindRef;

	PROCEDURE (m: Mem) GetRef* (IN name: ARRAY OF CHAR; OUT id: CasHash.Id; OUT res: INTEGER);
		VAR r: Ref;
	BEGIN
		ASSERT(ValidRef(name), 22);
		r := m.FindRef(name);
		IF r = NIL THEN CasHash.Clear(id) ELSE id := r.id END;
		res := ok
	END GetRef;

	PROCEDURE (m: Mem) CasRef* (IN name: ARRAY OF CHAR; IN old, new: CasHash.Id): BOOLEAN;
		VAR r: Ref; cur: CasHash.Id;
	BEGIN
		ASSERT(ValidRef(name), 22);
		r := m.FindRef(name);
		IF r = NIL THEN CasHash.Clear(cur) ELSE cur := r.id END;
		IF ~CasHash.Equal(cur, old) THEN RETURN FALSE END;
		IF r = NIL THEN NEW(r); r.name := name$; r.next := m.refs; m.refs := r END;
		r.id := new;
		RETURN TRUE
	END CasRef;

END CasStore.
