# Обзор на примерах: DemoAnts

Тысяча муравьёв на поле с окном.

Задач в программе бывает не две и не десять, а тысяча, и все они меняют одно общее поле, которое
окно показывает на ходу. Пример `DemoAnts` проверяет, что задачи, монитор и главный цикл окна
уживаются при таком числе объектов: 1000 задач-муравьёв живут на поле 160×100 со стенами, ищут
еду, несут её в гнездо и оставляют след феромона, который испаряет отдельная задача.

```
MODULE DemoAnts;
(** Муравьи: много координирующихся активных объектов с визуализацией.

	Постановка: поле-сетка 160×100, муравейник, кучи еды, стены и
	феромон. Каждый муравей — задача Tasks.Job со своим стеком: ищет еду,
	несёт в муравейник, оставляет феромон. Поле целиком — монитор-объект
	Field (операции StepOp и EvapOp); феромон испаряет отдельная задача.
	Муравьи сами не рисуют: View.Restore читает поле
	и рисует сетку, перерисовку заказывает таймер Services.Action через
	Views.Update.

	Стены — клетки, в которые муравей не входит и через которые не
	«видит» феромон: при выборе соседа рассматриваются только свободные
	клетки. По умолчанию четыре стены с проходами; прямой путь от
	гнезда к еде закрыт, тропа огибает стену.

	Скорость моделирования отделена от частоты перерисовки: муравей
	после шага спит 1000/speed мс, окно перерисовывается ~15 раз в
	секунду. Команды Faster/Slower меняют скорость (1…200 шагов/с),
	Pause/Resume останавливают и продолжают, Step делает один шаг на
	паузе.

	Команды:
		DemoAnts.Open       — открыть окно (GUI) и запустить
		DemoAnts.Start      — пуск
		DemoAnts.Stop       — остановка задач
		DemoAnts.Pause      — пауза модели
		DemoAnts.Resume     — продолжить после паузы
		DemoAnts.Step       — один шаг модели на паузе
		DemoAnts.Faster     — скорость ×2 (до 200)
		DemoAnts.Slower     — скорость ÷2 (до 1)
		DemoAnts.Reset      — сброс поля и муравьёв
		DemoAnts.Run        — консольный прогон с ASCII-кадрами
		DemoAnts.Simulate   — прогон без вывода (для Unit)
		DemoAnts.Bench      — замер шага на 1000 муравьях

	Стенд: один поток ОС и кооперативные задачи, поэтому чтение поля в
	Restore между шагами планировщика безопасно и без монитора. *)

	IMPORT Kernel, Services, Tasks, Console, Strings,
		Views, Ports, Properties, Fonts, DemoSupport;

	CONST
		W* = 160; H* = 100;		(* размер поля *)
		maxAnts = 1000;
		defaultAnts = 1000;
		defaultSteps = 300;
		maxPher = 100;
		foodPatch = 30;			(* еды в одной клетке пятна *)
		nestRadius = 4;			(* радиус пятна гнезда на экране, клетки *)
		maxPatches = 8;
		defaultSpeed = 10;
		minSpeed = 1; maxSpeed = 200;
		evapPeriod = 120;		(* мс на единицу испарения: 100 -> 12 с *)
		pausePeriod = 30;		(* мс опроса паузы *)
		redrawPeriod = 66;		(* мс между кадрами, ~15 кадров/с *)
		frameEvery = 100;		(* ASCII-кадр раз в 100 шагов муравья *)
		frameStride = 2;		(* каждая 2-я клетка в консольном кадре *)
		nestX0 = W DIV 2; nestY0 = H DIV 2;

		(* цвета *)
		bgColor = 00F8F8F4H;
		statusColor = 00F0F0EAH;
		wallColor = 0034343CH;		(* тёмная стена *)
		nestColor = 00468C96H;		(* оливковое пятно гнезда *)
		nestCore = 00235F69H;
		antColor = 00142332H;		(* муравей без еды *)
		antFoodColor = 001450E6H;	(* муравей с едой: оранжево-красный *)
		foodLow = 00AAEBBEH;		(* бледно-зелёный остаток *)
		foodHigh = 00147814H;		(* густо-зелёная полная клетка *)
		pherLow = 00FFF0E1H;		(* бледно-голубой слабый феромон *)
		pherHigh = 00C83C14H;		(* насыщенный синий сильный *)
		textColor = 001E1E28H;

	TYPE
		StepOp = RECORD (Tasks.Op) id: INTEGER END;
		EvapOp = RECORD (Tasks.Op) END;

		Field* = POINTER TO RECORD (Tasks.Object)
			food: ARRAY W * H OF INTEGER;
			pher: ARRAY W * H OF INTEGER;
			wall: ARRAY W * H OF BOOLEAN;
			antX, antY, antDir: ARRAY maxAnts OF INTEGER;
			antCarry: ARRAY maxAnts OF BOOLEAN;
			patchX, patchY: ARRAY maxPatches OF INTEGER;
			patches: INTEGER;
			walls: INTEGER;
			ants*, alive: INTEGER;
			nestX*, nestY: INTEGER;
			nestFood*: LONGINT;
			moves: LONGINT;
			limit: LONGINT;	(* 0 — без предела *)
			stop, paused, paced: BOOLEAN;
			seed: LONGINT;
			wallHits: LONGINT;
			wallViolations: INTEGER
		END;

		Ant = POINTER TO RECORD (Tasks.Job)
			f: Field;
			id: INTEGER
		END;

		Evaporator = POINTER TO RECORD (Tasks.Job) f: Field END;

		View* = POINTER TO RECORD (Views.View) f: Field END;

		Timer = POINTER TO RECORD (Services.Action) v: View END;

		Checker = POINTER TO RECORD (Services.Action) ticks: INTEGER END;

	VAR
		field: Field;
		antTasks: ARRAY maxAnts OF Ant;
		evap: Evaporator;
		running: BOOLEAN;
		speed: INTEGER;

		bfsSeen: ARRAY W * H OF BOOLEAN;
		bfsQueue: ARRAY W * H OF INTEGER;
		frameHere: ARRAY W * H OF BOOLEAN;

		Left*: INTEGER;		(* живых муравьёв *)
		LastOk*: BOOLEAN;	(* прогон завершился сам *)
		Failed*: BOOLEAN;	(* трап в задаче *)
		Deadlocked*: BOOLEAN;

		guiView: View;
		timer: Timer;
		timerActive: BOOLEAN;
		GuiTicks*, Restores*: INTEGER;
		FrameMs*: LONGINT;	(* суммарное время Restore, мс *)
		GuiStart: LONGINT;	(* начало GuiCheck, мс *)

	(* --- клетки и направления --------------------------------------------- *)

	PROCEDURE DirX (dir: INTEGER): INTEGER;
	BEGIN
		IF dir = 0 THEN RETURN 1 ELSIF dir = 1 THEN RETURN -1 ELSE RETURN 0 END
	END DirX;

	PROCEDURE DirY (dir: INTEGER): INTEGER;
	BEGIN
		IF dir = 2 THEN RETURN 1 ELSIF dir = 3 THEN RETURN -1 ELSE RETURN 0 END
	END DirY;

	PROCEDURE FreeCell (f: Field; x, y: INTEGER): BOOLEAN;
	BEGIN
		RETURN (x >= 0) & (x < W) & (y >= 0) & (y < H)
			& ~f.wall[y * W + x]
	END FreeCell;

	PROCEDURE NeighborFree (f: Field; x, y, dir: INTEGER): BOOLEAN;
	BEGIN
		RETURN FreeCell(f, x + DirX(dir), y + DirY(dir))
	END NeighborFree;

	(* --- поле ------------------------------------------------------------- *)

	PROCEDURE (f: Field) Random (): LONGINT, NEW;
	BEGIN
		f.seed := (f.seed * 1103515245 + 12345) MOD 2147483648;
		RETURN f.seed
	END Random;

	PROCEDURE PutFood (f: Field; cx, cy, r, amount: INTEGER);
		VAR x, y, cell: INTEGER;
	BEGIN
		IF f.patches < maxPatches THEN
			f.patchX[f.patches] := cx; f.patchY[f.patches] := cy;
			INC(f.patches)
		END;
		FOR y := cy - r TO cy + r DO
			FOR x := cx - r TO cx + r DO
				IF (x >= 0) & (x < W) & (y >= 0) & (y < H) THEN
					cell := y * W + x;
					IF ~f.wall[cell] THEN
						IF f.food[cell] < amount THEN f.food[cell] := amount END
					END
				END
			END
		END
	END PutFood;

	PROCEDURE BuildWalls (f: Field);
	(** Четыре стены с проходами: прямой путь от гнезда к каждой куче
		еды перекрыт, проходы заставляют тропу огибать стену. *)
		VAR x, y: INTEGER;
	BEGIN
		FOR y := 0 TO H - 1 DO
			IF (y < 42) OR (y > 54) THEN f.wall[y * W + 54] := TRUE END;
			IF (y < 46) OR (y > 58) THEN f.wall[y * W + 106] := TRUE END
		END;
		FOR x := 58 TO 102 DO
			IF (x < 86) OR (x > 96) THEN f.wall[25 * W + x] := TRUE END;
			IF (x < 62) OR (x > 72) THEN f.wall[75 * W + x] := TRUE END
		END;
		f.walls := 4
	END BuildWalls;

	PROCEDURE PlaceFood (f: Field);
	BEGIN
		(* углы и центр сверху/снизу: прямые линии к ним перекрыты *)
		PutFood(f, 20, 20, 4, foodPatch);
		PutFood(f, 140, 20, 4, foodPatch);
		PutFood(f, 20, 80, 4, foodPatch);
		PutFood(f, 140, 80, 4, foodPatch);
		PutFood(f, 80, 12, 4, foodPatch);
		PutFood(f, 80, 88, 4, foodPatch)
	END PlaceFood;

	PROCEDURE StepLocked (f: Field; id: INTEGER);
	(** Один шаг муравья под монитором поля. Муравей «нюхает» только
		соседние свободные клетки: стена непроходима и феромон за ней
		не виден. *)
		VAR cell, x, y, nx, ny, dir, prev, best, bestVal, minDist, dist,
			tieCount, freeCount, k: INTEGER; carry: BOOLEAN;
	BEGIN
		x := f.antX[id]; y := f.antY[id]; carry := f.antCarry[id];
		cell := y * W + x;
		IF carry THEN
			(* несёт еду: пахнет и идёт к муравейнику, обходя стены *)
			IF f.pher[cell] < maxPher THEN INC(f.pher[cell], 2) END;
			best := -1; minDist := MAX(INTEGER); tieCount := 0;
			prev := f.antDir[id];
			FOR dir := 0 TO 3 DO
				nx := x + DirX(dir); ny := y + DirY(dir);
				IF FreeCell(f, nx, ny) THEN
					dist := ABS(nx - f.nestX) + ABS(ny - f.nestY);
					IF dist < minDist THEN
						minDist := dist; best := dir; tieCount := 1
					ELSIF dist = minDist THEN
						INC(tieCount);
						IF (dir = prev) OR (f.Random() MOD tieCount = 0) THEN
							best := dir
						END
					END
				ELSIF (nx >= 0) & (nx < W) & (ny >= 0) & (ny < H) THEN
					INC(f.wallHits)	(* стена отклонила движение *)
				END
			END;
			IF best >= 0 THEN
				x := x + DirX(best); y := y + DirY(best); f.antDir[id] := best
			END;
			IF (x = f.nestX) & (y = f.nestY) THEN
				INC(f.nestFood);
				carry := FALSE
			END
		ELSIF f.food[cell] > 0 THEN
			DEC(f.food[cell]);
			carry := TRUE		(* нашёл еду *)
		ELSE
			(* идёт по феромону, иначе на удачу, сохраняя направление *)
			best := -1; bestVal := 0; tieCount := 0;
			FOR dir := 0 TO 3 DO
				nx := x + DirX(dir); ny := y + DirY(dir);
				IF FreeCell(f, nx, ny) THEN
					IF f.pher[ny * W + nx] > bestVal THEN
						bestVal := f.pher[ny * W + nx]; best := dir; tieCount := 1
					ELSIF (f.pher[ny * W + nx] = bestVal) & (bestVal > 0) THEN
						INC(tieCount);
						IF f.Random() MOD tieCount = 0 THEN best := dir END
					END
				ELSIF (nx >= 0) & (nx < W) & (ny >= 0) & (ny < H) THEN
					INC(f.wallHits)
				END
			END;
			IF bestVal = 0 THEN
				prev := f.antDir[id];
				IF (prev >= 0) & NeighborFree(f, x, y, prev)
						& (f.Random() MOD 10 < 5) THEN
					best := prev
				ELSE
					(* случайное свободное направление *)
					freeCount := 0; best := -1;
					FOR dir := 0 TO 3 DO
						IF NeighborFree(f, x, y, dir) THEN
							INC(freeCount);
							IF f.Random() MOD freeCount = 0 THEN best := dir END
						END
					END
				END
			END;
			IF best >= 0 THEN
				x := x + DirX(best); y := y + DirY(best); f.antDir[id] := best
			END
		END;
		(* страховка: муравей не может стоять в стене *)
		IF f.wall[y * W + x] THEN
			INC(f.wallViolations);
			x := f.antX[id]; y := f.antY[id]
		END;
		f.antX[id] := x; f.antY[id] := y; f.antCarry[id] := carry;
		INC(f.moves)
	END StepLocked;

	PROCEDURE EvaporateLocked (f: Field);
	(** Испарение феромона; зовёт отдельная задача раз в evapPeriod мс. *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO W * H - 1 DO
			IF f.pher[i] > 0 THEN DEC(f.pher[i]) END
		END
	END EvaporateLocked;

	PROCEDURE (f: Field) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: StepOp DO
			StepLocked(f, op.id)
		| op: EvapOp DO
			EvaporateLocked(f)
		ELSE
			HALT(20)
		END
	END Exclusive;

	(* --- задачи ----------------------------------------------------------- *)

	PROCEDURE (a: Ant) Body- (VAR ctx: Tasks.Ctx);
		VAR period: LONGINT; op: StepOp;
	BEGIN
		LOOP
			IF a.f.stop OR ((a.f.limit > 0) & (a.f.moves >= a.f.limit)) THEN EXIT END;
			IF a.f.paced THEN
				IF a.f.paused THEN
					Tasks.Sleep(pausePeriod)
				ELSE
					op.id := a.id; a.f.Do(ctx, op);
					period := 1000 DIV speed;
					IF period < 1 THEN period := 1 END;
					Tasks.Sleep(period)
				END
			ELSE
				op.id := a.id; a.f.Do(ctx, op);
				Tasks.Yield
			END
		END;
		DEC(a.f.alive)
	END Body;

	PROCEDURE (e: Evaporator) Body- (VAR ctx: Tasks.Ctx);
		VAR op: EvapOp;
	BEGIN
		LOOP
			Tasks.Sleep(evapPeriod);
			IF e.f.stop
					OR ((e.f.limit > 0) & (e.f.moves >= e.f.limit)) THEN EXIT END;
			IF ~e.f.paused THEN e.f.Do(ctx, op) END
		END
	END Body;

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
		field.stop := FALSE; field.paused := FALSE; field.paced := FALSE;
		field.seed := 12345; field.patches := 0; field.walls := 0;
		field.wallHits := 0; field.wallViolations := 0;
		BuildWalls(field);
		PlaceFood(field);
		FOR i := 0 TO ants - 1 DO
			(* треть муравьёв уже рыщет по полю: картинка читается сразу,
			   остальные выходят из гнезда *)
			IF (ants >= 6) & (i MOD 3 = 0) THEN
				REPEAT
					field.antX[i] := SHORT(field.Random() MOD W);
					field.antY[i] := SHORT(field.Random() MOD H)
				UNTIL ~field.wall[field.antY[i] * W + field.antX[i]]
			ELSE
				field.antX[i] := field.nestX; field.antY[i] := field.nestY
			END;
			field.antCarry[i] := FALSE; field.antDir[i] := -1
		END;
		Left := ants; LastOk := TRUE; Failed := FALSE; Deadlocked := FALSE
	END ResetWith;

	PROCEDURE StartTasks (paced: BOOLEAN);
		VAR i: INTEGER;
	BEGIN
		field.stop := FALSE; field.paused := FALSE; field.paced := paced;
		field.alive := field.ants; Left := field.ants;
		FOR i := 0 TO field.ants - 1 DO
			NEW(antTasks[i]); antTasks[i].f := field; antTasks[i].id := i
		END;
		FOR i := 0 TO field.ants - 1 DO Tasks.Start(antTasks[i]) END;
		NEW(evap); evap.f := field; Tasks.Start(evap);
		running := TRUE
	END StartTasks;

	PROCEDURE Reset*;
	(** Сброс поля: муравьи в гнезде, еда на местах, феромона нет. *)
	BEGIN
		ResetWith(defaultAnts);
		IF guiView # NIL THEN guiView.f := field END
	END Reset;

	PROCEDURE Start*;
	(** Запустить муравьёв и испаритель (если ещё не запущены). *)
	BEGIN
		IF field = NIL THEN ResetWith(defaultAnts) END;
		IF running THEN field.paused := FALSE; RETURN END;
		(* после Stop старые задачи могли ещё не выйти — дать им уйти *)
		IF Tasks.Count() > 0 THEN
			field.stop := TRUE;
			LastOk := DemoSupport.Pump(3000)
		END;
		field.limit := 0;
		StartTasks(TRUE)
	END Start;

	PROCEDURE Stop*;
	(** Остановить задачи: они выйдут в ближайшей точке ожидания. *)
	BEGIN
		IF field # NIL THEN field.stop := TRUE; field.paused := FALSE END;
		running := FALSE
	END Stop;

	PROCEDURE Pause*;
	(** Заморозить модель, оставив задачи и окно. *)
	BEGIN
		IF field # NIL THEN field.paused := TRUE END
	END Pause;

	PROCEDURE Resume*;
	(** Продолжить модель после Pause. *)
	BEGIN
		IF field # NIL THEN
			field.paused := FALSE;
			IF ~running THEN Start END
		END
	END Resume;

	PROCEDURE Step*;
	(** Один шаг модели на паузе: все муравьи делают по шагу.
		Команда идёт на главном стеке, где нет задачи и пропуска, поэтому
		Do недоступен. Модель на паузе: задачи не работают, и прямой вызов
		StepLocked ни с кем не пересекается. *)
		VAR i: INTEGER;
	BEGIN
		IF (field # NIL) & running & field.paused THEN
			FOR i := 0 TO field.ants - 1 DO StepLocked(field, i) END
		END
	END Step;

	PROCEDURE SetSpeed* (s: INTEGER);
	(** Скорость модели, шагов в секунду (1…200). *)
	BEGIN
		IF s < minSpeed THEN s := minSpeed ELSIF s > maxSpeed THEN s := maxSpeed END;
		speed := s
	END SetSpeed;

	PROCEDURE Speed* (): INTEGER;
	BEGIN
		RETURN speed
	END Speed;

	PROCEDURE Faster*;
	(** Скорость ×2, не выше 200 шагов/с. *)
	BEGIN
		SetSpeed(speed * 2)
	END Faster;

	PROCEDURE Slower*;
	(** Скорость ÷2, не ниже 1 шага/с. *)
	BEGIN
		SetSpeed(speed DIV 2)
	END Slower;

	PROCEDURE AntCount* (): INTEGER;
	(** Сколько муравьёв в текущем поле. *)
	BEGIN
		IF field = NIL THEN RETURN 0 ELSE RETURN field.ants END
	END AntCount;

	PROCEDURE Moves* (): LONGINT;
	(** Сколько шагов сделали муравьи в текущем поле. *)
	BEGIN
		IF field = NIL THEN RETURN 0 ELSE RETURN field.moves END
	END Moves;

	PROCEDURE FoodDelivered* (): LONGINT;
	(** Сколько еды доставлено в муравейник. *)
	BEGIN
		IF field = NIL THEN RETURN 0 ELSE RETURN field.nestFood END
	END FoodDelivered;

	PROCEDURE WallCount* (): INTEGER;
	(** Сколько стен в поле (4 по умолчанию). *)
	BEGIN
		IF field = NIL THEN RETURN 0 ELSE RETURN field.walls END
	END WallCount;

	PROCEDURE WallHits* (): LONGINT;
	(** Сколько раз стена отклонила движение муравья. *)
	BEGIN
		IF field = NIL THEN RETURN 0 ELSE RETURN field.wallHits END
	END WallHits;

	PROCEDURE WallViolations* (): INTEGER;
	(** Сколько раз муравей оказался в стене (должно быть 0). *)
	BEGIN
		IF field = NIL THEN RETURN 0 ELSE RETURN field.wallViolations END
	END WallViolations;

	PROCEDURE AntsInWalls* (): INTEGER;
	(** Сколько муравьёв сейчас стоит в клетке-стене (должно быть 0). *)
		VAR i, n: INTEGER;
	BEGIN
		n := 0;
		IF field # NIL THEN
			FOR i := 0 TO field.ants - 1 DO
				IF field.wall[field.antY[i] * W + field.antX[i]] THEN INC(n) END
			END
		END;
		RETURN n
	END AntsInWalls;

	PROCEDURE FoodReachable* (): BOOLEAN;
	(** Поиск в ширину из гнезда: каждая клетка еды достижима по
		свободным клеткам. *)
		VAR f: Field; head, tail, i, cell, x, y, dir, nx, ny, ncell: INTEGER;
			ok: BOOLEAN;
	BEGIN
		f := field;
		IF f = NIL THEN RETURN FALSE END;
		FOR i := 0 TO W * H - 1 DO bfsSeen[i] := FALSE END;
		cell := f.nestY * W + f.nestX;
		IF f.wall[cell] THEN RETURN FALSE END;
		bfsSeen[cell] := TRUE; bfsQueue[0] := cell;
		head := 0; tail := 1;
		WHILE head < tail DO
			cell := bfsQueue[head]; INC(head);
			x := cell MOD W; y := cell DIV W;
			FOR dir := 0 TO 3 DO
				nx := x + DirX(dir); ny := y + DirY(dir);
				IF FreeCell(f, nx, ny) THEN
					ncell := ny * W + nx;
					IF ~bfsSeen[ncell] THEN
						bfsSeen[ncell] := TRUE;
						bfsQueue[tail] := ncell; INC(tail)
					END
				END
			END
		END;
		ok := TRUE;
		FOR i := 0 TO W * H - 1 DO
			IF (f.food[i] > 0) & ~bfsSeen[i] THEN ok := FALSE END
		END;
		RETURN ok
	END FoodReachable;

	PROCEDURE Simulate* (ants, steps: INTEGER): LONGINT;
	(** Прогон без окна и вывода: ants муравьёв, steps шагов каждого. *)
		VAR i: INTEGER;
	BEGIN
		ResetWith(ants);
		IF steps < 1 THEN steps := 1 END;
		field.limit := field.ants * steps;
		StartTasks(FALSE);
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
		running := FALSE;
		RETURN field.nestFood
	END Simulate;

	PROCEDURE Bench*;
	(** Замер: 1000 муравьёв, 200 шагов, время одного хода. *)
		VAR t0, t1, ms, ns: LONGINT;
	BEGIN
		ResetWith(defaultAnts);
		field.limit := field.ants * 200;
		t0 := Kernel.Time();
		StartTasks(FALSE);
		WHILE (field.moves < field.limit) & (Kernel.Time() - t0 < 120000) DO
			Services.actionHook.Step
		END;
		field.stop := TRUE;
		LastOk := DemoSupport.Pump(3000);
		t1 := Kernel.Time();
		ms := t1 - t0;
		IF field.moves > 0 THEN ns := ms * 1000000 DIV field.moves ELSE ns := 0 END;
		running := FALSE;
		DemoSupport.LogInt("DemoAnts bench: ants=", field.ants);
		DemoSupport.LogInt(" moves=", field.moves);
		DemoSupport.LogInt(" ms=", ms);
		DemoSupport.LogInt(" ns/move=", ns);
		DemoSupport.LogLn
	END Bench;

	(* --- консольный режим ------------------------------------------------- *)

	PROCEDURE FrameChar (f: Field; x0, y0: INTEGER): CHAR;
		VAR x, y, cell: INTEGER; wall, food, pher, ant, nest: BOOLEAN;
	BEGIN
		wall := FALSE; food := FALSE; pher := FALSE; nest := FALSE;
		FOR y := y0 TO y0 + frameStride - 1 DO
			FOR x := x0 TO x0 + frameStride - 1 DO
				IF (x < W) & (y < H) THEN
					cell := y * W + x;
					IF f.wall[cell] THEN wall := TRUE END;
					IF f.food[cell] > 0 THEN food := TRUE END;
					IF f.pher[cell] > 0 THEN pher := TRUE END;
					IF (x = f.nestX) & (y = f.nestY) THEN nest := TRUE END
				END
			END
		END;
		ant := frameHere[(y0 DIV frameStride) * W + (x0 DIV frameStride)];
		IF nest THEN RETURN "#"
		ELSIF ant THEN RETURN "*"
		ELSIF wall THEN RETURN "X"
		ELSIF food THEN RETURN "+"
		ELSIF pher THEN RETURN "."
		ELSE RETURN " "
		END
	END FrameChar;

	PROCEDURE PrintFrame (f: Field);
		VAR x, y, i, cols, rows: INTEGER; line: ARRAY 128 OF CHAR;
	BEGIN
		FOR i := 0 TO W * H - 1 DO frameHere[i] := FALSE END;
		FOR i := 0 TO f.ants - 1 DO
			frameHere[(f.antY[i] DIV frameStride) * W
				+ f.antX[i] DIV frameStride] := TRUE
		END;
		cols := (W + frameStride - 1) DIV frameStride;
		rows := (H + frameStride - 1) DIV frameStride;
		FOR y := 0 TO rows - 1 DO
			FOR x := 0 TO cols - 1 DO
				line[x] := FrameChar(f, x * frameStride, y * frameStride)
			END;
			line[cols] := 0X;
			Console.WriteStr(line); Console.WriteLn
		END;
		DemoSupport.LogInt("DemoAnts frame: moves=", f.moves);
		DemoSupport.LogInt(" food=", f.nestFood);
		DemoSupport.LogInt(" alive=", f.alive); DemoSupport.LogLn
	END PrintFrame;

	PROCEDURE Run*;
	(** Консольный прогон: 1000 муравьёв, 100 шагов, уменьшенный кадр
		раз в 25 шагов; стены — X. *)
		VAR target, nextFrame, t0: LONGINT;
	BEGIN
		ResetWith(defaultAnts);
		field.limit := field.ants * defaultSteps;
		target := field.limit;
		StartTasks(FALSE);
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
		running := FALSE;
		DemoSupport.LogInt("DemoAnts: ants=", field.ants);
		DemoSupport.LogInt(" moves=", field.moves);
		DemoSupport.LogInt(" food=", field.nestFood);
		DemoSupport.LogInt(" alive=", Left);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

	(* --- View ------------------------------------------------------------- *)

	PROCEDURE Status (v: View; OUT s: ARRAY OF CHAR);
		VAR a: ARRAY 24 OF CHAR; step: LONGINT;
	BEGIN
		IF v.f.ants > 0 THEN step := v.f.moves DIV v.f.ants ELSE step := 0 END;
		Strings.IntToString(v.f.ants, a); s := "ants=" + a$;
		Strings.IntToString(step, a); s := s + "  step=" + a$;
		Strings.IntToString(v.f.nestFood, a); s := s + "  food=" + a$;
		Strings.IntToString(speed, a); s := s + "  speed=" + a$ + "/s";
		IF v.f.paused THEN s := s + "  paused" END;
		IF v.f.stop THEN s := s + "  stopped" END
	END Status;

	PROCEDURE Mix (a, b: Ports.Color; t, max: INTEGER): Ports.Color;
		VAR r, g, bl, ar, ag, ab, br, bg, bb: INTEGER;
	BEGIN
		IF max <= 0 THEN RETURN a END;
		IF t < 0 THEN t := 0 ELSIF t > max THEN t := max END;
		ar := a MOD 256; ag := a DIV 256 MOD 256; ab := a DIV 65536 MOD 256;
		br := b MOD 256; bg := b DIV 256 MOD 256; bb := b DIV 65536 MOD 256;
		r := ar + (br - ar) * t DIV max;
		g := ag + (bg - ag) * t DIV max;
		bl := ab + (bb - ab) * t DIV max;
		RETURN Ports.RGBColor(r, g, bl)
	END Mix;

	PROCEDURE (v: View) Restore* (f: Views.Frame; l, t, r, b: INTEGER);
		VAR t0: LONGINT; cs, ox, oy, x, y, cell, i, d, statusH, fieldT: INTEGER;
			s: ARRAY 128 OF CHAR;
	BEGIN
		t0 := Kernel.Time();
		INC(Restores);
		statusH := 6 * Ports.mm;
		fieldT := t + statusH;
		f.DrawRect(l, t, r, b, Ports.fill, bgColor);
		f.DrawRect(l, t, r, fieldT, Ports.fill, statusColor);
		(* клетка квадратная, поле растянуто на всё окно; минимум 3 px *)
		cs := (r - l) DIV W;
		IF (b - fieldT) DIV H < cs THEN cs := (b - fieldT) DIV H END;
		IF cs < (Ports.point * 9) DIV 4 THEN cs := (Ports.point * 9) DIV 4 END;
		ox := l + ((r - l) - W * cs) DIV 2;
		oy := fieldT + ((b - fieldT) - H * cs) DIV 2;
		FOR y := 0 TO H - 1 DO
			FOR x := 0 TO W - 1 DO
				cell := y * W + x;
				IF v.f.wall[cell] THEN
					f.DrawRect(ox + x * cs, oy + y * cs,
						ox + (x + 1) * cs, oy + (y + 1) * cs, Ports.fill, wallColor)
				ELSIF v.f.food[cell] > 0 THEN
					f.DrawRect(ox + x * cs, oy + y * cs,
						ox + (x + 1) * cs, oy + (y + 1) * cs, Ports.fill,
						Mix(foodLow, foodHigh, v.f.food[cell], foodPatch))
				ELSIF v.f.pher[cell] > 0 THEN
					f.DrawRect(ox + x * cs, oy + y * cs,
						ox + (x + 1) * cs, oy + (y + 1) * cs, Ports.fill,
						Mix(pherLow, pherHigh, v.f.pher[cell], maxPher))
				END
			END
		END;
		(* гнездо — крупное пятно *)
		f.DrawOval(ox + (v.f.nestX - nestRadius) * cs,
			oy + (v.f.nestY - nestRadius) * cs,
			ox + (v.f.nestX + nestRadius + 1) * cs,
			oy + (v.f.nestY + nestRadius + 1) * cs, Ports.fill, nestColor);
		f.DrawOval(ox + (v.f.nestX - 1) * cs, oy + (v.f.nestY - 1) * cs,
			ox + (v.f.nestX + 2) * cs, oy + (v.f.nestY + 2) * cs,
			Ports.fill, nestCore);
		(* муравьи: тёмный без еды, оранжевый с едой *)
		d := cs DIV 2;
		IF d < 2 THEN d := 2 END;
		FOR i := 0 TO v.f.ants - 1 DO
			x := ox + v.f.antX[i] * cs + (cs - d) DIV 2;
			y := oy + v.f.antY[i] * cs + (cs - d) DIV 2;
			IF v.f.antCarry[i] THEN
				f.DrawRect(x, y, x + d, y + d, Ports.fill, antFoodColor)
			ELSE
				f.DrawRect(x, y, x + d, y + d, Ports.fill, antColor)
			END
		END;
		Status(v, s);
		f.DrawString(l + Ports.mm, t + Ports.mm * 4, textColor, s,
			Fonts.dir.Default());
		INC(FrameMs, Kernel.Time() - t0)
	END Restore;

	PROCEDURE (v: View) HandlePropMsg- (VAR msg: Properties.Message);
	BEGIN
		WITH msg: Properties.SizePref DO
			IF (msg.w = Views.undefined) OR (msg.h = Views.undefined) THEN
				(* ~1000×650 px при 96 dpi *)
				msg.w := 265 * Ports.mm; msg.h := 172 * Ports.mm
			END
		ELSE
		END
	END HandlePropMsg;

	PROCEDURE (t: Timer) Do;
	BEGIN
		IF (t.v # NIL) & (t.v.context # NIL) THEN
			INC(GuiTicks);
			Views.Update(t.v, Views.keepFrames);
			IF t.v.f # NIL THEN
				(* перерисовка не зависит от скорости модели *)
				Services.DoLater(t, Services.Ticks() + redrawPeriod)
			ELSE
				timerActive := FALSE
			END
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
			Services.DoLater(timer, Services.Ticks() + redrawPeriod)
		END
	END Open;

	(* --- проверка GUI без экрана ------------------------------------------ *)

	PROCEDURE (c: Checker) Do;
		VAR ok: BOOLEAN; elapsed, sps: LONGINT;
	BEGIN
		INC(c.ticks);
		IF c.ticks MOD 40 = 0 THEN
			DemoSupport.LogInt("DemoAnts gui: tick=", c.ticks);
			DemoSupport.LogInt(" food=", field.nestFood);
			DemoSupport.LogInt(" frames=", Restores);
			DemoSupport.LogInt(" frame_ms=", FrameMs);
			DemoSupport.LogInt(" moves=", field.moves);
			DemoSupport.LogInt(" speed=", speed);
			DemoSupport.LogLn;
			PrintFrame(field)	(* журнал: состояние модели *)
		END;
		IF c.ticks >= 400 THEN
			(* ~40 с при 10 шагах/с; запас на медленную машину *)
			ok := (GuiTicks >= 100) & (field.nestFood > 0)
				& (Left = field.alive) & ~Tasks.Deadlocked();
			IF ok THEN Console.WriteStr("DemoAnts: ok=1")
			ELSE Console.WriteStr("DemoAnts: ok=0")
			END;
			DemoSupport.LogInt(" ticks=", GuiTicks);
			DemoSupport.LogInt(" food=", field.nestFood);
			DemoSupport.LogInt(" frames=", Restores);
			DemoSupport.LogInt(" frame_ms=", FrameMs);
			DemoSupport.LogInt(" alive=", field.alive);
			elapsed := Kernel.Time() - GuiStart;
			IF (elapsed > 0) & (field.ants > 0) THEN
				sps := (field.moves DIV field.ants) * 1000 DIV elapsed
			ELSE sps := 0
			END;
			DemoSupport.LogInt(" elapsed_ms=", elapsed);
			DemoSupport.LogInt(" steps/s=", sps);
			DemoSupport.LogLn;
			IF ok THEN Kernel.Quit(0) ELSE Kernel.Quit(1) END
		ELSE
			Services.DoLater(c, Services.Ticks() + redrawPeriod)
		END
	END Do;

	PROCEDURE GuiCheck*;
	(** Открыть окно, покрутить таймер и проверить кадры (headless). *)
		VAR c: Checker;
	BEGIN
		Open;
		GuiStart := Kernel.Time();
		NEW(c); c.ticks := 0;
		Services.DoLater(c, Services.Ticks() + redrawPeriod)
	END GuiCheck;

BEGIN
	speed := defaultSpeed
END DemoAnts.
```

