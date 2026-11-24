MODULE KvDb;
(** Встроенное упорядоченное хранилище «ключ -> значение» с транзакциями.
	Устройство следует BoltDB и LMDB: B+-дерево с копированием при записи
	(KvTree), один писатель, читатели по снимку, две мета-страницы с
	контрольной суммой, без журнала и без фоновой работы. Руководство —
	Kv/Docu/ru/Kv.md.

	Состояние базы — мета: корень дерева, список свободных страниц, число
	страниц и номер транзакции. Меты две, на страницах 0 и 1; транзакция
	txid пишет мету в страницу txid MOD 2, поэтому прежняя мета цела, пока
	пишется новая. Открытие берёт мету с верной контрольной суммой и
	большим номером.

	Порядок фиксации: страницы дерева и списка свободных -> Sync -> мета ->
	Sync. Первый Sync гарантирует, что мета никогда не укажет на страницу,
	не дошедшую до носителя; второй — что фиксация пережила питание.

	Свободные страницы. Страницу, которую отпустила транзакция w, ещё
	видят читатели снимков с номером меньше w. Она ждёт в pending с номером
	w и переходит в free, когда у каждого открытого читателя номер снимка
	не меньше w (ReleasePending). Список свободных на носителе содержит и
	free, и pending: после открытия читателей нет.

	Задачи. Внутри Begin, Get, Put, Delete, курсора и Commit нет точек
	переключения: ввод-вывод синхронный. Пишущая транзакция одна; Begin(TRUE)
	при открытой пишущей — трап 21 (ждать её конца на месте нельзя: в той же
	задаче это вечное ожидание). Задачам, которые пишут по очереди, нужен
	общий монитор вокруг транзакции. *)

	IMPORT Utf, KvPages, KvStore, KvTree;

	CONST
		ok* = KvStore.ok;
		ioError* = KvStore.ioError;
		corrupt* = KvStore.corrupt;

	TYPE
		Bytes* = KvPages.Bytes;
		Ints = POINTER TO ARRAY OF INTEGER;

		Pending = POINTER TO RECORD
			txid: LONGINT;	(* транзакция, отпустившая страницы *)
			ids: Ints; n: INTEGER;
			next: Pending
		END;

		Db* = POINTER TO RECORD
			store: KvStore.Store;
			pager: KvTree.Pager;
			meta: KvPages.Meta;	(* последняя зафиксированная *)
			freePages: INTEGER;	(* страниц у списка свободных меты *)
			free: Ints; nfree: INTEGER;	(* свободные, по возрастанию *)
			pending: Pending;
			readers: Tx;	(* открытые читающие *)
			writer: Tx;
			res: INTEGER	(* ошибка фиксации: база требует переоткрытия *)
		END;

		Tx* = POINTER TO RECORD
			db: Db;
			write-: BOOLEAN;
			open: BOOLEAN;
			meta: KvPages.Meta;	(* снимок; у писателя pages растёт *)
			tree: KvTree.Tree;
			res-: INTEGER;	(** первая ошибка среды в транзакции *)
			next: Tx;	(* в списке читателей *)
			space: Space
		END;

		Space = POINTER TO RECORD (KvTree.Space)
			tx: Tx;
			freed: Ints; nfreed: INTEGER;	(* отпущенные этой транзакцией *)
			free0: Ints; nfree0: INTEGER	(* копия db.free на случай отката *)
		END;

		Cursor* = KvTree.Cursor;

	(* --- множества номеров ----------------------------------------------- *)

	PROCEDURE Add (VAR a: Ints; VAR n: INTEGER; x: INTEGER);
		VAR b: Ints; i: INTEGER;
	BEGIN
		IF a = NIL THEN NEW(a, 16) END;
		IF n = LEN(a^) THEN
			NEW(b, 2 * n); FOR i := 0 TO n - 1 DO b[i] := a[i] END; a := b
		END;
		a[n] := x; INC(n)
	END Add;

	PROCEDURE Move (src, dst: Ints; n: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO dst[i] := src[i] END
	END Move;

	PROCEDURE Sort (a: Ints; n: INTEGER);
		VAR i, j, x, h: INTEGER;
	BEGIN	(* Шелл *)
		h := 1; WHILE h < n DIV 3 DO h := 3 * h + 1 END;
		WHILE h > 0 DO
			FOR i := h TO n - 1 DO
				x := a[i]; j := i;
				WHILE (j >= h) & (a[j - h] > x) DO a[j] := a[j - h]; DEC(j, h) END;
				a[j] := x
			END;
			h := h DIV 3
		END
	END Sort;

	(* --- выделение страниц ------------------------------------------------ *)

	PROCEDURE Take (s: Space; n: INTEGER): INTEGER;
		(* первая подходящая цепочка из n подряд свободных, иначе конец файла *)
		VAR db: Db; i, j, id: INTEGER;
	BEGIN
		db := s.tx.db; i := 0; id := -1;
		WHILE (id < 0) & (i + n <= db.nfree) DO
			IF db.free[i + n - 1] - db.free[i] = n - 1 THEN id := db.free[i]
			ELSE INC(i)
			END
		END;
		IF id >= 0 THEN
			FOR j := i TO db.nfree - n - 1 DO db.free[j] := db.free[j + n] END;
			DEC(db.nfree, n)
		ELSE
			id := s.tx.meta.pages; INC(s.tx.meta.pages, n)
		END;
		RETURN id
	END Take;

	PROCEDURE Release (s: Space; id, n: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := id TO id + n - 1 DO Add(s.freed, s.nfreed, i) END
	END Release;

	PROCEDURE (s: Space) Take- (n: INTEGER): INTEGER;
	BEGIN
		RETURN Take(s, n)
	END Take;

	PROCEDURE (s: Space) Release- (id, n: INTEGER);
	BEGIN
		Release(s, id, n)
	END Release;

	PROCEDURE ReleasePending (db: Db);
		(* pending, которые не видит ни один открытый читатель, — в free *)
		VAR oldest: LONGINT; r: Tx; p, q: Pending; i: INTEGER;
	BEGIN
		oldest := MAX(LONGINT); r := db.readers;
		WHILE r # NIL DO oldest := MIN(oldest, r.meta.txid); r := r.next END;
		p := db.pending; q := NIL;
		WHILE p # NIL DO
			IF p.txid <= oldest THEN
				FOR i := 0 TO p.n - 1 DO Add(db.free, db.nfree, p.ids[i]) END;
				IF q = NIL THEN db.pending := p.next ELSE q.next := p.next END
			ELSE q := p
			END;
			p := p.next
		END;
		IF db.free # NIL THEN Sort(db.free, db.nfree) END
	END ReleasePending;

	(* --- список свободных на носителе ----------------------------------- *)

	PROCEDURE ReadFree (db: Db; id: INTEGER; OUT res: INTEGER);
		VAR buf: Bytes; n, cnt, i: INTEGER;
	BEGIN
		db.nfree := 0; db.freePages := 0;
		KvTree.ReadRun(db.pager, id, buf, n, res);
		IF res = ok THEN
			cnt := KvPages.Int(buf^, KvPages.hdr);
			IF (KvPages.Kind(buf^) # KvPages.free) OR (cnt < 0) OR (KvPages.hdr + 4 + 4 * cnt > n * KvPages.size) THEN
				res := corrupt
			ELSE
				db.freePages := n;
				FOR i := 0 TO cnt - 1 DO Add(db.free, db.nfree, KvPages.Int(buf^, KvPages.hdr + 4 + 4 * i)) END;
				IF db.free # NIL THEN Sort(db.free, db.nfree) END
			END
		END
	END ReadFree;

	PROCEDURE WriteFree (tx: Tx; OUT id, pages, res: INTEGER);
		(* записать free + pending + отпущенные tx; страницы самого списка
			берутся до подсчёта, поэтому их может оказаться больше нужного *)
		VAR db: Db; s: Space; buf: Bytes; p: Pending; cnt, pos, i: INTEGER;

		PROCEDURE Put (x: INTEGER);
		BEGIN
			KvPages.PutInt(buf^, pos, x); INC(pos, 4); INC(cnt)
		END Put;

	BEGIN
		db := tx.db; s := tx.space;
		Release(s, tx.meta.free, db.freePages);
		cnt := db.nfree + s.nfreed; p := db.pending;
		WHILE p # NIL DO INC(cnt, p.n); p := p.next END;
		pages := (KvPages.hdr + 4 + 4 * cnt + KvPages.size - 1) DIV KvPages.size;
		id := Take(s, pages);
		NEW(buf, pages * KvPages.size); pos := KvPages.hdr + 4; cnt := 0;
		FOR i := 0 TO db.nfree - 1 DO Put(db.free[i]) END;
		FOR i := 0 TO s.nfreed - 1 DO Put(s.freed[i]) END;
		p := db.pending;
		WHILE p # NIL DO FOR i := 0 TO p.n - 1 DO Put(p.ids[i]) END; p := p.next END;
		KvPages.PutHeader(buf^, KvPages.free, 0, pages - 1);
		KvPages.PutInt(buf^, KvPages.hdr, cnt);
		i := 0; res := ok;
		WHILE (i < pages) & (res = ok) DO
			KvTree.Invalidate(db.pager, id + i);
			db.store.Write(id + i, buf^, i * KvPages.size, res); INC(i)
		END
	END WriteFree;

	(* --- открытие -------------------------------------------------------- *)

	PROCEDURE Init (s: KvStore.Store; OUT res: INTEGER);
		(* пустая база: меты 0 и 1, пустой список свободных (2), пустой лист (3) *)
		VAR b: ARRAY KvPages.size OF BYTE; m: KvPages.Meta;
	BEGIN
		KvPages.PutHeader(b, KvPages.free, 0, 0); KvPages.PutInt(b, KvPages.hdr, 0);
		s.Write(2, b, 0, res);
		IF res = ok THEN KvPages.PutHeader(b, KvPages.leaf, 0, 0); s.Write(3, b, 0, res) END;
		IF res = ok THEN s.Sync(res) END;
		m.root := 3; m.free := 2; m.pages := 4; m.txid := 0;
		IF res = ok THEN KvPages.PutMeta(b, m); s.Write(0, b, 0, res) END;
		m.txid := 1;
		IF res = ok THEN KvPages.PutMeta(b, m); s.Write(1, b, 0, res) END;
		IF res = ok THEN s.Sync(res) END
	END Init;

	PROCEDURE Open* (s: KvStore.Store; OUT db: Db; OUT res: INTEGER);
	(** Открыть базу в хранилище s; пустое хранилище становится пустой базой.
		Пред: s # NIL — 20.
		Пост: res = ok — db открыта; res = corrupt — ни одной целой меты или
		повреждён список свободных; res = ioError — ошибка чтения; иначе db = NIL. *)
		VAR b: ARRAY KvPages.size OF BYTE; m0, m1: KvPages.Meta; ok0, ok1: BOOLEAN; r: INTEGER;
	BEGIN
		ASSERT(s # NIL, 20);
		db := NIL; res := ok;
		IF s.Pages() = 0 THEN Init(s, res) END;
		ok0 := FALSE; ok1 := FALSE;
		IF res = ok THEN
			s.Read(0, b, 0, r); IF r = ok THEN KvPages.GetMeta(b, m0, ok0) END;
			s.Read(1, b, 0, r); IF r = ok THEN KvPages.GetMeta(b, m1, ok1) END;
			IF ~ok0 & ~ok1 THEN res := corrupt END
		END;
		IF res = ok THEN
			NEW(db); db.store := s; db.pager := KvTree.NewPager(s); NEW(db.free, 16);
			IF ok0 & (~ok1 OR (m0.txid > m1.txid)) THEN db.meta := m0 ELSE db.meta := m1 END;
			IF db.meta.pages > s.Pages() THEN res := corrupt
			ELSE ReadFree(db, db.meta.free, res)
			END;
			IF res # ok THEN db := NIL END
		END
	END Open;

	PROCEDURE (db: Db) Close*, NEW;
	(** Пред: открытых транзакций нет — 22. Пост: хранилище закрыто. *)
	BEGIN
		ASSERT((db.readers = NIL) & (db.writer = NIL), 22);
		IF db.store # NIL THEN db.store.Close; db.store := NIL END
	END Close;

	(* --- транзакции ------------------------------------------------------ *)

	PROCEDURE (db: Db) Begin* (write: BOOLEAN): Tx, NEW;
	(** Начать транзакцию над снимком последней фиксации.
		Пред: база не закрыта — 20; write -> нет открытой пишущей — 21.
		Пост: открытая транзакция; после ошибки фиксации у неё res # ok. *)
		VAR tx: Tx; s: Space;
	BEGIN
		ASSERT(db.store # NIL, 20); ASSERT(~write OR (db.writer = NIL), 21);
		NEW(tx); tx.db := db; tx.write := write; tx.open := TRUE; tx.meta := db.meta;
		tx.res := db.res;
		IF write THEN
			ReleasePending(db);
			NEW(s); s.tx := tx; tx.space := s; INC(tx.meta.txid);
			s.nfree0 := db.nfree; NEW(s.free0, MAX(1, db.nfree));
			IF db.nfree > 0 THEN Move(db.free, s.free0, db.nfree) END;
			tx.tree := KvTree.Open(db.pager, s, tx.meta.root);
			db.writer := tx
		ELSE
			tx.tree := KvTree.Open(db.pager, NIL, tx.meta.root);
			tx.next := db.readers; db.readers := tx
		END;
		IF tx.res = ok THEN tx.res := tx.tree.res END;
		RETURN tx
	END Begin;

	PROCEDURE Finish (tx: Tx);
		VAR db: Db; r: Tx;
	BEGIN
		db := tx.db; tx.open := FALSE;
		IF tx.write THEN db.writer := NIL
		ELSIF db.readers = tx THEN db.readers := tx.next
		ELSE
			r := db.readers;
			WHILE r.next # tx DO r := r.next END;
			r.next := tx.next
		END
	END Finish;

	PROCEDURE (tx: Tx) Abort*, NEW;
	(** Закрыть транзакцию без следа: изменения пишущей пропадают, читающая
		отпускает свой снимок. Повторный Abort ничего не делает. *)
		VAR s: Space; db: Db;
	BEGIN
		IF tx.open THEN
			IF tx.write THEN
				db := tx.db; s := tx.space;
				IF s.nfree0 > LEN(db.free^) THEN NEW(db.free, s.nfree0) END;
				Move(s.free0, db.free, s.nfree0); db.nfree := s.nfree0
			END;
			Finish(tx)
		END
	END Abort;

	PROCEDURE Fail (tx: Tx; res: INTEGER);
	BEGIN
		tx.res := res; tx.Abort
	END Fail;

	PROCEDURE (tx: Tx) Commit* (OUT res: INTEGER), NEW;
	(** Зафиксировать и закрыть транзакцию. У читающей — то же, что Abort.
		Пред: транзакция открыта — 20.
		Пост: res = ok — изменения на носителе и видны новым транзакциям;
		res # ok — транзакция откачена; ошибка записи или Sync оставляет базу
		неисправной: каждая новая транзакция получает тот же res, базу надо
		закрыть и открыть заново. *)
		VAR db: Db; s: Space; root, fid, fpages: INTEGER; m: KvPages.Meta; b: Bytes; p: Pending;
	BEGIN
		ASSERT(tx.open, 20);
		db := tx.db; res := tx.res;
		IF ~tx.write OR (res # ok) THEN tx.Abort; RETURN END;
		s := tx.space;
		KvTree.Commit(tx.tree, root); res := tx.tree.res;
		IF res = ok THEN WriteFree(tx, fid, fpages, res) END;
		IF res = ok THEN db.store.Sync(res) END;
		IF res = ok THEN
			m := tx.meta; m.root := root; m.free := fid;
			NEW(b, KvPages.size); KvPages.PutMeta(b^, m);
			db.store.Write(SHORT(m.txid MOD 2), b^, 0, res)
		END;
		IF res = ok THEN db.store.Sync(res) END;
		IF res = ok THEN
			db.meta := m; db.freePages := fpages;
			NEW(p); p.txid := m.txid; p.ids := s.freed; p.n := s.nfreed;
			IF p.n > 0 THEN p.next := db.pending; db.pending := p END;
			Finish(tx)
		ELSE
			db.res := res; Fail(tx, res)
		END
	END Commit;

	(* --- данные ---------------------------------------------------------- *)

	PROCEDURE (tx: Tx) Get* (IN key: ARRAY OF BYTE; OUT val: Bytes): BOOLEAN, NEW;
	(** Пред: транзакция открыта — 20. Пост: TRUE — key есть, val — копия
		значения; FALSE — ключа нет или ошибка среды (тогда res # ok). *)
		VAR found: BOOLEAN;
	BEGIN
		ASSERT(tx.open, 20);
		val := NIL; found := FALSE;
		IF tx.res = ok THEN found := KvTree.Get(tx.tree, key, val); tx.res := tx.tree.res END;
		RETURN found & (tx.res = ok)
	END Get;

	PROCEDURE (tx: Tx) Put* (IN key, val: ARRAY OF BYTE), NEW;
	(** Пред: транзакция открыта — 20; пишущая — 21; LEN(key) <= KvTree.maxKey — 22.
		Пост: key связан с копией val; после ошибки среды ничего не делает. *)
	BEGIN
		ASSERT(tx.open, 20); ASSERT(tx.write, 21); ASSERT(LEN(key) <= KvTree.maxKey, 22);
		IF tx.res = ok THEN KvTree.Put(tx.tree, key, val); tx.res := tx.tree.res END
	END Put;

	PROCEDURE (tx: Tx) Delete* (IN key: ARRAY OF BYTE): BOOLEAN, NEW;
	(** Пред: транзакция открыта — 20; пишущая — 21. Пост: TRUE — ключ был и удалён. *)
		VAR found: BOOLEAN;
	BEGIN
		ASSERT(tx.open, 20); ASSERT(tx.write, 21);
		found := FALSE;
		IF tx.res = ok THEN found := KvTree.Delete(tx.tree, key); tx.res := tx.tree.res END;
		RETURN found
	END Delete;

	PROCEDURE (tx: Tx) Seek* (IN from: ARRAY OF BYTE): Cursor, NEW;
	(** Пред: транзакция открыта — 20. Пост: курсор на первом ключе >= from.
		Курсор годен, пока транзакция открыта и не меняет данные (трап 22). *)
	BEGIN
		ASSERT(tx.open, 20);
		RETURN KvTree.Seek(tx.tree, from)
	END Seek;

	PROCEDURE (tx: Tx) First* (): Cursor, NEW;
	(** Пред: транзакция открыта — 20. Пост: курсор на наименьшем ключе. *)
	BEGIN
		ASSERT(tx.open, 20);
		RETURN KvTree.Edge(tx.tree, FALSE)
	END First;

	PROCEDURE (tx: Tx) Last* (): Cursor, NEW;
	(** Пред: транзакция открыта — 20. Пост: курсор на наибольшем ключе. *)
	BEGIN
		ASSERT(tx.open, 20);
		RETURN KvTree.Edge(tx.tree, TRUE)
	END Last;

	(* --- проверка целостности ------------------------------------------- *)

	PROCEDURE (tx: Tx) Check* (OUT msg: ARRAY OF CHAR): BOOLEAN, NEW;
	(** Проверить снимок, с которого начата транзакция: инварианты дерева
		(KvTree, 100–102) и учёт страниц (103): каждая страница 0 .. pages-1 —
		мета, страница дерева, список свободных или свободная, ровно одно.
		Пред: транзакция открыта — 20; пишущая ещё ничего не меняла — 21.
		Пост: TRUE — снимок цел; FALSE — msg называет нарушение. *)
		VAR seen: POINTER TO ARRAY OF BOOLEAN; t: KvTree.Tree; buf: Bytes;
			n, cnt, i, res: INTEGER; good: BOOLEAN;
	BEGIN
		ASSERT(tx.open, 20); ASSERT(~tx.write OR (tx.tree.mods = 0), 21);
		msg := ""; NEW(seen, tx.meta.pages);
		good := KvTree.Mark(seen, 0, 2, msg);
		t := KvTree.Open(tx.db.pager, NIL, tx.meta.root);
		IF good & (t.res = ok) THEN good := KvTree.CheckTree(t, tx.meta.root, seen, msg)
		ELSIF good THEN msg := "unreadable root"; good := FALSE
		END;
		IF good THEN
			KvTree.ReadRun(tx.db.pager, tx.meta.free, buf, n, res);
			IF (res # ok) OR (KvPages.Kind(buf^) # KvPages.free) THEN msg := "bad free list"; good := FALSE
			ELSE
				cnt := KvPages.Int(buf^, KvPages.hdr);
				good := (cnt >= 0) & (KvPages.hdr + 4 + 4 * cnt <= n * KvPages.size)
					& KvTree.Mark(seen, tx.meta.free, n, msg);
				i := 0;
				WHILE good & (i < cnt) DO
					good := KvTree.Mark(seen, KvPages.Int(buf^, KvPages.hdr + 4 + 4 * i), 1, msg); INC(i)
				END;
				IF ~good & (msg = "") THEN msg := "bad free list" END
			END
		END;
		i := 0;
		WHILE good & (i < tx.meta.pages) DO
			IF ~seen[i] THEN msg := "page leaked"; good := FALSE END;
			INC(i)
		END;
		RETURN good
	END Check;

	PROCEDURE (db: Db) Pages* (): INTEGER, NEW;
	(** Пост: страниц в последней фиксации (размер файла в страницах без хвоста
		несостоявшихся фиксаций). *)
	BEGIN
		RETURN db.meta.pages
	END Pages;

	(* --- строки ---------------------------------------------------------- *)

	PROCEDURE Str* (IN s: ARRAY OF CHAR): Bytes;
	(** Пред: s # "" — 20 (массивов длины 0 в языке нет).
		Пост: строка s до 0X в UTF-8, без завершающего нуля. *)
		VAR u: POINTER TO ARRAY OF SHORTCHAR; b: Bytes; i, n, r: INTEGER;
	BEGIN
		ASSERT(s # "", 20);
		NEW(u, 3 * LEN(s$) + 4); Utf.StringToUtf8(s, u^, r);
		n := 0; WHILE u[n] # 0X DO INC(n) END;
		NEW(b, n);
		FOR i := 0 TO n - 1 DO KvPages.PutByte(b^, i, ORD(u[i])) END;
		RETURN b
	END Str;

	PROCEDURE ToStr* (IN b: ARRAY OF BYTE; OUT s: ARRAY OF CHAR);
	(** Пост: s — байты b как UTF-8; длинное обрезано по LEN(s). *)
		VAR u: POINTER TO ARRAY OF SHORTCHAR; i, r: INTEGER;
	BEGIN
		NEW(u, LEN(b) + 1);
		FOR i := 0 TO LEN(b) - 1 DO u[i] := SHORT(CHR(KvPages.Byte(b, i))) END;
		u[LEN(b)] := 0X;
		Utf.Utf8ToString(u^, s, r)
	END ToStr;

END KvDb.
