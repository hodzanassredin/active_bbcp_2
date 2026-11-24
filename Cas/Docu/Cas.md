# The Content-Addressed Store Cas

Once a minute a program saves the state of a hundred objects. Two or three of them change in that
minute, but the copy of the whole state is written in full, and the disk fills with identical bytes.
Another problem is worse: two programs save their versions under one name, and the one that writes
second silently overwrites the work of the first. The subsystem `Cas` solves both problems with one
device: a block of data is named by its hash. Identical blocks get one name and are stored once; a
name cannot be forged without changing the content; and the only mutable place — the reference to
the latest version — changes only if nobody has changed it in the meantime.

The subsystem consists of six modules. `CasHash` computes the SHA-256 hash. `CasStore` describes a
block store and implements it in memory, `CasFiles` in a file directory. `CasChunk` cuts a long
stream into chunks by content, `CasTree` builds a tree from the chunks. `CasChange` keeps change sets
and publishes them. The modules are built for amd64 and i386 from the common directory `Cas/Mod`.
The subsystem has no threads; file operations are synchronous.

## 1. A Block and Its Name

The store holds *blocks* — byte arrays at least one byte long. The name of a block is its SHA-256
hash, 32 bytes of type `CasHash.Id`. Three properties follow. Writing is idempotent: `Put` of the same
block yields the same `Id`, and the second write adds nothing. Identical data is stored once, no
matter who writes it and how often. A block that is read is checked: the hash of its bytes must equal
its name, otherwise it is not that block.

The store is the abstract type `CasStore.Store` with methods `Put`, `Get` and `Has`, modelled on
`Files.Directory`. The in-memory implementation is `CasStore.NewMem()`; its fields `blocks` and
`bytes` count distinct blocks and their bytes. The example stores three words, two of them equal:

```
MODULE ObxCas0;
	IMPORT Console, Strings, CasHash, CasStore;

	PROCEDURE Put (s: CasStore.Store; IN text: ARRAY OF CHAR; OUT id: CasHash.Id);
		VAR b: ARRAY 64 OF BYTE; n, res: INTEGER; hex: ARRAY 65 OF CHAR;
	BEGIN
		n := 0;
		WHILE text[n] # 0X DO b[n] := SHORT(SHORT(ORD(text[n]))); INC(n) END;
		s.Put(b, n, id, res);
		CasHash.ToHex(id, hex); hex[16] := 0X;	(* первые 16 знаков из 64 *)
		Console.WriteStr(text); Console.WriteStr(" -> "); Console.WriteStr(hex); Console.WriteLn
	END Put;

	PROCEDURE Do*;
		VAR s: CasStore.Mem; a, b, c: CasHash.Id; data: CasStore.Block; res, i: INTEGER;
			n: ARRAY 16 OF CHAR;
	BEGIN
		s := CasStore.NewMem();
		Put(s, "hello", a); Put(s, "world", b); Put(s, "hello", c);
		Strings.IntToString(s.blocks, n);
		Console.WriteStr("blocks: "); Console.WriteStr(n); Console.WriteLn;
		s.Get(c, data, res);
		Console.WriteStr("get: ");
		FOR i := 0 TO LEN(data) - 1 DO Console.WriteChar(CHR(data[i])) END;
		Console.WriteLn
	END Do;

END ObxCas0.
```

The word `hello` is written twice and gets one name, so the store holds two blocks, not three. The
output is explained on the example page `Obx/Docu/Cas0.md`.

There is no empty block: `Put` with length 0 traps with number 20. The reason lies in the language:
an open array of length 0 cannot be created in BlackBox (`NEW(b, 0)` traps), and `Get` returns a
block as `POINTER TO ARRAY OF BYTE`. `CasTree` stores an empty stream as a node without children, so
the restriction takes nothing away.

An error of the environment is not a trap. If the block is missing, `Get` returns `res = absent` and
`b = NIL`; if the block was read but the hash does not match, `res = corrupt` and also `NIL`; if a
file was not written or not read, `res = failed`. `Files` works the same way: the program decides
what to do about a loss, and traps remain for errors in the program itself.

