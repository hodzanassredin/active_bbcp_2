MODULE KvTree;
(** B+-дерево хранилища Kv с копированием при записи. Устройство следует
	BoltDB (node.go, cursor.go): узлы правятся в памяти, а на страницы
	попадают при фиксации, целиком и на новые места (Kv/Docu/ru/Kv.md).

	Узел (Node) — лист с парами «ключ, значение» или ветвь с парами «ключ,
	страница потомка». Ключи узла возрастают. Ключ ветви keys[i] при i > 0 —
	нижняя граница ключей потомка i; ключи потомка i меньше keys[i+1]; keys[0]
	не ограничивает ничего.

	Чистый узел прочитан со страницы и не меняется: его делят кеш страниц,
	читатели и курсоры. Писатель, прежде чем менять узел, делает грязную
	копию (Dirty) и отдаёт старые страницы в освобождённые (Space.Release).
	Грязная ветвь держит грязных потомков в sub. При фиксации Rebalance
	сливает тощие узлы, Spill делит толстые и пишет всё грязное на страницы,
	выделенные Space.Take.

	Инварианты (проверяет Check):
		100	ключи узла строго возрастают; ключи потомка i ветви лежат в
			[keys[i], keys[i+1]) (для i = 0 — только верхняя граница)
		101	все листья на одной глубине; узел, кроме корня, не пуст
		102	страница узла имеет вид leaf или branch и читается целиком
		103	каждая страница базы — ровно одно из: мета, узел дерева (с
			продолжением), список свободных, свободная

	Внутри процедур модуля нет точек переключения: чтение и запись
	страниц синхронные. *)

	IMPORT KvPages, KvStore;

	CONST
		size = KvPages.size;
		hdr = KvPages.hdr;
		maxKey* = 1024;	(** наибольшая длина ключа, байт *)
		cacheSize = 1024;	(* узлов в кеше страниц *)
		mergeBelow = size DIV 4;	(* узел меньше этого сливается с соседом *)
		fillHalf = size DIV 2;	(* обычное разбиение — пополам *)
		fillTail = size * 9 DIV 10;	(* разбиение у правого края после дозаписи *)
		maxDepth = 32;

	TYPE
		Bytes* = KvPages.Bytes;
		BytesArr = POINTER TO ARRAY OF Bytes;
		Ints = POINTER TO ARRAY OF INTEGER;
		Nodes = POINTER TO ARRAY OF Node;

		Node* = POINTER TO RECORD
			leaf: BOOLEAN;
			n: INTEGER;	(* число элементов *)
			keys, vals: BytesArr;	(* vals — только у листа *)
			kids: Ints;	(* страницы потомков — только у ветви *)
			sub: Nodes;	(* грязные потомки ветви или NIL *)
			id: INTEGER;	(* страница, откуда прочитан; 0 — новый *)
			pages: INTEGER;	(* страниц у источника *)
			dirty: BOOLEAN;
			appended: BOOLEAN;	(* в конец добавлен элемент — дозапись *)
			next: Node	(* список кусков после Spill *)
		END;

		Space* = POINTER TO ABSTRACT RECORD END;

		Pager* = POINTER TO RECORD
			store-: KvStore.Store;
			cache: ARRAY cacheSize OF Node;
			buf: Bytes
		END;

		Tree* = POINTER TO RECORD
			pager-: Pager;
			space: Space;
			root-: Node;
			res*: INTEGER;	(** первая ошибка среды; после неё операции ничего не делают *)
			mods-: INTEGER	(** число изменений: курсор проверяет, что дерево не менялось *)
		END;

		Frame = RECORD node: Node; i: INTEGER END;

		Cursor* = POINTER TO RECORD
			tree: Tree;
			mods: INTEGER;
			d: INTEGER;	(* глубина стека; -1 — курсор за краем *)
			st: ARRAY maxDepth OF Frame
		END;

	PROCEDURE (s: Space) Take- (n: INTEGER): INTEGER, NEW, ABSTRACT;
	(** Пост: первая из n подряд идущих страниц, которые никто не видит. *)

	PROCEDURE (s: Space) Release- (id, n: INTEGER), NEW, ABSTRACT;
	(** Страницы id .. id+n-1 больше не нужны новому состоянию дерева. *)

	(* --- страницы и кеш --------------------------------------------------- *)

	PROCEDURE NewPager* (s: KvStore.Store): Pager;
	(** Пред: s # NIL — 20. *)
		VAR p: Pager;
	BEGIN
		ASSERT(s # NIL, 20);
		NEW(p); p.store := s; NEW(p.buf, 4 * size);
		RETURN p
	END NewPager;

	PROCEDURE Invalidate* (p: Pager; id: INTEGER);
	(** Пост: в кеше нет узла страницы id. Зовётся перед записью страницы. *)
		VAR h: INTEGER;
	BEGIN
		h := id MOD cacheSize;
		IF (p.cache[h] # NIL) & (p.cache[h].id = id) THEN p.cache[h] := NIL END
	END Invalidate;

	PROCEDURE ReadRun* (p: Pager; id: INTEGER; OUT buf: Bytes; OUT n, res: INTEGER);
	(** Прочитать страницу id и её продолжение. Пост: res = ok — buf[0 .. n*size)
		содержит n страниц; буфер общий, он живёт до следующего чтения. *)
		VAR i: INTEGER; b: Bytes;
	BEGIN
		buf := NIL; n := 0;
		p.store.Read(id, p.buf^, 0, res);
		IF res = KvStore.ok THEN
			n := KvPages.Over(p.buf^) + 1;
			IF (n < 1) OR (n > 1000000) THEN res := KvStore.corrupt; n := 0
			ELSE
				IF n * size > LEN(p.buf^) THEN
					NEW(b, n * size);
					FOR i := 0 TO size - 1 DO b[i] := p.buf[i] END;
					p.buf := b
				END;
				i := 1;
				WHILE (i < n) & (res = KvStore.ok) DO
					p.store.Read(id + i, p.buf^, i * size, res); INC(i)
				END;
				IF res = KvStore.ok THEN buf := p.buf END
			END
		END
	END ReadRun;

	PROCEDURE Decode (IN b: ARRAY OF BYTE; len: INTEGER; OUT nd: Node): BOOLEAN;
		VAR kind, i, pos, kl, vl: INTEGER; ok: BOOLEAN;
	BEGIN
		kind := KvPages.Kind(b); NEW(nd);
		nd.leaf := kind = KvPages.leaf; nd.n := KvPages.Count(b);
		ok := (kind = KvPages.leaf) OR (kind = KvPages.branch);
		IF ok THEN
			NEW(nd.keys, MAX(nd.n, 4));
			IF nd.leaf THEN NEW(nd.vals, LEN(nd.keys)) ELSE NEW(nd.kids, LEN(nd.keys)) END;
			pos := hdr; i := 0;
			WHILE ok & (i < nd.n) DO
				ok := pos + 6 <= len;
				IF ok THEN
					kl := KvPages.Short(b, pos); vl := KvPages.Int(b, pos + 2);
					IF nd.leaf THEN
						ok := (kl > 0) & (vl > 0) & (vl <= len) & (pos + 6 + kl + vl <= len)
					ELSE
						ok := (kl > 0) & (pos + 6 + kl <= len); nd.kids[i] := vl; vl := 0
					END;
					IF ok THEN
						nd.keys[i] := KvPages.Copy(b, pos + 6, kl);
						IF nd.leaf THEN nd.vals[i] := KvPages.Copy(b, pos + 6 + kl, vl) END;
						pos := pos + 6 + kl + vl
					END
				END;
				INC(i)
			END
		END;
		RETURN ok
	END Decode;

	PROCEDURE Load* (t: Tree; id: INTEGER): Node;
	(** Чистый узел страницы id из кеша или с носителя.
		Пост: NIL — ошибка, она записана в t.res. *)
		VAR p: Pager; nd: Node; buf: Bytes; n, h, res: INTEGER;
	BEGIN
		p := t.pager; h := id MOD cacheSize; nd := p.cache[h];
		IF (nd = NIL) OR (nd.id # id) THEN
			ReadRun(p, id, buf, n, res);
			IF res # KvStore.ok THEN t.res := res; nd := NIL
			ELSIF ~Decode(buf^, n * size, nd) THEN t.res := KvStore.corrupt; nd := NIL
			ELSE nd.id := id; nd.pages := n; p.cache[h] := nd
			END
		END;
		RETURN nd
	END Load;

	(* --- размеры и поиск -------------------------------------------------- *)

	PROCEDURE ElemSize (nd: Node; i: INTEGER): INTEGER;
	BEGIN
		IF nd.leaf THEN RETURN 6 + LEN(nd.keys[i]^) + LEN(nd.vals[i]^)
		ELSE RETURN 6 + LEN(nd.keys[i]^)
		END
	END ElemSize;

	PROCEDURE NodeSize (nd: Node): INTEGER;
		VAR i, s: INTEGER;
	BEGIN
		s := hdr;
		FOR i := 0 TO nd.n - 1 DO INC(s, ElemSize(nd, i)) END;
		RETURN s
	END NodeSize;

	PROCEDURE Lower (nd: Node; IN key: ARRAY OF BYTE; len: INTEGER): INTEGER;
		(* первый i, где keys[i] >= key; nd.n, если такого нет *)
		VAR lo, hi, m: INTEGER;
	BEGIN
		lo := 0; hi := nd.n;
		WHILE lo < hi DO
			m := (lo + hi) DIV 2;
			IF KvPages.Compare(nd.keys[m]^, LEN(nd.keys[m]^), key, len) < 0 THEN lo := m + 1 ELSE hi := m END
		END;
		RETURN lo
	END Lower;

	PROCEDURE ChildIndex (nd: Node; IN key: ARRAY OF BYTE; len: INTEGER): INTEGER;
		(* последний i, где keys[i] <= key, или 0 *)
		VAR i: INTEGER;
	BEGIN
		i := Lower(nd, key, len);
		IF (i < nd.n) & (KvPages.Compare(nd.keys[i]^, LEN(nd.keys[i]^), key, len) = 0) THEN RETURN i
		ELSIF i > 0 THEN RETURN i - 1
		ELSE RETURN 0
		END
	END ChildIndex;

	PROCEDURE Child (t: Tree; nd: Node; i: INTEGER): Node;
	BEGIN
		IF (nd.sub # NIL) & (nd.sub[i] # NIL) THEN RETURN nd.sub[i]
		ELSE RETURN Load(t, nd.kids[i])
		END
	END Child;

	(* --- дерево ---------------------------------------------------------- *)

	PROCEDURE Open* (p: Pager; s: Space; root: INTEGER): Tree;
	(** Дерево с корнем на странице root. s = NIL — только чтение.
		Пост: ошибка чтения корня — в res. *)
		VAR t: Tree;
	BEGIN
		NEW(t); t.pager := p; t.space := s; t.root := Load(t, root);
		RETURN t
	END Open;

	PROCEDURE Get* (t: Tree; IN key: ARRAY OF BYTE; OUT val: Bytes): BOOLEAN;
	(** Пост: TRUE — ключ есть, val — копия значения. *)
		VAR nd: Node; i, len: INTEGER;
	BEGIN
		val := NIL; nd := t.root; len := LEN(key);
		WHILE (nd # NIL) & ~nd.leaf DO nd := Child(t, nd, ChildIndex(nd, key, len)) END;
		IF nd # NIL THEN
			i := Lower(nd, key, len);
			IF (i < nd.n) & (KvPages.Compare(nd.keys[i]^, LEN(nd.keys[i]^), key, len) = 0) THEN
				val := KvPages.Copy(nd.vals[i]^, 0, LEN(nd.vals[i]^))
			END
		END;
		RETURN val # NIL
	END Get;

	PROCEDURE Grow (nd: Node; n: INTEGER);
		VAR k: BytesArr; v: BytesArr; c: Ints; s: Nodes; i, m: INTEGER;
	BEGIN
		IF n > LEN(nd.keys) THEN
			m := MAX(n, 2 * LEN(nd.keys));
			NEW(k, m); FOR i := 0 TO nd.n - 1 DO k[i] := nd.keys[i] END; nd.keys := k;
			IF nd.leaf THEN
				NEW(v, m); FOR i := 0 TO nd.n - 1 DO v[i] := nd.vals[i] END; nd.vals := v
			ELSE
				NEW(c, m); FOR i := 0 TO nd.n - 1 DO c[i] := nd.kids[i] END; nd.kids := c;
				NEW(s, m); FOR i := 0 TO nd.n - 1 DO s[i] := nd.sub[i] END; nd.sub := s
			END
		END
	END Grow;

	PROCEDURE NewNode (leaf: BOOLEAN; cap: INTEGER): Node;
		VAR nd: Node;
	BEGIN
		NEW(nd); nd.leaf := leaf; nd.dirty := TRUE; cap := MAX(cap, 4);
		NEW(nd.keys, cap);
		IF leaf THEN NEW(nd.vals, cap) ELSE NEW(nd.kids, cap); NEW(nd.sub, cap) END;
		RETURN nd
	END NewNode;

	PROCEDURE Dirty (t: Tree; nd: Node): Node;
		(* грязная копия чистого узла; страницы источника освобождаются *)
		VAR m: Node; i: INTEGER;
	BEGIN
		IF nd.dirty THEN RETURN nd END;
		m := NewNode(nd.leaf, nd.n + 1); m.n := nd.n;
		FOR i := 0 TO nd.n - 1 DO
			m.keys[i] := nd.keys[i];
			IF nd.leaf THEN m.vals[i] := nd.vals[i] ELSE m.kids[i] := nd.kids[i] END
		END;
		t.space.Release(nd.id, nd.pages);
		RETURN m
	END Dirty;

	PROCEDURE Insert (nd: Node; i: INTEGER; key, val: Bytes; kid: INTEGER; sub: Node);
		VAR j: INTEGER;
	BEGIN
		Grow(nd, nd.n + 1);
		FOR j := nd.n TO i + 1 BY -1 DO
			nd.keys[j] := nd.keys[j - 1];
			IF nd.leaf THEN nd.vals[j] := nd.vals[j - 1]
			ELSE nd.kids[j] := nd.kids[j - 1]; nd.sub[j] := nd.sub[j - 1]
			END
		END;
		nd.keys[i] := key;
		IF nd.leaf THEN nd.vals[i] := val ELSE nd.kids[i] := kid; nd.sub[i] := sub END;
		INC(nd.n)
	END Insert;

	PROCEDURE Remove (nd: Node; i: INTEGER);
		VAR j: INTEGER;
	BEGIN
		FOR j := i TO nd.n - 2 DO
			nd.keys[j] := nd.keys[j + 1];
			IF nd.leaf THEN nd.vals[j] := nd.vals[j + 1]
			ELSE nd.kids[j] := nd.kids[j + 1]; nd.sub[j] := nd.sub[j + 1]
			END
		END;
		DEC(nd.n); nd.keys[nd.n] := NIL;
		IF nd.leaf THEN nd.vals[nd.n] := NIL ELSE nd.sub[nd.n] := NIL END
	END Remove;

	PROCEDURE DirtyPath (t: Tree; IN key: ARRAY OF BYTE; VAR path: ARRAY OF Frame; OUT d: INTEGER;
		OUT edge: BOOLEAN): Node;
		(* грязный путь от корня к листу ключа; пост: лист или NIL при ошибке;
			path[0 .. d-1] — ветви пути и номера потомков; edge — лист у правого
			края дерева *)
		VAR nd, c: Node; i: INTEGER;
	BEGIN
		edge := TRUE; d := 0;
		IF ~t.root.dirty THEN t.root := Dirty(t, t.root) END;
		nd := t.root;
		WHILE (nd # NIL) & ~nd.leaf DO
			i := ChildIndex(nd, key, LEN(key)); c := Child(t, nd, i);
			IF c # NIL THEN
				IF ~c.dirty THEN c := Dirty(t, c); nd.sub[i] := c END
			END;
			IF (i = 0) & (KvPages.Compare(key, LEN(key), nd.keys[0]^, LEN(nd.keys[0]^)) < 0) THEN
				nd.keys[0] := KvPages.Copy(key, 0, LEN(key))	(* keys[0] не больше ключей потомка 0: поиск двоичный *)
			END;
			ASSERT(d < maxDepth, 100);
			path[d].node := nd; path[d].i := i; INC(d);
			edge := edge & (i = nd.n - 1); nd := c
		END;
		RETURN nd
	END DirtyPath;

	PROCEDURE Delete* (t: Tree; IN key: ARRAY OF BYTE): BOOLEAN;
	(** Пред: t пишущее — 20. Пост: TRUE — ключ был и удалён. *)
		VAR nd: Node; i, d: INTEGER; v: Bytes; edge: BOOLEAN; path: ARRAY maxDepth OF Frame;
	BEGIN
		ASSERT(t.space # NIL, 20);
		IF (t.res = KvStore.ok) & (LEN(key) <= maxKey) & Get(t, key, v) THEN
			nd := DirtyPath(t, key, path, d, edge);
			IF nd # NIL THEN
				i := Lower(nd, key, LEN(key)); Remove(nd, i); INC(t.mods);
				RETURN TRUE
			END
		END;
		RETURN FALSE
	END Delete;

	(* --- фиксация: слияние и разбиение ---------------------------------- *)

	PROCEDURE Append (t: Tree; dst, src: Node; low: Bytes);
		(* перенести элементы src в конец dst; low — граница src из родителя *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO src.n - 1 DO
			IF dst.leaf THEN Insert(dst, dst.n, src.keys[i], src.vals[i], 0, NIL)
			ELSIF i = 0 THEN Insert(dst, dst.n, low, NIL, src.kids[0], src.sub[0])
			ELSE Insert(dst, dst.n, src.keys[i], NIL, src.kids[i], src.sub[i])
			END
		END;
		dst.appended := dst.appended OR src.appended
	END Append;

	PROCEDURE Rebalance (t: Tree; nd: Node);
		(* сливает грязных потомков ветви nd, которые меньше mergeBelow *)
		VAR i: INTEGER; c, s: Node;
	BEGIN
		FOR i := 0 TO nd.n - 1 DO
			IF (nd.sub[i] # NIL) & ~nd.sub[i].leaf THEN Rebalance(t, nd.sub[i]) END
		END;
		i := 0;
		WHILE (i < nd.n) & (t.res = KvStore.ok) DO
			c := nd.sub[i];
			IF (c = NIL) OR (NodeSize(c) >= mergeBelow) THEN INC(i)
			ELSIF c.n = 0 THEN Remove(nd, i)
			ELSIF i + 1 < nd.n THEN	(* правого соседа — в c *)
				s := Child(t, nd, i + 1);
				IF s # NIL THEN
					s := Dirty(t, s); Append(t, c, s, nd.keys[i + 1]); Remove(nd, i + 1)
				END;
				INC(i)
			ELSIF i > 0 THEN	(* c — в левого соседа *)
				s := Child(t, nd, i - 1);
				IF s # NIL THEN
					s := Dirty(t, s); nd.sub[i - 1] := s; Append(t, s, c, nd.keys[i]); Remove(nd, i)
				END
			ELSE INC(i)
			END
		END
	END Rebalance;

	PROCEDURE Move (IN src: ARRAY OF BYTE; sbeg: INTEGER; VAR dst: ARRAY OF BYTE; dbeg, len: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO len - 1 DO dst[dbeg + i] := src[sbeg + i] END
	END Move;

	PROCEDURE WriteNode (t: Tree; nd: Node);
		(* закодировать узел, выделить страницы и записать *)
		VAR b: Bytes; pages, sz, i, pos, kl, res: INTEGER; p: Pager;
	BEGIN
		p := t.pager; sz := NodeSize(nd); pages := (sz + size - 1) DIV size;
		NEW(b, pages * size);
		IF nd.leaf THEN KvPages.PutHeader(b^, KvPages.leaf, nd.n, pages - 1)
		ELSE KvPages.PutHeader(b^, KvPages.branch, nd.n, pages - 1)
		END;
		pos := hdr;
		FOR i := 0 TO nd.n - 1 DO
			kl := LEN(nd.keys[i]^);
			KvPages.PutShort(b^, pos, kl);
			IF nd.leaf THEN KvPages.PutInt(b^, pos + 2, LEN(nd.vals[i]^))
			ELSE KvPages.PutInt(b^, pos + 2, nd.kids[i])
			END;
			Move(nd.keys[i]^, 0, b^, pos + 6, kl); INC(pos, 6 + kl);
			IF nd.leaf THEN
				Move(nd.vals[i]^, 0, b^, pos, LEN(nd.vals[i]^)); INC(pos, LEN(nd.vals[i]^))
			END
		END;
		nd.id := t.space.Take(pages); nd.pages := pages; nd.dirty := FALSE; nd.sub := NIL;
		i := 0; res := KvStore.ok;
		WHILE (i < pages) & (res = KvStore.ok) DO
			Invalidate(p, nd.id + i); p.store.Write(nd.id + i, b^, i * size, res); INC(i)
		END;
		IF res # KvStore.ok THEN t.res := res END
	END WriteNode;

	PROCEDURE Cut (nd: Node; from, to: INTEGER): Node;
		(* новый грязный узел из элементов nd[from .. to) *)
		VAR m: Node; i: INTEGER;
	BEGIN
		m := NewNode(nd.leaf, to - from);
		FOR i := from TO to - 1 DO
			IF nd.leaf THEN Insert(m, m.n, nd.keys[i], nd.vals[i], 0, NIL)
			ELSIF nd.sub # NIL THEN Insert(m, m.n, nd.keys[i], NIL, nd.kids[i], nd.sub[i])
			ELSE Insert(m, m.n, nd.keys[i], NIL, nd.kids[i], NIL)
			END
		END;
		RETURN m
	END Cut;

	PROCEDURE Split (nd: Node): Node;
		(* список кусков nd, каждый не больше страницы, кроме узла из
			одного элемента. Пока остаток больше страницы, от него
			отрезается начало размером до fill. После дозаписи в конец
			fill — почти страница: левые куски остаются почти полными. *)
		VAR first, last, m: Node; i, beg, s, e, rest, fill: INTEGER;
	BEGIN
		IF nd.appended THEN fill := fillTail ELSE fill := fillHalf END;
		rest := NodeSize(nd); first := NIL; last := NIL; beg := 0;
		WHILE (rest > size) & (nd.n - beg > 1) DO
			i := beg; s := hdr;
			REPEAT e := ElemSize(nd, i); INC(s, e); INC(i)
			UNTIL (i >= nd.n - 1) OR (s + ElemSize(nd, i) > fill);
			m := Cut(nd, beg, i);
			IF first = NIL THEN first := m ELSE last.next := m END;
			last := m; rest := rest - (s - hdr); beg := i
		END;
		IF beg = 0 THEN m := nd ELSE m := Cut(nd, beg, nd.n) END;
		m.next := NIL;
		IF first = NIL THEN first := m ELSE last.next := m END;
		RETURN first
	END Split;

	PROCEDURE SplitUp (t: Tree; VAR path: ARRAY OF Frame; d: INTEGER; nd: Node);
		(* Узел больше двух страниц делится сразу, не дожидаясь фиксации:
			иначе каждая вставка в большой транзакции сдвигала бы тысячи
			элементов одного листа. Куски заменяют узел в родителе; родитель,
			ставший слишком большим, делится так же; корень — новым корнем. *)
		VAR pieces, c, root, parent: Node; i: INTEGER;
	BEGIN
		WHILE (nd # NIL) & (nd.n > 1) & (NodeSize(nd) > 2 * size) DO
			IF ~nd.leaf THEN nd.appended := (nd.sub[nd.n - 1] # NIL) & nd.sub[nd.n - 1].appended END;
			pieces := Split(nd); c := pieces;
			WHILE c.next # NIL DO c := c.next END;
			c.appended := nd.appended;
			IF d = 0 THEN
				root := NewNode(FALSE, 4); c := pieces;
				WHILE c # NIL DO Insert(root, root.n, c.keys[0], NIL, 0, c); c := c.next END;
				t.root := root; nd := root
			ELSE
				DEC(d); parent := path[d].node; i := path[d].i;
				parent.sub[i] := pieces; c := pieces.next;
				WHILE c # NIL DO INC(i); Insert(parent, i, c.keys[0], NIL, 0, c); c := c.next END;
				nd := parent
			END
		END
	END SplitUp;

	PROCEDURE Put* (t: Tree; IN key, val: ARRAY OF BYTE);
	(** Пред: t пишущее (есть Space) — 20; LEN(key) <= maxKey — 21.
		Пост: ключ key связан с копией val. *)
		VAR nd: Node; i, d: INTEGER; v: Bytes; edge: BOOLEAN; path: ARRAY maxDepth OF Frame;
	BEGIN
		ASSERT(t.space # NIL, 20); ASSERT(LEN(key) <= maxKey, 21);
		IF t.res = KvStore.ok THEN
			nd := DirtyPath(t, key, path, d, edge);
			IF nd # NIL THEN
				i := Lower(nd, key, LEN(key)); v := KvPages.Copy(val, 0, LEN(val));
				IF (i < nd.n) & (KvPages.Compare(nd.keys[i]^, LEN(nd.keys[i]^), key, LEN(key)) = 0) THEN
					nd.vals[i] := v
				ELSE
					Insert(nd, i, KvPages.Copy(key, 0, LEN(key)), v, 0, NIL);
					IF edge & (i = nd.n - 1) THEN nd.appended := TRUE END
				END;
				SplitUp(t, path, d, nd); INC(t.mods)
			END
		END
	END Put;

	PROCEDURE Spill (t: Tree; nd: Node): Node;
		(* записать грязное поддерево nd; результат — список записанных кусков *)
		VAR i, j: INTEGER; c, p, first: Node; k: Node;
	BEGIN
		IF ~nd.leaf THEN
			k := NewNode(FALSE, nd.n);
			FOR i := 0 TO nd.n - 1 DO
				IF nd.sub[i] = NIL THEN Insert(k, k.n, nd.keys[i], NIL, nd.kids[i], NIL)
				ELSE
					first := Spill(t, nd.sub[i]); c := first; j := 0;
					WHILE c # NIL DO
						(* граница потомка i > 0 остаётся прежней; у потомка 0 и у
							новых кусков граница — их первый ключ, иначе прежняя
							граница потомка 0 могла бы оказаться больше ключей,
							вставленных левее неё *)
						IF (j = 0) & (i > 0) THEN Insert(k, k.n, nd.keys[i], NIL, c.id, NIL)
						ELSE Insert(k, k.n, c.keys[0], NIL, c.id, NIL)
						END;
						c := c.next; INC(j)
					END
				END
			END;
			k.appended := (nd.n > 0) & (nd.sub[nd.n - 1] # NIL) & nd.sub[nd.n - 1].appended;
			nd := k
		END;
		first := Split(nd); p := first;
		WHILE (p # NIL) & (t.res = KvStore.ok) DO WriteNode(t, p); p := p.next END;
		RETURN first
	END Spill;

	PROCEDURE Commit* (t: Tree; OUT root: INTEGER);
	(** Пред: t пишущее — 20. Пост: t.res = ok — всё грязное записано на
		новые страницы, root — страница корня; дерево больше не правится. *)
		VAR nd, pieces, c: Node;
	BEGIN
		ASSERT(t.space # NIL, 20);
		root := 0; nd := t.root;
		IF (t.res = KvStore.ok) & nd.dirty THEN
			IF ~nd.leaf THEN
				Rebalance(t, nd);
				WHILE (t.res = KvStore.ok) & ~nd.leaf & (nd.n = 1) DO	(* корень с одним потомком уходит *)
					c := Child(t, nd, 0);
					IF c # NIL THEN nd := Dirty(t, c) END
				END;
				IF ~nd.leaf & (nd.n = 0) THEN nd := NewNode(TRUE, 4) END
			END;
			pieces := NIL;
			IF t.res = KvStore.ok THEN pieces := Spill(t, nd) END;
			WHILE (t.res = KvStore.ok) & (pieces.next # NIL) DO	(* корень разбит: новый корень *)
				nd := NewNode(FALSE, 4); c := pieces;
				WHILE c # NIL DO Insert(nd, nd.n, c.keys[0], NIL, c.id, NIL); c := c.next END;
				pieces := Spill(t, nd)
			END;
			IF t.res = KvStore.ok THEN root := pieces.id END
		ELSIF t.res = KvStore.ok THEN root := nd.id
		END;
		t.space := NIL
	END Commit;

	(* --- курсор ---------------------------------------------------------- *)

	PROCEDURE Settle (c: Cursor; forward: BOOLEAN);
		(* довести курсор до элемента листа, обходя пустые узлы *)
		VAR f: Frame; ch: Node;
	BEGIN
		LOOP
			IF c.d < 0 THEN EXIT END;
			f := c.st[c.d];
			IF (f.i < 0) OR (f.i >= f.node.n) THEN
				DEC(c.d);
				IF c.d < 0 THEN EXIT END;
				IF forward THEN INC(c.st[c.d].i) ELSE DEC(c.st[c.d].i) END
			ELSIF f.node.leaf THEN EXIT
			ELSE
				ch := Child(c.tree, f.node, f.i);
				IF ch = NIL THEN c.d := -1; EXIT END;
				INC(c.d); ASSERT(c.d < maxDepth, 100);
				c.st[c.d].node := ch;
				IF forward THEN c.st[c.d].i := 0 ELSE c.st[c.d].i := ch.n - 1 END
			END
		END
	END Settle;

	PROCEDURE NewCursor (t: Tree): Cursor;
		VAR c: Cursor;
	BEGIN
		NEW(c); c.tree := t; c.mods := t.mods; c.d := -1;
		IF t.root # NIL THEN c.d := 0; c.st[0].node := t.root END;
		RETURN c
	END NewCursor;

	PROCEDURE Seek* (t: Tree; IN from: ARRAY OF BYTE): Cursor;
	(** Пост: курсор на первом ключе >= from или за краем. *)
		VAR c: Cursor; nd: Node; len: INTEGER;
	BEGIN
		c := NewCursor(t); len := LEN(from);
		IF c.d = 0 THEN
			nd := t.root;
			WHILE (nd # NIL) & ~nd.leaf DO
				c.st[c.d].i := ChildIndex(nd, from, len);
				nd := Child(t, nd, c.st[c.d].i);
				IF nd # NIL THEN INC(c.d); c.st[c.d].node := nd ELSE c.d := -1 END
			END;
			IF nd # NIL THEN c.st[c.d].i := Lower(nd, from, len); Settle(c, TRUE) END
		END;
		RETURN c
	END Seek;

	PROCEDURE Edge* (t: Tree; last: BOOLEAN): Cursor;
	(** Пост: курсор на первом (last = FALSE) или последнем ключе. *)
		VAR c: Cursor;
	BEGIN
		c := NewCursor(t);
		IF c.d = 0 THEN
			IF last THEN c.st[0].i := t.root.n - 1 ELSE c.st[0].i := 0 END;
			Settle(c, ~last)
		END;
		RETURN c
	END Edge;

	PROCEDURE (c: Cursor) Valid* (): BOOLEAN, NEW;
	(** Пред: дерево не менялось после создания курсора — 22. *)
	BEGIN
		ASSERT(c.mods = c.tree.mods, 22);
		RETURN c.d >= 0
	END Valid;

	PROCEDURE (c: Cursor) Next*, NEW;
	(** Пред: Valid() — 20; дерево не менялось — 22. *)
	BEGIN
		ASSERT(c.mods = c.tree.mods, 22); ASSERT(c.d >= 0, 20);
		INC(c.st[c.d].i); Settle(c, TRUE)
	END Next;

	PROCEDURE (c: Cursor) Prev*, NEW;
	(** Пред: Valid() — 20; дерево не менялось — 22. *)
	BEGIN
		ASSERT(c.mods = c.tree.mods, 22); ASSERT(c.d >= 0, 20);
		DEC(c.st[c.d].i); Settle(c, FALSE)
	END Prev;

	PROCEDURE (c: Cursor) Key* (): Bytes, NEW;
	(** Пред: Valid() — 20; дерево не менялось — 22. Пост: копия ключа. *)
		VAR f: Frame;
	BEGIN
		ASSERT(c.mods = c.tree.mods, 22); ASSERT(c.d >= 0, 20);
		f := c.st[c.d];
		RETURN KvPages.Copy(f.node.keys[f.i]^, 0, LEN(f.node.keys[f.i]^))
	END Key;

	PROCEDURE (c: Cursor) Val* (): Bytes, NEW;
	(** Пред: Valid() — 20; дерево не менялось — 22. Пост: копия значения. *)
		VAR f: Frame;
	BEGIN
		ASSERT(c.mods = c.tree.mods, 22); ASSERT(c.d >= 0, 20);
		f := c.st[c.d];
		RETURN KvPages.Copy(f.node.vals[f.i]^, 0, LEN(f.node.vals[f.i]^))
	END Val;

	(* --- проверка целостности ------------------------------------------- *)

	PROCEDURE Mark* (VAR seen: ARRAY OF BOOLEAN; id, n: INTEGER; VAR msg: ARRAY OF CHAR): BOOLEAN;
	(** Отметить страницы id .. id+n-1 как занятые. Пост: FALSE — страница
		за концом или уже отмечена (нарушение 103), msg — причина. *)
		VAR i: INTEGER;
	BEGIN
		FOR i := id TO id + n - 1 DO
			IF (i < 0) OR (i >= LEN(seen)) THEN msg := "page beyond end"; RETURN FALSE
			ELSIF seen[i] THEN msg := "page used twice"; RETURN FALSE
			END;
			seen[i] := TRUE
		END;
		RETURN TRUE
	END Mark;

	PROCEDURE CheckNode (t: Tree; id, depth: INTEGER; lo, hi: Bytes; VAR leafDepth: INTEGER;
		VAR seen: ARRAY OF BOOLEAN; VAR msg: ARRAY OF CHAR): BOOLEAN;
		VAR nd: Node; i: INTEGER; ok: BOOLEAN; h: Bytes;
	BEGIN
		nd := Load(t, id);
		IF nd = NIL THEN msg := "unreadable node page"; RETURN FALSE END;
		IF ~Mark(seen, id, nd.pages, msg) THEN RETURN FALSE END;
		IF (nd.n = 0) & (depth > 0) THEN msg := "empty non-root node"; RETURN FALSE END;
		ok := TRUE; i := 0;
		WHILE ok & (i < nd.n) DO
			IF (i > 0) & (KvPages.Compare(nd.keys[i - 1]^, LEN(nd.keys[i - 1]^), nd.keys[i]^, LEN(nd.keys[i]^)) >= 0) THEN
				msg := "keys out of order"; ok := FALSE
			ELSIF ((i > 0) OR nd.leaf) & (lo # NIL) & (KvPages.Compare(nd.keys[i]^, LEN(nd.keys[i]^), lo^, LEN(lo^)) < 0) THEN
				msg := "key below bound"; ok := FALSE
			ELSIF ((i > 0) OR nd.leaf) & (hi # NIL) & (KvPages.Compare(nd.keys[i]^, LEN(nd.keys[i]^), hi^, LEN(hi^)) >= 0) THEN
				msg := "key above bound"; ok := FALSE
			END;
			INC(i)
		END;
		IF ok & nd.leaf THEN
			IF leafDepth < 0 THEN leafDepth := depth
			ELSIF leafDepth # depth THEN msg := "leaves at different depth"; ok := FALSE
			END
		ELSIF ok THEN
			i := 0;
			WHILE ok & (i < nd.n) DO
				IF i + 1 < nd.n THEN h := nd.keys[i + 1] ELSE h := hi END;
				IF i = 0 THEN ok := CheckNode(t, nd.kids[i], depth + 1, lo, h, leafDepth, seen, msg)
				ELSE ok := CheckNode(t, nd.kids[i], depth + 1, nd.keys[i], h, leafDepth, seen, msg)
				END;
				INC(i)
			END
		END;
		RETURN ok
	END CheckNode;

	PROCEDURE CheckTree* (t: Tree; root: INTEGER; VAR seen: ARRAY OF BOOLEAN; OUT msg: ARRAY OF CHAR): BOOLEAN;
	(** Проверить инварианты 100–103 дерева с корнем root и отметить его
		страницы в seen. Пост: FALSE — msg называет нарушение. *)
		VAR leafDepth: INTEGER;
	BEGIN
		msg := ""; leafDepth := -1;
		RETURN CheckNode(t, root, 0, NIL, NIL, leafDepth, seen, msg)
	END CheckTree;

END KvTree.
