MODULE ObxBank3;
	IMPORT ACTIVE, Tasks, Console;

	TYPE
		Account = POINTER TO RECORD balance: INTEGER END;
		Clerk = POINTER TO RECORD (Tasks.Task) from, to: Account END;

	VAR a, b: Account; c1, c2: Clerk;

	PROCEDURE [exclusive] (acc: Account) Deposit (n: INTEGER), NEW;
	BEGIN
		acc.balance := acc.balance + n
	END Deposit;

	PROCEDURE [exclusive] (acc: Account) TransferTo (to: Account; n: INTEGER), NEW;
	BEGIN
		acc.balance := acc.balance - n;
		Tasks.Yield;	(* держит монитор acc и уступает *)
		to.Deposit(n)	(* вход во второй монитор *)
	END TransferTo;

	PROCEDURE (c: Clerk) Run;
	BEGIN
		c.from.TransferTo(c.to, 10)
	END Run;

	PROCEDURE Do*;
	BEGIN
		NEW(a); a.balance := 100; NEW(b); b.balance := 100;
		NEW(c1); c1.from := a; c1.to := b; Tasks.SetName(c1, "clerk a->b");
		NEW(c2); c2.from := b; c2.to := a; Tasks.SetName(c2, "clerk b->a");
		Tasks.Start(c1); Tasks.Start(c2);
		WHILE (Tasks.Count() > 0) & ~Tasks.Deadlocked() DO Tasks.Step END;
		IF Tasks.Deadlocked() THEN
			Console.WriteStr("deadlock, cancelling"); Console.WriteLn;
			Tasks.Cancel(c1); Tasks.Cancel(c2);
			WHILE Tasks.Count() > 0 DO Tasks.Step END
		END
	END Do;

END ObxBank3.
