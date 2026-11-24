MODULE ActorCore;
(** Локальные акторы: адрес, экземпляр, ящик, жизненный цикл и надзор.

	Актор — экземпляр Actor с обработчиком Receive и ящиком (Context).
	Своей задачи у актора нет: ящик с работой отдаётся исполнителю через
	Dispatcher.Schedule, исполнитель зовёт Process. Каждое сообщение
	обрабатывается под Kernel.Try; трап превращается в сообщение об
	отказе родителю, родитель применяет директиву своей стратегии.

	Одна нить ОС, вытеснения нет: операции над ящиком между двумя
	точками переключения неделимы, поэтому мониторов в модуле нет.
	Пока обработчик ждёт внутри Receive, ящик помечен running: второе
	сообщение этому актору не выдаётся.

	Инвариант ящика (Context):
	- scheduled = ящик стоит у исполнителя (в очереди или в Process);
	  ~scheduled => ~HasWork или работа поставлена через Wake;
	- running => scheduled;
	- state = stopped => очереди пусты, actor = NIL, детей нет, имени
	  в каталоге нет;
	- nChildren = длина списка first/sibNext. *)

	IMPORT S := SYSTEM, Kernel, Services, Console, Strings, Stores, Tasks, AsyncSelect, AsyncChan;

	CONST
		(** директивы супервизора **)
		Resume* = 0; Restart* = 1; Stop* = 2; Escalate* = 3;
		(** результат Ask **)
		ok* = 0; timedOut* = 1; dead* = 2;
		nameLen* = 128;

		(* состояние ящика *)
		alive = 0; restarting = 1; stopping = 2; stopped = 3;
		(* вид вызова под Kernel.Try *)
		callRecv = 0; callNew = 1; callDecide = 2;
		cancelTrap = 127;
		ringMin = 4;
		buckets = 4096;

	TYPE
		Msg* = AsyncSelect.Msg;
		Name* = ARRAY nameLen OF CHAR;

		(** Адрес актора: узел и имя. Локальный адрес — Context; удалённый
			адрес даёт расширение модуля транспорта. *)
		Ref* = POINTER TO ABSTRACT RECORD
			node-, name-: Name
		END;

		(** Экземпляр актора. Расширение Stores.Store: состояние можно
			выносить и вносить механизмом хранилищ. *)
		Actor* = POINTER TO ABSTRACT RECORD (Stores.Store) END;

		(** Сменное поведение (Context.Become). *)
		Behavior* = POINTER TO ABSTRACT RECORD END;

		(** Счёт отказов ребёнка; хранится у ребёнка, ведёт стратегия. *)
		Stats* = RECORD
			failures*: INTEGER;
			since*: LONGINT
		END;

		(** Стратегия супервизора. *)
		Strategy* = POINTER TO ABSTRACT RECORD END;

		Factory* = PROCEDURE (): Actor;

		(** Описание актора. capacity = 0 — ящик без предела. *)
		Props* = POINTER TO RECORD
			new*: Factory;
			capacity*: INTEGER;
			dedicated*: BOOLEAN;
			strategy*: Strategy
		END;

		(** Исполнитель ящиков (ActorRun). *)
		Dispatcher* = POINTER TO ABSTRACT RECORD END;

		(** Каталог адресов: имя узла и актора -> адрес. *)
		Directory* = POINTER TO ABSTRACT RECORD END;

		(** сообщения жизненного цикла, их получает Receive **)
		Started* = POINTER TO RECORD (Msg) END;
		Stopping* = POINTER TO RECORD (Msg) END;
		Stopped* = POINTER TO RECORD (Msg) END;
		Restarting* = POINTER TO RECORD (Msg) END;
		ReceiveTimeout* = POINTER TO RECORD (Msg) END;
		Terminated* = POINTER TO RECORD (Msg)
			who-: Ref
		END;

		(* Конверт: сообщение и отправитель (NIL — без отправителя). *)
		Env = RECORD m: Msg; s: Ref END;

		(* Инвариант очереди: count = 0 или ring # NIL; конверты лежат в
		   ring[(head + i) MOD LEN(ring)], 0 <= i < count; прочие ячейки пусты. *)
		Queue = RECORD
			ring: POINTER TO ARRAY OF Env;
			head, count: INTEGER
		END;

		Watcher = POINTER TO WatcherDesc;
		Timer = POINTER TO TimerDesc;

		(** Ящик актора и контекст его обработчика; он же локальный адрес. *)
		Context* = POINTER TO LIMITED RECORD (Ref)
			host*: ANYPTR;	(** место исполнителя для своих данных *)
			dedicated-: BOOLEAN;
			props: Props;
			actor: Actor;
			beh: Behavior;
			parent, first, sibPrev, sibNext: Context;
			nChildren: INTEGER;
			watchers: Watcher;
			user, sys: Queue;
			state: INTEGER;
			suspended, scheduled, running: BOOLEAN;
			stats: Stats;
			sender: Ref;	(* отправитель сообщения в обработке *)
			rtMs, rtLast: LONGINT;	(* тайм-аут простоя и время последнего сообщения *)
			rtTimer: Timer;
			rtArmed: BOOLEAN;
			hnext: Context;	(* цепочка корзины каталога *)
			(* вызов под Kernel.Try *)
			callKind, callReason, callResult: INTEGER;
			callMsg: Msg;
			callChild: Context;
			done: BOOLEAN
		END;

		WatcherDesc = RECORD next: Watcher; w: Context END;
		TimerDesc = RECORD (Services.Action) c: Context END;

		(* системные сообщения *)
		StartMsg = POINTER TO RECORD (Msg) END;
		StopMsg = POINTER TO RECORD (Msg) END;
		ResumeMsg = POINTER TO RECORD (Msg) END;
		RestartMsg = POINTER TO RECORD (Msg) END;
		WatchMsg = POINTER TO RECORD (Msg) watcher: Context END;
		FailureMsg = POINTER TO RECORD (Msg) child: Context; reason: INTEGER END;
		ChildGone = POINTER TO RECORD (Msg) child: Context END;
		DeadMsg = POINTER TO RECORD (Msg) END;

		(* Адрес ответа Ask: ответ кладётся в ящик канала. *)
		Future = POINTER TO RECORD (Ref) box: AsyncChan.Mailbox END;

		StdDirectory = POINTER TO RECORD (Directory) END;

	VAR
		dispatcher-: Dispatcher;
		dir-, stdDir-: Directory;
		node-: Name;	(** имя своего узла *)
		deadLetters-: LONGINT;	(** число мёртвых писем с начала работы *)

		seq: INTEGER;	(* номер следующего безымянного актора *)
		table: ARRAY buckets OF Context;	(* каталог локальных имён *)
		started: Started; stoppingM: Stopping; stoppedM: Stopped;
		restartingM: Restarting; timeoutM: ReceiveTimeout;
		startS: StartMsg; stopS: StopMsg; resumeS: ResumeMsg; restartS: RestartMsg;
		deadM: DeadMsg;

	(* --- абстрактные операции --------------------------------------------- *)

	PROCEDURE (r: Ref) Post* (m: Msg; sender: Ref), NEW, ABSTRACT;
	(** Пред: m # NIL. Пост: m доставлено в ящик адресата или учтено как
		мёртвое письмо; отправитель не ждёт. *)

	PROCEDURE (a: Actor) Receive* (c: Context; m: Msg), NEW, ABSTRACT;
	(** Обработчик. Пред: зовёт только исполнитель, по одному вызову на
		актор за раз. Пост: сообщение обработано; трап — отказ актора. *)

	PROCEDURE (b: Behavior) Receive* (a: Actor; c: Context; m: Msg), NEW, ABSTRACT;
	(** Обработчик вместо a.Receive после c.Become(b). *)

	PROCEDURE (s: Strategy) Decide* (child: Ref; reason: INTEGER; VAR st: Stats): INTEGER, NEW, ABSTRACT;
	(** Пред: child отказал с трапом reason; st — счёт отказов child.
		Пост: директива Resume, Restart, Stop или Escalate; другое
		значение исполняется как Stop. *)

	PROCEDURE (d: Dispatcher) Schedule* (c: Context), NEW, ABSTRACT;
	(** Пред: у c есть работа, c не стоит у исполнителя. Пост: исполнитель
		позовёт Process(c, ...) и будет звать, пока Process возвращает TRUE. *)

	PROCEDURE (d: Directory) Lookup* (IN node, name: ARRAY OF CHAR): Ref, NEW, ABSTRACT;
	(** Пост: адрес актора name на узле node; NIL — неизвестен. *)

	(* --- очередь ---------------------------------------------------------- *)

	PROCEDURE Push (VAR q: Queue; m: Msg; s: Ref);
	(* Пост: (m, s) — последний конверт очереди. *)
		VAR new: POINTER TO ARRAY OF Env; i, n: INTEGER;
	BEGIN
		IF q.ring = NIL THEN NEW(q.ring, ringMin); q.head := 0
		ELSIF q.count = LEN(q.ring^) THEN
			n := LEN(q.ring^); NEW(new, 2 * n);
			FOR i := 0 TO n - 1 DO new[i] := q.ring[(q.head + i) MOD n] END;
			q.ring := new; q.head := 0
		END;
		i := (q.head + q.count) MOD LEN(q.ring^);
		q.ring[i].m := m; q.ring[i].s := s; INC(q.count)
	END Push;

	PROCEDURE Pop (VAR q: Queue; OUT m: Msg; OUT s: Ref);
	(* Пред: q.count > 0. Пост: (m, s) — бывший первый конверт. *)
	BEGIN
		ASSERT(q.count > 0, 100);
		m := q.ring[q.head].m; s := q.ring[q.head].s;
		q.ring[q.head].m := NIL; q.ring[q.head].s := NIL;
		q.head := (q.head + 1) MOD LEN(q.ring^); DEC(q.count)
	END Pop;

	(* --- мёртвые письма и пробуждение ------------------------------------- *)

	PROCEDURE Dead (m: Msg; s: Ref);
	(* Пост: письмо учтено; ждущий Ask узнаёт об этом сразу. *)
	BEGIN
		INC(deadLetters);
		IF (s # NIL) & (s IS Future) & (m # deadM) THEN s.Post(deadM, NIL) END
	END Dead;

	PROCEDURE UserReady (c: Context): BOOLEAN;
	BEGIN
		RETURN (c.state = alive) & ~c.suspended & (c.user.count > 0)
	END UserReady;

	PROCEDURE HasWork (c: Context): BOOLEAN;
	BEGIN
		RETURN (c.sys.count > 0) OR UserReady(c)
	END HasWork;

	PROCEDURE Wake (c: Context);
	(* Пост: есть работа => c стоит у исполнителя. *)
	BEGIN
		IF ~c.scheduled & HasWork(c) THEN
			c.scheduled := TRUE; dispatcher.Schedule(c)
		END
	END Wake;

	PROCEDURE PostSys (c: Context; m: Msg);
	(* Пост: m в системной очереди c; остановленный c отвечает на
	   наблюдение сообщением Terminated, прочее отбрасывает. *)
		VAR t: Terminated;
	BEGIN
		IF c.state = stopped THEN
			IF m IS WatchMsg THEN
				NEW(t); t.who := c; PostSys(m(WatchMsg).watcher, t)
			END
		ELSE
			Push(c.sys, m, NIL); Wake(c)
		END
	END PostSys;

	(* --- вызов кода пользователя под Kernel.Try --------------------------- *)

	PROCEDURE Call (a, b, cc: S.ADDRESS);
		VAR c: Context;
	BEGIN
		c := S.VAL(Context, a);
		CASE c.callKind OF
		| callRecv:
			IF c.beh # NIL THEN c.beh.Receive(c.actor, c, c.callMsg) ELSE c.actor.Receive(c, c.callMsg) END
		| callNew:
			c.actor := c.props.new()
		| callDecide:
			c.callResult := c.props.strategy.Decide(c.callChild, c.callReason, c.callChild.stats)
		END;
		c.done := TRUE
	END Call;

	PROCEDURE Invoke (c: Context; kind: INTEGER; m: Msg): INTEGER;
	(* Пост: 0 — вызов завершён; иначе номер трапа. Трап 127 (задача
	   исполнителя отменена) не возвращается: ящик снова отдаётся
	   исполнителю, задача прерывается тем же трапом. *)
		VAR err: INTEGER;
	BEGIN
		c.callKind := kind; c.callMsg := m; c.done := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(Call)), S.VAL(S.ADDRESS, c), 0, 0);
		c.callMsg := NIL;
		IF c.done THEN RETURN 0 END;
		err := Kernel.err;
		IF err = cancelTrap THEN
			c.running := FALSE; c.scheduled := FALSE; c.sender := NIL;
			Wake(c);
			HALT(cancelTrap)
		END;
		IF err = 0 THEN err := 1 END;
		RETURN err
	END Invoke;

	PROCEDURE Lifecycle (c: Context; m: Msg);
	(* Пост: обработчик получил m; трап в нём учтён в Console и отказом
	   не считается. *)
		VAR r: INTEGER;
	BEGIN
		IF c.actor # NIL THEN
			r := Invoke(c, callRecv, m);
			IF r # 0 THEN Console.WriteStr("Actor: trap in lifecycle of "); Console.WriteStr(c.name); Console.WriteLn END
		END
	END Lifecycle;

	(* --- надзор ----------------------------------------------------------- *)

	PROCEDURE Fail (c: Context; reason: INTEGER);
	(* Пост: c приостановлен; родитель получил отказ. Корень (нет
	   родителя) останавливает c. *)
		VAR f: FailureMsg; s: ARRAY 16 OF CHAR;
	BEGIN
		c.suspended := TRUE;
		IF c.parent # NIL THEN
			NEW(f); f.child := c; f.reason := reason; PostSys(c.parent, f)
		ELSE
			Strings.IntToString(reason, s);
			Console.WriteStr("Actor: root stops "); Console.WriteStr(c.name);
			Console.WriteStr(" after trap "); Console.WriteStr(s); Console.WriteLn;
			PostSys(c, stopS)
		END
	END Fail;

	PROCEDURE Supervise (p: Context; f: FailureMsg);
	(* Пост: к ребёнку f.child применена директива стратегии p; без
	   стратегии — Stop; трап стратегии — отказ p. *)
		VAR ch: Context; d, r: INTEGER;
	BEGIN
		ch := f.child;
		IF (ch.parent # p) OR (ch.state >= stopping) OR (p.state # alive) THEN RETURN END;
		IF p.props.strategy = NIL THEN d := Stop
		ELSE
			p.callChild := ch; p.callReason := f.reason;
			r := Invoke(p, callDecide, NIL);
			p.callChild := NIL;
			IF r # 0 THEN
				IF ~p.suspended THEN Fail(p, r) END;
				RETURN
			END;
			d := p.callResult
		END;
		CASE d OF
		| Resume: PostSys(ch, resumeS)
		| Restart: PostSys(ch, restartS)
		| Escalate:
			IF ~p.suspended THEN Fail(p, f.reason) END	(* ребёнок ждёт судьбы родителя *)
		ELSE PostSys(ch, stopS)
		END
	END Supervise;

	PROCEDURE StopChildren (c: Context);
		VAR ch: Context;
	BEGIN
		ch := c.first;
		WHILE ch # NIL DO PostSys(ch, stopS); ch := ch.sibNext END
	END StopChildren;

	PROCEDURE Unlink (p, ch: Context);
	BEGIN
		IF ch.sibPrev = NIL THEN p.first := ch.sibNext ELSE ch.sibPrev.sibNext := ch.sibNext END;
		IF ch.sibNext # NIL THEN ch.sibNext.sibPrev := ch.sibPrev END;
		ch.sibPrev := NIL; ch.sibNext := NIL; DEC(p.nChildren)
	END Unlink;

	(* --- каталог имён ----------------------------------------------------- *)

	PROCEDURE Hash (IN s: ARRAY OF CHAR): INTEGER;
		VAR h, i: INTEGER;
	BEGIN
		h := 0; i := 0;
		WHILE s[i] # 0X DO h := (h * 31 + ORD(s[i])) MOD buckets; INC(i) END;
		RETURN h
	END Hash;

	PROCEDURE Find (IN name: ARRAY OF CHAR): Context;
		VAR c: Context;
	BEGIN
		c := table[Hash(name)];
		WHILE (c # NIL) & (c.name # name) DO c := c.hnext END;
		RETURN c
	END Find;

	PROCEDURE Unregister (c: Context);
		VAR h: INTEGER; p: Context;
	BEGIN
		h := Hash(c.name);
		IF table[h] = c THEN table[h] := c.hnext
		ELSE
			p := table[h];
			WHILE (p # NIL) & (p.hnext # c) DO p := p.hnext END;
			IF p # NIL THEN p.hnext := c.hnext END
		END;
		c.hnext := NIL
	END Unregister;

	PROCEDURE (d: StdDirectory) Lookup (IN nd, name: ARRAY OF CHAR): Ref;
	BEGIN
		IF nd = node THEN RETURN Find(name) ELSE RETURN NIL END
	END Lookup;

	(* --- переходы жизненного цикла ---------------------------------------- *)

	PROCEDURE CancelTimer (c: Context);
	BEGIN
		IF c.rtTimer # NIL THEN Services.RemoveAction(c.rtTimer) END;
		c.rtArmed := FALSE
	END CancelTimer;

	PROCEDURE Finish (c: Context);
	(* Пред: state = stopping, детей нет. Пост: state = stopped;
	   обработчик получил Stopped; письма ящика — мёртвые; наблюдатели
	   получили Terminated, родитель — уход ребёнка; имени в каталоге нет. *)
		VAR m: Msg; s: Ref; w: Watcher; t: Terminated; g: ChildGone;
	BEGIN
		ASSERT((c.state = stopping) & (c.nChildren = 0), 100);
		Lifecycle(c, stoppedM);
		c.state := stopped; c.actor := NIL; c.beh := NIL;
		WHILE c.user.count > 0 DO Pop(c.user, m, s); Dead(m, s) END;
		WHILE c.sys.count > 0 DO
			Pop(c.sys, m, s);
			IF m IS WatchMsg THEN NEW(t); t.who := c; PostSys(m(WatchMsg).watcher, t) END
		END;
		w := c.watchers; c.watchers := NIL;
		WHILE w # NIL DO NEW(t); t.who := c; PostSys(w.w, t); w := w.next END;
		IF c.parent # NIL THEN NEW(g); g.child := c; PostSys(c.parent, g) END;
		Unregister(c)
	END Finish;

	PROCEDURE BeginStop (c: Context);
	(* Пост: c в stopping, обработчик получил Stopping, детям послан
	   Stop; без детей — c остановлен. *)
	BEGIN
		IF c.state >= stopping THEN RETURN END;
		c.state := stopping; CancelTimer(c);
		Lifecycle(c, stoppingM);
		StopChildren(c);
		IF c.nChildren = 0 THEN Finish(c) END
	END BeginStop;

	PROCEDURE FinishRestart (c: Context);
	(* Пред: state = restarting, детей нет. Пост: новый экземпляр от
	   фабрики получил Started; письма ящика сохранены. *)
		VAR r: INTEGER;
	BEGIN
		c.actor := NIL; c.beh := NIL;
		r := Invoke(c, callNew, NIL);
		c.state := alive; c.suspended := FALSE;
		IF (r = 0) & (c.actor = NIL) THEN r := 60 END;
		IF r = 0 THEN r := Invoke(c, callRecv, started) END;
		IF r # 0 THEN Fail(c, r) END
	END FinishRestart;

	PROCEDURE BeginRestart (c: Context);
	BEGIN
		IF c.state # alive THEN RETURN END;
		c.state := restarting; CancelTimer(c); c.rtMs := 0;
		Lifecycle(c, restartingM);
		StopChildren(c);
		IF c.nChildren = 0 THEN FinishRestart(c) END
	END BeginRestart;

	PROCEDURE DoResume (c: Context);
	(* Пост: c обрабатывает письма; приостановленные дети (ждавшие
	   судьбы c после Escalate) тоже возобновлены. *)
		VAR ch: Context;
	BEGIN
		IF c.state # alive THEN RETURN END;
		c.suspended := FALSE;
		ch := c.first;
		WHILE ch # NIL DO
			IF ch.suspended THEN PostSys(ch, resumeS) END;
			ch := ch.sibNext
		END
	END DoResume;

	PROCEDURE Notify (c: Context; m: Msg);
	(* Terminated и ReceiveTimeout: обработчику, если c работает;
	   приостановленный c получит Terminated после возобновления. *)
		VAR r: INTEGER;
	BEGIN
		IF c.state # alive THEN RETURN END;
		IF c.suspended THEN
			IF m IS Terminated THEN Push(c.user, m, NIL) END
		ELSE
			r := Invoke(c, callRecv, m);
			IF r # 0 THEN Fail(c, r) END
		END
	END Notify;

	PROCEDURE System (c: Context; m: Msg);
		VAR w: Watcher; r: INTEGER;
	BEGIN
		WITH m: StartMsg DO
			r := Invoke(c, callRecv, started);
			IF r # 0 THEN Fail(c, r) END
		| m: StopMsg DO BeginStop(c)
		| m: RestartMsg DO BeginRestart(c)
		| m: ResumeMsg DO DoResume(c)
		| m: WatchMsg DO
			NEW(w); w.w := m.watcher; w.next := c.watchers; c.watchers := w
		| m: FailureMsg DO Supervise(c, m)
		| m: ChildGone DO
			Unlink(c, m.child);
			IF c.nChildren = 0 THEN
				IF c.state = stopping THEN Finish(c)
				ELSIF c.state = restarting THEN FinishRestart(c)
				END
			END
		ELSE Notify(c, m)
		END
	END System;

	(* --- тайм-аут простоя ------------------------------------------------- *)

	PROCEDURE (t: Timer) Do;
		VAR c: Context;
	BEGIN
		c := t.c;
		IF (c.rtMs <= 0) OR (c.state # alive) THEN c.rtArmed := FALSE
		ELSIF Services.Ticks() - c.rtLast >= c.rtMs THEN
			c.rtArmed := FALSE; PostSys(c, timeoutM)
		ELSE
			Services.DoLater(t, c.rtLast + c.rtMs)
		END
	END Do;

	PROCEDURE Touch (c: Context);
	(* Пост: срок простоя отсчитывается от сейчас. *)
	BEGIN
		c.rtLast := Services.Ticks();
		IF ~c.rtArmed THEN
			IF c.rtTimer = NIL THEN NEW(c.rtTimer); c.rtTimer.c := c END;
			c.rtArmed := TRUE; Services.DoLater(c.rtTimer, c.rtLast + c.rtMs)
		END
	END Touch;

	(* --- исполнение ------------------------------------------------------- *)

	PROCEDURE Process* (c: Context; max: INTEGER): BOOLEAN;
	(** Зовёт исполнитель. Пред: c # NIL (трап 20); c стоит у исполнителя
		и не в обработке (21); max > 0 (22).
		Пост: обработаны все системные сообщения и не более max
		пользовательских; TRUE — работа осталась, c по-прежнему у
		исполнителя; FALSE — работы нет, c снят с исполнителя. *)
		VAR n: INTEGER; m: Msg; s: Ref; r: INTEGER; more: BOOLEAN;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c.scheduled & ~c.running, 21); ASSERT(max > 0, 22);
		c.running := TRUE; n := 0;
		LOOP
			(* Инвариант: обработано n <= max пользовательских сообщений,
			   c.running. *)
			IF c.sys.count > 0 THEN
				Pop(c.sys, m, s); System(c, m)
			ELSIF (n < max) & UserReady(c) THEN
				Pop(c.user, m, s); INC(n);
				c.sender := s; r := Invoke(c, callRecv, m); c.sender := NIL;
				IF r # 0 THEN Fail(c, r) ELSIF (c.rtMs > 0) & (c.state = alive) THEN Touch(c) END
			ELSE EXIT
			END
		END;
		c.running := FALSE;
		more := HasWork(c);
		IF ~more THEN c.scheduled := FALSE END;
		RETURN more
	END Process;

	PROCEDURE (c: Context) Post* (m: Msg; sender: Ref);
	BEGIN
		ASSERT(m # NIL, 20);
		IF (c.state >= stopping) OR (c.props.capacity > 0) & (c.user.count >= c.props.capacity) THEN
			Dead(m, sender)
		ELSE
			Push(c.user, m, sender); Wake(c)
		END
	END Post;

	PROCEDURE (f: Future) Post (m: Msg; sender: Ref);
	BEGIN
		f.box.Send(m)
	END Post;

	(* --- операции контекста ----------------------------------------------- *)

	PROCEDURE (c: Context) Tell* (to: Ref; m: Msg), NEW;
	(** Пред: to # NIL (трап 20), m # NIL (21). Пост: m послано to,
		отправитель — c. *)
	BEGIN
		ASSERT(to # NIL, 20); ASSERT(m # NIL, 21);
		to.Post(m, c)
	END Tell;

	PROCEDURE (c: Context) Sender* (): Ref, NEW;
	(** Пост: отправитель сообщения в обработке; NIL — без отправителя. *)
	BEGIN
		RETURN c.sender
	END Sender;

	PROCEDURE (c: Context) Respond* (m: Msg), NEW;
	(** Пред: m # NIL (трап 20). Пост: m послано отправителю сообщения в
		обработке; без отправителя — мёртвое письмо. *)
	BEGIN
		ASSERT(m # NIL, 20);
		IF c.sender # NIL THEN c.sender.Post(m, c) ELSE Dead(m, NIL) END
	END Respond;

	PROCEDURE (c: Context) Parent* (): Ref, NEW;
	(** Пост: адрес родителя; NIL — родитель корень. *)
	BEGIN
		IF c.parent = NIL THEN RETURN NIL ELSE RETURN c.parent END
	END Parent;

	PROCEDURE (c: Context) Watch* (r: Ref), NEW;
	(** Пред: r — локальный адрес (трап 20). Пост: после остановки r
		обработчик c получит Terminated с who = r, ровно один раз на вызов
		Watch; r уже остановлен — получит сразу. *)
		VAR w: WatchMsg;
	BEGIN
		ASSERT((r # NIL) & (r IS Context), 20);
		NEW(w); w.watcher := c; PostSys(r(Context), w)
	END Watch;

	PROCEDURE (c: Context) SetReceiveTimeout* (ms: LONGINT), NEW;
	(** Пост: ms > 0 — после ms мс без пользовательских сообщений
		обработчик получит ReceiveTimeout один раз, следующий отсчёт
		начнёт следующее сообщение; ms <= 0 — тайм-аута нет. *)
	BEGIN
		c.rtMs := ms;
		IF ms > 0 THEN c.rtArmed := FALSE; CancelTimer(c); Touch(c) ELSE CancelTimer(c) END
	END SetReceiveTimeout;

	PROCEDURE (c: Context) Become* (b: Behavior), NEW;
	(** Пост: следующие сообщения обрабатывает b.Receive; b = NIL —
		снова Actor.Receive. Перезапуск возвращает Actor.Receive. *)
	BEGIN
		c.beh := b
	END Become;

	PROCEDURE (c: Context) Pending* (): BOOLEAN, NEW;
	(** Пост: c стоит у исполнителя. *)
	BEGIN
		RETURN c.scheduled
	END Pending;

	PROCEDURE (c: Context) IsStopped* (): BOOLEAN, NEW;
	(** Пост: c остановлен, письма ему — мёртвые. *)
	BEGIN
		RETURN c.state = stopped
	END IsStopped;

	(* --- создание и общие операции ---------------------------------------- *)

	PROCEDURE Append (VAR s: Name; IN t: ARRAY OF CHAR);
		VAR i, j: INTEGER;
	BEGIN
		i := 0; WHILE s[i] # 0X DO INC(i) END;
		j := 0;
		WHILE t[j] # 0X DO
			ASSERT(i < nameLen - 1, 25);
			s[i] := t[j]; INC(i); INC(j)
		END;
		s[i] := 0X
	END Append;

	PROCEDURE NewProps* (f: Factory): Props;
	(** Пред: f # NIL (трап 20). Пост: описание с фабрикой f, ящик без
		предела, исполнитель пула, без стратегии (отказ ребёнка — Stop). *)
		VAR p: Props;
	BEGIN
		ASSERT(f # NIL, 20);
		NEW(p); p.new := f; p.capacity := 0; p.dedicated := FALSE; p.strategy := NIL;
		RETURN p
	END NewProps;

	PROCEDURE Spawn* (parent: Context; p: Props; IN name: ARRAY OF CHAR): Ref;
	(** Пред: p # NIL и p.new # NIL (трап 20); p.capacity >= 0 (21);
		исполнитель установлен (22); parent = NIL или parent работает (23);
		имя свободно (24); полное имя короче nameLen (25); фабрика не
		трапует и не возвращает NIL (60).
		Пост: новый актор, ребёнок parent (NIL — корня); имя — name
		(у ребёнка — "имя родителя/name"), пустое name — "$номер";
		первым его обработчик получит Started. *)
		VAR c: Context; s: ARRAY 16 OF CHAR;
	BEGIN
		ASSERT((p # NIL) & (p.new # NIL), 20); ASSERT(p.capacity >= 0, 21);
		ASSERT(dispatcher # NIL, 22);
		ASSERT((parent = NIL) OR (parent.state = alive), 23);
		NEW(c); c.node := node$; c.name := "";
		IF name = "" THEN
			INC(seq); Strings.IntToString(seq, s); Append(c.name, "$"); Append(c.name, s)
		ELSE
			IF parent # NIL THEN Append(c.name, parent.name); Append(c.name, "/") END;
			Append(c.name, name)
		END;
		ASSERT(Find(c.name) = NIL, 24);
		c.props := p; c.dedicated := p.dedicated; c.state := alive;
		c.actor := p.new(); ASSERT(c.actor # NIL, 60);
		c.parent := parent;
		IF parent # NIL THEN
			c.sibNext := parent.first;
			IF parent.first # NIL THEN parent.first.sibPrev := c END;
			parent.first := c; INC(parent.nChildren)
		END;
		c.hnext := table[Hash(c.name)]; table[Hash(c.name)] := c;
		PostSys(c, startS);
		RETURN c
	END Spawn;

	PROCEDURE Tell* (to: Ref; m: Msg);
	(** Пред: to # NIL (трап 20), m # NIL (21). Пост: m послано to без
		отправителя; ожидания нет. *)
	BEGIN
		ASSERT(to # NIL, 20); ASSERT(m # NIL, 21);
		to.Post(m, NIL)
	END Tell;

	PROCEDURE StopActor* (r: Ref);
	(** Пред: r — локальный адрес (трап 20). Пост: r получит Stop раньше
		своих пользовательских писем; остановленный r не меняется. *)
	BEGIN
		ASSERT((r # NIL) & (r IS Context), 20);
		PostSys(r(Context), stopS)
	END StopActor;

	PROCEDURE Ask* (to: Ref; m: Msg; ms: LONGINT; OUT reply: Msg): INTEGER;
	(** Ждёт ответа. Пред: вызов из задачи (трап 20); to # NIL (21);
		m # NIL (22). Отмена задачи — трап 127.
		Пост: ok — reply — первое сообщение, посланное адресу ответа
		(Respond адресата); timedOut — за ms мс ответа нет; dead — m стало
		мёртвым письмом. Кроме ok, reply = NIL. Поздний ответ теряется. *)
		VAR f: Future; ops: ARRAY 2 OF AsyncSelect.Op; res: INTEGER;
	BEGIN
		ASSERT(Tasks.InTask(), 20); ASSERT(to # NIL, 21); ASSERT(m # NIL, 22);
		NEW(f); f.node := node$; f.name := "$ask"; f.box := AsyncChan.NewMailbox();
		to.Post(m, f);
		AsyncChan.RecvAlt(f.box, ops[0]); AsyncChan.TimeoutAlt(ms, ops[1]);
		IF AsyncChan.Select(ops) = 0 THEN
			reply := ops[0].got;
			IF reply = deadM THEN reply := NIL; res := dead ELSE res := ok END
		ELSE
			reply := NIL; res := timedOut
		END;
		RETURN res
	END Ask;

	PROCEDURE InitRef* (r: Ref; IN nd, name: ARRAY OF CHAR);
	(** Для модулей транспорта. Пред: r # NIL (трап 20), nd и name
		короче nameLen (21). Пост: r.node = nd, r.name = name. *)
	BEGIN
		ASSERT(r # NIL, 20); ASSERT((LEN(nd$) < nameLen) & (LEN(name$) < nameLen), 21);
		r.node := nd$; r.name := name$
	END InitRef;

	PROCEDURE SetDispatcher* (d: Dispatcher);
	(** Пред: d # NIL (трап 20). Пост: dispatcher = d. *)
	BEGIN
		ASSERT(d # NIL, 20);
		dispatcher := d
	END SetDispatcher;

	PROCEDURE SetDir* (d: Directory);
	(** Пред: d # NIL (трап 20). Пост: dir = d; первый установленный
		каталог остаётся в stdDir. *)
	BEGIN
		ASSERT(d # NIL, 20);
		dir := d;
		IF stdDir = NIL THEN stdDir := d END
	END SetDir;

	PROCEDURE Init;
		VAR d: StdDirectory;
	BEGIN
		node := "local";
		NEW(started); NEW(stoppingM); NEW(stoppedM); NEW(restartingM); NEW(timeoutM);
		NEW(startS); NEW(stopS); NEW(resumeS); NEW(restartS); NEW(deadM);
		NEW(d); SetDir(d)
	END Init;

BEGIN
	Init
END ActorCore.
