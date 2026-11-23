MODULE ObxActions;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT Services, Views, TextModels, TextMappers, TextViews, Math, Dialog;

	TYPE
		PrimeAction = POINTER TO RECORD (Services.Action)
			to, stepSize, current, divisor: INTEGER;
			f: TextMappers.Formatter
		END;

	VAR
		primeDlg*: RECORD
			to*, stepSize*: INTEGER;
		END;

	PROCEDURE Step (stepSize, to: INTEGER; VAR cur, divisor: INTEGER; f: TextMappers.Formatter);
		VAR end, sqrtCur: INTEGER;
	BEGIN
		WHILE (stepSize > 0) & (cur <= to) DO
			end := divisor + stepSize;
			sqrtCur := SHORT(ENTIER(Math.Sqrt(cur)));
			WHILE (divisor <= sqrtCur) & (divisor < end) & (cur MOD divisor # 0) DO divisor := divisor + 2 END;
			stepSize := end - divisor;
			IF divisor > sqrtCur THEN	(* cur is a prime *)
				f.WriteInt(cur); f.WriteLn;
				divisor := 3; cur := cur + 2
			ELSIF divisor < end THEN	(* cur is not a prime, test next one *)
				divisor := 3; cur := cur + 2
			ELSE	(* time exhausted, continue test next time *)
			END
		END
	END Step;

	PROCEDURE (a: PrimeAction) Do;
	BEGIN
		Step(a.stepSize, a.to, a.current, a.divisor, a.f);
		IF a.current <= a.to THEN
			Services.DoLater(a, Services.now)
		ELSE
			Dialog.ShowMsg("prime number calculation finished")
		END ;
	END Do;

	PROCEDURE StartPrimes*;
		VAR a: PrimeAction;
	BEGIN
		NEW(a);
		a.to := primeDlg.to; a.stepSize := primeDlg.stepSize; a.current := 3; a.divisor := 3;
		IF a.stepSize < 1 THEN a.stepSize := 1 END ;
		a.f.ConnectTo(TextModels.dir.New());
		a.f.WriteInt(2); a.f.WriteLn;
		Views.OpenAux(TextViews.dir.New(a.f.rider.Base()), "");
		Services.DoLater(a, Services.now);
		Dialog.ShowMsg("prime number calculation started")
	END StartPrimes;

BEGIN
	primeDlg.to := 1000; primeDlg.stepSize := 10
END ObxActions.