Модуль собирает `tools/build64.sh` (исходник — `Demo/Mod/Ants.cp`; подсистема `Demo` — только
для amd64). Окно открывает коммандер:

```
   DemoAnts.Open
```

Строки «echo … | tools/run64.sh» с точным журналом у этой страницы нет: картина зависит от
времени и от планировщика, и одинакового вывода два прогона не дают. В консоли
`echo 'DemoAnts.Run' | tools/run64.sh` печатает уменьшенные ASCII-кадры поля каждые 100 шагов
(раздел «Консольный кадр»); детерминирован класс исхода, а не кадр.

## Что произошло


- `Field` — запись с массивами еды, феромона, стен, положений муравьёв и счётчиками. Её
  исключающие методы `Step` и `Evaporate` — единственный доступ к массивам: всё поле — один
  монитор.
- Каждый муравей — `Tasks.Task`: `Ant.Run` выполняет один `Field.Step`, спит `1000 DIV speed` мс
  и повторяет. Скорость по умолчанию — 10 шагов в секунду; скорость модели не зависит от
  перерисовки. Треть муравьёв стартует по свободным клеткам поля, остальные — в гнезде, поэтому
  картинка читается сразу.
- `Evaporator` — ещё одна задача: каждые 120 мс она уменьшает феромон каждой клетки на единицу.
  Насыщенный след (`maxPher = 100`) исчезает примерно за 12 с.
