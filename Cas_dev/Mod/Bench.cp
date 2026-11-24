MODULE Cas_devBench;
(** Замер скорости CasHash и CasChunk. Каждый замер печатает строку
	BENCH <имя> bytes=… ms=… mbPerS=…; порога нет, тесты TestSlow… зовут
	команды и проверяют только итог работы, а не время. Вход — 16 МБ
	xorshift32, одна нить. *)

	IMPORT SYSTEM, Services, Console, Strings, CasHash, CasChunk, UnitAssert;

	CONST
		size = 16 * 1024 * 1024;

	VAR
		data: POINTER TO ARRAY OF BYTE;
		cuts: INTEGER;

	PROCEDURE Fill;
		VAR i, x, v: INTEGER;
	BEGIN
		IF data = NIL THEN
			NEW(data, size); x := 1;
			FOR i := 0 TO size - 1 DO
				x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 13)));
				x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, -17)));
				x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 5)));
				v := x MOD 256; IF v >= 128 THEN DEC(v, 256) END;
				data[i] := SHORT(SHORT(v))
			END
		END
	END Fill;

	PROCEDURE Report (IN name: ARRAY OF CHAR; ms: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		IF ms < 1 THEN ms := 1 END;
		Console.WriteStr("BENCH "); Console.WriteStr(name);
		Strings.IntToString(size, s); Console.WriteStr(" bytes="); Console.WriteStr(s);
		Strings.IntToString(ms, s); Console.WriteStr(" ms="); Console.WriteStr(s);
		Strings.RealToStringForm(size / 1048576.0 / (ms / 1000.0), 4, 0, 0, " ", s);
		Console.WriteStr(" mbPerS="); Console.WriteStr(s); Console.WriteLn
	END Report;

	PROCEDURE Sha*;
		VAR t: LONGINT; id: CasHash.Id;
	BEGIN
		Fill;
		t := Services.Ticks();
		CasHash.Of(data, 0, size, id);
		Report("cas-sha256", Services.Ticks() - t)
	END Sha;

	PROCEDURE Chunk*;
		VAR t: LONGINT; b: INTEGER; p: CasChunk.Params;
	BEGIN
		Fill; CasChunk.Default(p);
		t := Services.Ticks();
		b := 0; cuts := 0;
		WHILE b < size DO b := CasChunk.Cut(data, b, size, p); INC(cuts) END;
		Report("cas-chunk", Services.Ticks() - t)
	END Chunk;

	PROCEDURE TestSlowSha*;
	BEGIN
		Sha; UnitAssert.Checked
	END TestSlowSha;

	PROCEDURE TestSlowChunk*;
	BEGIN
		Chunk;
		UnitAssert.True((cuts > size DIV 65536) & (cuts < size DIV 2048), "число кусков в пределах")
	END TestSlowChunk;

END Cas_devBench.
