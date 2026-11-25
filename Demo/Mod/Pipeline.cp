MODULE DemoPipeline;
(** Конвейер с fan-out/fan-in.

	Перенесено из статьи Sameer Ajmani "Go Concurrency Patterns:
	Pipelines and Cancellation" (Go Blog, 2014): стадии generator ->
	square -> fan-out на двух рабочих -> fan-in в общий итог.
	Источники:
	  https://go.dev/blog/pipelines
	  https://go.dev/talks/2012/concurrency.slide#33 (fan-in)
	В Go стадии — горутины и каналы; здесь задачи Tasks и каналы-
	встречи AsyncChan.Chan. Признак конца — сообщение с n = -1.

	Команда DemoPipeline.Run: квадраты чисел 1..10. Итог
	детерминирован: 10 результатов, сумма 385. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, DemoSupport;

	CONST
		count = 10;
		workers = 2;

	TYPE
		IntMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Generator = POINTER TO RECORD (Tasks.Task) out: AsyncChan.Chan END;

		Dispatcher = POINTER TO RECORD (Tasks.Task)
			in, a, b: AsyncChan.Chan
		END;

		Worker = POINTER TO RECORD (Tasks.Task)
			in, out: AsyncChan.Chan
		END;

		Merger = POINTER TO RECORD (Tasks.Task) in: AsyncChan.Chan END;

	VAR
		Sum*: INTEGER;
		Count*: INTEGER;
		Failed*: BOOLEAN;

	PROCEDURE NewInt (n: INTEGER): IntMsg;
		VAR m: IntMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END NewInt;

	PROCEDURE (g: Generator) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO count DO g.out.Send(NewInt(i)) END;
		g.out.Send(NewInt(-1))
	END Run;

	PROCEDURE (d: Dispatcher) Run;
		VAR m: AsyncSelect.Msg; n, turn: INTEGER;
	BEGIN
		turn := 0;
		LOOP
			d.in.Recv(m);
			WITH m: IntMsg DO n := m.n ELSE n := -1 END;
			IF n < 0 THEN
				d.a.Send(NewInt(-1)); d.b.Send(NewInt(-1));
				EXIT
			END;
			(* fan-out: по кругу *)
			IF turn = 0 THEN d.a.Send(NewInt(n)) ELSE d.b.Send(NewInt(n)) END;
			turn := 1 - turn
		END
	END Run;

	PROCEDURE (w: Worker) Run;
		VAR m: AsyncSelect.Msg; n: INTEGER;
	BEGIN
		LOOP
			w.in.Recv(m);
			WITH m: IntMsg DO n := m.n ELSE n := -1 END;
			IF n < 0 THEN w.out.Send(NewInt(-1)); EXIT END;
			w.out.Send(NewInt(n * n))
		END
	END Run;

	PROCEDURE (m: Merger) Run;
		VAR msg: AsyncSelect.Msg; n: INTEGER;
	BEGIN
		LOOP
			m.in.Recv(msg);
			WITH msg: IntMsg DO n := msg.n ELSE n := -1 END;
			IF n < 0 THEN EXIT END;
			INC(Sum, n); INC(Count)		(* fan-in в общий итог *)
		END
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: 1 -> 2 -> квадрат -> 2 слияния. *)
		VAR src, a, b, ra, rb: AsyncChan.Chan;
			g: Generator; d: Dispatcher; w: ARRAY workers OF Worker;
			m: ARRAY workers OF Merger; i: INTEGER; ok: BOOLEAN;
	BEGIN
		Sum := 0; Count := 0; Failed := FALSE;
		src := AsyncChan.NewChan(); a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		ra := AsyncChan.NewChan(); rb := AsyncChan.NewChan();
		NEW(g); g.out := src;
		NEW(d); d.in := src; d.a := a; d.b := b;
		NEW(w[0]); w[0].in := a; w[0].out := ra;
		NEW(w[1]); w[1].in := b; w[1].out := rb;
		NEW(m[0]); m[0].in := ra;
		NEW(m[1]); m[1].in := rb;
		FOR i := 0 TO workers - 1 DO Tasks.Start(m[i]) END;
		FOR i := 0 TO workers - 1 DO Tasks.Start(w[i]) END;
		Tasks.Start(d); Tasks.Start(g);
		ok := DemoSupport.Pump(15000);
		Failed := ~ok OR Tasks.Failed(g) OR Tasks.Failed(d);
		FOR i := 0 TO workers - 1 DO
			IF Tasks.Failed(w[i]) OR Tasks.Failed(m[i]) THEN Failed := TRUE END
		END;
		DemoSupport.LogInt("DemoPipeline: count=", Count);
		DemoSupport.LogInt(" sum=", Sum);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoPipeline.
