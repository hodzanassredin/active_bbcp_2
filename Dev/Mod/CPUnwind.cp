MODULE DevCPUnwind;

	(* Optional, versioned AMD64 OCF trailer. Not part of runtime metadata. *)

	IMPORT DevCPM;

	CONST magic* = 34365755H; (* UW64 *)

	TYPE
		Entry = POINTER TO RECORD
			next: Entry; first, last, size: INTEGER; bytes: ARRAY 260 OF BYTE
		END;
	VAR first, last: Entry; count: INTEGER;
	PROCEDURE Reset*;
	BEGIN first := NIL; last := NIL; count := 0 END Reset;

	PROCEDURE Add* (beg, end, prolog, frame: INTEGER; IN codes: ARRAY OF BYTE; len: INTEGER);
		VAR e: Entry; i: INTEGER;
	BEGIN
		IF (beg < 0) OR (end <= beg) OR (prolog > 255) OR (len > 254) OR ODD(len) THEN DevCPM.err(200); RETURN END;
		NEW(e); e.first := beg; e.last := end; e.size := (4 + len + 3) DIV 4 * 4;
		e.bytes[0] := 1; e.bytes[1] := SHORT(SHORT(prolog));
		e.bytes[2] := SHORT(SHORT(len DIV 2)); e.bytes[3] := SHORT(SHORT(frame));
		FOR i := 0 TO len - 1 DO e.bytes[4+i] := codes[i] END;
		IF last = NIL THEN first := e ELSE last.next := e END;
		last := e; INC(count)
	END Add;

	PROCEDURE Write*;
		VAR e: Entry; i: INTEGER;
	BEGIN
		IF count = 0 THEN RETURN END;
		DevCPM.ObjWLInt(magic); DevCPM.ObjWLInt(1); DevCPM.ObjWLInt(count);
		e := first;
		WHILE e # NIL DO
			DevCPM.ObjWLInt(e.first); DevCPM.ObjWLInt(e.last); DevCPM.ObjWLInt(e.size);
			FOR i := 0 TO e.size - 1 DO DevCPM.ObjW(SHORT(CHR(e.bytes[i] MOD 256))) END;
			e := e.next
		END
	END Write;

END DevCPUnwind.
