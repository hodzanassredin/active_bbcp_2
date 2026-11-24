# The Kv Store

A program keeps an event log in a file: it writes a record, then another, then updates a counter at
the start of the file. The power fails in the middle of a record. After power-on the file holds half
a record and the old counter, and the program does not know where the whole data ends. Rewriting the
whole file does not help either: the power may fail in the middle of the rewrite, and then there is
neither the old file nor the new one. Subsystem `Kv` solves this problem for data of the form
"key → value": every change either reaches the disk entirely or not at all, and after any failure
the database opens in the last committed state.

`Kv` is an embedded ordered store with transactions. Its design follows BoltDB and LMDB: a copy-on-write
B+-tree, one writer, readers on a snapshot, two meta pages with a checksum. There is no log, no
recovery at open and no background work. Modules: `KvPages` — the page and its bytes, `KvStore` —
the file layer, `KvLin` — a Linux file, `KvFault` — an in-memory store with failures for tests,
`KvTree` — the tree and the cursor, `KvDb` — the database and transactions.

## 1. A First Database

Example `ObxKv0` opens a file, writes four pairs, reads one and walks a range of keys:

```
MODULE ObxKv0;
	IMPORT Console, KvStore, KvLin, KvDb;

	CONST path = "/tmp/obxkv0.db";

	VAR db: KvDb.Db;

	PROCEDURE Put (tx: KvDb.Tx; IN key, val: ARRAY OF CHAR);
	BEGIN
		tx.Put(KvDb.Str(key)^, KvDb.Str(val)^)
	END Put;

	PROCEDURE Show (c: KvDb.Cursor);
		VAR k, v: ARRAY 64 OF CHAR;
	BEGIN
		KvDb.ToStr(c.Key(), k); KvDb.ToStr(c.Val(), v);
		Console.WriteStr(k); Console.WriteStr(" = "); Console.WriteStr(v); Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR s: KvStore.Store; tx: KvDb.Tx; c: KvDb.Cursor; res: INTEGER;
			v: KvDb.Bytes; str: ARRAY 64 OF CHAR;
	BEGIN
		KvLin.Delete(path);
		KvLin.Open(path, s, res); KvDb.Open(s, db, res);
		tx := db.Begin(TRUE);
		Put(tx, "fruit/apple", "red"); Put(tx, "fruit/banana", "yellow");
		Put(tx, "fruit/cherry", "dark red"); Put(tx, "veg/carrot", "orange");
		tx.Commit(res);
		tx := db.Begin(FALSE);
		IF tx.Get(KvDb.Str("fruit/banana")^, v) THEN
			KvDb.ToStr(v, str); Console.WriteStr("banana: "); Console.WriteStr(str); Console.WriteLn
		END;
		c := tx.Seek(KvDb.Str("fruit/b")^);
		WHILE c.Valid() & (c.Key()[0] = ORD("f")) DO Show(c); c.Next END;
		tx.Abort;
		db.Close; KvLin.Delete(path)
	END Do;

END ObxKv0.
```

Key and value are byte arrays; keys are ordered bytewise, unsigned. `KvDb.Str` and `KvDb.ToStr`
convert strings to bytes and back (UTF-8). All reading and changing happens inside a *transaction*:
`Begin(TRUE)` opens a writing one, `Begin(FALSE)` a reading one. A writing transaction ends with
`Commit` or `Abort`, a reading one with `Abort`. The cursor of `Seek` stands on the first key not less
than the given one; `Next` and `Prev` move in key order. The output is discussed on page
`Obx/Docu/Kv0.md`.

There are no named tables. Data of different kinds is separated by a key prefix, like `fruit/` and
`veg/` in the example: a prefix gathers the keys of one kind into a contiguous range, and a cursor walks
it from `Seek(prefix)` while the key starts with the prefix.

## 2. Page and Tree

The database file is an array of 4096-byte pages (`KvPages.size`). A page number is 32 bits. Pages 0
and 1 are metas; the others are tree nodes, the free list, or free.

The pairs lie in a *B+-tree*. A leaf holds pairs in ascending key order. A branch holds pairs "key,
page of the child": key `keys[i]` is the lower bound of the keys of child `i`. A search goes from the
root: in each branch a binary search picks the last child whose bound is not greater than the key
sought. With thousands of pairs per page a three-level tree holds millions of keys, and a search reads
three pages.

