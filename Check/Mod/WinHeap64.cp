MODULE CheckWinHeap64;

	IMPORT S := SYSTEM, Kernel, WinKernel, Services, Strings, Meta, Dialog, StdConfig, StdCmds, Windows;

	CONST blockSize = 64*1024*1024;

	TYPE
		Node* = POINTER TO EXTENSIBLE RECORD value*: INTEGER; next*: Node END;
		Child* = POINTER TO RECORD (Node) extra*: INTEGER END;
		Final = POINTER TO RECORD END;
		NodeValue = RECORD (Meta.Value) p: Node END;
		Test = POINTER TO RECORD (Services.Action) END;

	VAR
		root*: Node;
		cells: POINTER TO ARRAY OF RECORD p: Node; value: INTEGER END;
		blocks: ARRAY 68 OF POINTER TO ARRAY OF BYTE;
		action: Test; finalized, callbacks: INTEGER;

	PROCEDURE Say (IN text: ARRAY OF CHAR);
	BEGIN WinKernel.WkWrite(S.ADR(text),LEN(text$)) END Say;

	PROCEDURE Fail (code: INTEGER);
		VAR n: ARRAY 32 OF CHAR;
	BEGIN Strings.IntToString(code,n); Say("WinHeap x64 FAILED "); Say(n); Kernel.Quit(code) END Fail;

	PROCEDURE Trap;
	BEGIN Fail(100) END Trap;

	PROCEDURE (n: Node) Address* (): S.ADDRESS, NEW, EXTENSIBLE;
	BEGIN RETURN S.VAL(S.ADDRESS,n) END Address;

	PROCEDURE (n: Child) Address* (): S.ADDRESS;
	BEGIN RETURN S.VAL(S.ADDRESS,n)+n.extra END Address;

	PROCEDURE Echo* (n: Node): Node;
	BEGIN RETURN n END Echo;

	PROCEDURE SetNode (VAR dest: Node; source: Node);
	BEGIN dest := source END SetNode;

	PROCEDURE (f: Final) FINALIZE;
	BEGIN INC(finalized) END FINALIZE;

	PROCEDURE MakeFinal;
		VAR f: Final;
	BEGIN NEW(f) END MakeFinal;

	PROCEDURE ScrubAndCollect;
		VAR clear: ARRAY 4096 OF S.ADDRESS; i: INTEGER;
	BEGIN FOR i := 0 TO LEN(clear)-1 DO clear[i] := 0 END; Kernel.Collect END ScrubAndCollect;

	PROCEDURE Callback (x,y,z: S.ADDRESS);
		VAR n: Node;
	BEGIN
		n := S.VAL(Node,x);
		IF (n # root) OR (y # x+8) OR (z # x-8) OR (n.value # 42) THEN Fail(71) END;
		INC(callbacks)
	END Callback;

	PROCEDURE Objects;
		VAR c: Child; p: Node; i: INTEGER; item: Meta.Item; got: NodeValue; input: POINTER TO NodeValue;
			args: ARRAY 1 OF POINTER TO Meta.Value; ok: BOOLEAN; a: ANYPTR; address: S.ADDRESS;
	BEGIN
		NEW(c); c.value := 42; c.extra := 7; root := c; address := S.VAL(S.ADDRESS,root);
		IF (address < 100000000L) OR (root.Address() # address+7) OR ~(root IS Child) THEN Fail(61) END;
		NEW(cells,257);
		FOR i := 0 TO LEN(cells)-1 DO NEW(p); p.value := i; cells[i].p := p; cells[i].value := i END;
		SetNode(cells[128].p.next,root);
		Kernel.Collect;
		FOR i := 0 TO LEN(cells)-1 DO IF cells[i].p.value # i THEN Fail(62) END END;
		IF cells[128].p.next # root THEN Fail(63) END;
		Meta.LookupPath("CheckWinHeap64.root",item); item.GetVal(got,ok);
		IF ~ok OR (got.p # root) THEN Fail(64) END;
		Meta.LookupPath("CheckWinHeap64.Child",item); a := item.New();
		IF (a = NIL) OR ~(a IS Child) OR (S.VAL(S.ADDRESS,a) < 100000000L) THEN Fail(65) END;
		NEW(input); input.p := root; args[0] := input;
		Meta.LookupPath("CheckWinHeap64.Echo",item); item.ParamCallVal(args,got,ok);
		IF ~ok OR (got.p # root) THEN Fail(66) END;
		Kernel.Try(Callback,address,address+8,address-8); IF callbacks # 1 THEN Fail(67) END;
		MakeFinal;
		FOR i := 1 TO 5 DO ScrubAndCollect END;
		IF finalized # 1 THEN Fail(68) END;
		Say("high pointers, GC, dispatch, Meta, callbacks and finalizers OK; ")
	END Objects;

	PROCEDURE Storage;
		VAR mode: ARRAY 8 OF CHAR; res,count,i: INTEGER; used: LONGINT; invalid: S.ADDRESS;
	BEGIN
		invalid := Kernel.NewArr(3,MAX(INTEGER)-28,1); IF invalid # 0 THEN Fail(78) END;
		Dialog.GetPar("BB_HEAP_LARGE",mode,res); IF (res = 0) & (mode = "1") THEN count := LEN(blocks) ELSE count := 2 END;
		FOR i := 0 TO count-1 DO
			NEW(blocks[i],blockSize); blocks[i,0] := 17; blocks[i,blockSize-1] := 111;
			IF S.ADR(blocks[i,0]) < 100000000L THEN Fail(72) END
		END;
		used := Kernel.Allocated();
		IF (count = LEN(blocks)) & (used <= 100000000L) THEN Fail(73) END;
		Kernel.Collect;
		FOR i := 0 TO count-1 DO
			IF (blocks[i,0] # 17) OR (blocks[i,blockSize-1] # 111) THEN Fail(74) END
		END;
		IF (count = LEN(blocks)) & (Kernel.Allocated() <= 100000000L) THEN Fail(75) END;
		FOR i := 0 TO count-1 DO blocks[i] := NIL END;
		ScrubAndCollect; ScrubAndCollect;
		IF (count = LEN(blocks)) & (Kernel.Used() >= 40000000H) THEN Fail(76) END;
		NEW(blocks[0],blockSize); blocks[0,blockSize-1] := 99;
		Kernel.Collect; IF blocks[0,blockSize-1] # 99 THEN Fail(77) END;
		blocks[0] := NIL;
		IF count = LEN(blocks) THEN Say("4.25 GiB live heap, collection and reuse OK; ") END
	END Storage;

	PROCEDURE (a: Test) Do;
		VAR win: Windows.Window;
	BEGIN
		Objects; Storage;
		win := Windows.dir.First();
		WHILE win # NIL DO win.seq.SetDirty(FALSE); win := Windows.dir.Next(win) END;
		StdCmds.Exit; Say("WinHeap x64 OK")
	END Do;

	PROCEDURE Setup*;
	BEGIN
		StdConfig.Setup; Kernel.InstallTrapViewer(Trap); NEW(action); Services.DoLater(action,Services.Ticks()+10)
	END Setup;
END CheckWinHeap64.