SHA-256 on amd64 hashes 24–31 MB per second on one thread; conditions and command are in
`docs/measurements.md`. The words of the hash are `INTEGER`, 32 bits on both platforms. `INTEGER`
addition in BlackBox does not check overflow and drops the high bits — this is addition modulo 2^32,
exactly what SHA-256 needs. Rotations are `SYSTEM.ROT`, shifts `SYSTEM.LSH`; both code generators
translate them into a machine rotation of a 32-bit word. The NIST vectors in `Cas_devHash` check
this on amd64 and i386.

## 2. A Store in a Directory

`CasFiles.New(path, res)` keeps a block as a file named by the 64 hexadecimal digits of its `Id`.
The program may be interrupted in the middle of a write. If a block were written under its own name
directly, after a crash the directory would hold a file with the right name and half the data.
Therefore `CasFiles` writes the file under the temporary name `tmp-write` and then renames it; a
rename in Linux replaces the name as a whole. An unfinished block stays under the temporary name,
and `CasFiles.New` deletes it when the store is opened.

On reading, `Get` computes the hash again. The test `Cas_devStore.TestFilesCorrupt` damages the first
byte of a block file; `Get` returns `corrupt`, not the damaged data. Without this check, disk damage
would pass on silently: the name of a block promises content that is not there.

A store in a directory is used by one process. Comparing and replacing a reference (section 3) is
indivisible only within a process: another process can step in between reading the reference and
writing the new file. Neither the compiler nor `CasFiles` checks this.

## 3. References and Publication

Blocks are immutable, but a program needs to know which version is the latest. For this there is a
*reference* — a mutable name pointing to an `Id`. `GetRef(name)` reads it; if there is no reference,
the result is the empty `Id` (`CasHash.IsNil`). A reference name has 1 to 63 characters from
letters, digits, `_`, `-` and `.`; any other name traps with number 22.

Only `CasRef(name, expected, new)` changes a reference: if the reference equals the expected `Id`, it
receives the new one and the result is `TRUE`; otherwise the reference does not change and the
result is `FALSE`. This compare-and-swap is the publication. Suppose Ann and Bob read the head `h` and
each prepared a version. Ann publishes first: `CasRef("main", h, ann)` yields `TRUE`. Bob calls
`CasRef("main", h, bob)` and gets `FALSE`, because the head is already `ann`. Without the comparison
Bob would write `bob` over `ann`, and Ann's work would vanish from the history without a single
error. With the check Bob learns about it at once, rereads the head and retries. The check is
optimistic: nobody waits for anybody, and the one who came late pays for the collision.

`FALSE` is also returned when writing the reference failed. The caller tells this case apart by the
`res` of the following `GetRef`.

## 4. Cutting by Content

A 10 MB block is a poor deal: one byte changes, and a new 10 MB block is written. A long stream is
cut into chunks, and each chunk is a separate block. The question is where to cut. If the stream is
cut every 8192 bytes, inserting ten bytes in the middle shifts all boundaries after the insertion,
and all later chunks get new names. The example `ObxCas1` cuts two inputs of 300 000 bytes, the second
with ten bytes in the middle: with 8 KB cuts 18 of 37 chunks are common, with cuts by content 30 of 31.

`CasChunk.Cut` places a boundary where the hash of the last 48 bytes (`window`) is divisible by the
mask: `hash * mask = {}`. The hash is rolling, Buzhash: when the window moves by a byte, the hash
rotates by one bit and is combined by exclusive or with the table word for the incoming byte and for
the outgoing one. The hash depends only on the 48 bytes under the window, so a boundary is tied to
content, not to position: after the insertion the window sees the same bytes and finds the same
boundaries. The table of 256 words is built in the module body by the xorshift32 generator with a
constant seed; the test `Cas_devChunk.TestMatchesReference` compares the cut points with a reference,
and a changed table will not pass it.

