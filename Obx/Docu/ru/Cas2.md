# Обзор на примерах: ObxCas2

Два автора публикуют наборы изменений; второй получает отказ, переносит набор и публикует.

Аня и Боря начинают от одной вершины и готовят по набору изменений. Аня публикует первой. Пример
показывает, как публикация через сравнение с заменой отказывает Боре, вместо того чтобы стереть
работу Ани, и как Боря переносит свой набор на новую вершину (`CasChange.Rebase`). Пример относится к
руководству `Cas/Docu/ru/Cas.md`.

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

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod/Cas2.cp`; подсистема `Cas` собирается для amd64 и i386). В окне
BlackBox команду исполняет коммандер:

```
   ObxCas2.Do
```

В консоли:

```sh
echo 'ObxCas2.Do' | tools/run64.sh
```

В журнале появится:

```
Ann: published
Bob: refused, head moved
Bob: rebased
Bob: published
log: Bob add b.txt
log: Ann add a.txt
log: Ann start
```

## Что произошло

Первый набор, `start`, не имеет родителя. `Commit` записывает его блоком и переводит ссылку `main`
из пустого `Id` в `Id` набора.

Аня и Боря читают вершину `h` и создают наборы с родителем `h`: Аня задаёт имя `a.txt`, Боря —
`b.txt`. `Commit` Ани проходит: ссылка равна родителю её набора. `Commit` Бори возвращает
`conflict`: ссылка указывает уже на набор Ани, а не на `h`. Ссылка при этом не меняется, и набор Ани
остаётся в истории.

Боря перечитывает вершину и зовёт `Rebase`. Тот проходит цепочку от новой вершины назад до `h` и
видит один чужой набор, который меняет только `a.txt`. Имени `b.txt` никто не трогал, конфликта нет,
и `Rebase` строит новый набор с теми же записями и родителем — набором Ани. Его `Commit` проходит.

`Log` обходит цепочку от вершины к первому набору. Если бы оба автора изменили одно имя по-разному,
`Rebase` вернул бы `conflict` и имя в `clash`, и соединять правки пришлось бы Боре.

В этом примере мы увидели, как публикация через сравнение с заменой обнаруживает опоздавшего
автора, как он переносит свой набор на новую вершину и как обойти историю.
