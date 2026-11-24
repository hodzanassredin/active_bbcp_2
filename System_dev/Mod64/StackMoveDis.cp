MODULE System_devStackMoveDis;
(** Образцы для инвентаризации адресов стека в коде генератора (docs/stack-copy-study.md):
	разбираются дизассемблером, не исполняются. *)

	TYPE
		R = EXTENSIBLE RECORD x: INTEGER END;
		E = RECORD (R) y: INTEGER END;
		P = POINTER TO R;

	PROCEDURE F (VAR a: INTEGER): INTEGER;
	BEGIN INC(a); RETURN a END F;

	PROCEDURE G (IN s: ARRAY OF CHAR; VAR r: R): INTEGER;
	BEGIN RETURN LEN(s$) + r.x END G;

	PROCEDURE TwoCalls (): INTEGER;
		VAR a, b: INTEGER;
	BEGIN
		a := 1; b := 2;
		RETURN F(a) * 3 + F(b)
	END TwoCalls;

	PROCEDURE NestedArgs (): INTEGER;
		VAR a: INTEGER; r: R; s: ARRAY 8 OF CHAR;
	BEGIN
		a := 1; s := "ab"; r.x := 0;
		RETURN G(s, r) + G("xy", r) * F(a)
	END NestedArgs;

	PROCEDURE With (VAR r: R): INTEGER;
	BEGIN
		WITH r: E DO RETURN r.y + F(r.x) ELSE RETURN 0 END
	END With;

	PROCEDURE Concat (IN a, b: ARRAY OF CHAR): INTEGER;
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		s := a + b; RETURN LEN(s$)
	END Concat;

	PROCEDURE ForCase (n: INTEGER): INTEGER;
		VAR i, s: INTEGER;
	BEGIN
		s := 0;
		FOR i := 0 TO n DO
			CASE i MOD 3 OF 0: INC(s, F(s)) | 1: INC(s) ELSE DEC(s) END
		END;
		RETURN s
	END ForCase;

	PROCEDURE Reals (x: REAL): REAL;
		VAR a: INTEGER;
	BEGIN
		a := 2;
		RETURN x * 2.0 + F(a)
	END Reals;

	PROCEDURE Assign (VAR r: R; i: INTEGER): INTEGER;
		VAR a: ARRAY 4 OF INTEGER; b: INTEGER;
	BEGIN
		b := 1;
		a[i MOD 4] := F(b);
		r.x := F(a[(i + 1) MOD 4]);
		RETURN a[0]
	END Assign;

END System_devStackMoveDis.
