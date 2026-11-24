MODULE Monitors;
(** Мониторы.

	Ядро знает о мониторах только через один объект-крючок
	Kernel.MonitorHook: экспортируемые Kernel.Lock/Unlock/Await
	проверяют его и зовут методы. Компилятор зовёт три имени ядра;
	их сигнатуры и отпечатки совпадают с Kernel.

	Таблица мониторов, цепочки взятий, очереди ожидания и свободные
	списки — глобальные и кучевые объекты этого модуля. MarkGlobals
	сканирует их сам; отдельных MarkMonitors/MarkLocks ядру не нужно.
	Объект под монитором хранится полем ANYPTR (не S.ADDRESS): только
	так сборщик видит его как корень.

	Цепочка взятий привязана к токену задачи, а не к записи стека ядра:
	Kernel.Stack о мониторах не знает. Записи цепочек лежат в
	хэше по токену; текущая записка кэшируется (cur) и переключается
	лениво при первом обращении к мониторам в другой задаче.

	Схема AWAIT — Мюллера: условие читает состояние в кадре ждущего,
	перевычисляет уходящий (Unlock/Await) в кадрах ждущих. Трап в чужом
	условии ловится Kernel.Try с подавлением уборки (Kernel.SetQuiet) и
	переадресуется ждущей задаче.

	Трапы (перед трапом в консоль пишется «Kernel: <текст>»):
		20	Lock: объект NIL
		21	Lock: монитор держит другая задача, а вызов — с главного стека
		22	Unlock, Await: монитор не у вызывающего
		23	Unlock: мониторы отпускаются не в порядке LIFO
		24	Await: ожидание вне задачи
		25	Await: условие NIL
		60	Await: постусловие не достигнуто — условие трапнуло при
			вычислении другой задачей; исходный номер — в консоли
		100	снятие взятий при трапе: цепочка короче своей глубины
			(нарушен инвариант модуля)

	Ожидание монитора — цикл «пока не владелец»: пробуждение без
	монитора (посторонний Tasks.Unpark) ложное и не ошибка. *)

	IMPORT S := SYSTEM, Kernel;

	CONST
		monHash = 64;	(* корзин в таблице мониторов *)
		chainHash = 64;	(* корзин в таблице цепочек взятий *)
		mainToken = Kernel.mainToken;	(* токен главного стека *)
		managed = 2;	(* поле владельца: монитором управляет таблица *)
		word = SIZE(S.ADDRESS);
		monName = "@mon";	(* скрытое поле владельца, которое заводит компилятор *)

	TYPE
		CondProc = Kernel.CondProc;

		TaskRef = POINTER TO RECORD
			task: S.ADDRESS;	(* токен ждущей/входящей задачи *)
			next: TaskRef
		END;

		WaitRef = POINTER TO RECORD
			task: S.ADDRESS;
			cond: CondProc;
			frame: S.ADDRESS;
			trap: INTEGER;		(* трап при вычислении условия: переадресуется *)
			next: WaitRef
		END;

		Monitor = POINTER TO MonitorRec;
		MonitorRec = RECORD
			next: Monitor;		(* цепочка корзины *)
			key: S.ADDRESS;	(* адрес объекта: ключ таблицы *)
			obj: ANYPTR;		(* объект, пока монитор занят или в очередях: корень сборщика; у свободной записи hot — NIL *)
			owner: S.ADDRESS;	(* токен владельца; 0 — свободен *)
			off: S.ADDRESS;	(* смещение поля владельца в объекте; -1 — поля нет *)
			count: INTEGER;		(* глубина повторного входа *)
			enter, enterTail: TaskRef;
			wait, waitTail: WaitRef
		END;

		(* Одно взятие монитора: запись в цепочке токена. Отдельная запись
		   нужна и для повторного входа (тот же монитор), и чтобы трап снял
		   ровно взятия внутри Kernel.Try (глубина сохраняется в BeginTry). *)
		LockRec = POINTER TO RECORD
			next: LockRec;
			mon: Monitor
		END;

		(* Цепочка взятий задачи. Запись — корень сборщика (лежит в chains),
		   cur — только кэш текущего токена. *)
		Chain = POINTER TO RECORD
			next: Chain;		(* цепочка корзины *)
			tok: S.ADDRESS;
			head: LockRec;
			depth: INTEGER
		END;

		Hook = POINTER TO RECORD (Kernel.MonitorHook) END;

	VAR
		monBuckets: ARRAY monHash OF Monitor;
		nofMonitors: INTEGER;	(* записей в таблице, диагностика *)
		freeMonitors: Monitor;	(* свободные записи: Lock без соперников не выделяет *)
		(* Последний монитор, ставший свободным. Его запись остаётся в таблице
		   (obj = NIL — объект не удерживается), и повторный Lock того же
		   объекта не ищет корзину и не выделяет запись.
		   Инвариант: hot = NIL или hot в таблице; у свободной hot obj = NIL.
		   Свободная запись с ключом умершего объекта безвредна: свободный
		   монитор без очередей неотличим от отсутствия записи. *)
		hot: Monitor;
		freeLocks: LockRec;	(* свободные записи цепочки взятий *)
		(* Свободные записи очередей входа и ожидания: блокирующая операция не
		   выделяет памяти. Инвариант: записи свободных списков не лежат ни в
		   одной очереди монитора. *)
		freeTaskRefs: TaskRef;
		freeWaitRefs: WaitRef;

		chains: ARRAY chainHash OF Chain;
		freeChains: Chain;
		curTok: S.ADDRESS;	(* токен, к которому относится cur *)
		cur: Chain;		(* кэш цепочки текущего токена; может быть NIL *)

		hook: Hook;

		(* Вычисление чужого условия внутри Unlock/Await. *)
		condP: CondProc;
		condF: S.ADDRESS;
		condResult, condCompleted: BOOLEAN;
		(* Глубина вложенных HandOff, перебирающих условия. Пока > 0, LockDepth
		   считает монитор взятым: точка уступки в условии (прямой Kernel.Await
		   с процедурой из модуля с ключом ^) не переключает задачу, и в
		   передаваемый монитор никто не входит. *)
		handing: INTEGER;
		(* Число открытых Kernel.Try на главном стеке (beginTry без endTry).
		   Трап главного стека вне Try (окно: зритель трапов, главный цикл
		   перезапускается) не доходит ни до одного endTry: взятия снимает
		   purge, если открыт только Try самой уборки. *)
		mainTries: INTEGER;
		(* Последний тип, для которого искали поле владельца, и ответ. *)
		fieldType: Kernel.Type;
		fieldOff: S.ADDRESS;

	(* -------------------- диагностика ------------------------------------ *)

	(* Прямой вывод платформы: не выделяет память (важно при трапах). *)
	PROCEDURE MonStr (IN s: ARRAY OF CHAR);
	BEGIN
		IF (Kernel.platform # NIL) & (Kernel.platform.String # NIL) THEN Kernel.platform.String(s) END
	END MonStr;

	PROCEDURE MonLn;
	BEGIN
		IF (Kernel.platform # NIL) & (Kernel.platform.Ln # NIL) THEN Kernel.platform.Ln END
	END MonLn;

	PROCEDURE MonInt (x: LONGINT);
		VAR a: ARRAY 24 OF CHAR; b: ARRAY 24 OF CHAR; i, j, k: INTEGER; y: LONGINT; neg: BOOLEAN;
	BEGIN
		neg := x < 0; IF neg THEN x := -x END;
		i := 0; REPEAT y := x MOD 10; b[i] := CHR(SHORT(y) + ORD('0')); INC(i); x := x DIV 10 UNTIL x = 0;
		k := 0;
		IF neg THEN a[k] := '-'; INC(k) END;
		j := i - 1;
		WHILE j >= 0 DO a[k] := b[j]; INC(k); DEC(j) END;
		a[k] := 0X;
		MonStr(a)
	END MonInt;

	PROCEDURE MonHex (x: LONGINT);
		CONST digits = "0123456789ABCDEF";
		VAR a: ARRAY 20 OF CHAR; dg: ARRAY 17 OF CHAR; i, y: INTEGER;
	BEGIN
		dg := digits;
		i := 15;
		REPEAT
			y := SHORT(x MOD 16); IF y < 0 THEN y := y + 16 END;
			a[i] := dg[y]; DEC(i); x := x DIV 16
		UNTIL (x = 0) OR (i < 0);
		a[16] := 0X;
		MonStr(a)
	END MonHex;

	PROCEDURE MonErr (IN msg: ARRAY OF CHAR);
	(* Текст нарушения перед трапом: номер трапа — у вызывающего. *)
	BEGIN
		MonStr("Kernel: "); MonStr(msg); MonLn
	END MonErr;

	(* -------------------- планировщик (через крючок ядра) ---------------- *)

	PROCEDURE CurrentToken (): S.ADDRESS;
	BEGIN
		RETURN Kernel.monTok
	END CurrentToken;

	PROCEDURE UnparkTask (t: S.ADDRESS);
		VAR s: Kernel.Scheduler;
	BEGIN
		s := Kernel.sched;
		IF (s # NIL) & (t # 0) THEN s.unpark(t) END
	END UnparkTask;

	PROCEDURE ParkNow;
		VAR s: Kernel.Scheduler;
	BEGIN
		s := Kernel.sched;
		IF s = NIL THEN MonErr("wait outside a task") END;
		ASSERT(s # NIL, 24);
		s.park()
	END ParkNow;

	PROCEDURE TaskName (t: S.ADDRESS; OUT s: ARRAY OF CHAR);
		VAR sc: Kernel.Scheduler;
	BEGIN
		IF t = 0 THEN s := "?"
		ELSIF t = mainToken THEN s := "main"
		ELSE
			sc := Kernel.sched;
			IF sc # NIL THEN sc.name(t, s) ELSE s := "?" END
		END
	END TaskName;

	(* -------------------- таблица мониторов ------------------------------- *)

	PROCEDURE MonIndex (p: S.ADDRESS): INTEGER;
	BEGIN
		RETURN SHORT(S.VAL(S.ADDRESS, p) DIV 16 MOD monHash)
	END MonIndex;

	PROCEDURE FindMon (obj: S.ADDRESS): Monitor;
		VAR m: Monitor;
	BEGIN
		IF (hot # NIL) & (hot.key = obj) THEN RETURN hot END;
		m := monBuckets[MonIndex(obj)];
		WHILE (m # NIL) & (m.key # obj) DO m := m.next END;
		RETURN m
	END FindMon;

	PROCEDURE TakeMon (): Monitor;
	(** Свободный список записей мониторов: в цикле Lock/Unlock одного
		объекта память больше не выделяется. *)
		VAR m: Monitor;
	BEGIN
		IF freeMonitors # NIL THEN
			m := freeMonitors; freeMonitors := m.next; m.next := NIL
		ELSE
			NEW(m)
		END;
		RETURN m
	END TakeMon;

	PROCEDURE NewMon (obj, off: S.ADDRESS): Monitor;
	(* Пред: записи с ключом obj в таблице нет. Пост: off >= 0 — поле
	   владельца объекта = managed. *)
		VAR m: Monitor; i: INTEGER;
	BEGIN
		i := MonIndex(obj);
		m := TakeMon();
		m.next := monBuckets[i]; monBuckets[i] := m;
		m.key := obj; m.off := off; m.obj := NIL; m.owner := 0; m.count := 0;
		IF off >= 0 THEN S.PUT(obj + off, S.VAL(S.ADDRESS, managed)) END;
		m.enter := NIL; m.enterTail := NIL; m.wait := NIL; m.waitTail := NIL;
		INC(nofMonitors);
		RETURN m
	END NewMon;

	PROCEDURE^ DropMon (m: Monitor);

	PROCEDURE GetMon (obj, off: S.ADDRESS): Monitor;
	(** Запись монитора obj. Свободная запись без очередей с другим off —
		след умершего объекта по тому же адресу: она заменяется. *)
		VAR m: Monitor;
	BEGIN
		m := FindMon(obj);
		IF (m # NIL) & (m.off # off) & (m.owner = 0) & (m.enter = NIL) & (m.wait = NIL) THEN
			DropMon(m); m := NIL
		END;
		IF m = NIL THEN m := NewMon(obj, off) END;
		RETURN m
	END GetMon;

	PROCEDURE DropMon (m: Monitor);
		VAR i: INTEGER; p: Monitor;
	BEGIN
		i := MonIndex(m.key);
		IF monBuckets[i] = m THEN
			monBuckets[i] := m.next
		ELSE
			p := monBuckets[i];
			WHILE (p # NIL) & (p.next # m) DO p := p.next END;
			IF p # NIL THEN p.next := m.next END
		END;
		IF hot = m THEN hot := NIL END;
		IF m.off >= 0 THEN S.PUT(m.key + m.off, S.VAL(S.ADDRESS, 0)) END;	(* снова быстрый путь *)
		m.key := 0; m.off := -1; m.obj := NIL; m.owner := 0; m.count := 0;
		m.enter := NIL; m.enterTail := NIL; m.wait := NIL; m.waitTail := NIL;
		m.next := freeMonitors; freeMonitors := m;
		DEC(nofMonitors)
	END DropMon;

	PROCEDURE DropIfUnused (m: Monitor);
	(** Свободная запись без очередей: последняя остаётся в таблице как hot
		(объект отпускается), прежняя hot уходит в свободный список. *)
		VAR old: Monitor;
	BEGIN
		IF (m # NIL) & (m.owner = 0) & (m.enter = NIL) & (m.wait = NIL) THEN
			m.obj := NIL;
			IF m.off >= 0 THEN
				DropMon(m)	(* поле владельца снова 0: следующий вход — быстрый путь *)
			ELSIF m # hot THEN
				old := hot; hot := m;
				IF (old # NIL) & (old.owner = 0) & (old.enter = NIL) & (old.wait = NIL) THEN DropMon(old) END
			END
		END
	END DropIfUnused;

	PROCEDURE AppendWait (m: Monitor; w: WaitRef);
	BEGIN
		w.next := NIL;
		IF m.waitTail = NIL THEN m.wait := w ELSE m.waitTail.next := w END;
		m.waitTail := w
	END AppendWait;

	PROCEDURE RemoveWait (m: Monitor; w: WaitRef);
		VAR p: WaitRef;
	BEGIN
		IF m.wait = w THEN
			m.wait := w.next;
			IF m.wait = NIL THEN m.waitTail := NIL END
		ELSE
			p := m.wait;
			WHILE (p # NIL) & (p.next # w) DO p := p.next END;
			IF p # NIL THEN
				p.next := w.next;
				IF m.waitTail = w THEN m.waitTail := p END
			END
		END;
		w.next := NIL
	END RemoveWait;

	PROCEDURE AppendEnter (m: Monitor; q: TaskRef);
	BEGIN
		q.next := NIL;
		IF m.enterTail = NIL THEN m.enter := q ELSE m.enterTail.next := q END;
		m.enterTail := q
	END AppendEnter;

	PROCEDURE RemoveEnter (m: Monitor; q: TaskRef);
		VAR p: TaskRef;
	BEGIN
		IF m.enter = q THEN
			m.enter := q.next;
			IF m.enter = NIL THEN m.enterTail := NIL END
		ELSE
			p := m.enter;
			WHILE (p # NIL) & (p.next # q) DO p := p.next END;
			IF p # NIL THEN
				p.next := q.next;
				IF m.enterTail = q THEN m.enterTail := p END
			END
		END;
		q.next := NIL
	END RemoveEnter;

	PROCEDURE TakeTaskRef (t: S.ADDRESS): TaskRef;
	(* Пост: запись вне очередей, task = t. *)
		VAR q: TaskRef;
	BEGIN
		IF freeTaskRefs # NIL THEN q := freeTaskRefs; freeTaskRefs := q.next ELSE NEW(q) END;
		q.next := NIL; q.task := t;
		RETURN q
	END TakeTaskRef;

	PROCEDURE FreeTaskRef (q: TaskRef);
	(* Пред: q снята с очереди входа. *)
	BEGIN
		q.task := 0; q.next := freeTaskRefs; freeTaskRefs := q
	END FreeTaskRef;

	PROCEDURE TakeWaitRef (t: S.ADDRESS; cond: CondProc; frame: S.ADDRESS): WaitRef;
	(* Пост: запись вне очередей, поля заданы, trap = 0. *)
		VAR w: WaitRef;
	BEGIN
		IF freeWaitRefs # NIL THEN w := freeWaitRefs; freeWaitRefs := w.next ELSE NEW(w) END;
		w.next := NIL; w.task := t; w.cond := cond; w.frame := frame; w.trap := 0;
		RETURN w
	END TakeWaitRef;

	PROCEDURE FreeWaitRef (w: WaitRef);
	(* Пред: w снята с очереди ожидания. *)
	BEGIN
		w.task := 0; w.cond := NIL; w.frame := 0; w.next := freeWaitRefs; freeWaitRefs := w
	END FreeWaitRef;

	(* -------------------- цепочки взятий по токену ------------------------ *)

	PROCEDURE ChainIndex (t: S.ADDRESS): INTEGER;
	BEGIN
		RETURN SHORT(S.VAL(S.ADDRESS, t) DIV 16 MOD chainHash)
	END ChainIndex;

	PROCEDURE FindChain (t: S.ADDRESS): Chain;
		VAR c: Chain;
	BEGIN
		c := chains[ChainIndex(t)];
		WHILE (c # NIL) & (c.tok # t) DO c := c.next END;
		RETURN c
	END FindChain;

	PROCEDURE NewChain (t: S.ADDRESS): Chain;
		VAR c: Chain; i: INTEGER;
	BEGIN
		IF freeChains # NIL THEN
			c := freeChains; freeChains := c.next
		ELSE
			NEW(c)
		END;
		i := ChainIndex(t);
		c.tok := t; c.head := NIL; c.depth := 0;
		c.next := chains[i]; chains[i] := c;
		RETURN c
	END NewChain;

	PROCEDURE DropChain (c: Chain);
		VAR i: INTEGER; p: Chain;
	BEGIN
		i := ChainIndex(c.tok);
		IF chains[i] = c THEN
			chains[i] := c.next
		ELSE
			p := chains[i];
			WHILE (p # NIL) & (p.next # c) DO p := p.next END;
			IF p # NIL THEN p.next := c.next END
		END;
		c.tok := 0; c.head := NIL; c.depth := 0;
		c.next := freeChains; freeChains := c
	END DropChain;

	PROCEDURE Sync;
	(** Переключить кэш на токен текущей задачи. Пустая запись прежнего
		токена удаляется: иначе таблица росла бы на каждую когда-либо
		запущенную задачу. Запись с взятиями остаётся корнем сборщика. *)
		VAR t: S.ADDRESS;
	BEGIN
		t := CurrentToken();
		IF t # curTok THEN
			IF (cur # NIL) & (cur.depth = 0) THEN DropChain(cur); cur := NIL END;
			curTok := t;
			cur := FindChain(t)
		END
	END Sync;

	PROCEDURE TakeLock (): LockRec;
		VAR r: LockRec;
	BEGIN
		IF freeLocks # NIL THEN
			r := freeLocks; freeLocks := r.next
		ELSE
			NEW(r)
		END;
		r.mon := NIL; r.next := NIL;
		RETURN r
	END TakeLock;

	PROCEDURE FreeLock (r: LockRec);
	BEGIN
		r.mon := NIL; r.next := freeLocks; freeLocks := r
	END FreeLock;

	PROCEDURE ChainPush (m: Monitor);
		VAR r: LockRec;
	BEGIN
		IF cur = NIL THEN cur := NewChain(curTok) END;
		r := TakeLock(); r.mon := m; r.next := cur.head; cur.head := r; INC(cur.depth)
	END ChainPush;

	PROCEDURE PushTo (t: S.ADDRESS; m: Monitor);
	(** Записать взятие в цепочку спящей задачи: монитор передан ей
		вместе с владением. Нужно, чтобы отмена/трап на пробуждении
		освободил монитор, даже если задача ещё не вернулась из Park. *)
		VAR c: Chain; r: LockRec;
	BEGIN
		IF t = curTok THEN
			c := cur
		ELSE
			c := FindChain(t)
		END;
		IF c = NIL THEN c := NewChain(t) END;
		r := TakeLock(); r.mon := m; r.next := c.head; c.head := r; INC(c.depth);
		IF t = curTok THEN cur := c END
	END PushTo;

	(* -------------------- условия и передача монитора --------------------- *)

	PROCEDURE CondBody (a, b, c: S.ADDRESS);
	BEGIN
		condCompleted := FALSE;
		condResult := Kernel.CallCond(condP, condF);	(* кадр ждущего — в BX *)
		condCompleted := TRUE
	END CondBody;

	PROCEDURE EvalCond (p: CondProc; frame: S.ADDRESS; OUT ok: BOOLEAN; OUT code: INTEGER): BOOLEAN;
	BEGIN
		condP := p; condF := frame; condResult := FALSE; condCompleted := FALSE; code := 0;
		Kernel.SetQuiet(TRUE);
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(CondBody)), 0, 0, 0);
		Kernel.SetQuiet(FALSE);
		ok := condCompleted;
		IF ~ok THEN code := Kernel.err END;
		RETURN condResult
	END EvalCond;

	(* Передать монитор: сначала ждущий с истинным условием, затем первый
	   входящий, иначе объект свободен. При переборе
	   условий m.owner = 0, но переключения нет: handing > 0, и LockDepth
	   запрещает уступку в Kernel.Poll. Поэтому между вычислением условия
	   и передачей монитор никто не берёт, и новый владелец получает его
	   атомарно (семантика Active Oberon). *)
	PROCEDURE HandOff (m: Monitor; skip: S.ADDRESS);
	(** skip — задача, которая сама уходит в ожидание (Await): ей
		монитор не передаётся, иначе она получила бы его обратно и
		ожидание стало бы холостым циклом. *)
		VAR w, nx: WaitRef; e: TaskRef; t: S.ADDRESS; ok, res: BOOLEAN; code: INTEGER;
	BEGIN
		w := m.wait;
		WHILE w # NIL DO
			nx := w.next;
			IF w.task # skip THEN
				INC(handing);
				res := EvalCond(w.cond, w.frame, ok, code);
				DEC(handing);
				IF res THEN
					RemoveWait(m, w);
					m.owner := w.task; m.count := 1;
					UnparkTask(w.task); RETURN
				ELSIF ~ok THEN
					(* трап в условии: будим ждущего, его Await трапует *)
					RemoveWait(m, w);
					w.trap := code;
					m.owner := w.task; m.count := 1;
					UnparkTask(w.task); RETURN
				END
			END;
			w := nx
		END;
		(* входящий *)
		e := m.enter;
		IF e # NIL THEN
			m.enter := e.next;
			IF m.enter = NIL THEN m.enterTail := NIL END;
			t := e.task; e.next := NIL;
			m.owner := t; m.count := 1;
			PushTo(t, m);	(* входящий ещё не имеет записи о взятии *)
			UnparkTask(t); RETURN
		END;
		m.owner := 0; m.count := 0;
		DropIfUnused(m)
	END HandOff;

	(* Снять мониторы, взятые внутри Kernel.Try, до сохранённой глубины.
	   Монитор, которым сейчас владеет другая задача (ждём в Await),
	   снимается из цепочки без передачи. *)
	PROCEDURE ReleaseTo (depth: INTEGER);
		VAR r: LockRec; m: Monitor; me: S.ADDRESS;
	BEGIN
		me := CurrentToken();
		WHILE (cur # NIL) & (cur.depth > depth) DO
			r := cur.head;
			ASSERT(r # NIL, 100);	(* Инвариант: depth — длина цепочки head *)
			cur.head := r.next; DEC(cur.depth);
			m := r.mon; FreeLock(r);
			(* Монитор, которым владеет другая задача (мы ждём в Await),
			   только снимаем из цепочки — его счётчик не наш. *)
			IF m.owner = me THEN
				DEC(m.count);
				IF m.count <= 0 THEN
					m.count := 0; m.owner := 0; HandOff(m, 0)
				END
			END
		END
	END ReleaseTo;

	(* Трап в задаче: её записи в очередях мониторов больше не действительны.
	   Сами мониторы освобождает ReleaseTo (Try). *)
	PROCEDURE PurgeCurrent;
		VAR me: S.ADDRESS; i: INTEGER; m, nx: Monitor; w, wn: WaitRef; e, en: TaskRef;
	BEGIN
		me := curTok;
		i := 0;
		WHILE i < monHash DO
			m := monBuckets[i];
			WHILE m # NIL DO
				nx := m.next;
				w := m.wait;
				WHILE w # NIL DO
					wn := w.next;
					IF w.task = me THEN RemoveWait(m, w) END;
					w := wn
				END;
				e := m.enter;
				WHILE e # NIL DO
					en := e.next;
					IF e.task = me THEN RemoveEnter(m, e) END;
					e := en
				END;
				DropIfUnused(m);
				m := nx
			END;
			INC(i)
		END
	END PurgeCurrent;

	(* -------------------- операции над мониторами ------------------------- *)

	PROCEDURE DoLock (obj, off: S.ADDRESS);
	(** Взять монитор объекта. Повторный вход разрешён (счётчик).
		Занятый чужим монитор: задача ждёт; главный стек или отсутствие
		Tasks — трап. *)
		VAR me: S.ADDRESS; m: Monitor; q: TaskRef;
	BEGIN
		IF obj = 0 THEN MonErr("Lock: NIL object") END;
		ASSERT(obj # 0, 20);
		me := curTok;	(* Sync в крючке уже прочитал токен *)
		m := GetMon(obj, off);
		IF m.owner = me THEN
			INC(m.count); ChainPush(m)
		ELSIF m.owner = 0 THEN
			m.obj := S.VAL(ANYPTR, obj); m.owner := me; m.count := 1; ChainPush(m)
		ELSIF me # mainToken THEN
			m.obj := S.VAL(ANYPTR, obj);	(* очередь входа: объект — корень *)
			q := TakeTaskRef(me); AppendEnter(m, q);
			(* Пробуждение без монитора (посторонний Unpark) — ложное: ждём дальше. *)
			REPEAT ParkNow UNTIL m.owner = me;
			FreeTaskRef(q)	(* HandOff снял её с очереди входа *)
			(* запись о взятии уже добавлена PushTo при передаче *)
		ELSE
			MonErr("Lock: monitor is held by another task"); HALT(21)
		END
	END DoLock;

	PROCEDURE DoUnlock (obj: S.ADDRESS);
	(** Отпустить монитор. Вложенные мониторы снимаются в порядке LIFO;
		если счётчик стал нулём, монитор передаётся ждущему или входящему. *)
		VAR me: S.ADDRESS; m: Monitor; r: LockRec;
	BEGIN
		me := curTok;
		m := FindMon(obj);
		IF (m = NIL) OR (m.owner # me) THEN
			MonErr("Unlock: monitor is not held by the caller"); HALT(22)
		END;
		IF cur # NIL THEN r := cur.head ELSE r := NIL END;
		IF (r = NIL) OR (r.mon # m) THEN
			MonErr("Unlock: monitors must be released in LIFO order"); HALT(23)
		END;
		cur.head := r.next; DEC(cur.depth); FreeLock(r);
		DEC(m.count);
		IF m.count = 0 THEN
			m.owner := 0;
			HandOff(m, 0)
		END
	END DoUnlock;

	PROCEDURE DoAwait (cond: CondProc; frame: S.ADDRESS; obj: S.ADDRESS);
	(** Условное ожидание (схема Мюллера): cond читает состояние в кадре
		ждущего; если оно ложно, монитор отпускается и задача спит. Условия
		перевычисляет уходящий (Unlock/Await) в кадрах ждущих; монитор
		получает первый, чьё условие истинно, раньше новых входящих.
		Трап в условии переадресуется ждущей задаче. *)
		VAR me: S.ADDRESS; m: Monitor; w: WaitRef; saved: INTEGER;
	BEGIN
		me := curTok;
		IF me = mainToken THEN MonErr("Await: wait outside a task"); HALT(24) END;
		IF cond = NIL THEN MonErr("Await: NIL condition"); HALT(25) END;
		m := FindMon(obj);
		IF (m = NIL) OR (m.owner # me) THEN
			MonErr("Await: monitor is not held by the caller"); HALT(22)
		END;
		IF Kernel.CallCond(cond, frame) THEN RETURN END;	(* компилятор тоже проверяет до вызова *)
		w := TakeWaitRef(me, cond, frame);
		AppendWait(m, w);
		saved := m.count;
		m.count := 0;
		m.owner := 0;
		HandOff(m, me);
		(* Монитор передаётся вместе с исходом условия (и трапом в нём);
		   пробуждение без монитора — ложное, ждём дальше. *)
		REPEAT ParkNow UNTIL m.owner = me;
		IF w.trap # 0 THEN
			(* Трап случился в чужом условии; переадресуем его этой задаче.
			   HALT требует константу, поэтому исходный код — в журнал. *)
			MonStr("Kernel: Await: trap "); MonInt(w.trap);
			MonStr(" in the condition"); MonLn;
			HALT(60)
		END;
		FreeWaitRef(w);	(* HandOff снял её с очереди ожидания *)
		m.count := saved
	END DoAwait;

	(* -------------------- поле владельца и быстрый путь ------------------ *)

	PROCEDURE FieldOf (obj: S.ADDRESS): S.ADDRESS;
	(** Пред: obj — запись в куче. Пост: смещение скрытого поля владельца
		"@mon" ближайшего к корню типа цепочки, где оно есть; -1 — поля нет.
		Ответ для последнего типа запоминается. *)
		VAR t, b: Kernel.Type; lev, i: INTEGER; name: Kernel.Name; d: Kernel.Directory; p: ANYPTR;
	BEGIN
		p := S.VAL(ANYPTR, obj); t := Kernel.TypeOf(p^);
		IF t # fieldType THEN
			fieldType := t; fieldOff := -1;
			lev := 0;
			WHILE (fieldOff < 0) & (lev <= Kernel.LevelOf(t)) DO
				b := t.base[lev]; d := b.fields;
				IF d # NIL THEN
					i := 0;
					WHILE (fieldOff < 0) & (i < d.num) DO
						Kernel.GetObjName(b.mod, S.VAL(Kernel.Object, S.ADR(d.obj[i])), name);
						IF name = monName THEN fieldOff := d.obj[i].offs END;
						INC(i)
					END
				END;
				INC(lev)
			END
		END;
		RETURN fieldOff
	END FieldOf;

	PROCEDURE Flush;
	(** Пост: Kernel.monTop = Kernel.monBase; каждое взятие быстрым путём
		стало записью таблицы (поле владельца = managed, владелец — monTok,
		счётчик 1) и записью цепочки в прежнем порядке. *)
		VAR a, key, off: S.ADDRESS; obj: ANYPTR; m: Monitor;
	BEGIN
		IF Kernel.monTop # Kernel.monBase THEN
			Sync;
			a := Kernel.monBase;
			WHILE a # Kernel.monTop DO
				S.GET(a, obj); S.GET(a + word, off);
				S.PUT(a, S.VAL(S.ADDRESS, 0));	(* не удерживать объект *)
				key := S.VAL(S.ADDRESS, obj);
				m := GetMon(key, off);
				m.obj := obj; m.owner := curTok; m.count := 1; ChainPush(m);
				INC(a, 2 * word)
			END;
			Kernel.monTop := Kernel.monBase
		END
	END Flush;

	(* -------------------- крючок для ядра -------------------------------- *)

	(* Быстрый путь lock/unlock для объектов без поля владельца: тот же токен,
	   что у кэша цепочки, объект — hot, запись взятия из свободного списка.
	   Действия те же, что у DoLock/DoUnlock на этом случае; остальное
	   (поле владельца, занятый чужим, очереди, другой токен) — медленный путь.
	   Каждый вход сначала сбрасывает взятия быстрым путём компилятора (Flush). *)

	PROCEDURE (h: Hook) lock (obj: S.ADDRESS);
		VAR m: Monitor; r: LockRec; t, off: S.ADDRESS;
	BEGIN
		IF obj = 0 THEN MonErr("Lock: NIL object") END;
		ASSERT(obj # 0, 20);
		Flush;
		off := FieldOf(obj);
		t := Kernel.monTok; m := hot;
		IF (off < 0) & (t = curTok) & (cur # NIL) & (m # NIL) & (m.key = obj) & (m.off < 0) & (freeLocks # NIL)
				& ((m.owner = 0) OR (m.owner = t)) THEN
			IF m.owner = 0 THEN m.obj := S.VAL(ANYPTR, obj); m.owner := t; m.count := 1 ELSE INC(m.count) END;
			r := freeLocks; freeLocks := r.next;
			r.mon := m; r.next := cur.head; cur.head := r; INC(cur.depth)
		ELSE
			Sync; DoLock(obj, off)
		END
	END lock;

	PROCEDURE (h: Hook) lockAt (obj, off: S.ADDRESS);
	BEGIN
		Flush; Sync; DoLock(obj, off)
	END lockAt;

	PROCEDURE (h: Hook) flush;
	BEGIN
		Flush
	END flush;

	PROCEDURE (h: Hook) unlock (obj: S.ADDRESS);
		VAR m: Monitor; r: LockRec; t: S.ADDRESS;
	BEGIN
		Flush;
		t := Kernel.monTok; m := hot;
		IF (t = curTok) & (cur # NIL) & (m # NIL) & (m.key = obj) & (m.owner = t)
				& (cur.head # NIL) & (cur.head.mon = m)
				& ((m.count > 1) OR (m.wait = NIL) & (m.enter = NIL)) THEN
			r := cur.head; cur.head := r.next; DEC(cur.depth); FreeLock(r);
			DEC(m.count);
			IF m.count = 0 THEN m.owner := 0; m.obj := NIL END
		ELSE
			Sync; DoUnlock(obj)
		END
	END unlock;

	PROCEDURE (h: Hook) await (cond: CondProc; frame, obj: S.ADDRESS);
	BEGIN
		Flush; Sync; DoAwait(cond, frame, obj)
	END await;

	PROCEDURE (h: Hook) beginTry (): INTEGER;
	BEGIN
		Flush; Sync;
		IF curTok = mainToken THEN INC(mainTries) END;
		IF cur # NIL THEN RETURN cur.depth ELSE RETURN 0 END
	END beginTry;

	PROCEDURE (h: Hook) endTry (mark: INTEGER);
	BEGIN
		Flush; Sync;
		IF curTok = mainToken THEN DEC(mainTries) END;
		ReleaseTo(mark)
	END endTry;

	PROCEDURE (h: Hook) purge;
	(** Вызывается ядром после трапа (из Kernel.Try уборки): задача уходит
		из очередей мониторов. Трап главного стека вне пользовательского
		Try (mainTries = 1 — открыт только Try уборки) снимает и все взятия
		главного стека: исключающие процедуры, прерванные трапом, выходят
		из своих мониторов (сообщение о языке, раздел 8). *)
	BEGIN
		Flush; Sync; PurgeCurrent;
		IF (curTok = mainToken) & (mainTries <= 1) THEN ReleaseTo(0) END
	END purge;

	(* -------------------- рабочие запросы -------------------------------- *)

	PROCEDURE IsLocked* (obj: ANYPTR): BOOLEAN;
	(** Пост: TRUE — монитор obj кем-то взят; obj = NIL — FALSE. *)
		VAR m: Monitor;
	BEGIN
		IF obj = NIL THEN RETURN FALSE END;
		(* Ничего не взято: в таблице нет записей, кроме свободной hot, и
		   стек взятий быстрым путём пуст. Так каналы, которые спрашивают
		   на каждой операции, не ищут в таблице. *)
		IF (Kernel.monTop = Kernel.monBase)
				& ((nofMonitors = 0) OR (nofMonitors = 1) & (hot # NIL) & (hot.owner = 0)) THEN
			RETURN FALSE
		END;
		Flush;
		m := FindMon(S.VAL(S.ADDRESS, obj));
		RETURN (m # NIL) & (m.owner # 0)
	END IsLocked;

	PROCEDURE LockDepth* (): INTEGER;
	(** Пост: число взятий мониторов текущей задачей (главным стеком),
		плюс один на время перебора условий при передаче монитора;
		планировщик не уступает в точке опроса, пока оно > 0. *)
		VAR n: INTEGER;
	BEGIN
		Sync;	(* взятия быстрым путём лежат на стеке ядра: сбрасывать их незачем *)
		IF cur # NIL THEN n := cur.depth + handing ELSE n := handing END;
		RETURN n + SHORT((Kernel.monTop - Kernel.monBase) DIV (2 * word))
	END LockDepth;

	(* -------------------- запросы для тестов и отладки -------------------- *)

	PROCEDURE WaitersCount* (): INTEGER;
	(** Пост: число записей во всех очередях входа и ожидания. *)
		VAR i, n: INTEGER; m: Monitor; w: WaitRef; e: TaskRef;
	BEGIN
		Flush;
		n := 0; i := 0;
		WHILE i < monHash DO
			m := monBuckets[i];
			WHILE m # NIL DO
				w := m.wait; WHILE w # NIL DO INC(n); w := w.next END;
				e := m.enter; WHILE e # NIL DO INC(n); e := e.next END;
				m := m.next
			END;
			INC(i)
		END;
		RETURN n
	END WaitersCount;

	PROCEDURE DumpMonitors*;
	(** Пост: в консоли — журнал «кто чего ждёт»: все мониторы таблицы и
		их очереди. Зовёт и планировщик при тупике. *)
		VAR i, n: INTEGER; m: Monitor; w: WaitRef; e: TaskRef; nm: ARRAY 64 OF CHAR;
	BEGIN
		Flush;	(* взятия быстрым путём текущего токена — в таблицу *)
		MonStr("Kernel: monitor dump"); MonLn;
		n := 0; i := 0;
		WHILE i < monHash DO
			m := monBuckets[i];
			WHILE m # NIL DO
				INC(n);
				MonStr("  monitor "); MonHex(S.VAL(LONGINT, m.key));
				IF m.owner = 0 THEN MonStr(": free")
				ELSE
					TaskName(m.owner, nm);
					MonStr(": held by "); MonStr(nm); MonStr(" count="); MonInt(m.count)
				END;
				MonLn;
				w := m.wait;
				WHILE w # NIL DO
					TaskName(w.task, nm); MonStr("    waits "); MonStr(nm); MonStr(" (condition)");
					MonLn; w := w.next
				END;
				e := m.enter;
				WHILE e # NIL DO
					TaskName(e.task, nm); MonStr("    enters "); MonStr(nm); MonLn;
					e := e.next
				END;
				m := m.next
			END;
			INC(i)
		END;
		IF n = 0 THEN MonStr("  (no monitors)"); MonLn END
	END DumpMonitors;

BEGIN
	NEW(hook);
	fieldType := NIL; fieldOff := -1;
	Kernel.SetMonitorHook(hook)
END Monitors.