- `Restore` только читает поле и рисует. Окно не владеет моделированием: `Services.Action`
  тикает каждые 66 мс и зовёт `Views.Update`.
- Система кооперативная и однонитевая: `Restore` исполняется между шагами планировщика и читает
  согласованное поле, не беря монитор.

## Стены и еда

Четыре стены с проходами отделяют гнездо от еды:

| Стена | Клетки | Проход |
|---|---|---|
| левая вертикальная | `x = 54`, `y = 0..99` | `y = 42..54` |
| правая вертикальная | `x = 106`, `y = 0..99` | `y = 46..58` |
| верхняя горизонтальная | `y = 25`, `x = 58..102` | `x = 86..96` |
| нижняя горизонтальная | `y = 75`, `x = 58..102` | `x = 62..72` |

Шесть куч еды (радиус 4) лежат за стенами: четыре угла и середина верха и низа. Прямая от гнезда
к каждой куче пересекает стену, поэтому видимый след феромона идёт вдоль стены и через проход.
Стены непроходимы, и муравей «нюхает» феромон только в соседних свободных клетках: стена
скрывает след за собой.

`Reset` сначала строит стены и кладёт еду только в свободные клетки; клетка гнезда никогда не
бывает стеной. `FoodReachable` выполняет поиск в ширину от гнезда по свободным клеткам и
проверяет, что каждая клетка еды достижима; это использует тест.

