# Overview by Example: ObxCas2

Two authors publish change sets; the second is refused, rebases and publishes.

Ann and Bob start from one head and each prepares a change set. Ann publishes first. The example
shows how publication by compare-and-swap refuses Bob instead of erasing Ann's work, and how Bob
moves his set onto the new head (`CasChange.Rebase`). The example belongs to the guide
`Cas/Docu/Cas.md`.

```
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
```

The module is built by `tools/build64.sh` (source `Obx/Mod/Cas2.cp`; the subsystem `Cas` is built for amd64 and i386). In a
BlackBox window the commander executes the command:

```
   ObxCas2.Do
```

In the console:

```sh
echo 'ObxCas2.Do' | tools/run64.sh
```

The log shows:

```
Ann: published
Bob: refused, head moved
Bob: rebased
Bob: published
log: Bob add b.txt
log: Ann add a.txt
log: Ann start
```

## What happened

The first set, `start`, has no parent. `Commit` writes it as a block and moves the reference `main`
from the empty `Id` to the `Id` of the set.

Ann and Bob read the head `h` and create sets with parent `h`: Ann sets the name `a.txt`, Bob
`b.txt`. Ann's `Commit` succeeds: the reference equals the parent of her set. Bob's `Commit` returns
`conflict`: the reference already points to Ann's set, not to `h`. The reference does not change,
and Ann's set stays in the history.

Bob rereads the head and calls `Rebase`. It walks the chain from the new head back to `h` and finds
one foreign set, which changes only `a.txt`. Nobody touched the name `b.txt`, there is no conflict,
and `Rebase` builds a new set with the same entries and with Ann's set as parent. Its `Commit`
succeeds.

`Log` walks the chain from the head to the first set. Had both authors changed one name in
different ways, `Rebase` would return `conflict` and the name in `clash`, and combining the edits
would be Bob's task.

In this example we saw how publication by compare-and-swap detects the author who came late, how he
moves his set onto the new head and how to walk the history.
