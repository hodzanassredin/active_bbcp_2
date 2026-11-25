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
