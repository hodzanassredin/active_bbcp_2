MODULE DemoTestsGui;
(** GUI-сценарий этапа 3/1 без участия человека.

	Запускается из tools/test-gui.sh хостом под broadwayd:
	`blackbox --BB_RUN=DemoTestsGui.Run`. Команда только заводит две задачи
	(производитель и потребитель ограниченного буфера на [exclusive] и
	ACTIVE.AWAIT) и действие-тикер; дальше крутится штатный главный цикл
	Loop.Start с окнами GTK4.

	Тикер идёт каждые 100 мс и меряет промежутки: если цикл замер, шаг
	вырастает. Итог печатается в stdout, код выхода — 0 только при
	верном результате, завершённых задачах и нормальном шаге. *)

	IMPORT ACTIVE, Kernel, Services, Tasks, Console, Strings;

	CONST
		n = 100;
		capacity = 8;
		tickMs = 100;
		minTicks = 20;
		maxTicks = 150;
		maxGap = 500;	(* мс; больше — считаем, что цикл замер *)

	TYPE
		Buffer = POINTER TO RECORD
			item: ARRAY capacity OF INTEGER;
			head, tail, count: INTEGER
		END;
		Producer = POINTER TO RECORD (Tasks.Task) b: Buffer END;
		Consumer = POINTER TO RECORD (Tasks.Task) b: Buffer END;
		Ticker = POINTER TO RECORD (Services.Action)
			ticks: INTEGER;
			last, maxGap, started: LONGINT
		END;

	VAR
		produced, consumed, sum, badOrder: INTEGER;
		dlg: ARRAY 64 OF CHAR;

	PROCEDURE [exclusive] (b: Buffer) Put (x: INTEGER), NEW;
	BEGIN
		ACTIVE.AWAIT(b.count < capacity);
		b.item[b.tail] := x;
		b.tail := (b.tail + 1) MOD capacity;
		INC(b.count)
	END Put;

	PROCEDURE [exclusive] (b: Buffer) Get (OUT x: INTEGER), NEW;
	BEGIN
		ACTIVE.AWAIT(b.count > 0);
		x := b.item[b.head];
		b.head := (b.head + 1) MOD capacity;
		DEC(b.count)
	END Get;

	PROCEDURE (p: Producer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO n DO p.b.Put(i) END;
		produced := 1
	END Run;

	PROCEDURE (c: Consumer) Run;
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO n DO
			c.b.Get(x);
			IF x # i THEN badOrder := 1 END;
			INC(sum, x);
			consumed := i
		END
	END Run;

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
			IF ok THEN Console.WriteStr("DemoTestsGui: ok=1")
			ELSE Console.WriteStr("DemoTestsGui: ok=0")
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

END DemoTestsGui.
