MODULE CheckFiles64;

	IMPORT Files64, LinFiles64;

	PROCEDURE Do*;
	VAR f: Files64.File; wr: Files64.Writer; res: INTEGER; i: LONGINT;
	BEGIN
		f := Files64.dir.New(Files64.dir.This("/media/dia/DATA"), Files64.shared);
		wr := f.NewWriter(NIL);
		i := 0;
		WHILE i < (1024 * 1024) * 1024 * 6 DO  (* 6 Gb file *)
			wr.WriteByte(ORD("A"));
			INC(i);
		END;
		
		f.Register("someName",  "txt", Files64.dontAsk, res);
		
		ASSERT(res = 0, 61);
		
	END Do;

END CheckFiles64.

CheckFiles64.Do