## Команды

```
DemoAnts.Open      (* окно: создать поле, запустить муравьёв, открыть вид *)
DemoAnts.Start     (* запустить или продолжить после паузы; после Stop — заново *)
DemoAnts.Stop      (* завершить задачи муравьёв и испарителя *)
DemoAnts.Pause     (* заморозить модель, сохранив задачи и окно *)
DemoAnts.Resume    (* продолжить после Pause *)
DemoAnts.Step      (* один шаг модели для каждого муравья, только на паузе *)
DemoAnts.Faster    (* скорость ×2, не более 200 шагов/с *)
DemoAnts.Slower    (* скорость ÷2, не менее 1 шага/с *)
DemoAnts.Reset     (* 1000 муравьёв, еда восстановлена, феромон стёрт *)
DemoAnts.Run       (* консоль: 1000 муравьёв, 300 шагов, уменьшенные ASCII-кадры *)
DemoAnts.Simulate  (* API: Simulate(ants, steps) — без вывода, для тестов *)
DemoAnts.Bench     (* замер: 1000 муравьёв, 200 шагов *)
```

`Simulate(ants, steps)` выполняет `ants × steps` ходов как можно быстрее, без темпа, останавливает
все задачи и возвращает доставленную в гнездо еду. `DemoAnts.SetSpeed(n)` — API за
`Faster`/`Slower`; `DemoAnts.Speed()` читает текущее значение.

