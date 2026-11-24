MODULE CasTree;
(** Большой объект как дерево Меркла в хранилище CasStore.

	Лист — кусок потока (CasChunk). Узел — блок: байт уровня (1 — дети
	листья, 2 — дети узлы уровня 1 и т. д.), затем Id детей подряд, не
	больше fan. Корень всегда узел, поэтому пустой поток — узел уровня 1 без
	детей. Корень однозначно определяет содержимое: Id узла — хеш списка Id
	детей. *)

	IMPORT Files, CasHash, CasStore, CasChunk;

	CONST
		fan* = 64;	(** детей у узла *)
		maxLevel = 16;

	TYPE
		Ids = POINTER TO ARRAY OF CasHash.Id;

	PROCEDURE Append (VAR ids: Ids; VAR n: INTEGER; IN id: CasHash.Id);
		VAR new: Ids; i: INTEGER;
	BEGIN
		IF n = LEN(ids) THEN
			NEW(new, 2 * n);
			FOR i := 0 TO n - 1 DO new[i] := ids[i] END;
			ids := new
		END;
		ids[n] := id; INC(n)
	END Append;

	PROCEDURE PutNode (s: CasStore.Store; level: INTEGER; ids: Ids; beg, n: INTEGER;
		OUT id: CasHash.Id; OUT res: INTEGER);
		VAR b: ARRAY 1 + fan * CasHash.size OF BYTE; i, j: INTEGER;
	BEGIN
		b[0] := SHORT(SHORT(level));
		FOR i := 0 TO n - 1 DO
			FOR j := 0 TO CasHash.size - 1 DO b[1 + i * CasHash.size + j] := ids[beg + i][j] END
		END;
		s.Put(b, 1 + n * CasHash.size, id, res)
	END PutNode;

	PROCEDURE PutStream* (s: CasStore.Store; r: Files.Reader; OUT root: CasHash.Id; OUT res: INTEGER);
	(** Записать поток от текущей позиции r до конца файла.
		Пред: s # NIL — 20; r # NIL — 21.
		Пост: res = CasStore.ok: root — корень дерева; GetStream(root) даёт те же байты.
		Пост: res # ok — запись блока не удалась, root пуст. *)
		VAR p: CasChunk.Params; buf: POINTER TO ARRAY OF BYTE; ids: Ids;
			have, left, cut, n, k, i, level: INTEGER; id: CasHash.Id;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(r # NIL, 21);
		CasChunk.Default(p);
		NEW(buf, 2 * p.max); NEW(ids, 16); n := 0; have := 0; res := CasStore.ok;
		left := r.Base().Length() - r.Pos();
		WHILE (res = CasStore.ok) & ((have > 0) OR (left > 0)) DO
			IF (have < p.max) & (left > 0) THEN	(* дочитать буфер до max байт и больше *)
				k := LEN(buf) - have; IF k > left THEN k := left END;
				r.ReadBytes(buf, have, k); INC(have, k); DEC(left, k)
			END;
			cut := CasChunk.Cut(buf, 0, have, p);
			IF (cut < have) OR (left = 0) OR (cut = p.max) THEN
				s.Put(buf, cut, id, res); Append(ids, n, id);
				FOR i := cut TO have - 1 DO buf[i - cut] := buf[i] END;
				DEC(have, cut)
			END
		END;
		level := 1;
		REPEAT	(* уровень за уровнем, пока не останется один узел *)
			k := 0; i := 0;
			REPEAT
				IF n - i < fan THEN cut := n - i ELSE cut := fan END;
				IF res = CasStore.ok THEN PutNode(s, level, ids, i, cut, id, res) END;
				ids[k] := id; INC(k); INC(i, cut)
			UNTIL i >= n;
			n := k; INC(level)
		UNTIL n = 1;
		IF res = CasStore.ok THEN root := ids[0] ELSE CasHash.Clear(root) END
	END PutStream;

	PROCEDURE Get (s: CasStore.Store; IN id: CasHash.Id; level: INTEGER; w: Files.Writer;
		OUT res: INTEGER);
	(* level = 0 — лист; < 0 — корень, уровень берётся из блока *)
		VAR b: CasStore.Block; i, j, n, l: INTEGER; child: CasHash.Id;
	BEGIN
		s.Get(id, b, res);
		IF res # CasStore.ok THEN RETURN END;
		IF level = 0 THEN w.WriteBytes(b, 0, LEN(b)); RETURN END;
		l := b[0];
		n := (LEN(b) - 1) DIV CasHash.size;
		IF ((LEN(b) - 1) MOD CasHash.size # 0) OR (n > fan) OR (l < 1) OR (l > maxLevel)
			OR (level > 0) & (l # level) THEN
			res := CasStore.corrupt; RETURN
		END;
		i := 0;
		WHILE (res = CasStore.ok) & (i < n) DO
			FOR j := 0 TO CasHash.size - 1 DO child[j] := b[1 + i * CasHash.size + j] END;
			Get(s, child, l - 1, w, res); INC(i)
		END
	END Get;

	PROCEDURE GetStream* (s: CasStore.Store; IN root: CasHash.Id; w: Files.Writer; OUT res: INTEGER);
	(** Записать в w содержимое дерева root.
		Пред: s # NIL — 20; w # NIL — 21.
		Пост: res = CasStore.ok: в w записаны байты, из которых PutStream построил root.
		Пост: res = absent или corrupt — блока нет, или блок не узел дерева; в w может
		остаться начало содержимого. *)
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(w # NIL, 21);
		Get(s, root, -1, w, res)
	END GetStream;

END CasTree.
