MODULE DemoAnts;
(** Муравьи: много координирующихся активных объектов с визуализацией.

	Постановка: поле-сетка, муравейник, еда, феромон. Каждый муравей —
	задача Tasks со своим стеком: ищет еду, несёт в муравейник,
	оставляет феромон. Поле целиком — монитор ([exclusive] методы
	Field.Step и Field.Evaporate); феромон испаряет отдельная задача.
	Муравьи сами не рисуют: View.Restore читает поле и рисует сетку,
	перерисовку заказывает таймер Services.Action через Views.Update.

	Команды:
		DemoAnts.Open       — открыть окно (GUI) и запустить
		DemoAnts.Start      — пуск
		DemoAnts.Stop       — остановка
		DemoAnts.Reset      — сброс поля и муравьёв
		DemoAnts.Run        — консольный прогон с ASCII-кадрами
		DemoAnts.Simulate   — прогон без вывода (для Unit)

	Стенд: один поток ОС и кооперативные задачи, поэтому чтение поля в
	Restore между шагами планировщика безопасно и без монитора. *)

	IMPORT ACTIVE, Kernel, Services, Tasks, Console, Strings,
		Views, Ports, Properties, Fonts, DemoSupport;

	CONST
		W* = 48; H* = 32;		(* размер поля *)
		maxAnts = 1000;
		maxPher = 20;
		defaultAnts = 200;
		defaultSteps = 100;
		frameEvery = 25;		(* ASCII-кадр раз в 25 шагов муравья *)
		nestX0 = W DIV 2; nestY0 = H DIV 2;

		pherColor = 00C0C0C0H;	(* светлый феромон *)
		nestColor = 00808000H;	(* муравейник *)

	TYPE
		Field* = POINTER TO RECORD
			food: ARRAY W * H OF INTEGER;
			pher: ARRAY W * H OF INTEGER;
			antX, antY: ARRAY maxAnts OF INTEGER;
			antCarry: ARRAY maxAnts OF BOOLEAN;
			ants*, alive: INTEGER;
			nestX*, nestY: INTEGER;
			nestFood*: LONGINT;
			moves: LONGINT;
			limit: LONGINT;	(* 0 — без предела *)
			stop: BOOLEAN;
			seed: LONGINT
		END;

		Ant = POINTER TO RECORD (Tasks.Task)
			f: Field;
			id: INTEGER
		END;

		Evaporator = POINTER TO RECORD (Tasks.Task) f: Field END;

		View* = POINTER TO RECORD (Views.View) f: Field END;

		Timer = POINTER TO RECORD (Services.Action) v: View END;

		Checker = POINTER TO RECORD (Services.Action) ticks: INTEGER END;

	VAR
		field: Field;
		antTasks: ARRAY maxAnts OF Ant;
		evap: Evaporator;
		running: BOOLEAN;

		Left*: INTEGER;		(* живых муравьёв *)
		LastOk*: BOOLEAN;	(* прогон завершился сам *)
		Failed*: BOOLEAN;	(* трап в задаче *)
		Deadlocked*: BOOLEAN;

		guiView: View;
		timer: Timer;
		timerActive: BOOLEAN;
		GuiTicks*, Restores*: INTEGER;

	(* --- поле ------------------------------------------------------------- *)

	PROCEDURE (f: Field) Random (): LONGINT, NEW;
	BEGIN
		f.seed := (f.seed * 1103515245 + 12345) MOD 2147483648;
		RETURN f.seed
	END Random;

	PROCEDURE PutFood (f: Field; cx, cy, r, amount: INTEGER);
		VAR x, y: INTEGER;
	BEGIN
		FOR y := cy - r TO cy + r DO
			FOR x := cx - r TO cx + r DO
				IF (x >= 0) & (x < W) & (y >= 0) & (y < H) THEN
					IF f.food[y * W + x] < amount THEN f.food[y * W + x] := amount END
				END
			END
		END
	END PutFood;

	PROCEDURE [exclusive] (f: Field) Step* (id: INTEGER;
		VAR x, y: INTEGER; VAR carry: BOOLEAN), NEW;
	(** Один шаг муравья под монитором поля. *)
		VAR cell, nx, ny, dir, best, bestVal, v, dx, dy: INTEGER;
	BEGIN
		cell := y * W + x;
		IF carry THEN
			(* несёт еду: пахнет и идёт к муравейнику *)
			IF f.pher[cell] < maxPher THEN INC(f.pher[cell]) END;
			IF x < f.nestX THEN INC(x) ELSIF x > f.nestX THEN DEC(x) END;
			IF y < f.nestY THEN INC(y) ELSIF y > f.nestY THEN DEC(y) END;
			IF (x = f.nestX) & (y = f.nestY) THEN
				INC(f.nestFood);
				carry := FALSE
			END
		ELSIF f.food[cell] > 0 THEN
			DEC(f.food[cell]);
			carry := TRUE		(* нашёл еду *)
		ELSE
			(* идёт по феромону, иначе на удачу *)
			best := -1; bestVal := 0;
			FOR dir := 0 TO 3 DO
				dx := 0; dy := 0;
				CASE dir OF
					0: dx := 1
				|	1: dx := -1
				|	2: dy := 1
				|	3: dy := -1
				END;
				nx := x + dx; ny := y + dy;
				IF (nx >= 0) & (nx < W) & (ny >= 0) & (ny < H) THEN
					v := f.pher[ny * W + nx];
					IF v > bestVal THEN bestVal := v; best := dir END
				END
			END;
			IF (best < 0) OR (bestVal = 0) THEN
				best := SHORT(f.Random() MOD 4)
			END;
			dx := 0; dy := 0;
			CASE best OF
				0: dx := 1
			|	1: dx := -1
			|	2: dy := 1
			|	3: dy := -1
			END;
			nx := x + dx; ny := y + dy;
			IF (nx >= 0) & (nx < W) & (ny >= 0) & (ny < H) THEN
				x := nx; y := ny
			END
		END;
		f.antX[id] := x; f.antY[id] := y; f.antCarry[id] := carry;
		INC(f.moves)
	END Step;

	PROCEDURE [exclusive] (f: Field) Evaporate*, NEW;
	(** Испарение феромона; зовёт отдельная задача. *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO W * H - 1 DO
			IF f.pher[i] > 0 THEN DEC(f.pher[i]) END
		END
	END Evaporate;

	(* --- задачи ----------------------------------------------------------- *)

	PROCEDURE (a: Ant) Run;
		VAR x, y: INTEGER; carry: BOOLEAN;
	BEGIN
		x := a.f.antX[a.id]; y := a.f.antY[a.id]; carry := FALSE;
		LOOP
			a.f.Step(a.id, x, y, carry);
			IF a.f.stop OR ((a.f.limit > 0) & (a.f.moves >= a.f.limit)) THEN EXIT END;
			Tasks.Yield
		END;
		DEC(a.f.alive)
	END Run;

	PROCEDURE (e: Evaporator) Run;
	BEGIN
		LOOP
			Tasks.Sleep(2);
			e.f.Evaporate;
			IF e.f.stop OR ((e.f.limit > 0) & (e.f.moves >= e.f.limit)) THEN EXIT END
		END
	END Run;

	(* --- управление ------------------------------------------------------- *)

	PROCEDURE ResetWith (ants: INTEGER);
		VAR i: INTEGER;
	BEGIN
		IF field # NIL THEN
			field.stop := TRUE;
			LastOk := DemoSupport.Pump(3000);
			running := FALSE
		END;
		IF (ants < 1) OR (ants > maxAnts) THEN ants := defaultAnts END;
		NEW(field);
		field.ants := ants; field.alive := ants;
		field.nestX := nestX0; field.nestY := nestY0;
		field.nestFood := 0; field.moves := 0; field.limit := 0;
		field.stop := FALSE; field.seed := 12345;
		FOR i := 0 TO ants - 1 DO
			field.antX[i] := field.nestX; field.antY[i] := field.nestY;
			field.antCarry[i] := FALSE
		END;
		(* пятна еды вокруг муравейника *)
		PutFood(field, nestX0 + 7, nestY0, 2, 6);
		PutFood(field, nestX0 - 7, nestY0, 2, 6);
		PutFood(field, nestX0, nestY0 + 6, 2, 6);
		PutFood(field, nestX0 + 5, nestY0 - 5, 2, 6);
		PutFood(field, nestX0 - 5, nestY0 - 5, 2, 6);
		Left := ants; LastOk := TRUE; Failed := FALSE; Deadlocked := FALSE
	END ResetWith;

	PROCEDURE Reset*;
	(** Сброс поля: 200 муравьёв, еда на местах, феромона нет. *)
	BEGIN
		ResetWith(defaultAnts);
		IF guiView # NIL THEN guiView.f := field END
	END Reset;

	PROCEDURE Start*;
	(** Запустить муравьёв и испаритель (если ещё не запущены). *)
		VAR i: INTEGER;
	BEGIN
		IF field = NIL THEN ResetWith(defaultAnts) END;
		IF running THEN RETURN END;
		(* после Stop старые задачи могли ещё не выйти — дать им уйти *)
		IF Tasks.Count() > 0 THEN
			field.stop := TRUE;
			LastOk := DemoSupport.Pump(3000)
		END;
		field.stop := FALSE;	(* limit ставит Simulate/Run; GUI оставляет 0 *)
		field.alive := field.ants; Left := field.ants;
		FOR i := 0 TO field.ants - 1 DO
			NEW(antTasks[i]); antTasks[i].f := field; antTasks[i].id := i;
			Tasks.Start(antTasks[i])
		END;
		NEW(evap); evap.f := field; Tasks.Start(evap);
		running := TRUE
	END Start;

	PROCEDURE Stop*;
	(** Остановить муравьёв: они выйдут в ближайшей точке ожидания. *)
	BEGIN
		IF field # NIL THEN field.stop := TRUE END;
		running := FALSE
	END Stop;

	PROCEDURE AntCount* (): INTEGER;
	(** Сколько муравьёв в текущем поле. *)
	BEGIN
		IF field = NIL THEN RETURN 0 ELSE RETURN field.ants END
	END AntCount;

	PROCEDURE Simulate* (ants, steps: INTEGER): LONGINT;
	(** Прогон без окна и вывода: ants муравьёв, steps шагов каждого. *)
		VAR i: INTEGER;
	BEGIN
		ResetWith(ants);
		IF steps < 1 THEN steps := 1 END;
		field.limit := field.ants * steps;
		Start;
		LastOk := DemoSupport.Pump(120000);
		field.stop := TRUE;
		IF ~LastOk THEN
			Deadlocked := Tasks.Deadlocked();
			LastOk := DemoSupport.Pump(3000)
		END;
		Left := field.alive;
		Failed := FALSE;
		FOR i := 0 TO field.ants - 1 DO
			IF (antTasks[i] # NIL) & Tasks.Failed(antTasks[i]) THEN Failed := TRUE END
		END;
		RETURN field.nestFood
	END Simulate;

	(* --- консольный режим ------------------------------------------------- *)

	PROCEDURE PrintFrame (f: Field);
		VAR x, y, i, cell: INTEGER; line: ARRAY W + 1 OF CHAR;
			here: ARRAY W * H OF BOOLEAN;
	BEGIN
		FOR i := 0 TO W * H - 1 DO here[i] := FALSE END;
		FOR i := 0 TO f.ants - 1 DO
			here[f.antY[i] * W + f.antX[i]] := TRUE
		END;
		FOR y := 0 TO H - 1 DO
			FOR x := 0 TO W - 1 DO
				cell := y * W + x;
				IF (x = f.nestX) & (y = f.nestY) THEN line[x] := "#"
				ELSIF here[cell] THEN line[x] := "*"
				ELSIF f.food[cell] > 0 THEN line[x] := "+"
				ELSIF f.pher[cell] > 0 THEN line[x] := "."
				ELSE line[x] := " "
				END
			END;
			line[W] := 0X;
			Console.WriteStr(line); Console.WriteLn
		END;
		DemoSupport.LogInt("DemoAnts frame: moves=", f.moves);
		DemoSupport.LogInt(" food=", f.nestFood);
		DemoSupport.LogInt(" alive=", f.alive); DemoSupport.LogLn
	END PrintFrame;

	PROCEDURE Run*;
	(** Консольный прогон: 200 муравьёв, 100 шагов, ASCII-кадр раз в 25. *)
		VAR target, nextFrame, t0: LONGINT;
	BEGIN
		ResetWith(defaultAnts);
		field.limit := field.ants * defaultSteps;
		target := field.limit;
		Start;
		nextFrame := field.ants * frameEvery;
		t0 := Kernel.Time();
		WHILE (field.moves < target) & (Kernel.Time() - t0 < 120000) DO
			Services.actionHook.Step;
			IF field.moves >= nextFrame THEN
				PrintFrame(field);
				nextFrame := nextFrame + field.ants * frameEvery
			END
		END;
		field.stop := TRUE;
		LastOk := DemoSupport.Pump(3000);
		PrintFrame(field);
		Left := field.alive;
		Failed := FALSE;
		DemoSupport.LogInt("DemoAnts: ants=", field.ants);
		DemoSupport.LogInt(" moves=", field.moves);
		DemoSupport.LogInt(" food=", field.nestFood);
		DemoSupport.LogInt(" alive=", Left);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

	(* --- View ------------------------------------------------------------- *)

	PROCEDURE Status (v: View; OUT s: ARRAY OF CHAR);
		VAR a: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(v.f.ants, a); s := "ants=" + a$;
		Strings.IntToString(v.f.nestFood, a); s := s + " food=" + a$;
		Strings.IntToString(v.f.moves, a); s := s + " moves=" + a$;
		IF v.f.stop THEN s := s + " stopped" END
	END Status;

	PROCEDURE (v: View) Restore* (f: Views.Frame; l, t, r, b: INTEGER);
		VAR cs, ox, oy, x, y, cell, i: INTEGER; s: ARRAY 128 OF CHAR;
	BEGIN
		INC(Restores);
		f.DrawRect(l, t, r, b, Ports.fill, Ports.white);
		cs := (r - l) DIV W;
		IF (b - t) DIV H < cs THEN cs := (b - t) DIV H END;
		IF cs < 2 THEN cs := 2 END;
		ox := l + ((r - l) - W * cs) DIV 2;
		oy := t + ((b - t) - H * cs) DIV 2;
		FOR y := 0 TO H - 1 DO
			FOR x := 0 TO W - 1 DO
				cell := y * W + x;
				IF v.f.food[cell] > 0 THEN
					f.DrawRect(ox + x * cs, oy + y * cs,
						ox + (x + 1) * cs, oy + (y + 1) * cs, Ports.fill, Ports.green)
				ELSIF v.f.pher[cell] > 0 THEN
					f.DrawRect(ox + x * cs, oy + y * cs,
						ox + (x + 1) * cs, oy + (y + 1) * cs, Ports.fill, pherColor)
				END
			END
		END;
		f.DrawRect(ox + v.f.nestX * cs, oy + v.f.nestY * cs,
			ox + (v.f.nestX + 1) * cs, oy + (v.f.nestY + 1) * cs,
			Ports.fill, nestColor);
		FOR i := 0 TO v.f.ants - 1 DO
			x := v.f.antX[i]; y := v.f.antY[i];
			f.DrawRect(ox + x * cs, oy + y * cs,
				ox + x * cs + 2, oy + y * cs + 2, Ports.fill, Ports.black)
		END;
		Status(v, s);
		f.DrawString(l + 4, t + 12, Ports.black, s, Fonts.dir.Default())
	END Restore;

	PROCEDURE (v: View) HandlePropMsg- (VAR msg: Properties.Message);
	BEGIN
		WITH msg: Properties.SizePref DO
			IF (msg.w = Views.undefined) OR (msg.h = Views.undefined) THEN
				msg.w := 120 * Ports.mm; msg.h := 80 * Ports.mm
			END
		ELSE
		END
	END HandlePropMsg;

	PROCEDURE (t: Timer) Do;
	BEGIN
		IF (t.v # NIL) & (t.v.context # NIL) THEN
			INC(GuiTicks);
			Views.Update(t.v, Views.keepFrames);
			Services.DoLater(t, Services.Ticks() + 100)
		ELSE
			timerActive := FALSE
		END
	END Do;

	PROCEDURE Open*;
	(** Открыть окно и запустить муравьёв. *)
	BEGIN
		IF guiView = NIL THEN
			ResetWith(defaultAnts)
		END;
		Start;
		IF guiView = NIL THEN
			NEW(guiView); guiView.f := field
		ELSE
			guiView.f := field
		END;
		Views.OpenView(guiView);
		IF ~timerActive THEN
			NEW(timer); timer.v := guiView; timerActive := TRUE;
			Services.DoLater(timer, Services.Ticks() + 100)
		END
	END Open;

	(* --- проверка GUI без экрана ------------------------------------------ *)

	PROCEDURE (c: Checker) Do;
		VAR ok: BOOLEAN;
	BEGIN
		INC(c.ticks);
		IF c.ticks MOD 10 = 0 THEN
			DemoSupport.LogInt("DemoAnts gui: tick=", c.ticks);
			DemoSupport.LogInt(" food=", field.nestFood);
			DemoSupport.LogInt(" frames=", Restores);
			DemoSupport.LogInt(" moves=", field.moves);
			DemoSupport.LogLn;
			PrintFrame(field)	(* журнал: состояние модели *)
		END;
		IF c.ticks >= 40 THEN
			ok := (GuiTicks >= 20) & (field.nestFood > 0)
				& (Left = field.alive) & ~Tasks.Deadlocked();
			IF ok THEN Console.WriteStr("DemoAnts: ok=1")
			ELSE Console.WriteStr("DemoAnts: ok=0")
			END;
			DemoSupport.LogInt(" ticks=", GuiTicks);
			DemoSupport.LogInt(" food=", field.nestFood);
			DemoSupport.LogInt(" frames=", Restores);
			DemoSupport.LogInt(" alive=", field.alive);
			DemoSupport.LogLn;
			IF ok THEN Kernel.Quit(0) ELSE Kernel.Quit(1) END
		ELSE
			Services.DoLater(c, Services.Ticks() + 100)
		END
	END Do;

	PROCEDURE GuiCheck*;
	(** Открыть окно, покрутить таймер и проверить кадры (headless). *)
		VAR c: Checker;
	BEGIN
		Open;
		NEW(c); c.ticks := 0;
		Services.DoLater(c, Services.Ticks() + 100)
	END GuiCheck;

END DemoAnts.
