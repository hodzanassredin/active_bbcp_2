MODULE ActorCore;
(** Локальные акторы: адрес, экземпляр, ящик, жизненный цикл и надзор.

	Актор — экземпляр Actor с обработчиком Receive и ящиком (Context).
	Своей задачи у актора нет: ящик с работой отдаётся исполнителю через
	Dispatcher.Schedule, исполнитель зовёт Dispatcher.Process. Методы
	Dispatcher, кроме Schedule, работают только у установленного
	исполнителя (SetDispatcher). Каждое сообщение обрабатывается под
	Kernel.Try; трап превращается в сообщение об отказе родителю,
	родитель применяет директиву своей стратегии.

	Отмена задачи исполнителя (трап 127) прерывает только пользовательский
	обработчик: письмо теряется. Шаг жизненного цикла и надзора доводится
	до конца: прерванный обработчик Started, Stopping, Stopped или
	Restarting считается выполненным, прерванные фабрика и Decide
	повторяются следующим исполнителем (их системное письмо возвращается
	в начало очереди). Затем задача прерывается.

	Одна нить ОС, вытеснения нет: операции над ящиком между двумя
	точками переключения неделимы, поэтому мониторов в модуле нет.
	Пока обработчик ждёт внутри Receive, ящик помечен running: второе
	сообщение этому актору не выдаётся.

	Инвариант ящика (Context):
	- scheduled = ящик стоит у исполнителя (в очереди или в Process);
	  ~scheduled => ~HasWork или работа поставлена через Wake;
	- running => scheduled;
	- state = stopped => очереди пусты, actor = NIL, детей нет, имени
	  в каталоге нет, наблюдателей и наблюдаемых нет;
	- state = alive & ~suspended => actor # NIL;
	- адрес в watchers и в watching — не более одного раза;
	- nChildren = длина списка first/sibNext. *)

	IMPORT S := SYSTEM, Kernel, Services, Console, Strings, Stores, Tasks, AsyncSelect, AsyncChan;

	CONST
		(** директивы супервизора: первые четыре решают судьбу отказавшего
			ребёнка, остальные — группы детей (стратегии AllForOne и
			RestForOne) **)
		Resume* = 0; Restart* = 1; Stop* = 2; Escalate* = 3;
		RestartAll* = 4;	(** перезапустить всех детей родителя *)
		StopAll* = 5;	(** остановить всех детей родителя *)
		RestartRest* = 6;	(** перезапустить отказавшего и рождённых после него *)
		StopRest* = 7;	(** остановить отказавшего и рождённых после него *)
		(** результат Ask **)
		ok* = 0; timedOut* = 1; dead* = 2;
		(** причина в Terminated **)
		byStop* = 0; byFailure* = 1; byNodeLoss* = 2;
		nameLen* = 128;

		(* состояние ящика *)
		alive = 0; restarting = 1; stopping = 2; stopped = 3;
		(* вид вызова под Kernel.Try *)
		callUser = 0; callLife = 1; callNew = 2; callDecide = 3;
		cancelTrap = 127;
		nilActor = 26;	(* фабрика вернула NIL *)
		ringMin = 4;
		buckets = 4096;

	TYPE
		Msg* = AsyncSelect.Msg;
		Name* = ARRAY nameLen OF CHAR;

		(** Адрес актора: узел и имя. Локальный адрес — Context; адрес
			ответа Ask — свой тип модуля; удалённый адрес даёт расширение
			модуля транспорта. *)
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

		(** Исполнитель ящиков (ActorRun). Ящик — Context. *)
		Dispatcher* = POINTER TO ABSTRACT RECORD END;

		(** Каталог адресов: имя узла и актора -> адрес. *)
		Directory* = POINTER TO ABSTRACT RECORD END;

		(** Служебное письмо: в ящик идёт мимо предела capacity. Им отвечают
			актору, который ждёт этого ответа и без него из ожидания не выйдет
			(ответ журнала стойкому актору); потеря такого письма на полном
			ящике оставила бы актор ждать вечно. **)
		Urgent* = POINTER TO ABSTRACT RECORD (Msg) END;

		(** Приёмник мёртвых писем: узнаёт о каждом письме, которое не дошло
			до адресата (ActorEvent ставит сюда поток событий). *)
		DeadHook* = POINTER TO ABSTRACT RECORD END;

		(** сообщения жизненного цикла, их получает Receive **)
		Started* = POINTER TO RECORD (Msg) END;
		Stopping* = POINTER TO RECORD (Msg) END;
		Stopped* = POINTER TO RECORD (Msg) END;
		Restarting* = POINTER TO RECORD (Msg) END;
		ReceiveTimeout* = POINTER TO RECORD (Msg) END;
		Terminated* = POINTER TO RECORD (Msg)
			who-: Ref;
			reason-: INTEGER	(** byStop — остановлен, byFailure — остановлен после отказа, byNodeLoss — узел потерян *)
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
			dedicated-: BOOLEAN;	(** ящик обслуживает своя задача *)
			host: ANYPTR;	(* данные исполнителя (Dispatcher.Host) *)
			props: Props;
			actor: Actor;
			beh: Behavior;
			parent, first, sibPrev, sibNext: Context;
			nChildren: INTEGER;
			watchers: Watcher;	(* кто ждёт Terminated от этого актора *)
			watching: Watcher;	(* за кем следит этот актор (Watch) *)
			user, sys: Queue;
			held: Queue;	(* отказы детей, отложенные, пока актор приостановлен *)
			stash: Queue;	(* письма, отложенные обработчиком (Stash) *)
			state: INTEGER;
			suspended, scheduled, running: BOOLEAN;
			failed: BOOLEAN;	(* остановка — решение надзора после отказа *)
			interrupted: BOOLEAN;	(* шаг прерван отменой задачи, см. Invoke *)
			task: Tasks.Task;	(* задача, исполняющая Process; NIL вне Process *)
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

		WatcherDesc = RECORD next: Watcher; r: Ref END;
		TimerDesc = RECORD (Services.Action) c: Context END;

		(* системные сообщения *)
		StartMsg = POINTER TO RECORD (Msg) END;
		StopMsg = POINTER TO RECORD (Msg) END;
		ResumeMsg = POINTER TO RECORD (Msg) END;
		RestartMsg = POINTER TO RECORD (Msg) END;
		NewMsg = POINTER TO RECORD (Msg) END;	(* повтор фабрики после отмены *)
		WatchMsg = POINTER TO RECORD (Msg) watcher: Ref END;
		UnwatchMsg = POINTER TO RECORD (Msg) watcher: Ref END;
		FailureMsg = POINTER TO RECORD (Msg) child: Context; reason: INTEGER END;
		ChildGone = POINTER TO RECORD (Msg) child: Context END;
		DeadMsg = POINTER TO RECORD (Msg) END;

		(* Адрес ответа Ask: ответ кладётся в ящик канала. Пока Ask ждёт,
		   адрес стоит в реестре asks под именем "$ask/номер". *)
		Future = POINTER TO RECORD (Ref)
			box: AsyncChan.Mailbox;
			next: Future
		END;

		(* Снимает адрес ответа, если ждущую в Ask задачу отменили. *)
		AskCleaner = POINTER TO RECORD (Kernel.TrapCleaner) f: Future; to: Ref END;

		StdDirectory = POINTER TO RECORD (Directory) END;

	VAR
		dispatcher-: Dispatcher;
		dir-, stdDir-: Directory;
		node-: Name;	(** имя своего узла *)
		deadLetters-: LONGINT;	(** число мёртвых писем с начала работы *)
		deadHook: DeadHook;
		hookMsg: Msg; hookSender, hookTo: Ref;
		inDead: BOOLEAN;	(* идёт вызов приёмника: вложенное мёртвое письмо не повторяет вызов *)

		seq: INTEGER;	(* номер следующего безымянного актора *)
		askSeq: INTEGER;	(* номер следующего адреса ответа *)
		asks: Future;	(* реестр ждущих Ask *)
		live: INTEGER;	(* акторов, ещё не остановленных *)
		table: ARRAY buckets OF Context;	(* каталог локальных имён *)
		started: Started; stoppingM: Stopping; stoppedM: Stopped;
		restartingM: Restarting; timeoutM: ReceiveTimeout;
		startS: StartMsg; stopS: StopMsg; resumeS: ResumeMsg; restartS: RestartMsg;
		newS: NewMsg; deadM: DeadMsg;

	(* --- абстрактные операции --------------------------------------------- *)

	PROCEDURE (h: DeadHook) Dead* (m: Msg; sender, to: Ref), NEW, ABSTRACT;
	(** Пред: m # NIL — не проверяется, зовёт ActorCore.
		Пост: факт мёртвого письма учтён; to — адресат, NIL — адреса нет. *)

	PROCEDURE (r: Ref) Post* (m: Msg; sender: Ref), NEW, ABSTRACT;
	(** Пред: m # NIL — 20. Пост: m доставлено в ящик адресата или учтено
		как мёртвое письмо; отправитель не ждёт. *)

	PROCEDURE (r: Ref) AddWatcher* (w: Ref), NEW, ABSTRACT;
	(** Пред: w # NIL — 20. Пост: после остановки r наблюдатель w получит
		Terminated с who = r один раз; r уже остановлен — получит сразу;
		повторный вызов с тем же w ничего не добавляет. *)

	PROCEDURE (r: Ref) RemoveWatcher* (w: Ref), NEW, ABSTRACT;
	(** Пред: w # NIL — 20. Пост: w не наблюдает r. *)

	PROCEDURE (r: Ref) Stop*, NEW, ABSTRACT;
	(** Пост: r получит Stop раньше своих пользовательских писем;
		остановленный r не меняется. *)

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
		позовёт d.Process(c, ...) и будет звать, пока Process возвращает
		TRUE. *)

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

	PROCEDURE PushFront (VAR q: Queue; m: Msg; s: Ref);
	(* Пост: (m, s) — первый конверт очереди. *)
		VAR i, n: INTEGER;
	BEGIN
		Push(q, m, s);	(* место в кольце *)
		n := LEN(q.ring^);
		i := (q.head + q.count - 1) MOD n;
		q.ring[i].m := NIL; q.ring[i].s := NIL;
		q.head := (q.head - 1 + n) MOD n;
		q.ring[q.head].m := m; q.ring[q.head].s := s
	END PushFront;

	PROCEDURE Pop (VAR q: Queue; OUT m: Msg; OUT s: Ref);
	(* Пред: q.count > 0. Пост: (m, s) — бывший первый конверт. *)
	BEGIN
		ASSERT(q.count > 0, 100);
		m := q.ring[q.head].m; s := q.ring[q.head].s;
		q.ring[q.head].m := NIL; q.ring[q.head].s := NIL;
		q.head := (q.head + 1) MOD LEN(q.ring^); DEC(q.count)
	END Pop;

	(* --- списки наблюдения ------------------------------------------------ *)

	PROCEDURE Add (VAR l: Watcher; r: Ref);
	(* Пост: r в l ровно один раз. *)
		VAR w: Watcher;
	BEGIN
		w := l;
		WHILE (w # NIL) & (w.r # r) DO w := w.next END;
		IF w = NIL THEN NEW(w); w.r := r; w.next := l; l := w END
	END Add;

	PROCEDURE Remove (VAR l: Watcher; r: Ref): BOOLEAN;
	(* Пост: r нет в l; TRUE — был. *)
		VAR w, prev: Watcher;
	BEGIN
		w := l; prev := NIL;
		WHILE (w # NIL) & (w.r # r) DO prev := w; w := w.next END;
		IF w = NIL THEN RETURN FALSE END;
		IF prev = NIL THEN l := w.next ELSE prev.next := w.next END;
		RETURN TRUE
	END Remove;

	(* --- мёртвые письма и пробуждение ------------------------------------- *)

	PROCEDURE HookCall (a, b, cc: S.ADDRESS);
	(* Вызов приёмника под Kernel.Try: трап приёмника не должен стать
	   отказом актора, пославшего мёртвое письмо. *)
	BEGIN
		deadHook.Dead(hookMsg, hookSender, hookTo)
	END HookCall;

	PROCEDURE Dead (m: Msg; sender, to: Ref);
	(* Пост: письмо учтено; ждущий Ask узнаёт об этом сразу; приёмник
	   мёртвых писем извещён. Вложенное мёртвое письмо (приёмник ставит
	   письмо остановленному потоку) не повторяет вызов и не считается:
	   иначе остановленный приёмник замкнул бы круг. *)
	BEGIN
		IF inDead THEN RETURN END;
		INC(deadLetters);
		IF (sender # NIL) & (sender IS Future) & (m # deadM) THEN sender.Post(deadM, NIL) END;
		IF deadHook # NIL THEN
			inDead := TRUE; hookMsg := m; hookSender := sender; hookTo := to;
			Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(HookCall)), 0, 0, 0);
			hookMsg := NIL; hookSender := NIL; hookTo := NIL; inDead := FALSE
		END
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

	PROCEDURE^ PostSys (c: Context; m: Msg);

	PROCEDURE Gone (w: Ref; who: Context);
	(* Пост: наблюдатель w получил Terminated с who и причиной остановки who. *)
		VAR t: Terminated;
	BEGIN
		NEW(t); t.who := who;
		IF who.failed THEN t.reason := byFailure ELSE t.reason := byStop END;
		IF w IS Context THEN PostSys(w(Context), t) ELSE w.Post(t, NIL) END
	END Gone;

	PROCEDURE PostSys (c: Context; m: Msg);
	(* Пост: m в системной очереди c; остановленный c отвечает на
	   наблюдение сообщением Terminated, прочее отбрасывает. *)
	BEGIN
		IF c.state = stopped THEN
			IF m IS WatchMsg THEN Gone(m(WatchMsg).watcher, c) END
		ELSE
			Push(c.sys, m, NIL); Wake(c)
		END
	END PostSys;

	PROCEDURE Release (c: Context);
	(* Пост: c снят с исполнителя; есть работа — отдан исполнителю снова. *)
	BEGIN
		c.running := FALSE; c.scheduled := FALSE; c.sender := NIL; c.task := NIL;
		Wake(c)
	END Release;

	PROCEDURE InHandler (c: Context): BOOLEAN;
	(* Пост: вызов идёт из обработчика c (из Process c в текущей задаче). *)
	BEGIN
		RETURN c.running & (c.task # NIL) & (c.task = Tasks.Current())
	END InHandler;

	(* --- вызов кода пользователя под Kernel.Try --------------------------- *)

	PROCEDURE Call (a, b, cc: S.ADDRESS);
		VAR c: Context;
	BEGIN
		c := S.VAL(Context, a);
		CASE c.callKind OF
		| callUser, callLife:
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
	   исполнителя отменена) в пользовательском обработчике (callUser) не
	   возвращается: письмо потеряно, ящик снова отдан исполнителю, задача
	   прерывается тем же трапом. В шаге жизненного цикла и надзора 127
	   возвращается и c.interrupted = TRUE: вызывающий доводит шаг до
	   конца, Process затем прерывает задачу. *)
		VAR err: INTEGER;
	BEGIN
		c.callKind := kind; c.callMsg := m; c.done := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(Call)), S.VAL(S.ADDRESS, c), 0, 0);
		c.callMsg := NIL;
		IF c.done THEN RETURN 0 END;
		err := Kernel.err;
		IF err = cancelTrap THEN
			IF kind = callUser THEN Release(c); HALT(cancelTrap) END;
			c.interrupted := TRUE
		ELSIF err = 0 THEN err := 1
		END;
		RETURN err
	END Invoke;

	PROCEDURE Lifecycle (c: Context; m: Msg);
	(* Пост: обработчик получил m; трап в нём учтён в Console и отказом
	   не считается; отмена задачи прерывает обработчик, шаг идёт дальше. *)
		VAR r: INTEGER;
	BEGIN
		IF c.actor # NIL THEN
			r := Invoke(c, callLife, m);
			IF (r # 0) & (r # cancelTrap) THEN
				Console.WriteStr("Actor: trap in lifecycle of "); Console.WriteStr(c.name); Console.WriteLn
			END
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
			c.failed := TRUE; PostSys(c, stopS)
		END
	END Fail;

	PROCEDURE Group (p: Context; ch: Context; all, stop: BOOLEAN);
	(* Пост: каждому ребёнку p послан Stop (stop) или Restart; all = FALSE —
	   только ch и рождённым после него (дети в списке p.first идут от
	   младшего к старшему, поэтому обход идёт от first до ch). *)
		VAR x: Context;
	BEGIN
		x := p.first;
		LOOP
			IF x = NIL THEN EXIT END;
			IF stop THEN PostSys(x, stopS) ELSE PostSys(x, restartS) END;
			IF ~all & (x = ch) THEN EXIT END;
			x := x.sibNext
		END
	END Group;

	PROCEDURE Supervise (p: Context; f: FailureMsg);
	(* Пост: к ребёнку f.child применена директива стратегии p; без
	   стратегии — Stop; Resume ребёнку без экземпляра — Stop. Пока p
	   приостановлен, отказ отложен до его возобновления. Трап стратегии —
	   отказ p, решение отложено; отмена задачи в Decide — f снова первым
	   в очереди p. *)
		VAR ch: Context; d, r: INTEGER;
	BEGIN
		ch := f.child;
		IF (ch.parent # p) OR (ch.state >= stopping) OR (p.state # alive) THEN RETURN END;
		IF p.suspended THEN Push(p.held, f, NIL); RETURN END;
		IF p.props.strategy = NIL THEN d := Stop
		ELSE
			p.callChild := ch; p.callReason := f.reason;
			r := Invoke(p, callDecide, NIL);
			p.callChild := NIL;
			IF r = cancelTrap THEN PushFront(p.sys, f, NIL); RETURN
			ELSIF r # 0 THEN Push(p.held, f, NIL); Fail(p, r); RETURN
			END;
			d := p.callResult
		END;
		IF (d = Resume) & (ch.actor = NIL) THEN d := Stop END;
		CASE d OF
		| Resume: PostSys(ch, resumeS)
		| Restart: PostSys(ch, restartS)
		| RestartAll: Group(p, ch, TRUE, FALSE)
		| StopAll: ch.failed := TRUE; Group(p, ch, TRUE, TRUE)
		| RestartRest: Group(p, ch, FALSE, FALSE)
		| StopRest: ch.failed := TRUE; Group(p, ch, FALSE, TRUE)
		| Escalate: Fail(p, f.reason)	(* ребёнок ждёт судьбы родителя *)
		ELSE ch.failed := TRUE; PostSys(ch, stopS)
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
		VAR c: Context; f: Future;
	BEGIN
		IF nd # node THEN RETURN NIL END;
		c := Find(name);
		IF c # NIL THEN RETURN c END;
		f := asks;
		WHILE (f # NIL) & (f.name # name) DO f := f.next END;
		RETURN f
	END Lookup;

	(* --- переходы жизненного цикла ---------------------------------------- *)

	PROCEDURE Merge (c: Context);
	(* Пост: отложенные письма — в начале ящика в прежнем порядке, за ними прежние письма ящика. *)
		VAR m: Msg; s: Ref;
	BEGIN
		IF c.stash.count > 0 THEN
			WHILE c.user.count > 0 DO Pop(c.user, m, s); Push(c.stash, m, s) END;
			c.user := c.stash; c.stash.ring := NIL; c.stash.head := 0; c.stash.count := 0
		END
	END Merge;

	PROCEDURE CancelTimer (c: Context);
	BEGIN
		IF c.rtTimer # NIL THEN Services.RemoveAction(c.rtTimer) END;
		c.rtArmed := FALSE
	END CancelTimer;

	PROCEDURE Finish (c: Context);
	(* Пред: state = stopping, детей нет. Пост: state = stopped;
	   обработчик получил Stopped; письма ящика — мёртвые; наблюдатели
	   получили Terminated, родитель — уход ребёнка; свои наблюдения сняты;
	   имени в каталоге нет. *)
		VAR m: Msg; s: Ref; w: Watcher; g: ChildGone;
	BEGIN
		ASSERT((c.state = stopping) & (c.nChildren = 0), 100);
		Lifecycle(c, stoppedM);
		c.state := stopped; c.actor := NIL; c.beh := NIL; DEC(live);
		WHILE c.stash.count > 0 DO Pop(c.stash, m, s); Dead(m, s, c) END;
		WHILE c.user.count > 0 DO Pop(c.user, m, s); Dead(m, s, c) END;
		WHILE c.sys.count > 0 DO
			Pop(c.sys, m, s);
			IF m IS WatchMsg THEN Add(c.watchers, m(WatchMsg).watcher) END
		END;
		WHILE c.held.count > 0 DO Pop(c.held, m, s) END;
		w := c.watchers; c.watchers := NIL;
		WHILE w # NIL DO Gone(w.r, c); w := w.next END;
		w := c.watching; c.watching := NIL;
		WHILE w # NIL DO w.r.RemoveWatcher(c); w := w.next END;
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
	   фабрики получил Started; письма ящика сохранены, отложенные (Stash) —
	   в его начале. Трап фабрики или
	   NIL от неё — отказ c с actor = NIL. Отмена задачи в фабрике — c
	   остаётся в restarting, повтор фабрики первым в очереди. *)
		VAR r: INTEGER;
	BEGIN
		c.actor := NIL; c.beh := NIL; Merge(c);
		r := Invoke(c, callNew, NIL);
		IF r = cancelTrap THEN c.actor := NIL; PushFront(c.sys, newS, NIL); RETURN END;
		c.state := alive; c.suspended := FALSE;
		IF (r = 0) & (c.actor = NIL) THEN r := nilActor END;
		IF r = 0 THEN
			r := Invoke(c, callLife, started);
			IF r = cancelTrap THEN r := 0 END
		END;
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

	PROCEDURE^ Touch (c: Context);

	PROCEDURE Held (c, ch: Context): BOOLEAN;
	(* Пост: отказ ch отложен у c. *)
		VAR i: INTEGER; m: Msg;
	BEGIN
		i := 0;
		WHILE i < c.held.count DO
			m := c.held.ring[(c.held.head + i) MOD LEN(c.held.ring^)].m;
			IF m(FailureMsg).child = ch THEN RETURN TRUE END;
			INC(i)
		END;
		RETURN FALSE
	END Held;

	PROCEDURE DoResume (c: Context);
	(* Пост: c обрабатывает письма; приостановленные дети (ждавшие
	   судьбы c после Escalate) возобновлены; отложенные отказы детей —
	   в системной очереди; тайм-аут простоя взведён заново. Без
	   экземпляра (фабрика упала) c останавливается. *)
		VAR ch: Context; m: Msg; s: Ref;
	BEGIN
		IF c.state # alive THEN RETURN END;
		IF c.actor = NIL THEN BeginStop(c); RETURN END;
		c.suspended := FALSE;
		ch := c.first;
		WHILE ch # NIL DO
			IF ch.suspended & ~Held(c, ch) THEN PostSys(ch, resumeS) END;
			ch := ch.sibNext
		END;
		WHILE c.held.count > 0 DO Pop(c.held, m, s); Push(c.sys, m, NIL) END;
		IF c.rtMs > 0 THEN Touch(c) END
	END DoResume;

	PROCEDURE Deliver (c: Context; m: Msg);
	(* Пост: обработчик получил m; Terminated — только от наблюдаемого
	   (после Unwatch или повторно — нет); трап — отказ c. *)
		VAR r: INTEGER;
	BEGIN
		IF ~(m IS Terminated) OR Remove(c.watching, m(Terminated).who) THEN
			r := Invoke(c, callUser, m);
			IF r # 0 THEN Fail(c, r) END
		END
	END Deliver;

	PROCEDURE Notify (c: Context; m: Msg);
	(* Terminated и ReceiveTimeout: обработчику, если c работает;
	   приостановленный c получит Terminated после возобновления;
	   ReceiveTimeout при ждущих письмах или приостановке не выдаётся. *)
	BEGIN
		IF c.state # alive THEN RETURN END;
		IF m IS Terminated THEN
			IF c.suspended THEN Push(c.user, m, NIL) ELSE Deliver(c, m) END
		ELSIF ~c.suspended & (c.user.count = 0) THEN
			Deliver(c, m)
		END
	END Notify;

	PROCEDURE System (c: Context; m: Msg);
		VAR r: INTEGER;
	BEGIN
		WITH m: StartMsg DO
			r := Invoke(c, callLife, started);
			IF (r # 0) & (r # cancelTrap) THEN Fail(c, r) END
		| m: StopMsg DO BeginStop(c)
		| m: RestartMsg DO BeginRestart(c)
		| m: ResumeMsg DO DoResume(c)
		| m: NewMsg DO IF (c.state = restarting) & (c.nChildren = 0) THEN FinishRestart(c) END
		| m: WatchMsg DO Add(c.watchers, m.watcher)
		| m: UnwatchMsg DO IF Remove(c.watchers, m.watcher) THEN END
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
	(* Срок истёк: тайм-аут выдаётся, только если обработчик не идёт и
	   писем нет; иначе срок проверяется снова (письмо сдвинет rtLast).
	   Приостановленный без писем снимает таймер; DoResume взводит его. *)
		VAR c: Context; now: LONGINT;
	BEGIN
		c := t.c; now := Services.Ticks();
		IF (c.rtMs <= 0) OR (c.state # alive) THEN c.rtArmed := FALSE
		ELSIF now - c.rtLast < c.rtMs THEN Services.DoLater(t, c.rtLast + c.rtMs)
		ELSIF c.running OR (c.user.count > 0) THEN Services.DoLater(t, now + c.rtMs)
		ELSIF c.suspended THEN c.rtArmed := FALSE
		ELSE c.rtArmed := FALSE; PostSys(c, timeoutM)
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

	(* --- исполнитель ------------------------------------------------------ *)

	PROCEDURE (d: Dispatcher) Process* (c: Context; max: INTEGER): BOOLEAN, NEW;
	(** Пред: d — установленный исполнитель (трап 20); c # NIL (21); c
		стоит у исполнителя и не в обработке (22); max > 0 (23); вызов из
		задачи (24).
		Пост: обработаны все системные сообщения и не более max
		пользовательских; TRUE — работа осталась, c по-прежнему у
		исполнителя; FALSE — работы нет, c снят с исполнителя. Отмена
		задачи — трап 127, ящик снова отдан исполнителю. *)
		VAR n: INTEGER; m: Msg; s: Ref; more: BOOLEAN;
	BEGIN
		ASSERT(d = dispatcher, 20); ASSERT(c # NIL, 21); ASSERT(c.scheduled & ~c.running, 22);
		ASSERT(max > 0, 23); ASSERT(Tasks.InTask(), 24);
		c.running := TRUE; c.task := Tasks.Current(); n := 0;
		LOOP
			(* Инвариант: обработано n <= max пользовательских сообщений,
			   c.running, ~c.interrupted. *)
			IF c.sys.count > 0 THEN
				Pop(c.sys, m, s); System(c, m);
				IF c.interrupted THEN c.interrupted := FALSE; Release(c); HALT(cancelTrap) END
			ELSIF (n < max) & UserReady(c) THEN
				Pop(c.user, m, s); INC(n);
				c.sender := s; Deliver(c, m); c.sender := NIL;
				IF ~c.suspended & (c.rtMs > 0) & (c.state = alive) THEN Touch(c) END
			ELSE EXIT
			END
		END;
		c.running := FALSE; c.task := NIL;
		more := HasWork(c);
		IF ~more THEN c.scheduled := FALSE END;
		RETURN more
	END Process;

	PROCEDURE (d: Dispatcher) Pending* (c: Context): BOOLEAN, NEW;
	(** Пред: d — установленный исполнитель (трап 20). Пост: c стоит у
		исполнителя. *)
	BEGIN
		ASSERT(d = dispatcher, 20);
		RETURN c.scheduled
	END Pending;

	PROCEDURE (d: Dispatcher) Stopped* (c: Context): BOOLEAN, NEW;
	(** Пред: d — установленный исполнитель (трап 20). Пост: c остановлен,
		письма ему — мёртвые. *)
	BEGIN
		ASSERT(d = dispatcher, 20);
		RETURN c.state = stopped
	END Stopped;

	PROCEDURE (d: Dispatcher) Host* (c: Context): ANYPTR, NEW;
	(** Пред: d — установленный исполнитель (трап 20). Пост: данные
		исполнителя при ящике c (SetHost); NIL — нет. *)
	BEGIN
		ASSERT(d = dispatcher, 20);
		RETURN c.host
	END Host;

	PROCEDURE (d: Dispatcher) SetHost* (c: Context; h: ANYPTR), NEW;
	(** Пред: d — установленный исполнитель (трап 20). Пост: d.Host(c) = h. *)
	BEGIN
		ASSERT(d = dispatcher, 20);
		c.host := h
	END SetHost;

	PROCEDURE (d: Dispatcher) Live* (): INTEGER, NEW;
	(** Пред: d — установленный исполнитель (трап 20). Пост: число
		неостановленных акторов. *)
	BEGIN
		ASSERT(d = dispatcher, 20);
		RETURN live
	END Live;

	PROCEDURE (d: Dispatcher) StopRoots*, NEW;
	(** Пред: d — установленный исполнитель (трап 20). Пост: каждый
		неостановленный актор без родителя получил Stop. *)
		VAR i: INTEGER; c: Context;
	BEGIN
		ASSERT(d = dispatcher, 20);
		FOR i := 0 TO buckets - 1 DO
			c := table[i];
			WHILE c # NIL DO
				IF c.parent = NIL THEN PostSys(c, stopS) END;
				c := c.hnext
			END
		END
	END StopRoots;

	(* --- локальный адрес -------------------------------------------------- *)

	PROCEDURE (c: Context) Post* (m: Msg; sender: Ref);
	(** Пред: m # NIL — 20. Пост: m в ящике c; c останавливается, остановлен
		или ящик полон — мёртвое письмо. Ящик полон, когда писем в нём вместе
		с отложенными (Stash) не меньше capacity > 0; письмо Urgent идёт мимо
		предела. Вызов из задачи или с главного стека. *)
	BEGIN
		ASSERT(m # NIL, 20);
		IF (c.state >= stopping)
		OR (c.props.capacity > 0) & ~(m IS Urgent) & (c.user.count + c.stash.count >= c.props.capacity) THEN
			Dead(m, sender, c)
		ELSE
			Push(c.user, m, sender); Wake(c)
		END
	END Post;

	PROCEDURE (c: Context) AddWatcher* (w: Ref);
		VAR m: WatchMsg;
	BEGIN
		ASSERT(w # NIL, 20);
		NEW(m); m.watcher := w; PostSys(c, m)
	END AddWatcher;

	PROCEDURE (c: Context) RemoveWatcher* (w: Ref);
		VAR m: UnwatchMsg;
	BEGIN
		ASSERT(w # NIL, 20);
		IF c.state # stopped THEN NEW(m); m.watcher := w; PostSys(c, m) END
	END RemoveWatcher;

	PROCEDURE (c: Context) Stop*;
	BEGIN
		PostSys(c, stopS)
	END Stop;

	(* --- адрес ответа: не останавливается и не наблюдает ----------------- *)

	PROCEDURE (f: Future) Post (m: Msg; sender: Ref);
	BEGIN
		ASSERT(m # NIL, 20);
		f.box.Send(m)
	END Post;

	PROCEDURE (f: Future) AddWatcher (w: Ref);
	BEGIN
		ASSERT(w # NIL, 20)
	END AddWatcher;

	PROCEDURE (f: Future) RemoveWatcher (w: Ref);
	BEGIN
		ASSERT(w # NIL, 20)
	END RemoveWatcher;

	PROCEDURE (f: Future) Stop;
	END Stop;

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
		IF c.sender # NIL THEN c.sender.Post(m, c) ELSE Dead(m, NIL, NIL) END
	END Respond;

	PROCEDURE (c: Context) Parent* (): Ref, NEW;
	(** Пост: адрес родителя; NIL — у c нет родителя. *)
	BEGIN
		RETURN c.parent
	END Parent;

	PROCEDURE (c: Context) Watch* (r: Ref), NEW;
	(** Пред: r # NIL (трап 20). Пост: после остановки r обработчик c
		получит Terminated с who = r один раз; повторный Watch того же r
		ничего не добавляет; r уже остановлен — получит сразу. Остановка c
		снимает его наблюдения. *)
	BEGIN
		ASSERT(r # NIL, 20);
		Add(c.watching, r); r.AddWatcher(c)
	END Watch;

	PROCEDURE (c: Context) Unwatch* (r: Ref), NEW;
	(** Пред: r # NIL (трап 20). Пост: c не получит Terminated от r, даже
		посланный раньше Unwatch. *)
	BEGIN
		ASSERT(r # NIL, 20);
		IF Remove(c.watching, r) THEN r.RemoveWatcher(c) END
	END Unwatch;

	PROCEDURE (c: Context) SetReceiveTimeout* (ms: LONGINT), NEW;
	(** Пред: вызов из обработчика c (трап 20).
		Пост: ms > 0 — после ms мс без пользовательских сообщений
		обработчик получит ReceiveTimeout один раз, следующий отсчёт
		начнёт следующее сообщение; тайм-аут не обгоняет ждущие письма и
		не теряется при приостановке; ms <= 0 — тайм-аута нет.
		Перезапуск снимает тайм-аут. *)
	BEGIN
		ASSERT(InHandler(c), 20);
		c.rtMs := ms; CancelTimer(c);
		IF ms > 0 THEN Touch(c) END
	END SetReceiveTimeout;

	PROCEDURE (c: Context) Become* (b: Behavior), NEW;
	(** Пред: вызов из обработчика c (трап 20).
		Пост: следующие сообщения обрабатывает b.Receive; b = NIL —
		снова Actor.Receive. Перезапуск возвращает Actor.Receive. *)
	BEGIN
		ASSERT(InHandler(c), 20);
		c.beh := b
	END Become;

	PROCEDURE (c: Context) Stash*, NEW;
	(** Отложить письмо в обработке: обработчик сейчас не может его принять.
		Пред: вызов из обработчика c, обрабатывающего пользовательское
		письмо (не шаг жизненного цикла) — трап 20.
		Пост: письмо и его отправитель отложены; Unstash вернёт их в ящик.
		Перезапуск возвращает отложенные в начало ящика, остановка делает
		их мёртвыми письмами. *)
	BEGIN
		ASSERT(InHandler(c) & (c.callKind = callUser) & (c.callMsg # NIL), 20);
		Push(c.stash, c.callMsg, c.sender)
	END Stash;

	PROCEDURE (c: Context) Unstash*, NEW;
	(** Пред: вызов из обработчика c — трап 20.
		Пост: отложенные письма — в начале ящика в порядке Stash, с прежними
		отправителями; следующим обработчик получит первое из них. *)
	BEGIN
		ASSERT(InHandler(c), 20);
		Merge(c)
	END Unstash;

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
		возвращает NIL (26). Трап фабрики — трап вызывающего.
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
		c.actor := p.new(); ASSERT(c.actor # NIL, nilActor);
		c.parent := parent;
		IF parent # NIL THEN
			c.sibNext := parent.first;
			IF parent.first # NIL THEN parent.first.sibPrev := c END;
			parent.first := c; INC(parent.nChildren)
		END;
		c.hnext := table[Hash(c.name)]; table[Hash(c.name)] := c;
		INC(live);
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
	(** Пред: r # NIL (трап 20). Пост: r.Stop — r получит Stop раньше своих
		пользовательских писем; остановленный r не меняется. *)
	BEGIN
		ASSERT(r # NIL, 20);
		r.Stop
	END StopActor;

	PROCEDURE NewFuture (): Future;
	(* Пост: новый адрес ответа "$ask/номер" в реестре asks. *)
		VAR f: Future; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(f); f.node := node$; f.name := "$ask/";
		INC(askSeq); Strings.IntToString(askSeq, s); Append(f.name, s);
		f.box := AsyncChan.NewMailbox();
		f.next := asks; asks := f;
		RETURN f
	END NewFuture;

	PROCEDURE DropFuture (f: Future);
	(* Пост: f нет в реестре asks. *)
		VAR p: Future;
	BEGIN
		IF asks = f THEN asks := f.next
		ELSE
			p := asks;
			WHILE (p # NIL) & (p.next # f) DO p := p.next END;
			IF p # NIL THEN p.next := f.next END
		END;
		f.next := NIL
	END DropFuture;

	PROCEDURE (k: AskCleaner) Cleanup;
	BEGIN
		DropFuture(k.f); k.to.RemoveWatcher(k.f)
	END Cleanup;

	PROCEDURE Ask* (to: Ref; m: Msg; ms: LONGINT; OUT reply: Msg): INTEGER;
	(** Ждёт ответа. Пред: вызов из задачи (трап 20); to # NIL (21);
		m # NIL (22); вызов не из обработчика самого to (23). Отмена
		задачи — трап 127.
		Пост: ok — reply — первое сообщение, посланное адресу ответа
		(Respond адресата); timedOut — за ms мс ответа нет; dead — m стало
		мёртвым письмом или to остановился, не ответив. Кроме ok,
		reply = NIL. Поздний ответ теряется. После возврата и после отмены
		адреса ответа в каталоге нет. *)
		VAR f: Future; ops: ARRAY 2 OF AsyncSelect.Op; res: INTEGER; k: AskCleaner;
	BEGIN
		ASSERT(Tasks.InTask(), 20); ASSERT(to # NIL, 21); ASSERT(m # NIL, 22);
		ASSERT(~(to IS Context) OR ~InHandler(to(Context)), 23);
		f := NewFuture();
		to.AddWatcher(f); to.Post(m, f);
		NEW(k); k.f := f; k.to := to;
		Kernel.PushTrapCleaner(k);	(* отмена в Select снимает адрес ответа *)
		AsyncChan.RecvAlt(f.box, ops[0]); AsyncChan.TimeoutAlt(ms, ops[1]);
		IF AsyncChan.Select(ops) = 0 THEN
			reply := ops[0].got;
			IF (reply = deadM) OR (reply IS Terminated) & (reply(Terminated).who = to) THEN
				reply := NIL; res := dead
			ELSE res := ok
			END
		ELSE
			reply := NIL; res := timedOut
		END;
		Kernel.PopTrapCleaner(k);
		DropFuture(f); to.RemoveWatcher(f);
		RETURN res
	END Ask;

	PROCEDURE InitRef* (r: Ref; IN nd, name: ARRAY OF CHAR);
	(** Для модулей транспорта. Пред: r # NIL (трап 20), nd и name
		короче nameLen (21). Пост: r.node = nd, r.name = name. *)
	BEGIN
		ASSERT(r # NIL, 20); ASSERT((LEN(nd$) < nameLen) & (LEN(name$) < nameLen), 21);
		r.node := nd$; r.name := name$
	END InitRef;

	PROCEDURE Terminate* (w, who: Ref; reason: INTEGER);
	(** Для модулей транспорта: адрес who, не локальный ящик, кончился
		(остановлен на своём узле или узел потерян).
		Пред: w # NIL (трап 20), who # NIL (21), reason — byStop, byFailure
		или byNodeLoss (22).
		Пост: w получил Terminated с этим who и reason, как от остановки
		локального актора; обработчик w получит его, только если наблюдает who. *)
		VAR t: Terminated;
	BEGIN
		ASSERT(w # NIL, 20); ASSERT(who # NIL, 21); ASSERT((reason >= byStop) & (reason <= byNodeLoss), 22);
		NEW(t); t.who := who; t.reason := reason;
		IF w IS Context THEN PostSys(w(Context), t) ELSE w.Post(t, NIL) END
	END Terminate;

	PROCEDURE SetDispatcher* (d: Dispatcher);
	(** Для модулей исполнителей. Пред: d # NIL (трап 20).
		Пост: dispatcher = d; методы Dispatcher, кроме Schedule, работают
		только у d. *)
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

	PROCEDURE SetDeadHook* (h: DeadHook);
	(** Пост: каждое мёртвое письмо с этого момента передаётся h.Dead;
		h = NIL — приёмника нет. Трап приёмника не считается отказом
		актора и не повторяет учёт письма. *)
	BEGIN
		deadHook := h
	END SetDeadHook;

	PROCEDURE Init;
		VAR d: StdDirectory;
	BEGIN
		node := "local";
		NEW(started); NEW(stoppingM); NEW(stoppedM); NEW(restartingM); NEW(timeoutM);
		NEW(startS); NEW(stopS); NEW(resumeS); NEW(restartS); NEW(newS); NEW(deadM);
		NEW(d); SetDir(d)
	END Init;

BEGIN
	Init
END ActorCore.
