# Overview by Example: ObxCas0

A block is named by its hash: identical data is stored once.

The example puts three words, two of them equal, into an in-memory store and reads a block back by
its name. It shows that the name of a block is the SHA-256 hash of its bytes and that writing the
same block again adds nothing. The example belongs to the guide `Cas/Docu/Cas.md`.

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

The module is built by `tools/build64.sh` (source `Obx/Mod/Cas0.cp`; the subsystem `Cas` is built for amd64 and i386). In a
BlackBox window the commander executes the command:

```
   ObxCas0.Do
```

In the console:

```sh
echo 'ObxCas0.Do' | tools/run64.sh
```

The log shows:

```
hello -> 2cf24dba5fb0a30e
world -> 486ea46224d1bb4f
hello -> 2cf24dba5fb0a30e
blocks: 2
get: hello
```

## What happened

`CasStore.NewMem` creates an empty in-memory store. The procedure `Put` copies the characters of the
word into bytes and calls `s.Put`; the store computes the SHA-256 of these bytes, and the hash
becomes the name of the block. For brevity the first 16 of the 64 hexadecimal digits are printed.

The words `hello` and `world` get different names. The second `hello` gets the same name as the
first: the hash depends only on the bytes. The store finds a block with this name and does not write
it a second time, so the field `blocks` is 2, not 3.

`s.Get(c, data, res)` returns a copy of the block named `c`. The name of the second `hello` equals the
name of the first, so the bytes of the first write are returned — `hello`.

In this example we saw how a block gets its name from its content, why writing is idempotent and how
identical data is stored once.
