MODULE AsyncObject;
(** Монитор в базовом классе: вход и выход не забываются.

	Object — объект-монитор. Запрос к объекту — запись-наследник Op,
	заведённая на стеке вызывающего. Do входит в монитор, зовёт
	Exclusive и выходит; трап внутри Exclusive монитор отпускает.
	Exclusive пишет наследник, вид запроса разбирается WITH.

	Ожидание условия пишется WHILE ~условие DO o.Wait END. Wait
	отпускает монитор, спит и по пробуждении входит снова. Ждущих будит
	выход любого чужого вызова Do того же объекта; они сами
	перепроверяют условие (сигнал «разбудить всех», не «условие истинно»).

	Только одна нить ОС, задачи переключаются в точках ожидания.
	Поэтому монитор — это поле владельца и две очереди, а не замок ОС:
	взаимное исключение возникает ровно там, где владелец уступает
	(Wait, Park, Sleep). Kernel.Lock/Unlock и Monitors не используются.

	Трап: каждый вход и каждое ожидание кладут на стек задачи уборщик
	Kernel.PushTrapCleaner. Трап снимает монитор и убирает запись из
	очереди. Записи ожидания берутся из свободного списка, поэтому после
	прогрева вход, выход и ожидание не выделяют памяти. Трап, пойманный
	вложенным Kernel.Try внутри Exclusive, тоже снимает монитор (в
	[exclusive] он сохраняется): форма рассчитана на код, который не
	ловит трапы внутри Exclusive.

	Повторный вход разрешён: o.Do из своей же Exclusive увеличивает
	счётчик; Wait отпускает все взятия и восстанавливает их после
	пробуждения.

	Трапы:
		20	Do, Wait: объект NIL
		21	Wait вне Exclusive
		22	ждать монитор с главного стека (нет задачи)
		100	нарушен инвариант очереди *)

	IMPORT S := SYSTEM, Kernel, Tasks;

	CONST
		mainToken = 1;	(* токен главного стека, как Kernel.mainToken *)

	TYPE
		Op* = ABSTRACT RECORD END;

		(* Запись ожидания. Берётся из свободного списка: после прогрева
		   вход, выход и ожидание не выделяют памяти. У записи есть
		   динамический тип (уборщик трапа), поэтому она в куче, а не в
		   кадре: вызов метода через указатель на запись стека не работает.
		   next (TrapCleaner) — связь стека уборщиков, qnext — связь
		   очереди монитора или свободного списка. *)
		NodeRec = RECORD (Kernel.TrapCleaner)
			obj: ANYPTR;		(* монитор; корень сборщика, пока запись в очереди *)
			tok: S.ADDRESS;		(* токен задачи этой записи *)
			qnext: Node;
			flag: S.ADDRESS;	(* INTEGER кадра Do: уборщик уже сработал *)
			woken, queued, waiting, held: BOOLEAN
		END;

		Node = POINTER TO NodeRec;

		Object* = POINTER TO ABSTRACT RECORD
			owner: S.ADDRESS;	(* токен владельца; 0 — свободен *)
			depth: INTEGER;		(* взятия владельца: повторный вход *)
			enter, enterTail: Node;	(* очередь входа в монитор *)
			wait, waitTail: Node	(* очередь ожидания условия *)
		END;

	VAR
		freeNodes: Node;	(* свободные записи ожидания *)

	(* -------------------- записи ожидания -------------------------------- *)

	PROCEDURE TakeNode (): Node;
		VAR p: Node;
	BEGIN
		IF freeNodes # NIL THEN
			p := freeNodes; freeNodes := p.qnext
		ELSE
			NEW(p)
		END;
		p.obj := NIL; p.tok := 0; p.qnext := NIL; p.flag := 0;
		p.woken := FALSE; p.queued := FALSE; p.waiting := FALSE; p.held := FALSE;
		RETURN p
	END TakeNode;

	PROCEDURE GiveNode (p: Node);
	(* Пред: запись не в очереди и не на стеке уборщиков. *)
	BEGIN
		p.obj := NIL; p.tok := 0; p.flag := 0;
		p.woken := FALSE; p.queued := FALSE; p.waiting := FALSE; p.held := FALSE;
		p.qnext := freeNodes; freeNodes := p
	END GiveNode;

	(* -------------------- диагностика ------------------------------------ *)

	PROCEDURE Str (IN s: ARRAY OF CHAR);
	(* Прямой вывод платформы: трап не должен выделять память. *)
	BEGIN
		IF (Kernel.platform # NIL) & (Kernel.platform.String # NIL) THEN Kernel.platform.String(s) END
	END Str;

	PROCEDURE Ln;
	BEGIN
		IF (Kernel.platform # NIL) & (Kernel.platform.Ln # NIL) THEN Kernel.platform.Ln END
	END Ln;

	PROCEDURE Err (IN msg: ARRAY OF CHAR);
	BEGIN
		Str("AsyncObject: "); Str(msg); Ln
	END Err;

	(* -------------------- ожидание задачи -------------------------------- *)

	PROCEDURE Unpark (tok: S.ADDRESS);
	(* Разбудить задачу по токену, не трогая её пользовательский флаг
	   пробуждения: иначе следующий Tasks.Park вернулся бы сразу. *)
		VAR s: Kernel.Scheduler;
	BEGIN
		s := Kernel.sched;
		IF (s # NIL) & (tok # 0) & (tok # mainToken) THEN s.unpark(tok) END
	END Unpark;

	PROCEDURE Park;
	(* Ожидание монитора: как у Monitors — ParkMonitor крючка ядра. *)
		VAR s: Kernel.Scheduler;
	BEGIN
		s := Kernel.sched;
		IF s = NIL THEN Err("wait outside a task"); HALT(22) END;
		s.park()
	END Park;

	(* -------------------- очереди ---------------------------------------- *)

	PROCEDURE AppendEnter (o: Object; p: Node);
	BEGIN
		p.qnext := NIL;
		IF o.enterTail = NIL THEN o.enter := p ELSE o.enterTail.qnext := p END;
		o.enterTail := p
	END AppendEnter;

	PROCEDURE AppendWait (o: Object; p: Node);
	BEGIN
		p.qnext := NIL;
		IF o.waitTail = NIL THEN o.wait := p ELSE o.waitTail.qnext := p END;
		o.waitTail := p
	END AppendWait;

	PROCEDURE UnlinkEnter (o: Object; p: Node);
		VAR q: Node;
	BEGIN
		IF o.enter = p THEN
			o.enter := p.qnext;
			IF o.enter = NIL THEN o.enterTail := NIL END
		ELSE
			q := o.enter;
			WHILE (q # NIL) & (q.qnext # p) DO q := q.qnext END;
			ASSERT(q # NIL, 100);
			q.qnext := p.qnext;
			IF o.enterTail = p THEN o.enterTail := q END
		END;
		p.qnext := NIL; p.queued := FALSE
	END UnlinkEnter;

	PROCEDURE UnlinkWait (o: Object; p: Node);
		VAR q: Node;
	BEGIN
		IF o.wait = p THEN
			o.wait := p.qnext;
			IF o.wait = NIL THEN o.waitTail := NIL END
		ELSE
			q := o.wait;
			WHILE (q # NIL) & (q.qnext # p) DO q := q.qnext END;
			ASSERT(q # NIL, 100);
			q.qnext := p.qnext;
			IF o.waitTail = p THEN o.waitTail := q END
		END;
		p.qnext := NIL; p.queued := FALSE
	END UnlinkWait;

	PROCEDURE WakeWaiters (o: Object);
	(* Сигнал «разбудить всех»: монитор свободен, ждущие перепроверят
	   условие сами. *)
		VAR p, q: Node;
	BEGIN
		p := o.wait; o.wait := NIL; o.waitTail := NIL;
		WHILE p # NIL DO
			q := p.qnext; p.qnext := NIL;
			p.queued := FALSE; p.woken := TRUE;
			Unpark(p.tok);
			p := q
		END
	END WakeWaiters;

	PROCEDURE Grant (o: Object): BOOLEAN;
	(* Передать свободный монитор первому входящему; он уже стоит в
	   очереди и спит. *)
		VAR p: Node;
	BEGIN
		IF o.enter = NIL THEN RETURN FALSE END;
		p := o.enter; o.enter := p.qnext;
		IF o.enter = NIL THEN o.enterTail := NIL END;
		p.qnext := NIL; p.queued := FALSE; p.woken := TRUE;
		o.owner := p.tok; o.depth := 1; p.held := TRUE;
		Unpark(p.tok);
		RETURN TRUE
	END Grant;

	PROCEDURE Release (o: Object);
	(* Отпустить одно взятие; на нуле разбудить ждущих и передать
	   монитор входящему. *)
	BEGIN
		IF o.owner = 0 THEN RETURN END;
		DEC(o.depth);
		IF o.depth <= 0 THEN
			o.depth := 0; o.owner := 0;
			WakeWaiters(o);
			IF ~Grant(o) THEN o.owner := 0 END
		END
	END Release;

	(* -------------------- уборщик трапа ---------------------------------- *)

	PROCEDURE (p: Node) Cleanup;
	(* Трап на этом стеке: убрать запись из очереди, снять взятие, если
	   оно ещё наше, и вернуть запись в свободный список. *)
		VAR o: Object; flag: S.ADDRESS;
	BEGIN
		flag := p.flag;
		IF p.obj # NIL THEN
			o := S.VAL(Object, p.obj);
			IF p.queued THEN
				IF p.waiting THEN UnlinkWait(o, p) ELSE UnlinkEnter(o, p) END
			END;
			IF p.held & (o.owner = p.tok) THEN
				p.held := FALSE;
				Release(o)
			END
		END;
		(* Кадр Do ещё жив: сказать ему, что уборщик уже сработал. *)
		IF flag # 0 THEN S.PUT(flag, 1) END;
		GiveNode(p)
	END Cleanup;

	(* -------------------- вход ------------------------------------------- *)

	PROCEDURE Acquire (o: Object; p: Node);
	(* Пред: p вне очередей, p.tok — токен текущей задачи. Пост: монитор
	   у этой записи (held), повторный вход увеличил глубину. *)
	BEGIN
		IF o.owner = p.tok THEN
			INC(o.depth); p.held := TRUE; RETURN
		END;
		WHILE o.owner # 0 DO
			IF ~Tasks.InTask() THEN
				Err("Do: monitor is held, cannot wait on the main stack"); HALT(22)
			END;
			p.woken := FALSE; p.waiting := FALSE; p.queued := TRUE;
			AppendEnter(o, p);
			REPEAT Park UNTIL p.woken;
			(* Grant снял запись с очереди и отдал монитор. Разбудить
			   могли и без монитора (посторонний Unpark) — тогда ждём снова. *)
			IF o.owner = p.tok THEN p.held := TRUE; RETURN END
		END;
		o.owner := p.tok; o.depth := 1; p.held := TRUE
	END Acquire;

	PROCEDURE (o: Object) Exclusive- (VAR op: Op), NEW, ABSTRACT;
	(** Тело под монитором; пишет наследник Object. *)

	PROCEDURE (o: Object) Do* (VAR op: Op), NEW;
	(** Пред: o # NIL (трап 20); ждать с главного стека нельзя (22).
		Пост: op обработан Exclusive; при трапе внутри Exclusive монитор
		отпущен. *)
		VAR p: Node; released: INTEGER;
	BEGIN
		IF o = NIL THEN Err("Do: NIL object"); HALT(20) END;
		released := 0;
		p := TakeNode();
		p.obj := o; p.tok := Kernel.monTok; p.flag := S.ADR(released);
		Kernel.PushTrapCleaner(p);
		Acquire(o, p);
		o.Exclusive(op);
		(* released # 0: трап уже снял монитор и вернул запись в список. *)
		IF released = 0 THEN
			IF p.held THEN p.held := FALSE; Release(o) END;
			Kernel.PopTrapCleaner(p);
			GiveNode(p)
		END
	END Do;

	PROCEDURE (o: Object) Wait*, NEW;
	(** Пред: o # NIL (20); монитор у вызывающей задачи (21); вызов из
		задачи (22). Пост: монитор отпущен на время сна и снова взят;
		глубина повторного входа восстановлена. *)
		VAR p: Node; tok: S.ADDRESS; saved: INTEGER;
	BEGIN
		IF o = NIL THEN Err("Wait: NIL object"); HALT(20) END;
		tok := Kernel.monTok;
		IF o.owner # tok THEN Err("Wait outside Exclusive"); HALT(21) END;
		IF ~Tasks.InTask() THEN Err("Wait outside a task"); HALT(22) END;
		saved := o.depth;
		p := TakeNode();
		p.obj := o; p.tok := tok;
		Kernel.PushTrapCleaner(p);
		p.waiting := TRUE; p.queued := TRUE;
		AppendWait(o, p);
		(* Отпустить монитор после постановки в очередь: иначе сигнал
		   между отпусканием и Park потерялся бы. *)
		o.owner := 0; o.depth := 0;
		IF ~Grant(o) THEN o.owner := 0 END;
		REPEAT Park UNTIL p.woken;
		p.woken := FALSE; p.waiting := FALSE;
		(* Войти снова; запись уже снята с очереди ожидания. *)
		Acquire(o, p);
		IF o.owner = tok THEN o.depth := saved END;
		p.held := FALSE;
		Kernel.PopTrapCleaner(p);
		GiveNode(p)
	END Wait;

	(* -------------------- запросы для тестов и отладки -------------------- *)

	PROCEDURE Held* (o: Object): BOOLEAN;
	(** Пост: TRUE — монитор кем-то взят. *)
	BEGIN
		RETURN (o # NIL) & (o.owner # 0)
	END Held;

	PROCEDURE Queued* (o: Object): INTEGER;
	(** Пост: записей в очередях входа и ожидания. *)
		VAR p: Node; n: INTEGER;
	BEGIN
		n := 0;
		IF o # NIL THEN
			p := o.enter; WHILE p # NIL DO INC(n); p := p.qnext END;
			p := o.wait; WHILE p # NIL DO INC(n); p := p.qnext END
		END;
		RETURN n
	END Queued;

	PROCEDURE Depth* (o: Object): INTEGER;
	(** Пост: глубина повторного входа владельца; 0 — монитор свободен. *)
	BEGIN
		IF o = NIL THEN RETURN 0 END;
		RETURN o.depth
	END Depth;

END AsyncObject.