A node that does not fit into a page is split into pieces. Normally the pieces are half full: then an
insertion into the middle does not split the node again at once. In an event log keys grow, and an
insertion always goes to the right edge; half pieces would leave the file half empty. Therefore a node
at the right edge of the tree that was appended to is split into nearly full pieces (nine tenths of a
page), as with `FillPercent` in bolt. A value larger than a page occupies *overflow pages*: a node of
one large element lies on several consecutive pages, and the header of the first records their count.

A node smaller than a quarter of a page is merged with a neighbour at commit. A merged node that became
larger than a page is split again. So the tree does not degenerate after deletions.

## 3. Copy on Write

A transaction changes no page that someone else sees. To change a leaf it makes a copy in memory; for
the parent to point to the copy it copies the parent as well, and so on up to the root. At commit the
changed nodes are written to free pages, and the old ones become free. Inserting key `d` into a tree
whose root is on page 5:

```
before:                          after:
     [5: a | m]                       [5: a | m]        [9: a | m]
      /       \                        /       \          /     \
[3: a b c]   [4: m x]           [3: a b c]  [4: m x]  [8: a b c d]  (4)

metas: 0 -> root 5              metas: 1 -> root 9; pages 3 and 5 are free
                                once no reader sees them
```

The new tree shares with the old one everything that did not change (page 4). The old tree stays whole
to the end: whoever began reading before the commit finishes reading the old one. The price is writing
the whole path from leaf to root on each transaction; therefore it pays to gather changes into one
transaction.

Inside a transaction a large node is split as soon as it exceeds two pages, not only at commit:
otherwise a hundred thousand insertions into one transaction would shift the elements of one growing
leaf.

## 4. Two Metas and Two Syncs

A meta is the tree root, the page of the free list, the number of file pages and the transaction
number, with a CRC32 checksum. Transaction `txid` writes its meta into page `txid MOD 2`. Commit goes
in four steps:

1. write the new nodes and the free list to free pages;
2. `Sync` — wait until they reach the medium;
3. write the meta into page `txid MOD 2`;
4. `Sync`.

A disk promises no order: writes between two `Sync` calls may arrive in any order, partially, and a
page may arrive half written. Step 2 guarantees that a meta never points to a page that is not on the
medium. If the power failed before step 3 or in the middle of it, the second meta is whole: it is
written to the other page. Open reads both metas, discards the one whose checksum does not match and
takes the remaining one with the larger number. Step 4 is needed only for the promise "after `Commit`
the changes survive the power". Nothing more is required: the disk always holds one whole tree that a
whole meta points to.

If the first `Sync` is dropped, the meta may arrive before the pages, and after a failure the database
points to garbage. Test `Kv_devCrash` catches this: it cuts every write and every `Sync` of a commit,
switches the power off (everything unsynchronized is lost entirely or by lot) and requires the database
to be whole and equal to the state before or after the transaction.

Open recovers nothing. The transaction number is 64 bits and only grows.

## 5. The Reader's Snapshot

A reading transaction remembers the meta at the moment of `Begin` and reads the tree of its root. The
writer does not touch the pages of that tree, so the reader sees a *snapshot*: everything committed
before it began, and nothing after. There may be any number of readers; they wait neither for the
writer nor for each other. Example `ObxKv1` opens a reader, appends 500 events and shows that the reader
still sees 1000 while a new one sees 1500.

There is one writer. If another task calls `Begin(TRUE)` while a writing transaction is open, it waits
for its end. Waiting tasks stand in a queue in order of arrival; a cancelled waiting task leaves the
queue with trap 127. If the same task or code outside a task opens the second writer — trap 21: such a
wait would be eternal, and on the main stack waiting is not allowed.

The recommended way is `KvDb.Update(db, body, res)` and `KvDb.View(db, body)`. The body is an object
with a method `Do(tx)`. `Update` runs the body and commits; if the body traps (or the task is cancelled,
127), the transaction is rolled back and the trap goes on with the same number:

```
TYPE Add = POINTER TO RECORD (KvDb.Body) END;

PROCEDURE (a: Add) Do (tx: KvDb.Tx);
BEGIN
	tx.Put(KvDb.Str("k")^, KvDb.Str("v")^)
END Do;

...	NEW(add); KvDb.Update(db, add, res)
```

