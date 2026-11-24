MODULE ObxStm1;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST price = 250;

	TYPE
		Buy = POINTER TO RECORD (AsyncStm.Tx) tries: INTEGER END;
		Buyer = POINTER TO RECORD (Tasks.Task) END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;
		Opener = POINTER TO RECORD (Tasks.Task) END;

	VAR
		money: AsyncStm.IntVar;	(* счёт покупателя *)
		open: AsyncStm.BoolVar;	(* магазин открыт *)

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (t: Buy) Do;
	BEGIN
		INC(t.tries);	(* обычное поле: откат его не трогает *)
		IF (money.Get() < price) OR ~open.Get() THEN AsyncStm.Retry END;
		money.Set(money.Get() - price)
	END Do;

	PROCEDURE (b: Buyer) Run;
		VAR t: Buy;
	BEGIN
		NEW(t); t.tries := 0;
		AsyncStm.Atomically(t);
		Say("bought, money = ", money.Get());
		Say("transaction ran times: ", t.tries)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10);
		money.Set(money.Get() + 200);
		Say("salary paid, money = ", money.Get())
	END Run;

	PROCEDURE (o: Opener) Run;
	BEGIN
		Tasks.Sleep(20);
		open.Set(TRUE);
		Console.WriteStr("shop opened"); Console.WriteLn
	END Run;

	PROCEDURE Do*;
		VAR b: Buyer; p: Payroll; o: Opener;
	BEGIN
		money := AsyncStm.NewInt(100); open := AsyncStm.NewBool(FALSE);
		NEW(b); NEW(p); NEW(o);
		Tasks.Start(b); Tasks.Start(p); Tasks.Start(o);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxStm1.
