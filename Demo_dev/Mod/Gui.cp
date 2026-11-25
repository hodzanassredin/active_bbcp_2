MODULE Demo_devGui;
(** GUI-сценарий без участия человека.

	Запускается из tools/test-gui.sh хостом под broadwayd:
	`blackbox --BB_RUN=Demo_devGui.Run`. Команда только заводит две задачи
	(производитель и потребитель ограниченного буфера на мониторе-объекте
	Tasks.Object) и действие-тикер; дальше крутится штатный главный цикл
	Loop.Start с окнами GTK4.

	Тикер идёт каждые 100 мс и меряет промежутки: если цикл замер, шаг
	вырастает. Итог печатается в stdout, код выхода — 0 только при
	верном результате, завершённых задачах и нормальном шаге. *)

	IMPORT Kernel, Services, Tasks, Console, Strings;

	CONST
		n = 100;
		capacity = 8;
		tickMs = 100;
		minTicks = 20;
		maxTicks = 150;
		maxGap = 500;	(* мс; больше — считаем, что цикл замер *)
		empty = 0;	(* флаг Ready: есть место *)
		full = 1;	(* флаг Ready: есть элемент *)

	TYPE
		PutOp = RECORD (Tasks.Op) x: INTEGER END;
		GetOp = RECORD (Tasks.Op) x: INTEGER END;

		Buffer = POINTER TO RECORD (Tasks.Object)
			item: ARRAY capacity OF INTEGER;
			head, tail, count: INTEGER
		END;
		Producer = POINTER TO RECORD (Tasks.Job) b: Buffer END;
		Consumer = POINTER TO RECORD (Tasks.Job) b: Buffer END;
		Ticker = POINTER TO RECORD (Services.Action)
			ticks: INTEGER;
			last, maxGap, started: LONGINT
		END;

	VAR
		produced, consumed, sum, badOrder: INTEGER;
		dlg: ARRAY 64 OF CHAR;

	PROCEDURE (b: Buffer) Ready* (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF b.count < capacity THEN INCL(s, empty) END;
		IF b.count > 0 THEN INCL(s, full) END;
		RETURN s
	END Ready;

	PROCEDURE (b: Buffer) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = capacity DO
				IF ~b.Await(ctx, {empty}) THEN RETURN END
			END;
			b.item[b.tail] := op.x;
			b.tail := (b.tail + 1) MOD capacity;
			INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO
				IF ~b.Await(ctx, {full}) THEN RETURN END
			END;
			op.x := b.item[b.head];
			b.head := (b.head + 1) MOD capacity;
			DEC(b.count)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: Producer) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO n DO op.x := i; p.b.Do(ctx, op) END;
		produced := 1
	END Body;

	PROCEDURE (c: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO n DO
			op.x := 0; c.b.Do(ctx, op);
			IF op.x # i THEN badOrder := 1 END;
			INC(sum, op.x);
			consumed := i
		END
	END Body;

	PROCEDURE Out (IN label: ARRAY OF CHAR; x: LONGINT);
	BEGIN
		Console.WriteStr(label); Strings.IntToString(x, dlg);
		Console.WriteStr(dlg)
	END Out;

	PROCEDURE (t: Ticker) Do;
		VAR now, gap: LONGINT; ok: BOOLEAN;
	BEGIN
		now := Kernel.Time();
		IF t.last # 0 THEN
			gap := now - t.last;
			IF gap > t.maxGap THEN t.maxGap := gap END
		END;
		t.last := now; INC(t.ticks);
		IF (produced = 1) & (consumed = n) & (t.ticks >= minTicks) OR (t.ticks >= maxTicks) THEN
			ok := (produced = 1) & (consumed = n) & (badOrder = 0)
				& (sum = n * (n + 1) DIV 2) & (t.maxGap <= maxGap) & (t.ticks < maxTicks);
			IF ok THEN Console.WriteStr("Demo_devGui: ok=1")
			ELSE Console.WriteStr("Demo_devGui: ok=0")
			END;
			Out(" ticks=", t.ticks); Out(" maxgap=", t.maxGap);
			Out(" consumed=", consumed); Out(" sum=", sum);
			Out(" elapsed=", now - t.started);
			Out(" badorder=", badOrder); Console.WriteLn;
			IF ok THEN Kernel.Quit(0) ELSE Kernel.Quit(1) END
		ELSE
			Services.DoLater(t, Services.Ticks() + tickMs)
		END
	END Do;

	PROCEDURE Run*;
		VAR b: Buffer; p: Producer; c: Consumer; t: Ticker;
	BEGIN
		produced := 0; consumed := 0; sum := 0; badOrder := 0;
		NEW(b); NEW(p); p.b := b; NEW(c); c.b := b;
		Tasks.Start(p); Tasks.Start(c);
		NEW(t); t.ticks := 0; t.last := 0; t.maxGap := 0; t.started := Kernel.Time();
		Services.DoLater(t, Services.Ticks() + tickMs)
	END Run;

END Demo_devGui.