`Begin` stays for manual control. A violation with a cost: a trap between `Begin` and `Commit` leaves the
transaction open. Neither the compiler nor `Kv` catches it. An open writer locks the database: the next
`Begin(TRUE)` of this task traps 21, other tasks wait forever. An open reader holds pages (section 6).
Test `Kv_devTasks.TestUpdateTrapRollsBack` shows that `Update` removes this cost.

`Begin(TRUE)` is the only switch point in `Kv`, and only when it has to wait.

Inside `Get`, `Put`, `Delete`, the cursor and `Commit` there are no switch points: input and
output are synchronous. A task that holds a transaction does not give up the processor in the middle
of an operation, but may give it up between operations; the transaction stays open. A long `Sync` stops
all tasks — as any blocking OS call from a task does.

## 6. Free Pages

A page freed by transaction `w` is still seen by readers of snapshots numbered below `w`. Such a page
waits in the pending list with number `w`. When a writing transaction begins, pending pages become
free if every open reader has a snapshot number not less than `w`. The disk holds the common list of
free and pending pages: after open there are no readers.

A reading transaction left open is a violation with a price. Neither the compiler nor `Kv` catches it.
While the reader lives, all pages freed after its start are not reused, and the file grows by a path
from leaf to root with every commit. In test `Kv_devTree.TestReaderHoldsPages` a hundred commits with
an open reader grow the file by more than twenty pages; after the reader's `Abort` the file stops
growing. The file never shrinks: freed pages are reused but not returned to the OS.

## 7. Errors and Checking

Environment errors are returned in `res`, as in `Files`: `ok`, `ioError`, `corrupt`. A read error
inside a transaction is recorded in `tx.res`; after it the operations of the transaction do nothing,
and `Commit` returns the same code and rolls the transaction back. A write or `Sync` error at commit
leaves the database broken: every following transaction gets the same `res`; the database is closed and
opened again. Both metas damaged — `Open` returns `corrupt`, without a trap.

A contract violation is a trap: 20 — the transaction is closed, 21 — a second writer of the same task or
outside a task, a change in a reading transaction, 22 — a key longer than `KvTree.maxKey` (1024 bytes) or a cursor after the tree
changed. An empty key and an empty value are impossible: the language has no arrays of length 0.

`tx.Check` checks the snapshot the transaction began with: node keys ascend and lie within the
parent's bounds, all leaves are at one depth, and every page of the file is exactly one of: meta, node,
free list, free. The tests call it after every commit; a user — after a suspicious failure.

## 8. Cost

Measurements — `docs/measurements.md`, section 13 "Kv store". On amd64 under a machine load of about 20,
one writing transaction inserts about 25 thousand keys per second in order and about 9 thousand at
random; a random `Get` — about 40 thousand per second: every node of the path is decoded from page
bytes. A commit with `fsync` costs about 2 ms (540 per second on NVMe), without `fsync` about 50 µs.
Therefore it pays to gather small changes into one transaction.

## 9. Limits and Comparison with LSM

One writer; the file does not shrink; a long reader holds pages. The page number is 32 bits — up to
16 TB. There is no `mmap`: pages are read with ordinary reads through a cache of 1024 nodes, so memory
is bounded and read errors are codes, not signals. The subsystem builds for amd64 and i386 under Linux;
Windows needs its own file layer.

An LSM tree (LevelDB, RocksDB) writes changes to a log and an in-memory table, and to disk in large
sorted files that background work merges. Its writes are cheaper: no path to the root per commit, no
random writes. Its reads are dearer: a key is looked up in several levels. LSM is better when writes
far outnumber reads and the data exceeds memory. A copy-on-write B+-tree is simpler, reads faster and
needs no background work — in a single-threaded task system this decides.

## 10. Interface