`Params` bound the length of a chunk: `min` (2048 by default), `max` (65 536) and `mask` (8191 —
a boundary on average 8 KB after `min`). A chunk is never shorter than `min`: the window starts
counting at `min - window`, and no boundary is checked before `min`. A chunk is never longer than
`max`: on uniform data the hash is constant, no boundary is found, and `Cut` cuts at `max`. The mask
must have the form 2^k − 1; otherwise, as with `min < window` or `max < min`, `Cut` traps with
number 21.

Cutting on amd64 runs at 570–640 MB per second, twenty times faster than SHA-256, so writing a
stream is bounded by the hash, not by cutting.

## 5. A Large Object as a Tree

`CasTree.PutStream(store, reader, root, res)` cuts the stream of a `Files.Reader` into chunks and
writes them as blocks. From the names of the chunks it builds *nodes*: a node is a block holding a
level byte and up to 64 (`fan`) names of children. Nodes of level 1 list chunks, nodes of level 2
list nodes of level 1, and so on until one node — the root — remains. The root is always a node, so
an empty stream is a node without children.

The name of the root depends on the names of the children, and those on the content, so the root
determines the content uniquely, as the name of a block determines the block. One byte in the middle
changes — the chunk, the path of nodes from it to the root and the root itself change; the other
blocks remain. The test `Cas_devTree.TestRootIdentifiesContent` changes a byte in a stream of 200 000
bytes and sees no more than three new blocks.

`GetStream(store, root, writer, res)` walks the tree and writes the chunks in order. A missing block
gives `res = absent`; a block that does not look like a node (length not 1 + 32·n, level outside
1..16) gives `res = corrupt`. In both cases the writer may already hold the beginning of the content,
so write to a temporary file.

## 6. Change Sets

A reference points to a version but does not say what changed in it and who changed it. A *change
set* `CasChange.Change` keeps the parent (the `Id` of the previous set or the empty `Id`), a mapping
"name → `Id`", the author, the time and a note. A set is written as a block (`Put`), so it is
immutable, and its `Id` is the name of the version. The chain of sets from the head to the first one
is the history; `Log` walks it and links the sets by the field `older`.

`Commit(store, reference, set, id, res)` writes the set and publishes it by `CasRef` from the set's
`parent` to its `Id`. If the head has moved on, `res = conflict`, and the reference does not change.
Then `Rebase(store, set, head, new, name, res)` moves the set onto the new head: it walks the chain
from the head back to the parent of the set and looks at which names others changed. A name to which
others gave a value different from this set is a conflict: `res = conflict`, and the name is returned
in `clash`. If there are no conflicts, `Rebase` builds a new set with the same entries, author, time
and note and with the head as parent. The example `ObxCas2` shows this with two authors.

`Rebase` does not merge content. If Ann and Bob changed one file in different ways, how to combine
the edits must be decided by a person or by a program that knows the file format. A set that quietly
chose one of the versions would lose the other, just like writing a reference without comparison. If
both gave a name the same value, there is no conflict. A parent that is not in the chain of the head
gives `res = unrelated`: such a set belongs to another history.

A written set cannot be changed: `Set` after `Put` traps with number 23. A new version is a new set.

## 7. Cost and Limits

The store only grows: blocks no reference leads to any more are not deleted. There is no garbage
collection of the store. The in-memory store finds a block by the first byte of its `Id` in one of
256 chains; the directory store by opening a file. A change set reads the whole chain down to its
parent on every `Rebase`, so a long foreign history between the parent and the head costs one `Get`
per set.

## Interface

```
DEFINITION CasHash;

	CONST size = 32;

	TYPE Id = ARRAY 32 OF BYTE;

	PROCEDURE Of (IN data: ARRAY OF BYTE; beg, len: INTEGER; OUT id: Id);
	PROCEDURE Equal (IN a, b: Id): BOOLEAN;
	PROCEDURE Clear (OUT id: Id);
	PROCEDURE IsNil (IN id: Id): BOOLEAN;
	PROCEDURE ToHex (IN id: Id; OUT s: ARRAY OF CHAR);
	PROCEDURE FromHex (IN s: ARRAY OF CHAR; OUT id: Id; OUT ok: BOOLEAN);

END CasHash.
```

