MODULE ObxBank5;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	CONST n = 3;

	TYPE
		Request = POINTER TO RECORD (AsyncSelect.Msg)
			from, to, sum: INTEGER;
			reply: AsyncChan.Chan	(* сюда кассир кладёт ответ *)
		END;
		Answer = POINTER TO RECORD (AsyncSelect.Msg) ok: BOOLEAN END;
		Cashier = POINTER TO RECORD (Tasks.Task) END;
		Client = POINTER TO RECORD (Tasks.Task) id, sum: INTEGER END;
		Manager = POINTER TO RECORD (Tasks.Task) END;

	VAR window: AsyncChan.Mailbox;	(* окошко кассы *)

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	(* Счета принадлежат кассиру: их видит только эта задача *)
	PROCEDURE (c: Cashier) Run;
		VAR balance: ARRAY n OF INTEGER; m: AsyncSelect.Msg; a: Answer; i, total: INTEGER;
	BEGIN
		balance[0] := 100; balance[1] := 100; balance[2] := 100;
		LOOP
			window.Recv(m);
			IF m = NIL THEN EXIT END;	(* касса закрывается *)
			WITH m: Request DO
				NEW(a); a.ok := balance[m.from] >= m.sum;
				IF a.ok THEN
					balance[m.from] := balance[m.from] - m.sum;
					balance[m.to] := balance[m.to] + m.sum
				END;
				m.reply.Send(a)
			END
		END;
		total := 0;
		FOR i := 0 TO n - 1 DO Say("balance ", balance[i]); total := total + balance[i] END;
		Say("total ", total)
	END Run;

	PROCEDURE (c: Client) Run;
		VAR r: Request; m: AsyncSelect.Msg;
	BEGIN
		NEW(r); r.from := c.id; r.to := (c.id + 1) MOD n; r.sum := c.sum;
		r.reply := AsyncChan.NewChan();
		window.Send(r);
		r.reply.Recv(m);
		IF m(Answer).ok THEN Say("done, client ", c.id) ELSE Say("refused, client ", c.id) END
	END Run;

	PROCEDURE (m: Manager) Run;
		VAR c: ARRAY n OF Client; i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO
			NEW(c[i]); c[i].id := i; c[i].sum := 60 + 40 * i;
			IF i = 2 THEN c[i].sum := 250 END;	(* столько нет ни на одном счёте *)
			Tasks.Start(c[i])
		END;
		FOR i := 0 TO n - 1 DO Tasks.Join(c[i]) END;
		window.Send(NIL)	(* закрыть кассу *)
	END Run;

	PROCEDURE Do*;
		VAR k: Cashier; m: Manager;
	BEGIN
		window := AsyncChan.NewMailbox();
		NEW(k); Tasks.Start(k); NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxBank5.