## Обозначения

| Элемент | Вид |
|---|---|
| фон | почти белый `00F8F8F4H` |
| стена | тёмно-серый `0034343CH` |
| гнездо | оливковое пятно (радиус 4 клетки) с более тёмным ядром |
| еда | зелёный, бледный `00AAEBBEH` (почти съедена) → глубокий `00147814H` |
| феромон | синий, бледный `00FFF0E1H` (слабый) → глубокий `00C83C14H` (насыщенный) |
| муравей без еды | тёмно-коричневый `00142332H` |
| муравей с едой | оранжево-красный `001450E6H` |

Строка состояния сверху показывает `ants=`, `step=` (ходов на муравья), `food=`, `speed=`
(шагов/с) и `paused`/`stopped`, когда это применимо.

## Окно и размер клетки

Размер вида по умолчанию — `265 × 172 Ports.mm` (около 1000×650 px при 96 dpi). Клетка квадратная:
`min((r-l) DIV W, (b-t) DIV H)` после полосы состояния 6 мм; она не меньше 3 px. Поле
центрируется и растягивается по окну.

## Консольный кадр

`DemoAnts.Run` печатает уменьшенный кадр (каждая 2-я клетка: 80×50) каждые 100 шагов. Гнездо
`#`, муравей `*`, стена `X`, еда `+`, феромон `.`, свободная клетка — пробел. Тот же `PrintFrame`
использует GUI-проверка.