**PROCEDURE Of (IN data: ARRAY OF BYTE; beg, len: INTEGER; OUT id: Id)**
SHA-256 hash of the segment `data[beg .. beg + len - 1]`.

```
Pre: beg >= 0, len >= 0 — 20
Pre: beg + len <= LEN(data) — 21
Post: id — SHA-256 of the segment
```

**PROCEDURE Equal (IN a, b: Id): BOOLEAN**, **IsNil (IN id: Id): BOOLEAN**, **Clear (OUT id: Id)**
Post: `Equal` — `a` and `b` are equal byte by byte; `IsNil` — all bytes are 0; after `Clear`,
`IsNil(id)` holds. The empty `Id` means "no block".

**PROCEDURE ToHex (IN id: Id; OUT s: ARRAY OF CHAR)**

```
Pre: LEN(s) > 64 — 20
Post: s — 64 characters 0-9a-f and 0X
```

**PROCEDURE FromHex (IN s: ARRAY OF CHAR; OUT id: Id; OUT ok: BOOLEAN)**
Parses the output of `ToHex`; upper-case letters are allowed.
Post: `ok` — `s` consists of exactly 64 hexadecimal digits; otherwise `id` is empty.

```
DEFINITION CasStore;

	IMPORT CasHash;

	CONST
		ok = 0; absent = 1; corrupt = 2; failed = 3;
		maxRef = 64;

	TYPE
		Block = POINTER TO ARRAY OF BYTE;
		RefName = ARRAY 64 OF CHAR;

		Store = POINTER TO ABSTRACT RECORD
			(s: Store) Put (IN data: ARRAY OF BYTE; len: INTEGER; OUT id: CasHash.Id;
				OUT res: INTEGER), NEW, ABSTRACT;
			(s: Store) Get (IN id: CasHash.Id; OUT b: Block; OUT res: INTEGER), NEW, ABSTRACT;
			(s: Store) Has (IN id: CasHash.Id): BOOLEAN, NEW, ABSTRACT;
			(s: Store) GetRef (IN name: ARRAY OF CHAR; OUT id: CasHash.Id; OUT res: INTEGER),
				NEW, ABSTRACT;
			(s: Store) CasRef (IN name: ARRAY OF CHAR; IN old, new: CasHash.Id): BOOLEAN,
				NEW, ABSTRACT
		END;

		Mem = POINTER TO RECORD (Store)
			blocks-, bytes-: INTEGER
		END;

	PROCEDURE ValidRef (IN name: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE NewMem (): Mem;

END CasStore.
```

**PROCEDURE (s: Store) Put (IN data: ARRAY OF BYTE; len: INTEGER; OUT id: CasHash.Id; OUT res: INTEGER)**
Writes the block `data[0 .. len-1]`.

```
Pre: 0 < len <= LEN(data) — 20
Post: res = ok: id = SHA-256 of the block, Has(id); a block already present is not rewritten
Post: res = failed: id — the same hash, the block may not be saved
```

**PROCEDURE (s: Store) Get (IN id: CasHash.Id; OUT b: Block; OUT res: INTEGER)**

```
Post: res = ok: b # NIL, SHA-256(b) = id
Post: res = absent, corrupt or failed: b = NIL
```

**PROCEDURE (s: Store) Has (IN id: CasHash.Id): BOOLEAN**
Post: block `id` is written; the content is not checked.

**PROCEDURE (s: Store) GetRef (IN name: ARRAY OF CHAR; OUT id: CasHash.Id; OUT res: INTEGER)**

```
Pre: ValidRef(name) — 22
Post: res = ok: id — value of the reference; no reference — id is empty
Post: res = failed or corrupt: id is empty
```

**PROCEDURE (s: Store) CasRef (IN name: ARRAY OF CHAR; IN old, new: CasHash.Id): BOOLEAN**
Compares and swaps: a reference equal to `old` (the empty `Id` — no reference) receives the value
`new`.