```
DEFINITION KvDb;

	CONST
		ok = 0; ioError = 1; corrupt = 2;

	TYPE
		Body = POINTER TO ABSTRACT RECORD
			(b: Body) Do- (tx: Tx), NEW, ABSTRACT
		END;

		Bytes = KvPages.Bytes;
		Cursor = KvTree.Cursor;

		Db = POINTER TO RECORD
			(db: Db) Begin (write: BOOLEAN): Tx, NEW;
			(db: Db) Close, NEW;
			(db: Db) Pages (): INTEGER, NEW
		END;

		Tx = POINTER TO RECORD
			write-: BOOLEAN;
			res-: INTEGER;
			(tx: Tx) Abort, NEW;
			(tx: Tx) Check (OUT msg: ARRAY OF CHAR): BOOLEAN, NEW;
			(tx: Tx) Commit (OUT res: INTEGER), NEW;
			(tx: Tx) Delete (IN key: ARRAY OF BYTE): BOOLEAN, NEW;
			(tx: Tx) First (): Cursor, NEW;
			(tx: Tx) Get (IN key: ARRAY OF BYTE; OUT val: Bytes): BOOLEAN, NEW;
			(tx: Tx) Last (): Cursor, NEW;
			(tx: Tx) Put (IN key, val: ARRAY OF BYTE), NEW;
			(tx: Tx) Seek (IN from: ARRAY OF BYTE): Cursor, NEW
		END;

	PROCEDURE Open (s: KvStore.Store; OUT db: Db; OUT res: INTEGER);
	PROCEDURE Str (IN s: ARRAY OF CHAR): Bytes;
	PROCEDURE ToStr (IN b: ARRAY OF BYTE; OUT s: ARRAY OF CHAR);
	PROCEDURE Update (db: Db; body: Body; OUT res: INTEGER);
	PROCEDURE View (db: Db; body: Body);

END KvDb.
```

`Open` opens a database in a store; an empty store becomes an empty database.

```
Pre: s # NIL — 20
Post: res = ok — db is open; corrupt — no whole meta or a damaged free list; otherwise db = NIL
```

`db.Begin` begins a transaction on the snapshot of the last commit.

```
Pre: the database is not closed — 20
Pre: write -> this task does not hold the writer; outside a task — no writer — 21
Post: the transaction is open; after a commit error its res # ok
```

A writer from another task waits in order of arrival; cancelling the waiter — trap 127.

`Update` opens a writing transaction, runs `body.Do` and commits; `View` does the same with a reader.

```
Pre: as for Begin
Post: the body ended — res = the Commit code; the body trapped — the transaction is closed, the trap is passed with the same number
```

`db.Close` closes the store. `db.Pages` is the number of pages of the last commit.

```
Pre: no open transactions — 22
```

`tx.Get` looks a key up.

```
Pre: the transaction is open — 20
Post: TRUE — the key exists, val is a copy of the value; FALSE — no key or an environment error (res # ok)
```

`tx.Put` binds a key to a copy of the value; `tx.Delete` removes a key.

```
Pre: the transaction is open — 20
Pre: the transaction is writing — 21
Pre: LEN(key) <= KvTree.maxKey — 22 (Put only)
Post: Delete = TRUE — the key existed and is removed
```

`tx.Seek`, `tx.First`, `tx.Last` give a cursor on the first key not less than `from`, on the smallest
and on the largest key. A cursor is valid while the transaction is open and does not change data.

```
Pre: the transaction is open — 20
```

`tx.Commit` commits and closes the transaction; for a reading one it is the same as `Abort`.
`tx.Abort` closes the transaction without a trace; a repeated `Abort` does nothing.

```
Pre: the transaction is open — 20 (Commit)
Post: res = ok — the changes are on the medium and visible to new transactions; otherwise rolled back
```

`tx.Check` checks the transaction's snapshot (invariants 100–103 of `KvTree`).

```
Pre: the transaction is open — 20
Pre: a writing transaction has changed nothing yet — 21
Post: TRUE — the snapshot is whole; FALSE — msg names the violation
```

The cursor (`KvTree.Cursor`): `Valid`, `Next`, `Prev`, `Key`, `Val`; `Key` and `Val` return copies.

```
Pre: Valid() — 20 (Next, Prev, Key, Val)
Pre: the tree has not changed since the cursor was made — 22
```

`Str` converts a string to UTF-8 without the zero, `ToStr` converts back, truncating at `LEN(s)`.

```
Pre: s # "" — 20 (Str)
```

The file layer is `KvStore.Store` with methods `Read`, `Write`, `Sync`, `Pages`, `Close`.
`KvLin.Open(path, OUT st, OUT res)` opens or creates a file (mode 0600), `KvLin.Delete(path)` deletes
it. `KvFault.New()` gives an in-memory store; `Arm(k, keep)` sets a failure at event `k` (a write or
a `Sync`), `PowerOff(rnd)` switches the power off, `Clone` and `Damage` serve the tests.