## GUI-проверка без экрана

`tools/test-demo-gui.sh` поднимает `gtk4-broadwayd` в отдельном каталоге времени выполнения,
запускает `blackbox --BB_RUN=DemoAnts.GuiCheck` и требует `DemoAnts: ok=1`. `GuiCheck` открывает
окно, печатает ASCII-кадр и счётчики (включая `frame_ms`, `elapsed_ms`, `steps/s`) каждые 40
тиков перерисовки, а через 400 тиков проверяет, что таймер работал, еда выросла, все 1000
муравьёв живы и тупика нет. Мышь не эмулируется, экран пользователя не затрагивается.

Замер с 1000 муравьями (скорость 10):

| Что | Результат |
|---|---|
| шаг модели, 200 тыс. ходов (`Bench`) | 248 мс, 1.24 мкс на ход; полный круг 1000 муравьёв — около 1.24 мс |
| кадр (`Restore`, 160×100 и 1000 муравьёв) | 355 мс на 407 кадров → 0.87 мс на кадр |
| эффективная скорость | 9 шагов муравья в секунду при `speed=10` |
| первая еда | между 8-й и 12-й секундой; 71 единица к 40-й секунде |
| задачи | 1000 задач-муравьёв и 1 испаритель, все живы, тупика нет |

## Тест