```
Pre: ValidRef(name) — 22
Post: TRUE — the reference equals new
Post: FALSE — the reference is not old or the write failed; the reference is unchanged
```

**PROCEDURE ValidRef (IN name: ARRAY OF CHAR): BOOLEAN**
Post: `name` has 1 to 63 characters `A-Z a-z 0-9 _ - .`.

**PROCEDURE NewMem (): Mem**
Post: an empty in-memory store: `blocks = 0`, `bytes = 0`, no references.

```
DEFINITION CasFiles;

	IMPORT CasStore;

	TYPE Store = POINTER TO RECORD (CasStore.Store) END;

	PROCEDURE New (IN path: ARRAY OF CHAR; OUT res: INTEGER): Store;

END CasFiles.
```

**PROCEDURE New (IN path: ARRAY OF CHAR; OUT res: INTEGER): Store**
A store in the directory `path`; the directory is created on the first write. The methods are those
of `CasStore.Store`; `Get` checks the hash again, an empty file is `corrupt`.
Post: `res = CasStore.ok`, the result is `# NIL`; the temporary file of an interrupted write is
deleted.

```
DEFINITION CasChunk;

	CONST window = 48;

	TYPE Params = RECORD
		min, mask, max: INTEGER
	END;

	PROCEDURE Default (OUT p: Params);
	PROCEDURE Cut (IN data: ARRAY OF BYTE; beg, end: INTEGER; IN p: Params): INTEGER;

END CasChunk.
```

**PROCEDURE Default (OUT p: Params)**
Post: `p.min = 2048`, `p.mask = 8191`, `p.max = 65536`.

**PROCEDURE Cut (IN data: ARRAY OF BYTE; beg, end: INTEGER; IN p: Params): INTEGER**
End of the chunk that starts at `beg`.

```
Pre: 0 <= beg < end <= LEN(data) — 20
Pre: window <= p.min <= p.max, p.mask + 1 is a power of two — 21
Post: beg < result <= end; result - beg <= p.max — 60
Post: result = end or result - beg >= p.min
```

```
DEFINITION CasTree;

	IMPORT Files, CasHash, CasStore;

	CONST fan = 64;

	PROCEDURE PutStream (s: CasStore.Store; r: Files.Reader; OUT root: CasHash.Id;
		OUT res: INTEGER);
	PROCEDURE GetStream (s: CasStore.Store; IN root: CasHash.Id; w: Files.Writer;
		OUT res: INTEGER);

END CasTree.
```

**PROCEDURE PutStream (s: CasStore.Store; r: Files.Reader; OUT root: CasHash.Id; OUT res: INTEGER)**
Writes the stream from the current position of `r` to the end of the file.

```
Pre: s # NIL — 20
Pre: r # NIL — 21
Post: res = ok: root — the root; GetStream(root) yields the same bytes
Post: res # ok — writing a block failed, root is empty
```

**PROCEDURE GetStream (s: CasStore.Store; IN root: CasHash.Id; w: Files.Writer; OUT res: INTEGER)**

```
Pre: s # NIL — 20
Pre: w # NIL — 21
Post: res = ok: w received the bytes from which PutStream built root
Post: res = absent or corrupt: w may hold the beginning of the content
```

