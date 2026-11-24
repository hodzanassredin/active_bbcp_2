MODULE ObxCas2;
	IMPORT Console, CasHash, CasStore, CasChange;

	VAR
		s: CasStore.Store;

	PROCEDURE Text (IN t: ARRAY OF CHAR; OUT id: CasHash.Id);	(* блок с текстом файла *)
		VAR b: ARRAY 64 OF BYTE; n, res: INTEGER;
	BEGIN
		n := 0;
		WHILE t[n] # 0X DO b[n] := SHORT(SHORT(ORD(t[n]))); INC(n) END;
		s.Put(b, n, id, res)
	END Text;

	PROCEDURE Head (OUT h: CasHash.Id);
		VAR res: INTEGER;
	BEGIN
		s.GetRef("main", h, res)
	END Head;

	PROCEDURE Say (IN who, what: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(who); Console.WriteStr(": "); Console.WriteStr(what); Console.WriteLn
	END Say;

	PROCEDURE Do*;
		VAR h, id, x: CasHash.Id; first, ann, bob, moved, c: CasChange.Change; res: INTEGER;
			clash: CasChange.Name;
	BEGIN
		s := CasStore.NewMem();
		CasHash.Clear(h);
		first := CasChange.New(h, "Ann", 1, "start");
		Text("read me", x); first.Set("readme", x);
		CasChange.Commit(s, "main", first, id, res);

		Head(h);	(* Ann и Bob начинают от одной вершины *)
		ann := CasChange.New(h, "Ann", 2, "add a.txt");
		Text("text of a", x); ann.Set("a.txt", x);
		bob := CasChange.New(h, "Bob", 3, "add b.txt");
		Text("text of b", x); bob.Set("b.txt", x);

		CasChange.Commit(s, "main", ann, id, res);
		IF res = CasStore.ok THEN Say("Ann", "published") END;
		CasChange.Commit(s, "main", bob, id, res);
		IF res = CasChange.conflict THEN Say("Bob", "refused, head moved") END;

		Head(h);
		CasChange.Rebase(s, bob, h, moved, clash, res);
		IF res = CasStore.ok THEN Say("Bob", "rebased") END;
		CasChange.Commit(s, "main", moved, id, res);
		IF res = CasStore.ok THEN Say("Bob", "published") END;

		Head(h); CasChange.Log(s, h, c, res);
		WHILE c # NIL DO
			Console.WriteStr("log: "); Console.WriteStr(c.author); Console.WriteStr(" ");
			Console.WriteStr(c.note); Console.WriteLn;
			c := c.older
		END
	END Do;

END ObxCas2.
