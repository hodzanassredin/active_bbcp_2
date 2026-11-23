MODULE CheckKernel2;

	IMPORT Kernel, Log := CheckLog;

	CONST
		r1Size = 16; r2Size = 64 * 1024; r3Size = 1024 * 1024;
		a1Size = 16; a2Size = 64 * 1024; a3Size = 1024 * 1024;

	TYPE
		Item = POINTER TO RECORD
			next: Item;
			p1: Item1; p2: Item2; p3: Item3;
			a1, a2, a3: POINTER TO ARRAY OF INTEGER
		END;
		
		Item1 = POINTER TO RECORD
			x: ARRAY r1Size DIV SIZE(INTEGER) OF INTEGER
		END;
		
		Item2 = POINTER TO RECORD
			x: ARRAY r2Size DIV SIZE(INTEGER) OF INTEGER
		END;
		
		Item3 = POINTER TO RECORD
			x: ARRAY r3Size DIV SIZE(INTEGER) OF INTEGER
		END;

	VAR finCnt, maxAllocated: INTEGER;

	PROCEDURE (i: Item) FINALIZE;
	BEGIN
		i.p1 := NIL; i.p2 := NIL; i.p3 := NIL;
		i.a1 := NIL; i.a2 := NIL; i.a3 := NIL;
		INC(finCnt)
	END FINALIZE;

	PROCEDURE NewItem (): Item;
		VAR	res: Item;
	BEGIN
		NEW(res);
		NEW(res.p1);
		NEW(res.p2);
		NEW(res.p3);
		NEW(res.a1, a1Size DIV SIZE(INTEGER));
		NEW(res.a2, a2Size DIV SIZE(INTEGER));
		NEW(res.a3, a3Size DIV SIZE(INTEGER));
		RETURN res
	END NewItem;

	PROCEDURE MakeList (n: INTEGER; OUT first: Item);
		VAR	head, last: Item; m: INTEGER;
	BEGIN
		NEW(head); last := head;
		WHILE 0 < n DO
			last.next := NewItem(); last := last.next;
			DEC(n)
		END;
		first := head.next;
		m := Kernel.Allocated(); IF m > maxAllocated THEN maxAllocated := m END
	END MakeList;

	PROCEDURE ShowStatus;
	BEGIN
		Log.String("finCnt = "); Log.Int(finCnt); Log.Char(9X);
		Log.String("Kernel.Allocated() = "); Log.Int(Kernel.Allocated() DIV 1024); Log.String(" kB, ");
		Log.String("maxAllocated = "); Log.Int(maxAllocated DIV 1024);
		Log.String("kB        ");
		Log.Ln
	END ShowStatus;

	PROCEDURE Do (n: INTEGER);
		VAR	list: Item; i: INTEGER;
	BEGIN
		ASSERT(0 < n, 20);
		i := 0;
		LOOP IF i < n THEN
			MakeList(i, list); INC(i)
		ELSIF i < 2 * n THEN
			MakeList(2*n - i, list); INC(i)
		ELSIF i = 2 * n THEN
			ShowStatus;
			i := 0
		ELSE EXIT END END
	END Do;

	PROCEDURE Start;
	BEGIN
		Log.String("Ctrl+C to stop"); Log.Ln;
		Do(25)
	END Start;

BEGIN
	Start
END CheckKernel2.