```
DEFINITION CasChange;

	IMPORT CasHash, CasStore;

	CONST conflict = 10; unrelated = 11;

	TYPE
		Name = ARRAY 64 OF CHAR;
		Note = ARRAY 256 OF CHAR;

		Entry = POINTER TO RECORD
			next-: Entry;
			name-: Name;
			id-: CasHash.Id
		END;

		Change = POINTER TO RECORD
			id-, parent-: CasHash.Id;
			author-: Name;
			time-: LONGINT;
			note-: Note;
			entries-: Entry;
			older-: Change;
			(c: Change) Set (IN name: ARRAY OF CHAR; IN id: CasHash.Id), NEW;
			(c: Change) Find (IN name: ARRAY OF CHAR; OUT id: CasHash.Id): BOOLEAN, NEW
		END;

	PROCEDURE New (IN parent: CasHash.Id; IN author: ARRAY OF CHAR; time: LONGINT;
		IN note: ARRAY OF CHAR): Change;
	PROCEDURE Put (s: CasStore.Store; c: Change; OUT id: CasHash.Id; OUT res: INTEGER);
	PROCEDURE Get (s: CasStore.Store; IN id: CasHash.Id; OUT c: Change; OUT res: INTEGER);
	PROCEDURE Commit (s: CasStore.Store; IN ref: ARRAY OF CHAR; c: Change; OUT id: CasHash.Id;
		OUT res: INTEGER);
	PROCEDURE Rebase (s: CasStore.Store; c: Change; IN head: CasHash.Id; OUT new: Change;
		OUT clash: Name; OUT res: INTEGER);
	PROCEDURE Log (s: CasStore.Store; IN head: CasHash.Id; OUT first: Change; OUT res: INTEGER);

END CasChange.
```

The entries of a set are in ascending order of names, and the names differ. The field `id` is empty
until the set is written; `Log` fills `older`.

**PROCEDURE New (IN parent: CasHash.Id; IN author: ARRAY OF CHAR; time: LONGINT; IN note: ARRAY OF CHAR): Change**

```
Pre: author shorter than 64 characters — 20
Pre: note shorter than 256 characters — 21
Post: result # NIL, entries = NIL, id is empty
```

**PROCEDURE (c: Change) Set (IN name: ARRAY OF CHAR; IN id: CasHash.Id)**

```
Pre: name of 1..63 characters — 22
Pre: the set is not written (c.id is empty) — 23
Post: Find(name) = id; the previous value of the name is replaced
```

**PROCEDURE (c: Change) Find (IN name: ARRAY OF CHAR; OUT id: CasHash.Id): BOOLEAN**
Post: `TRUE` — the set gives the name `name` the value `id`; `FALSE` — no such name, `id` is empty.

**PROCEDURE Put (s: CasStore.Store; c: Change; OUT id: CasHash.Id; OUT res: INTEGER)**

```
Pre: s # NIL — 20
Pre: c # NIL — 21
Post: res = ok: id = c.id — Id of the set's block; references are untouched
```

**PROCEDURE Get (s: CasStore.Store; IN id: CasHash.Id; OUT c: Change; OUT res: INTEGER)**

```
Pre: s # NIL — 20
Post: res = ok: c.id = id, fields as at Put
Post: res = corrupt — the block is not a set; absent, failed — from the store; c = NIL
```

**PROCEDURE Commit (s: CasStore.Store; IN ref: ARRAY OF CHAR; c: Change; OUT id: CasHash.Id; OUT res: INTEGER)**

```
Pre: s # NIL — 20
Pre: c # NIL — 21
Pre: CasStore.ValidRef(ref) — 22
Post: res = ok: ref = id = c.id
Post: res = conflict: ref # c.parent, the reference is unchanged, the set's block is written
Post: otherwise — a store error, the reference is unchanged
```

**PROCEDURE Rebase (s: CasStore.Store; c: Change; IN head: CasHash.Id; OUT new: Change; OUT clash: Name; OUT res: INTEGER)**

```
Pre: s # NIL — 20
Pre: c # NIL — 21
Post: res = ok: new.parent = head, entries, author, time and note as in c; new is not written
Post: res = conflict: a set after c.parent up to head inclusive gave the name clash another value; new = NIL
Post: res = unrelated: c.parent is not in the chain of head; new = NIL
Post: absent, corrupt, failed — error reading the chain; new = NIL
```

**PROCEDURE Log (s: CasStore.Store; IN head: CasHash.Id; OUT first: Change; OUT res: INTEGER)**

```
Pre: s # NIL — 20
Post: res = ok: first — the set head, older — its parent, and so on to the set without a parent; empty head — first = NIL
Post: otherwise — a read error, first = NIL
```
