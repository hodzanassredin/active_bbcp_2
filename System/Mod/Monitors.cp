MODULE Monitors;
(** Мониторы.

	Ядро знает о мониторах только через один объект-крючок
	Kernel.MonitorHook: экспортируемые Kernel.Lock/Unlock/Wait/Await
	проверяют его и зовут методы. Компилятор зовёт четыре имени ядра;
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
		22	Unlock, Wait, Await: монитор не у вызывающего
		23	Unlock: мониторы отпускаются не в порядке LIFO
		24	Wait, Await: ожидание вне задачи
		25	Await: условие NIL
		60	Await: постусловие не достигнуто — условие трапнуло при
			вычислении другой задачей; исходный номер — в консоли
		100	Lock, Wait, Await: задача разбужена без монитора *)

	IMPORT S := SYSTEM, Kernel;

	CONST
		monHash = 64;	(* корзин в таблице мониторов *)
		chainHash = 64;	(* корзин в таблице цепочек взятий *)
		mainToken = 1;	(* токен главного стека: адреса выровнены, 1 не совпадёт *)

	TYPE
		CondProc = Kernel.CondProc;

		TaskRef = POINTER TO RECORD
			task: S.ADDRESS;	(* токен ждущей/входящей задачи *)
			next: TaskRef
		END;

		WaitRef = POINTER TO RECORD
			task: S.ADDRESS;
			cond: CondProc;		(* NIL — ручной Wait *)
			frame: S.ADDRESS;
			trap: INTEGER;		(* трап при вычислении условия: переадресуется *)
			next: WaitRef
		END;

		Monitor = POINTER TO MonitorRec;
		MonitorRec = RECORD
			next: Monitor;		(* цепочка корзины *)
			obj: ANYPTR;		(* объект: поле-указатель, чтобы MarkGlobals видел корень *)
			owner: S.ADDRESS;	(* токен владельца; 0 — свободен *)
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
		VAR s: Kernel.Scheduler; t: S.ADDRESS;
	BEGIN
		s := Kernel.sched;
		IF s # NIL THEN
			t := s.current();
			IF t = 0 THEN RETURN mainToken ELSE RETURN t END
		ELSE
			RETURN mainToken
		END
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
		m := monBuckets[MonIndex(obj)];
		WHILE (m # NIL) & (S.VAL(S.ADDRESS, m.obj) # obj) DO m := m.next END;
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

	PROCEDURE NewMon (obj: S.ADDRESS): Monitor;
		VAR m: Monitor; i: INTEGER;
	BEGIN
		i := MonIndex(obj);
		m := TakeMon();
		m.next := monBuckets[i]; monBuckets[i] := m;
		m.obj := S.VAL(ANYPTR, obj); m.owner := 0; m.count := 0;
		m.enter := NIL; m.enterTail := NIL; m.wait := NIL; m.waitTail := NIL;
		INC(nofMonitors);
		RETURN m
	END NewMon;

	PROCEDURE GetMon (obj: S.ADDRESS): Monitor;
		VAR m: Monitor;
	BEGIN
		m := FindMon(obj);
		IF m = NIL THEN m := NewMon(obj) END;
		RETURN m
	END GetMon;

	PROCEDURE DropMon (m: Monitor);
		VAR i: INTEGER; p: Monitor;
	BEGIN
		i := MonIndex(S.VAL(S.ADDRESS, m.obj));
		IF monBuckets[i] = m THEN
			monBuckets[i] := m.next
		ELSE
			p := monBuckets[i];
			WHILE (p # NIL) & (p.next # m) DO p := p.next END;
			IF p # NIL THEN p.next := m.next END
		END;
		m.obj := NIL; m.owner := 0; m.count := 0;
		m.enter := NIL; m.enterTail := NIL; m.wait := NIL; m.waitTail := NIL;
		m.next := freeMonitors; freeMonitors := m;
		DEC(nofMonitors)
	END DropMon;

	PROCEDURE DropIfUnused (m: Monitor);
	BEGIN
		IF (m # NIL) & (m.owner = 0) & (m.enter = NIL) & (m.wait = NIL) THEN DropMon(m) END
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

	(* Передать монитор: сначала ждущий с истинным условием, затем ручной
	   Wait, затем первый входящий, иначе объект свободен. Условия
	   вычисляются под удерживаемым монитором — новый владелец получает
	   его атомарно и не может «перехватить» (семантика Active Oberon). *)
	PROCEDURE HandOff (m: Monitor; skip: S.ADDRESS);
	(** skip — задача, которая сама уходит в ожидание (Wait/Await): ей
		монитор не передаётся, иначе она получила бы его обратно и
		ожидание стало бы холостым циклом. *)
		VAR w, nx: WaitRef; e: TaskRef; t: S.ADDRESS; ok: BOOLEAN; code: INTEGER;
	BEGIN
		w := m.wait;
		WHILE w # NIL DO
			nx := w.next;
			IF (w.cond # NIL) & (w.task # skip) THEN
				IF EvalCond(w.cond, w.frame, ok, code) THEN
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
		(* Уходящий ждущий (skip # 0) не смог продолжить по своему условию:
		   он уступает сначала входящим, иначе два ждущих передают друг
		   другу пустой монитор и входящие голодают (пинг-понг). *)
		IF skip # 0 THEN
			e := m.enter;
			IF e # NIL THEN
				m.enter := e.next;
				IF m.enter = NIL THEN m.enterTail := NIL END;
				t := e.task; e.next := NIL;
				m.owner := t; m.count := 1;
				PushTo(t, m);
				UnparkTask(t); RETURN
			END
		END;
		(* ручной Wait: первый ждущий без условия *)
		w := m.wait;
		WHILE (w # NIL) & ((w.cond # NIL) OR (w.task = skip)) DO w := w.next END;
		IF w # NIL THEN
			RemoveWait(m, w);
			m.owner := w.task; m.count := 1;
			UnparkTask(w.task); RETURN
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
			IF r = NIL THEN cur.depth := depth; RETURN END;
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

	PROCEDURE DoLock (obj: S.ADDRESS);
	(** Взять монитор объекта. Повторный вход разрешён (счётчик).
		Занятый чужим монитор: задача ждёт; главный стек или отсутствие
		Tasks — трап. *)
		VAR me: S.ADDRESS; m: Monitor; q: TaskRef;
	BEGIN
		IF obj = 0 THEN MonErr("Lock: NIL object") END;
		ASSERT(obj # 0, 20);
		me := curTok;	(* Sync в крючке уже прочитал токен *)
		m := GetMon(obj);
		IF m.owner = me THEN
			INC(m.count); ChainPush(m)
		ELSIF m.owner = 0 THEN
			m.owner := me; m.count := 1; ChainPush(m)
		ELSIF me # mainToken THEN
			q := TakeTaskRef(me); AppendEnter(m, q);
			ParkNow;
			IF m.owner # me THEN MonErr("Lock: woken without the monitor"); HALT(100) END;
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

	PROCEDURE DoWait (obj: S.ADDRESS);
	(** Ручное ожидание на мониторе: отпустить его и спать до передачи
		монитора (Unlock) или до отмены. Вызывающий перепроверяет условие
		в цикле WHILE. *)
		VAR me: S.ADDRESS; m: Monitor; w: WaitRef; saved: INTEGER;
	BEGIN
		me := curTok;
		IF me = mainToken THEN MonErr("Wait: wait outside a task"); HALT(24) END;
		m := FindMon(obj);
		IF (m = NIL) OR (m.owner # me) THEN
			MonErr("Wait: monitor is not held by the caller"); HALT(22)
		END;
		w := TakeWaitRef(me, NIL, 0);
		AppendWait(m, w);
		saved := m.count;
		m.count := 0;
		m.owner := 0;
		HandOff(m, me);
		ParkNow;
		IF m.owner # me THEN MonErr("Wait: woken without the monitor"); HALT(100) END;
		FreeWaitRef(w);	(* HandOff снял её с очереди ожидания *)
		m.count := saved
	END DoWait;

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
		ParkNow;
		IF w.trap # 0 THEN
			(* Трап случился в чужом условии; переадресуем его этой задаче.
			   HALT требует константу, поэтому исходный код — в журнал. *)
			MonStr("Kernel: Await: trap "); MonInt(w.trap);
			MonStr(" in the condition"); MonLn;
			HALT(60)
		END;
		IF m.owner # me THEN MonErr("Await: woken without the monitor"); HALT(100) END;
		FreeWaitRef(w);	(* HandOff снял её с очереди ожидания *)
		m.count := saved
	END DoAwait;

	(* -------------------- крючок для ядра -------------------------------- *)

	PROCEDURE (h: Hook) lock (obj: S.ADDRESS);
	BEGIN
		Sync; DoLock(obj)
	END lock;

	PROCEDURE (h: Hook) unlock (obj: S.ADDRESS);
	BEGIN
		Sync; DoUnlock(obj)
	END unlock;

	PROCEDURE (h: Hook) wait (obj: S.ADDRESS);
	BEGIN
		Sync; DoWait(obj)
	END wait;

	PROCEDURE (h: Hook) await (cond: CondProc; frame, obj: S.ADDRESS);
	BEGIN
		Sync; DoAwait(cond, frame, obj)
	END await;

	PROCEDURE (h: Hook) beginTry (): INTEGER;
	BEGIN
		Sync;
		IF cur # NIL THEN RETURN cur.depth ELSE RETURN 0 END
	END beginTry;

	PROCEDURE (h: Hook) endTry (mark: INTEGER);
	BEGIN
		Sync; ReleaseTo(mark)
	END endTry;

	PROCEDURE (h: Hook) purge;
	(** Вызывается ядром после трапа: задача уходит из очередей мониторов. *)
	BEGIN
		Sync; PurgeCurrent
	END purge;

	(* -------------------- рабочие запросы -------------------------------- *)

	PROCEDURE IsLocked* (obj: ANYPTR): BOOLEAN;
	(** Пост: TRUE — монитор obj кем-то взят; obj = NIL — FALSE. *)
		VAR m: Monitor;
	BEGIN
		IF obj = NIL THEN RETURN FALSE END;
		m := FindMon(S.VAL(S.ADDRESS, obj));
		RETURN (m # NIL) & (m.owner # 0)
	END IsLocked;

	PROCEDURE LockDepth* (): INTEGER;
	(** Пост: число взятий мониторов текущей задачей (главным стеком);
		планировщик не уступает в точке опроса, пока оно > 0. *)
	BEGIN
		Sync;
		IF cur # NIL THEN RETURN cur.depth ELSE RETURN 0 END
	END LockDepth;

	(* -------------------- запросы для тестов и отладки -------------------- *)

	PROCEDURE WaitersCount* (): INTEGER;
	(** Пост: число записей во всех очередях входа и ожидания. *)
		VAR i, n: INTEGER; m: Monitor; w: WaitRef; e: TaskRef;
	BEGIN
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
		MonStr("Kernel: monitor dump"); MonLn;
		n := 0; i := 0;
		WHILE i < monHash DO
			m := monBuckets[i];
			WHILE m # NIL DO
				INC(n);
				MonStr("  monitor "); MonHex(S.VAL(LONGINT, S.VAL(S.ADDRESS, m.obj)));
				IF m.owner = 0 THEN MonStr(": free")
				ELSE
					TaskName(m.owner, nm);
					MonStr(": held by "); MonStr(nm); MonStr(" count="); MonInt(m.count)
				END;
				MonLn;
				w := m.wait;
				WHILE w # NIL DO
					TaskName(w.task, nm); MonStr("    waits "); MonStr(nm);
					IF w.cond = NIL THEN MonStr(" (manual)") ELSE MonStr(" (condition)") END;
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
	Kernel.SetMonitorHook(hook)
END Monitors.
