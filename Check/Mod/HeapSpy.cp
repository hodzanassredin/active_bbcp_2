MODULE CheckHeapSpy;

	IMPORT S := SYSTEM, Kernel, Console, Strings, DevHeapSpy, Views, Windows, StdWindows,
		Controllers, Ports, G := LinGtk2GLib, GU := LinGtk4Util;

	TYPE Cluster = POINTER TO RECORD [untagged] size: INTEGER; next: Cluster END;

	VAR blocks: ARRAY 16 OF POINTER TO ARRAY OF BYTE;
		largeBlocks: ARRAY 512 OF POINTER TO ARRAY OF BYTE;

	PROCEDURE Fail (code: INTEGER);
		VAR n: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(code,n); Console.WriteStr("Heap Spy FAILED "); Console.WriteStr(n); Console.WriteLn; Kernel.Quit(code)
	END Fail;

	PROCEDURE Open*;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO LEN(blocks)-1 DO NEW(blocks[i],8*1024*1024); blocks[i][0] := 1 END;
		DevHeapSpy.ShowHeap
	END Open;

	PROCEDURE Open4G*;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO LEN(largeBlocks)-1 DO
			NEW(largeBlocks[i],8*1024*1024); largeBlocks[i][0] := 1
		END;
		Console.WriteStr("Heap Spy: 4 GiB allocated"); Console.WriteLn;
		DevHeapSpy.ShowHeap
	END Open4G;

	PROCEDURE Verify*;
		VAR i,j,res,w,h,dot,scale: INTEGER; expected: LONGINT; c: Cluster;
			win: Windows.Window; title: Views.Title; scroll: Controllers.ScrollMsg; poll: Controllers.PollSectionMsg;
	BEGIN
		(* A fixed 8-byte scale exceeds the document coordinate limit above about 42 MiB. *)
		Open;
		win := Windows.dir.First();
		LOOP
			IF win = NIL THEN EXIT END; win.GetTitle(title); IF title = "Heap Layout" THEN EXIT END; win := Windows.dir.Next(win)
		END;
		IF win = NIL THEN Fail(71) END;
		FOR j := 0 TO 29 DO StdWindows.Validate(NIL); res := GU.Iterate(0); G.g_usleep(10000) END;
		win.doc.ThisView().context.GetSize(w,h); dot := MAX(win.frame.dot,(Ports.point DIV win.frame.dot)*win.frame.dot);
		scale := 8;
		LOOP
			expected := 8; c := S.VAL(Cluster,Kernel.Root());
			WHILE c # NIL DO
				INC(expected,11+((c.size DIV scale+511) DIV 512)*8); c := c.next
			END;
			IF expected <= (MAX(INTEGER) DIV 2) DIV dot THEN EXIT END;
			scale := scale*2
		END;
		Console.WriteStr("Expected rows: "); Strings.IntToString(expected,title); Console.WriteStr(title); Console.WriteStr(" height: "); Strings.IntToString(h,title); Console.WriteStr(title); Console.WriteLn;
		IF scale = 8 THEN Fail(72) END;
		IF h # expected*dot THEN Fail(73) END;
		scroll.vertical := TRUE; scroll.focus := FALSE; scroll.op := Controllers.gotoPos; scroll.pos := MAX(INTEGER); scroll.done := FALSE;
		win.ForwardCtrlMsg(scroll); StdWindows.Validate(NIL);
		poll.vertical := TRUE; poll.focus := FALSE; poll.done := FALSE; win.ForwardCtrlMsg(poll);
		Strings.IntToString(poll.wholeSize,title); Console.WriteStr("Whole: "); Console.WriteStr(title);
		Strings.IntToString(poll.partSize,title); Console.WriteStr(" part: "); Console.WriteStr(title);
		Strings.IntToString(poll.partPos,title); Console.WriteStr(" pos: "); Console.WriteStr(title); Console.WriteLn;
		IF ~poll.done OR ~poll.valid OR (poll.partPos # poll.wholeSize-poll.partSize) THEN Fail(74) END;
		FOR i := 0 TO LEN(blocks)-1 DO IF blocks[i][0] # 1 THEN Fail(75) END END;
		Console.WriteStr("Heap Spy large heap layout and bottom scrolling OK"); Console.WriteLn; Kernel.Quit(0)
	END Verify;

END CheckHeapSpy.