`Demo_devExamples` (пометка `proc`) проверяет муравьёв:

- `TestProcAnts` — `Simulate(1000, 300)`: еда доставлена, 1000 муравьёв, все завершились, без
  трапов и тупика;
- `TestProcAntsWalls` — не менее трёх стен, ни один муравей не в стене, нарушения нет, стены
  действительно отклоняли ходы (`WallHits > 0`);
- `TestProcAntsReachable` — поиск в ширину от гнезда достигает каждой клетки еды;
- `TestProcAntsSpeed` — `Faster` останавливается на 200, `Slower` — на 1;
- `TestProcAntsFoodWalls` — 200 муравьёв, 1200 шагов: еда доставлена в обход стен;
- `TestProcAntsMany` — 1000 муравьёв завершаются и убираются: `Tasks.Count() = 0`, без трапов;
- `TestProcAntsPauseStep` — `Pause` + `Step` делает ровно один круг.

Точное количество еды не проверяется: планировщик и испаритель в реальном времени делают его
зависимым от машины, но класс исхода (еда растёт, муравьи живы, тупика нет) детерминирован.

## Только AMD64

Подсистема `Demo` собирается для AMD64. Ядро задач и мониторов работает и на i386, но библиотека
`Async` (каналы) там не собирается; `DemoAnts` нужны только `Tasks`, `Views` и `Services`, но он
входит в ту же подсистему и собирается вместе с ней.

В этом примере мы увидели тысячу задач над одним монитором, окно, которое читает поле между
шагами планировщика без захвата монитора, и разделение скорости модели и перерисовки.
