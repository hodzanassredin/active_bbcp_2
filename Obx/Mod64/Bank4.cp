MODULE ObxBank4;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST n = 3;

	TYPE
		Transfer = POINTER TO RECORD (AsyncStm.Tx) from, to, sum: INTEGER END;
		Either = POINTER TO RECORD (AsyncStm.Tx) first, second: Transfer END;
		Clerk = POINTER TO RECORD (Tasks.Task) END;
		Buyer = POINTER TO RECORD (Tasks.Task) END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;

	VAR
		balance: ARRAY n OF AsyncStm.IntVar;
		frozen: AsyncStm.BoolVar;	(* счёт 2 арестован: зачислять на него нельзя *)

	PROCEDURE Show (IN what: ARRAY OF CHAR);
		VAR i, sum: INTEGER; s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(what); Console.WriteStr(":"); sum := 0;
		FOR i := 0 TO n - 1 DO
			Strings.IntToString(balance[i].Get(), s); Console.WriteStr(" "); Console.WriteStr(s);
			sum := sum + balance[i].Get()
		END;
		Strings.IntToString(sum, s); Console.WriteStr(", total "); Console.WriteStr(s); Console.WriteLn
	END Show;

	PROCEDURE (t: Transfer) Do;
		VAR from, to: AsyncStm.IntVar;
	BEGIN
		from := balance[t.from]; to := balance[t.to];
		IF from.Get() < t.sum THEN AsyncStm.Retry END;	(* ждать денег *)
		from.Set(from.Get() - t.sum);
		IF (t.to = 2) & frozen.Get() THEN HALT(30) END;	(* трап посреди перевода *)
		to.Set(to.Get() + t.sum)
	END Do;

	PROCEDURE (t: Either) Do;
	BEGIN
		AsyncStm.OrElse(t.first, t.second)	(* с первого счёта, а нет — со второго *)
	END Do;

	PROCEDURE NewTransfer (from, to, sum: INTEGER): Transfer;
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := from; t.to := to; t.sum := sum;
		RETURN t
	END NewTransfer;

	PROCEDURE (c: Clerk) Run;
	BEGIN
		AsyncStm.Atomically(NewTransfer(0, 2, 50))
	END Run;

	PROCEDURE (b: Buyer) Run;
		VAR t: Either;
	BEGIN
		NEW(t); t.first := NewTransfer(1, 0, 120); t.second := NewTransfer(2, 0, 120);
		AsyncStm.Atomically(t);
		Show("buyer paid 120")
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10);
		balance[2].Set(balance[2].Get() + 100);
		Show("salary 100 to account 2")
	END Run;

	PROCEDURE Do*;
		VAR c: Clerk; b: Buyer; p: Payroll;
	BEGIN
		balance[0] := AsyncStm.NewInt(100); balance[1] := AsyncStm.NewInt(100);
		balance[2] := AsyncStm.NewInt(50); frozen := AsyncStm.NewBool(TRUE);
		Show("start");
		NEW(c); Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.Failed(c) THEN Show("clerk trapped") END;
		NEW(b); NEW(p); Tasks.Start(b); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxBank4.
