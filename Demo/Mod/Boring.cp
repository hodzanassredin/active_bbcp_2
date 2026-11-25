MODULE DemoBoring;
(** «Скучный» генератор и мультиплексор.

	Перенесено из доклада Rob Pike "Go Concurrency Patterns" (Google,
	2012): генератор boring шлёт сообщения, multiplexer (fan-in)
	сливает два канала в один. Источник:
	  https://go.dev/talks/2012/concurrency.slide#25
	  https://go.dev/talks/2012/concurrency.slide#30
	В Go мультиплексор использует select; здесь — AsyncChan.Select по
	двум приёмам, признак конца — сообщение n = -1.

	Команда DemoBoring.Run: два генератора по 8 сообщений. Итог
	детерминирован: 16 сообщений, по 8 от каждого. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, DemoSupport;

	CONST
		perGenerator = 8;

	TYPE
		BoringMsg = POINTER TO RECORD (AsyncSelect.Msg)
			who: INTEGER;
			n: INTEGER
		END;

		Generator = POINTER TO RECORD (Tasks.Task)
			out: AsyncChan.Chan;
			who: INTEGER
		END;

		Mux = POINTER TO RECORD (Tasks.Task)
			a, b: AsyncChan.Chan
		END;

	VAR
		Messages*: INTEGER;
		FromA*: INTEGER;
		FromB*: INTEGER;
		Failed*: BOOLEAN;

	PROCEDURE NewMsg (who, n: INTEGER): BoringMsg;
		VAR m: BoringMsg;
	BEGIN
		NEW(m); m.who := who; m.n := n;
		RETURN m
	END NewMsg;

	PROCEDURE (g: Generator) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO perGenerator DO
			g.out.Send(NewMsg(g.who, i));
			Tasks.Sleep(1)		(* «скучная» работа *)
		END;
		g.out.Send(NewMsg(g.who, -1))
	END Run;

	PROCEDURE (m: Mux) Run;
		VAR ops: ARRAY 2 OF AsyncSelect.Op; done, idx: INTEGER;
			msg: AsyncSelect.Msg; who, n: INTEGER;
	BEGIN
		done := 0;
		LOOP
			AsyncChan.RecvAlt(m.a, ops[0]);
			AsyncChan.RecvAlt(m.b, ops[1]);
			idx := AsyncChan.Select(ops);
			msg := ops[idx].got;
			WITH msg: BoringMsg DO who := msg.who; n := msg.n
			ELSE who := -1; n := -1
			END;
			IF n < 0 THEN
				INC(done);
				IF done = 2 THEN EXIT END
			ELSE
				INC(Messages);
				IF who = 0 THEN INC(FromA) ELSE INC(FromB) END
			END
		END
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: два генератора и один мультиплексор. *)
		VAR a, b: AsyncChan.Chan; g1, g2: Generator; m: Mux;
			ok: BOOLEAN;
	BEGIN
		Messages := 0; FromA := 0; FromB := 0; Failed := FALSE;
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(g1); g1.out := a; g1.who := 0;
		NEW(g2); g2.out := b; g2.who := 1;
		NEW(m); m.a := a; m.b := b;
		Tasks.Start(m); Tasks.Start(g1); Tasks.Start(g2);
		ok := DemoSupport.Pump(15000);
		Failed := ~ok OR Tasks.Failed(m) OR Tasks.Failed(g1) OR Tasks.Failed(g2);
		DemoSupport.LogInt("DemoBoring: messages=", Messages);
		DemoSupport.LogInt(" fromA=", FromA);
		DemoSupport.LogInt(" fromB=", FromB);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoBoring.
