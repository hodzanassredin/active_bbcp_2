MODULE Async_devEvents;
(** Тесты событий первого класса: базовые события, комбинаторы, nack,
	отмена и трап, случайные деревья против простой модели. *)

	IMPORT Kernel, Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, UnitAssert;

	CONST
		nSeeds = 100;
		maxNodes = 96;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		SyncJob = POINTER TO RECORD (Tasks.Job)
			e: AsyncEvent.Event; got: AsyncSelect.Msg; done: BOOLEAN
		END;

		CtxSyncJob = POINTER TO RECORD (Tasks.Job)
			e: AsyncEvent.Event; got: AsyncSelect.Msg;
			deadline: LONGINT;
			ok, done: BOOLEAN
		END;

		(* Наблюдатель уведомления — задача с пропуском: отмена в тесте
		   завершает его без трапа. *)
		NackWatch = POINTER TO RECORD (Tasks.Job)
			nack: AsyncEvent.Event; got: AsyncSelect.Msg;
			fired, done: BOOLEAN
		END;

		TagAction = POINTER TO RECORD (AsyncEvent.Action)
			msg: AsyncSelect.Msg
		END;

		DoubleAction = POINTER TO RECORD (AsyncEvent.Action) END;
		PlusAction = POINTER TO RECORD (AsyncEvent.Action) n: INTEGER END;

		CountAction = POINTER TO RECORD (AsyncEvent.Action) count: INTEGER END;

		TrapAction = POINTER TO RECORD (AsyncEvent.Action) END;

		NestedAction = POINTER TO RECORD (AsyncEvent.Action)
			c: AsyncChan.Mailbox
		END;

		CountBuilder = POINTER TO RECORD (AsyncEvent.Builder)
			e: AsyncEvent.Event; count: INTEGER
		END;

		NilBuilder = POINTER TO RECORD (AsyncEvent.Builder) END;
		TrapBuilder = POINTER TO RECORD (AsyncEvent.Builder) END;

		(* Сигнал, который зажигает тест: Arm запоминает ctx решения. *)
		LaterSignal = POINTER TO RECORD (AsyncSelect.Signal)
			ready: BOOLEAN; ctx: AsyncSelect.Ctx; idx: INTEGER
		END;

		(* --- модель случайного дерева -------------------------------------- *)

		Node = POINTER TO RECORD
			kind: INTEGER;	(* 0 recv, 1 always, 2 never, 3 timeout,
								4 choose, 5 wrap, 6 guard, 7 withNack *)
			kids: ARRAY 2 OF Node;
			nk: INTEGER;
			id: INTEGER;
			box: AsyncChan.Mailbox;
			ev, nack: AsyncEvent.Event;
			applied: INTEGER;	(* сработало действие Wrap *)
			built: INTEGER;		(* построено Guard *)
			builder: NodeBuilder
		END;

		NodeBuilder = POINTER TO RECORD (AsyncEvent.Builder) node: Node END;
		NodeAction = POINTER TO RECORD (AsyncEvent.Action) node: Node END;

	VAR
		nodeSeq: INTEGER;
		nodes: ARRAY maxNodes OF Node;
		nNodes: INTEGER;

		leaves: ARRAY maxNodes OF Node;
		leafPath: ARRAY maxNodes, maxNodes OF BOOLEAN;
		nLeaves: INTEGER;

		watches: ARRAY maxNodes OF NackWatch;
		watchNode: ARRAY maxNodes OF Node;
		nWatches: INTEGER;

		(* покрытие случайных деревьев *)
		statNackFired, statNackSilent: INTEGER;
		statWrapOn, statWrapOff, statGuard, statUnitWin: INTEGER;

	PROCEDURE NewNum (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END NewNum;

	PROCEDURE Tag (n: INTEGER): AsyncSelect.Msg;
	BEGIN
		RETURN NewNum(n)
	END Tag;

	PROCEDURE NumOf (m: AsyncSelect.Msg): INTEGER;
	BEGIN
		IF m = NIL THEN RETURN -9999 END;
		WITH m: NumMsg DO RETURN m.n ELSE RETURN -9998 END
	END NumOf;

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE StepUntil (t: Tasks.Task; st: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.State(t) # st) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.State(t) = st
	END StepUntil;

	PROCEDURE WaitJob (t: Tasks.Task; ms: LONGINT): BOOLEAN;
	(* Ждать конца именно этой задачи: наблюдатель, который так и не
	   дождался уведомления, остаётся спать и не мешает проверке. *)
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.State(t) < Tasks.Finished) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.State(t) >= Tasks.Finished
	END WaitJob;

	(* --- тела задач -------------------------------------------------------- *)

	PROCEDURE (j: SyncJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.got := AsyncEvent.Sync(j.e); j.done := TRUE
	END Body;

	PROCEDURE (j: CtxSyncJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		IF j.deadline > 0 THEN Tasks.SetDeadline(ctx, j.deadline) END;
		j.ok := AsyncEvent.SyncCtx(ctx, j.e, j.got); j.done := TRUE
	END Body;

	PROCEDURE (w: NackWatch) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		w.fired := AsyncEvent.SyncCtx(ctx, w.nack, w.got); w.done := TRUE
	END Body;

	(* --- действия и построители ------------------------------------------- *)

	PROCEDURE (a: TagAction) Do (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		y := a.msg
	END Do;

	PROCEDURE (a: DoubleAction) Do (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: NumMsg;
	BEGIN
		WITH x: NumMsg DO NEW(r); r.n := 2 * x.n; y := r ELSE y := x END
	END Do;

	PROCEDURE (a: PlusAction) Do (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: NumMsg;
	BEGIN
		WITH x: NumMsg DO NEW(r); r.n := x.n + a.n; y := r ELSE y := x END
	END Do;

	PROCEDURE (a: CountAction) Do (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		INC(a.count); y := x
	END Do;

	PROCEDURE (a: TrapAction) Do (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		HALT(60)
	END Do;

	PROCEDURE (a: NestedAction) Do (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	(* Действие само синхронизируется: вложенный Sync не портит
	   продолжение внешнего. *)
	BEGIN
		y := AsyncEvent.Sync(AsyncEvent.Recv(a.c))
	END Do;

	PROCEDURE (b: CountBuilder) Make (OUT e: AsyncEvent.Event);
	BEGIN
		INC(b.count); e := b.e
	END Make;

	PROCEDURE (b: NilBuilder) Make (OUT e: AsyncEvent.Event);
	BEGIN
		e := NIL
	END Make;

	PROCEDURE (b: TrapBuilder) Make (OUT e: AsyncEvent.Event);
	BEGIN
		HALT(60)
	END Make;

	PROCEDURE (s: LaterSignal) Arm (ctx: AsyncSelect.Ctx; idx: INTEGER): BOOLEAN;
	BEGIN
		s.ctx := ctx; s.idx := idx; RETURN s.ready
	END Arm;

	PROCEDURE (s: LaterSignal) Disarm;
	BEGIN
		s.ctx := NIL
	END Disarm;

	(* --- вспомогательное --------------------------------------------------- *)

	PROCEDURE NewTagAction (n: INTEGER): TagAction;
		VAR a: TagAction;
	BEGIN
		NEW(a); a.msg := Tag(n);
		RETURN a
	END NewTagAction;

	PROCEDURE StartSync (e: AsyncEvent.Event; OUT j: SyncJob);
	BEGIN
		NEW(j); j.e := e; j.got := NIL; j.done := FALSE; Tasks.Start(j)
	END StartSync;

	PROCEDURE StartCtxSync (e: AsyncEvent.Event; OUT j: CtxSyncJob);
	BEGIN
		NEW(j); j.e := e; j.got := NIL; j.deadline := 0;
		j.ok := FALSE; j.done := FALSE; Tasks.Start(j)
	END StartCtxSync;

	PROCEDURE StartWatch (OUT w: NackWatch; nack: AsyncEvent.Event);
	BEGIN
		NEW(w); w.nack := nack; w.got := NIL; w.fired := FALSE; w.done := FALSE;
		Tasks.Start(w); Tasks.Step
	END StartWatch;

	PROCEDURE StopWatch (w: NackWatch);
	(* Убрать наблюдателя, который так и не дождался уведомления. *)
	BEGIN
		IF (w # NIL) & (Tasks.State(w) = Tasks.Parked) THEN Tasks.Cancel(w) END
	END StopWatch;

	(* -------------------- базовые события -------------------------------- *)

	PROCEDURE TestAlways*;
		VAR j: SyncJob;
	BEGIN
		StartSync(AsyncEvent.Always(), j);
		UnitAssert.True(WaitJob(j, 1000), "always completes");
		UnitAssert.True(~Tasks.Failed(j), "no trap");
		UnitAssert.True(j.done, "sync returned");
		UnitAssert.True(j.got = NIL, "always result is NIL")
	END TestAlways;

	PROCEDURE TestChooseLeftReady*;
		VAR a, b: AsyncChan.Mailbox; j: SyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
			m: AsyncSelect.Msg;
	BEGIN
		a := AsyncChan.NewMailbox(); b := AsyncChan.NewMailbox();
		a.Send(Tag(1)); b.Send(Tag(2));
		evs[0] := AsyncEvent.Recv(a); evs[1] := AsyncEvent.Recv(b);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "choose completes");
		UnitAssert.True(~Tasks.Failed(j), "no trap");
		UnitAssert.EqualInt(1, NumOf(j.got), "first ready branch wins");
		b.Recv(m);	(* данные есть: ожидания нет, главный стек допустим *)
		UnitAssert.EqualInt(2, NumOf(m), "loser message stays in its channel")
	END TestChooseLeftReady;

	PROCEDURE TestAlwaysBeforeReady*;
		(* Always левее готового приёма: побеждает Always (проверка по порядку). *)
		VAR a: AsyncChan.Mailbox; j: SyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		a := AsyncChan.NewMailbox(); a.Send(Tag(7));
		evs[0] := AsyncEvent.Wrap(AsyncEvent.Always(), NewTagAction(100));
		evs[1] := AsyncEvent.Recv(a);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "choose completes");
		UnitAssert.EqualInt(100, NumOf(j.got), "earlier Always wins")
	END TestAlwaysBeforeReady;

	PROCEDURE TestReadyBeforeAlways*;
		VAR a: AsyncChan.Mailbox; j: SyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		a := AsyncChan.NewMailbox(); a.Send(Tag(7));
		evs[0] := AsyncEvent.Recv(a);
		evs[1] := AsyncEvent.Wrap(AsyncEvent.Always(), NewTagAction(100));
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "choose completes");
		UnitAssert.EqualInt(7, NumOf(j.got), "earlier ready receive wins")
	END TestReadyBeforeAlways;

	PROCEDURE TestTimeoutOrder*;
		(* Готовый приём выигрывает у тайм-аута, даже если тайм-аут левее:
		   так же ведёт себя AsyncSelect.Select. *)
		VAR a: AsyncChan.Mailbox; j: SyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		a := AsyncChan.NewMailbox(); a.Send(Tag(5));
		evs[0] := AsyncEvent.Wrap(AsyncEvent.Timeout(0), NewTagAction(1));
		evs[1] := AsyncEvent.Recv(a);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 2000), "sync completes");
		UnitAssert.EqualInt(5, NumOf(j.got), "ready receive beats timeout")
	END TestTimeoutOrder;

	PROCEDURE TestSendRecv*;
		(* Отправка событием доставляется, результат отправки — NIL. *)
		VAR c: AsyncChan.Chan; j: SyncJob; d: SyncJob;
			evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		c := AsyncChan.NewChan();
		StartSync(AsyncEvent.Recv(c), j);
		Tasks.Step;
		UnitAssert.True(Tasks.State(j) = Tasks.Parked, "receiver waits");
		StartSync(AsyncEvent.Send(c, Tag(42)), d);
		UnitAssert.True(WaitJob(j, 1000) & WaitJob(d, 1000), "exchange completes");
		UnitAssert.EqualInt(42, NumOf(j.got), "message received");
		UnitAssert.True(d.got = NIL, "send result is NIL")
	END TestSendRecv;

	(* -------------------- Wrap и Guard ----------------------------------- *)

	PROCEDURE TestWrap*;
		VAR a: AsyncChan.Mailbox; j: SyncJob; d: DoubleAction;
	BEGIN
		a := AsyncChan.NewMailbox(); a.Send(Tag(5));
		NEW(d);
		StartSync(AsyncEvent.Wrap(AsyncEvent.Recv(a), d), j);
		UnitAssert.True(WaitJob(j, 1000), "wrap completes");
		UnitAssert.EqualInt(10, NumOf(j.got), "action applied to result")
	END TestWrap;

	PROCEDURE TestWrapOrder*;
		(* Внутреннее действие раньше внешнего: (5 * 2) + 1 = 11. *)
		VAR a: AsyncChan.Mailbox; j: SyncJob; d: DoubleAction; p: PlusAction;
	BEGIN
		a := AsyncChan.NewMailbox(); a.Send(Tag(5));
		NEW(d); NEW(p); p.n := 1;
		StartSync(AsyncEvent.Wrap(
			AsyncEvent.Wrap(AsyncEvent.Recv(a), d), p), j);
		UnitAssert.True(WaitJob(j, 1000), "wrapped sync completes");
		UnitAssert.EqualInt(11, NumOf(j.got), "inner wrap applied first")
	END TestWrapOrder;

	PROCEDURE TestNestedSync*;
		(* Действие Wrap зовёт Sync: вложенная синхронизация допустима. *)
		VAR a, b: AsyncChan.Mailbox; j: SyncJob; na: NestedAction;
	BEGIN
		a := AsyncChan.NewMailbox(); b := AsyncChan.NewMailbox();
		a.Send(Tag(1)); b.Send(Tag(2));
		NEW(na); na.c := b;
		StartSync(AsyncEvent.Wrap(AsyncEvent.Recv(a), na), j);
		UnitAssert.True(WaitJob(j, 1000), "nested sync completes");
		UnitAssert.True(~Tasks.Failed(j), "no trap");
		UnitAssert.EqualInt(2, NumOf(j.got), "inner result passed out")
	END TestNestedSync;

	PROCEDURE TestWrapOnlyWinner*;
		(* Действия проигравшей ветви не исполняются. *)
		VAR a: AsyncChan.Mailbox; j: SyncJob; l, w: CountAction;
			evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		a := AsyncChan.NewMailbox(); a.Send(Tag(3));
		NEW(l); l.count := 0; NEW(w); w.count := 0;
		evs[0] := AsyncEvent.Wrap(AsyncEvent.Recv(AsyncChan.NewMailbox()), l);
		evs[1] := AsyncEvent.Wrap(AsyncEvent.Recv(a), w);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "choose completes");
		UnitAssert.EqualInt(3, NumOf(j.got), "ready branch won");
		UnitAssert.EqualInt(0, l.count, "loser action not run");
		UnitAssert.EqualInt(1, w.count, "winner action ran once")
	END TestWrapOnlyWinner;

	PROCEDURE TestGuardOnce*;
		(* Guard строит событие при каждой синхронизации; до неё — ни разу. *)
		VAR a: AsyncChan.Mailbox; b: CountBuilder; g: AsyncEvent.Event;
			j: SyncJob; i: INTEGER;
	BEGIN
		NEW(b); b.e := AsyncEvent.Always(); b.count := 0;
		g := AsyncEvent.Guard(b);
		UnitAssert.EqualInt(0, b.count, "builder not called before sync");
		FOR i := 1 TO 2 DO
			a := AsyncChan.NewMailbox(); a.Send(Tag(i));
			StartSync(g, j);
			UnitAssert.True(WaitJob(j, 1000), "guard sync completes");
			UnitAssert.EqualInt(i, b.count, "builder called once per sync")
		END
	END TestGuardOnce;

	PROCEDURE TestGuardNILTrap*;
		(* Make вернул NIL — трап 22 в задаче. *)
		VAR j: SyncJob; b: NilBuilder;
	BEGIN
		NEW(b);
		StartSync(AsyncEvent.Guard(b), j);
		UnitAssert.True(WaitJob(j, 1000), "task ends");
		UnitAssert.True(Tasks.Failed(j), "NIL from Make traps");
		UnitAssert.True(~j.done, "sync did not return")
	END TestGuardNILTrap;

	(* -------------------- WithNack --------------------------------------- *)

	PROCEDURE TestNackLoser*;
		(* Готова вторая ветвь: её nack молчит, nack проигравшей — срабатывает. *)
		VAR a, b: AsyncChan.Mailbox; j: SyncJob;
			na, nb: AsyncEvent.Event; wa, wb: NackWatch;
			ea, eb: AsyncEvent.Event; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		a := AsyncChan.NewMailbox(); b := AsyncChan.NewMailbox(); b.Send(Tag(2));
		ea := AsyncEvent.WithNack(AsyncEvent.Recv(a), na);
		eb := AsyncEvent.WithNack(AsyncEvent.Recv(b), nb);
		StartWatch(wa, na); StartWatch(wb, nb);
		evs[0] := ea; evs[1] := eb;
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "sync finishes");
		Tasks.Step; Tasks.Step;
		UnitAssert.EqualInt(2, NumOf(j.got), "second branch won");
		UnitAssert.True(wa.fired, "loser nack fired");
		UnitAssert.True(~wb.fired, "winner nack silent");
		StopWatch(wb); IF Pump(500) THEN END
	END TestNackLoser;

	PROCEDURE TestNackWinnerSilent*;
		VAR a: AsyncChan.Mailbox; nack: AsyncEvent.Event; w: NackWatch;
			j: SyncJob; evs: ARRAY 1 OF AsyncEvent.Event;
	BEGIN
		a := AsyncChan.NewMailbox(); a.Send(Tag(4));
		evs[0] := AsyncEvent.WithNack(AsyncEvent.Recv(a), nack);
		StartWatch(w, nack);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "sync finishes");
		Tasks.Step;
		UnitAssert.EqualInt(4, NumOf(j.got), "receive won");
		UnitAssert.True(~w.fired, "winner nack silent");
		StopWatch(w); IF Pump(500) THEN END
	END TestNackWinnerSilent;

	PROCEDURE TestNackNested*;
		(* Вложенный выбор: nack приходит только ветви, внутри которой нет
		   победителя; обе проигравшие ветви получают свой nack. *)
		VAR b1, b2, b3: AsyncChan.Mailbox;
			e1, e2, e3: AsyncEvent.Event; n1, n2, n3: AsyncEvent.Event;
			w1, w2, w3: NackWatch; inner, root: ARRAY 2 OF AsyncEvent.Event;
			j: SyncJob;
	BEGIN
		b1 := AsyncChan.NewMailbox(); b2 := AsyncChan.NewMailbox(); b3 := AsyncChan.NewMailbox();
		b3.Send(Tag(3));
		e1 := AsyncEvent.WithNack(AsyncEvent.Recv(b1), n1);
		e2 := AsyncEvent.WithNack(AsyncEvent.Recv(b2), n2);
		e3 := AsyncEvent.WithNack(AsyncEvent.Recv(b3), n3);
		inner[0] := e1; inner[1] := e2;
		root[0] := AsyncEvent.Choose(inner); root[1] := e3;
		StartWatch(w1, n1); StartWatch(w2, n2); StartWatch(w3, n3);
		StartSync(AsyncEvent.Choose(root), j);
		UnitAssert.True(WaitJob(j, 1000), "sync finishes");
		Tasks.Step; Tasks.Step;
		UnitAssert.EqualInt(3, NumOf(j.got), "deep branch won");
		UnitAssert.True(w1.fired & w2.fired, "both losers of nested choose nacked");
		UnitAssert.True(~w3.fired, "winner's nack silent");
		StopWatch(w3); IF Pump(500) THEN END
	END TestNackNested;

	PROCEDURE TestNackWithoutWaiterLost*;
		(* Событие-уведомление без памяти: зажжённое без ожидающего теряется. *)
		VAR a, b: AsyncChan.Mailbox; nack: AsyncEvent.Event; w: NackWatch;
			j: SyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		a := AsyncChan.NewMailbox(); b := AsyncChan.NewMailbox(); b.Send(Tag(2));
		evs[0] := AsyncEvent.WithNack(AsyncEvent.Recv(a), nack);
		evs[1] := AsyncEvent.Recv(b);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "first sync finishes");
		(* никто не ждал nack в этот момент *)
		StartWatch(w, nack);
		UnitAssert.True(~w.fired, "no waiter at fire time — nack lost");
		StopWatch(w); IF Pump(500) THEN END
	END TestNackWithoutWaiterLost;

	PROCEDURE TestNackTwoWaiters*;
		(* Одно уведомление будит всех, кто ждёт его в этот момент. *)
		VAR a, b: AsyncChan.Mailbox; nack: AsyncEvent.Event;
			w1, w2: NackWatch; j: SyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		a := AsyncChan.NewMailbox(); b := AsyncChan.NewMailbox(); b.Send(Tag(2));
		evs[0] := AsyncEvent.WithNack(AsyncEvent.Recv(a), nack);
		evs[1] := AsyncEvent.Recv(b);
		StartWatch(w1, nack); StartWatch(w2, nack);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "sync finishes");
		Tasks.Step; Tasks.Step;
		UnitAssert.True(w1.fired & w2.fired, "both waiters woken")
	END TestNackTwoWaiters;

	(* -------------------- отмена и трап ---------------------------------- *)

	PROCEDURE TestNackOnCancel*;
		(* SyncCtx остановлен отменой: все ветви WithNack получают nack. *)
		VAR c: AsyncChan.Chan; nack: AsyncEvent.Event; w: NackWatch;
			j: CtxSyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		c := AsyncChan.NewChan();
		evs[0] := AsyncEvent.WithNack(AsyncEvent.Recv(c), nack);
		evs[1] := AsyncEvent.Never();
		StartWatch(w, nack);
		StartCtxSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(StepUntil(j, Tasks.Parked, 1000), "sync waits");
		Tasks.Cancel(j);
		UnitAssert.True(Pump(1000), "cancelled task and watcher finish");
		UnitAssert.True(j.done, "SyncCtx returned");
		UnitAssert.True(~j.ok, "sync reported stop");
		UnitAssert.True(j.got = NIL, "no result after stop");
		UnitAssert.True(w.fired, "nack fired on cancellation")
	END TestNackOnCancel;

	PROCEDURE TestNackOnCancelSync*;
		(* Отмена задачи в Sync (без пропуска): трап 127 прерывает Select,
		   уведомления проигравших зажигаются до передачи трапа. *)
		VAR c: AsyncChan.Chan; nack: AsyncEvent.Event; w: NackWatch;
			j: SyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		c := AsyncChan.NewChan();
		evs[0] := AsyncEvent.WithNack(AsyncEvent.Recv(c), nack);
		evs[1] := AsyncEvent.Never();
		StartWatch(w, nack);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(StepUntil(j, Tasks.Parked, 1000), "sync waits");
		Tasks.Cancel(j);
		UnitAssert.True(Pump(1000), "cancelled task and watcher finish");
		UnitAssert.True(Tasks.State(j) = Tasks.Cancelled, "cancel ends the task as cancelled");
		UnitAssert.True(~j.done, "Sync did not return");
		UnitAssert.True(w.fired, "nack fired on cancellation")
	END TestNackOnCancelSync;

	PROCEDURE TestNackOnDeadline*;
		(* Срок пропуска остановил SyncCtx: проигравшие получают nack. *)
		VAR c: AsyncChan.Chan; nack: AsyncEvent.Event; w: NackWatch;
			j: CtxSyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		c := AsyncChan.NewChan();
		evs[0] := AsyncEvent.WithNack(AsyncEvent.Recv(c), nack);
		evs[1] := AsyncEvent.Never();
		StartWatch(w, nack);
		StartCtxSync(AsyncEvent.Choose(evs), j);
		j.deadline := 20;
		UnitAssert.True(WaitJob(j, 2000), "deadline ends the sync");
		Tasks.Step;
		UnitAssert.True(j.done & ~j.ok, "SyncCtx reported stop");
		UnitAssert.True(w.fired, "nack fired on deadline")
	END TestNackOnDeadline;

	PROCEDURE TestNackOnTrapInCollect*;
		(* Трап в Guard при сборе: nack уже собранной ветви срабатывает. *)
		VAR nack: AsyncEvent.Event; w: NackWatch; j: SyncJob; b: TrapBuilder;
			evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		NEW(b);
		evs[0] := AsyncEvent.WithNack(AsyncEvent.Never(), nack);
		evs[1] := AsyncEvent.Guard(b);
		StartWatch(w, nack);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(Pump(1000), "trapped sync ends");
		UnitAssert.True(Tasks.Failed(j), "trap reached the task");
		UnitAssert.True(~j.done, "sync did not return");
		UnitAssert.True(w.fired, "nack fired on trap")
	END TestNackOnTrapInCollect;

	PROCEDURE TestNackOnTrapInWrap*;
		(* Победившая ветвь отмечена до действий Wrap: трап в действии не
		   делает её проигравшей; проигравшая ветвь получает nack. *)
		VAR na, nb: AsyncEvent.Event; wa, wb: NackWatch; j: SyncJob; ta: TrapAction;
			evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		NEW(ta);
		evs[0] := AsyncEvent.WithNack(
			AsyncEvent.Wrap(AsyncEvent.Always(), ta), na);
		evs[1] := AsyncEvent.WithNack(AsyncEvent.Never(), nb);
		StartWatch(wa, na); StartWatch(wb, nb);
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "trapped sync ends");
		Tasks.Step;
		UnitAssert.True(Tasks.Failed(j), "trap reached the task");
		UnitAssert.True(~wa.fired, "winner committed before its wrap trapped");
		UnitAssert.True(wb.fired, "loser nacked");
		StopWatch(wa); IF Pump(500) THEN END
	END TestNackOnTrapInWrap;

	PROCEDURE SyncOutside;
		(* Синхронизация без задачи: путь через Select должен трапать. *)
	BEGIN
		IF AsyncEvent.Sync(AsyncEvent.Always()) = NIL THEN END
	END SyncOutside;

	PROCEDURE TestSyncOutsideTaskTraps*;
	BEGIN
		UnitAssert.Traps(SyncOutside, "sync outside a task traps")
	END TestSyncOutsideTaskTraps;

	PROCEDURE TestTooManyLeaves*;
		(* Больше 32 атомарных операций в одной синхронизации — трап 23. *)
		CONST k = 33;
		VAR evs: ARRAY k OF AsyncEvent.Event; j: SyncJob; c: AsyncChan.Mailbox;
			i: INTEGER;
	BEGIN
		c := AsyncChan.NewMailbox();
		FOR i := 0 TO k - 1 DO evs[i] := AsyncEvent.Recv(c) END;
		StartSync(AsyncEvent.Choose(evs), j);
		UnitAssert.True(WaitJob(j, 1000), "task ends");
		UnitAssert.True(Tasks.Failed(j), "too many leaves trap");
		UnitAssert.True(~j.done, "sync did not return")
	END TestTooManyLeaves;

	PROCEDURE TestNoStaleOffer*;
		(* Тайм-аут выиграл: предложение приёма снято. Поздняя отправка
		   ждёт получателя, а не находит брошенное предложение. *)
		VAR c: AsyncChan.Chan; j1, j2: SyncJob; evs: ARRAY 2 OF AsyncEvent.Event;
		BEGIN
			c := AsyncChan.NewChan();
			evs[0] := AsyncEvent.Recv(c);
			evs[1] := AsyncEvent.Wrap(AsyncEvent.Timeout(0), NewTagAction(9));
			StartSync(AsyncEvent.Choose(evs), j1);
			UnitAssert.True(WaitJob(j1, 2000), "timeout sync ends");
			UnitAssert.EqualInt(9, NumOf(j1.got), "timeout won");
			StartSync(AsyncEvent.Recv(c), j2);
			Tasks.Step;
			UnitAssert.True(Tasks.State(j2) = Tasks.Parked, "late receiver waits");
			StartSync(AsyncEvent.Send(c, Tag(77)), j1);
			UnitAssert.True(WaitJob(j2, 1000) & WaitJob(j1, 1000), "late exchange completes");
			UnitAssert.EqualInt(77, NumOf(j2.got), "message delivered to the new receiver")
		END TestNoStaleOffer;

	(* -------------------- Signal ----------------------------------------- *)

	PROCEDURE TestSignal*;
		VAR s: LaterSignal; j: SyncJob;
	BEGIN
		NEW(s); s.ready := FALSE; s.ctx := NIL;
		StartSync(AsyncEvent.Signal(s), j);
		UnitAssert.True(StepUntil(j, Tasks.Parked, 1000), "signal waits");
		s.ready := TRUE;
		AsyncSelect.SignalReady(s.ctx, s.idx);
		UnitAssert.True(WaitJob(j, 1000), "signal sync finishes");
		UnitAssert.True(j.done, "sync returned");
		UnitAssert.True(j.got = NIL, "signal result is NIL")
	END TestSignal;

	(* -------------------- случайные деревья ------------------------------ *)

	PROCEDURE Rand (VAR seed: LONGINT): INTEGER;
	BEGIN
		seed := (seed * 1103515245 + 12345) MOD 1073741824;
		RETURN SHORT(seed DIV 65536)
	END Rand;

	PROCEDURE NewNode (kind: INTEGER): Node;
		VAR n: Node;
	BEGIN
		ASSERT(nNodes < maxNodes, 100);
		NEW(n); n.kind := kind; n.nk := 0; n.id := nodeSeq; INC(nodeSeq);
		n.box := NIL; n.ev := NIL; n.nack := NIL; n.applied := 0; n.built := 0;
		n.builder := NIL;
		nodes[nNodes] := n; INC(nNodes);
		RETURN n
	END NewNode;

	PROCEDURE Gen (VAR seed: LONGINT; depth: INTEGER): Node;
		VAR n: Node; r, k: INTEGER;
	BEGIN
		r := Rand(seed);
		IF depth >= 3 THEN k := r MOD 4 ELSE k := r MOD 8 END;
		CASE k OF
		| 0, 1: n := NewNode(0)
		| 2: n := NewNode(1)
		| 3: n := NewNode(2)
		| 4: n := NewNode(3)
		| 5:
			n := NewNode(4);
			n.nk := 2; n.kids[0] := Gen(seed, depth + 1); n.kids[1] := Gen(seed, depth + 1)
		| 6:
			n := NewNode(5);
			n.nk := 1; n.kids[0] := Gen(seed, depth + 1)
		| 7:
			IF Rand(seed) MOD 2 = 0 THEN
				n := NewNode(6); n.nk := 1; n.kids[0] := Gen(seed, depth + 1)
			ELSE
				n := NewNode(7); n.nk := 1; n.kids[0] := Gen(seed, depth + 1)
			END
		END;
		RETURN n
	END Gen;

	PROCEDURE HasReady (n: Node): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		IF (n.kind = 0) OR (n.kind = 1) OR (n.kind = 3) THEN RETURN TRUE END;
		FOR i := 0 TO n.nk - 1 DO
			IF HasReady(n.kids[i]) THEN RETURN TRUE END
		END;
		RETURN FALSE
	END HasReady;

	PROCEDURE ForceReady (n: Node);
		VAR i: INTEGER;
	BEGIN
		IF n.nk = 0 THEN
			IF n.kind = 2 THEN n.kind := 1 END
		ELSE
			FOR i := 0 TO n.nk - 1 DO ForceReady(n.kids[i]) END
		END
	END ForceReady;

	PROCEDURE (b: NodeBuilder) Make (OUT e: AsyncEvent.Event);
	BEGIN
		INC(b.node.built); e := b.node.kids[0].ev
	END Make;

	PROCEDURE (a: NodeAction) Do (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		INC(a.node.applied); y := x
	END Do;

	PROCEDURE Build (n: Node);
		VAR evs: ARRAY 2 OF AsyncEvent.Event; b: NodeBuilder;
			a: NodeAction; m: NumMsg;
	BEGIN
		CASE n.kind OF
		| 0:
			n.box := AsyncChan.NewMailbox();
			NEW(m); m.n := n.id; n.box.Send(m);
			n.ev := AsyncEvent.Recv(n.box)
		| 1: n.ev := AsyncEvent.Always()
		| 2: n.ev := AsyncEvent.Never()
		| 3: n.ev := AsyncEvent.Timeout(0)
		| 4:
			Build(n.kids[0]); Build(n.kids[1]);
			evs[0] := n.kids[0].ev; evs[1] := n.kids[1].ev;
			n.ev := AsyncEvent.Choose(evs)
		| 5:
			Build(n.kids[0]);
			NEW(a); a.node := n;
			n.ev := AsyncEvent.Wrap(n.kids[0].ev, a)
		| 6:
			Build(n.kids[0]);
			NEW(b); b.node := n;
			n.ev := AsyncEvent.Guard(b)
		| 7:
			Build(n.kids[0]);
			n.ev := AsyncEvent.WithNack(n.kids[0].ev, n.nack)
		END
	END Build;

	PROCEDURE Flatten (n: Node; VAR path: ARRAY OF BOOLEAN; VAR cnt: INTEGER);
		VAR i, j: INTEGER;
	BEGIN
		path[n.id] := TRUE;
		IF n.nk = 0 THEN
			ASSERT(cnt < maxNodes, 100);
			leaves[cnt] := n;
			FOR j := 0 TO maxNodes - 1 DO leafPath[cnt, j] := path[j] END;
			INC(cnt)
		ELSE
			FOR i := 0 TO n.nk - 1 DO Flatten(n.kids[i], path, cnt) END
		END;
		path[n.id] := FALSE
	END Flatten;

	PROCEDURE ModelWinner (): INTEGER;
		(* Готовый приём или Always — по порядку; иначе первый тайм-аут. *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nLeaves - 1 DO
			IF (leaves[i].kind = 0) OR (leaves[i].kind = 1) THEN RETURN i END
		END;
		FOR i := 0 TO nLeaves - 1 DO
			IF leaves[i].kind = 3 THEN RETURN i END
		END;
		RETURN -1
	END ModelWinner;

	PROCEDURE OnPath (n: Node; leaf: INTEGER): BOOLEAN;
	BEGIN
		RETURN leafPath[leaf, n.id]
	END OnPath;

	PROCEDURE RunSeed (seed: LONGINT): BOOLEAN;
		VAR root: Node; i, wi, cnt: INTEGER; path: ARRAY maxNodes OF BOOLEAN;
			j: SyncJob; wantTag, gotTag: INTEGER; wantNack, gotNack: BOOLEAN;
			ok, fail: BOOLEAN;
	BEGIN
		fail := FALSE;
		nodeSeq := 0; nNodes := 0; nLeaves := 0; nWatches := 0;
		root := Gen(seed, 0);
		IF ~HasReady(root) THEN ForceReady(root) END;
		Build(root);
		FOR i := 0 TO maxNodes - 1 DO path[i] := FALSE END;
		cnt := 0; Flatten(root, path, cnt); nLeaves := cnt;
		wi := ModelWinner();
		IF wi < 0 THEN RETURN FALSE END;
		(* наблюдатели за всеми уведомлениями *)
		FOR i := 0 TO nNodes - 1 DO
			IF nodes[i].kind = 7 THEN
				StartWatch(watches[nWatches], nodes[i].nack);
				watchNode[nWatches] := nodes[i]; INC(nWatches)
			END
		END;
		StartSync(root.ev, j);
		ok := WaitJob(j, 2000);
		Tasks.Step; Tasks.Step;	(* разбуженные наблюдатели *)
		IF ~ok THEN fail := TRUE END;
		IF Tasks.Failed(j) OR ~j.done THEN fail := TRUE END;
		IF leaves[wi].kind = 0 THEN wantTag := leaves[wi].id ELSE wantTag := -9999 END;
		gotTag := NumOf(j.got);
		IF gotTag # wantTag THEN fail := TRUE END;
		IF leaves[wi].kind # 0 THEN INC(statUnitWin) END;
		(* действия Wrap — только на пути победителя *)
		FOR i := 0 TO nNodes - 1 DO
			IF nodes[i].kind = 5 THEN
				IF OnPath(nodes[i], wi) THEN
					INC(statWrapOn);
					IF nodes[i].applied # 1 THEN fail := TRUE END
				ELSE
					INC(statWrapOff);
					IF nodes[i].applied # 0 THEN fail := TRUE END
				END
			END
		END;
		(* Guard построен ровно один раз за синхронизацию *)
		FOR i := 0 TO nNodes - 1 DO
			IF nodes[i].kind = 6 THEN
				INC(statGuard);
				IF nodes[i].built # 1 THEN fail := TRUE END
			END
		END;
		(* nack — ровно проигравшим ветвям *)
		FOR i := 0 TO nWatches - 1 DO
			wantNack := ~OnPath(watchNode[i], wi);
			gotNack := watches[i].fired;
			IF wantNack THEN INC(statNackFired) ELSE INC(statNackSilent) END;
			IF wantNack # gotNack THEN fail := TRUE END;
			StopWatch(watches[i])
		END;
		IF Pump(500) THEN END;
		RETURN ~fail
	END RunSeed;

	PROCEDURE TestRandomTrees*;
	(* Случайное дерево по зерну; модель: первый готовый лист в порядке
	   обхода, иначе первый тайм-аут; nack — всем, чья ветвь не содержит
	   победителя; Guard строится один раз, Wrap победителя — один раз. *)
		VAR seed: LONGINT; i: INTEGER; ok: BOOLEAN;
	BEGIN
		statNackFired := 0; statNackSilent := 0;
		statWrapOn := 0; statWrapOff := 0; statGuard := 0; statUnitWin := 0;
		seed := 1;
		FOR i := 1 TO nSeeds DO
			ok := RunSeed(seed);
			UnitAssert.True(ok, "random tree matches the model");
			seed := seed * 31 + 7
		END;
		(* Деревья обязаны покрывать все случаи, иначе проверка пустая. *)
		UnitAssert.True(statNackFired > 0, "random trees fire nacks");
		UnitAssert.True(statNackSilent > 0, "random trees keep winner nacks silent");
		UnitAssert.True(statWrapOn > 0, "random trees run winner wraps");
		UnitAssert.True(statWrapOff > 0, "random trees skip loser wraps");
		UnitAssert.True(statGuard > 0, "random trees build guards");
		UnitAssert.True(statUnitWin > 0, "random trees let unit events win")
	END TestRandomTrees;

END Async_devEvents.
