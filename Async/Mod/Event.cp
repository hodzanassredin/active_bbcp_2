MODULE AsyncEvent;
(** События первого класса по образцу Concurrent ML (Reppy) и Hopac.

	Событие — значение, которое описывает, чего ждать; Sync разворачивает
	дерево событий в один плоский AsyncSelect.Select и исполняет ровно одну
	базовую операцию. Базовые события: Send, Recv, Timeout, Signal, Always,
	Never. Комбинаторы: Choose (вложенный, произвольной глубины), Wrap
	(действие после срабатывания), Guard (построение события при
	синхронизации), WithNack (уведомление проигравшей ветви).

	Sync не меняет ядро задач и каналов: дерево событий опускается до
	альтернатив AsyncSelect.Op, а решение принимает AsyncSelect.Select.
	Поэтому на одну синхронизацию приходится не больше 32 атомарных
	операций — столько же, сколько у Select.

	Nack — событие без памяти, как в CML: WithNack возвращает событие и
	событие-уведомление; уведомление срабатывает в тот момент, когда
	защищённая ветвь проиграла выбор или синхронизация прервана отменой
	либо трапом. Если в этот момент уведомление никто не ждёт, оно теряется
	(в Hopac Promise помнит результат; здесь сознательно семантика CML).

	Синхронизация без Wrap и WithNack памяти не выделяет: записи шагов и
	уведомлений берутся из свободных списков, альтернативы лежат на стеке. *)

	IMPORT S := SYSTEM, Kernel, Tasks, AsyncSelect;

	CONST
		maxAlt = 32;	(* столько же альтернатив принимает AsyncSelect.Select *)

		(* вид шага продолжения *)
		wrapStep = 0; commitStep = 1;

	TYPE
		(** Событие: значение, которое можно синхронизировать. *)
		Event* = POINTER TO EXTENSIBLE RECORD END;

		(** Действие Wrap: получает результат события и возвращает новый. *)
		Action* = POINTER TO ABSTRACT RECORD END;

		(** Построитель Guard: зовётся при каждой синхронизации. *)
		Builder* = POINTER TO ABSTRACT RECORD END;

		(* Шаг продолжения победившей ветви: действие Wrap или отметка
		   «ветвь выбрана» для WithNack. Шаги идут от листа к корню. *)
		Step = POINTER TO RECORD
			next: Step;		(* внешний шаг *)
			scan: Step;		(* все шаги одной синхронизации *)
			kind: INTEGER;
			act: Action;
			token: Token
		END;

		(* Отметка одной ветви WithNack в одной синхронизации. *)
		Token = POINTER TO RECORD
			next: Token;
			nack: NackEvent;
			committed: BOOLEAN
		END;

		(* Состояние одной синхронизации. Лежит на стеке Sync/SyncCtx и
		   переживает Kernel.Try: RunBody пишет сюда, Execute читает. *)
		SyncState = RECORD
			ops: ARRAY maxAlt OF AsyncSelect.Op;
			steps: ARRAY maxAlt OF Step;
			n: INTEGER;			(* собрано листьев *)
			overflow: BOOLEAN;	(* листьев больше maxAlt *)
			tokens: Token;		(* отметки WithNack *)
			allSteps: Step;		(* все шаги: вернуть в свободный список *)
			armed: NackSignal;	(* вооружённые уведомления этой синхронизации *)
			winner: INTEGER;	(* индекс победившей альтернативы; -1 — остановка *)
			stopped: BOOLEAN;
			done: BOOLEAN;		(* RunBody дошёл до конца, трапа не было *)
			result: AsyncSelect.Msg
		END;

		CtxPtr = POINTER TO Tasks.Ctx;
		SyncPtr = POINTER TO SyncState;

		(* --- базовые события ------------------------------------------------- *)

		SendEvent = POINTER TO RECORD (Event)
			core: AsyncSelect.Core;
			msg: AsyncSelect.Msg
		END;

		RecvEvent = POINTER TO RECORD (Event) core: AsyncSelect.Core END;

		TimeoutEvent = POINTER TO RECORD (Event) ms: LONGINT END;

		SignalEvent = POINTER TO RECORD (Event) sig: AsyncSelect.Signal END;

		AlwaysEvent = POINTER TO RECORD (Event) END;
		NeverEvent = POINTER TO RECORD (Event) END;

		(* --- комбинаторы ------------------------------------------------------ *)

		ChooseEvent = POINTER TO RECORD (Event)
			evs: POINTER TO ARRAY OF Event
		END;

		WrapEvent = POINTER TO RECORD (Event) inner: Event; act: Action END;

		GuardEvent = POINTER TO RECORD (Event) builder: Builder END;

		WithNackEvent = POINTER TO RECORD (Event)
			inner: Event;
			nack: NackEvent
		END;

		(* Уведомление о проигрыше: событие-источник, которое зажигает
		   WithNack. Ожидающие — вооружённые сигналы Select. *)
		NackEvent = POINTER TO RECORD (Event)
			waiters: NackSignal
		END;

		(* Один вооружённый сигнал уведомления. Создаётся на каждое
		   вхождение события-уведомления в синхронизацию. *)
		NackSignal = POINTER TO RECORD (AsyncSelect.Signal)
			owner: NackEvent;
			next: NackSignal;	(* список ожидающих у owner *)
			scan: NackSignal;	(* список сигналов одной синхронизации *)
			ctx: AsyncSelect.Ctx;
			idx: INTEGER;
			armed: BOOLEAN
		END;

		(* Always и Never — сигналы без владельца: Arm отвечает сразу. *)
		AlwaysSignal = POINTER TO RECORD (AsyncSelect.Signal) END;
		NeverSignal = POINTER TO RECORD (AsyncSelect.Signal) END;

	VAR
		alwaysSig: AlwaysSignal;
		neverSig: NeverSignal;
		neverOp: AsyncSelect.Op;	(* добивка массива альтернатив *)

		freeSteps: Step;
		freeTokens: Token;
		freeSignals: NackSignal;

	(** Пред: идёт сбор одной синхронизации. Пост: листья e добавлены в
		s.ops[0..s.n), их продолжения записаны в s.steps; s.overflow — листьев
		больше maxAlt. Переопределяется конкретными событиями модуля. *)
	PROCEDURE (e: Event) Collect (VAR s: SyncState; outer: Step), NEW, EXTENSIBLE;
	BEGIN
		ASSERT(FALSE, 100)
	END Collect;

	PROCEDURE (a: Action) Do- (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg), NEW, ABSTRACT;
	(** Пред: ветвь действия победила. Пост: y — результат события. *)

	PROCEDURE (b: Builder) Make- (OUT e: Event), NEW, ABSTRACT;
	(** Пред: идёт синхронизация Guard. Пост: e — событие этой синхронизации. *)

	(* --- свободные списки ------------------------------------------------- *)

	PROCEDURE TakeStep (): Step;
		VAR st: Step;
	BEGIN
		IF freeSteps # NIL THEN st := freeSteps; freeSteps := st.scan
		ELSE NEW(st)
		END;
		st.next := NIL; st.scan := NIL; st.kind := wrapStep; st.act := NIL; st.token := NIL;
		RETURN st
	END TakeStep;

	PROCEDURE PutStep (st: Step);
	BEGIN
		st.next := NIL; st.act := NIL; st.token := NIL;
		st.scan := freeSteps; freeSteps := st
	END PutStep;

	PROCEDURE TakeToken (): Token;
		VAR t: Token;
	BEGIN
		IF freeTokens # NIL THEN t := freeTokens; freeTokens := t.next
		ELSE NEW(t)
		END;
		t.next := NIL; t.nack := NIL; t.committed := FALSE;
		RETURN t
	END TakeToken;

	PROCEDURE PutToken (t: Token);
	BEGIN
		t.nack := NIL; t.next := freeTokens; freeTokens := t
	END PutToken;

	PROCEDURE TakeSignal (): NackSignal;
		VAR sig: NackSignal;
	BEGIN
		IF freeSignals # NIL THEN sig := freeSignals; freeSignals := sig.scan
		ELSE NEW(sig)
		END;
		sig.owner := NIL; sig.next := NIL; sig.scan := NIL;
		sig.ctx := NIL; sig.idx := 0; sig.armed := FALSE;
		RETURN sig
	END TakeSignal;

	PROCEDURE PutSignal (sig: NackSignal);
	BEGIN
		sig.owner := NIL; sig.next := NIL; sig.ctx := NIL; sig.armed := FALSE;
		sig.scan := freeSignals; freeSignals := sig
	END PutSignal;

	(* --- базовые события --------------------------------------------------- *)

	PROCEDURE AddLeaf (VAR s: SyncState; outer: Step; OUT i: INTEGER): BOOLEAN;
	(* Пост: TRUE — i — номер нового листа; FALSE — листьев уже maxAlt. *)
	BEGIN
		IF s.n >= maxAlt THEN s.overflow := TRUE; i := -1; RETURN FALSE END;
		i := s.n; INC(s.n); s.steps[i] := outer;
		RETURN TRUE
	END AddLeaf;

	PROCEDURE (e: SendEvent) Collect (VAR s: SyncState; outer: Step);
		VAR i: INTEGER;
	BEGIN
		IF AddLeaf(s, outer, i) THEN AsyncSelect.AltSend(e.core, e.msg, s.ops[i]) END
	END Collect;

	PROCEDURE (e: RecvEvent) Collect (VAR s: SyncState; outer: Step);
		VAR i: INTEGER;
	BEGIN
		IF AddLeaf(s, outer, i) THEN AsyncSelect.AltRecv(e.core, s.ops[i]) END
	END Collect;

	PROCEDURE (e: TimeoutEvent) Collect (VAR s: SyncState; outer: Step);
		VAR i: INTEGER;
	BEGIN
		IF AddLeaf(s, outer, i) THEN AsyncSelect.AltTimeout(e.ms, s.ops[i]) END
	END Collect;

	PROCEDURE (e: SignalEvent) Collect (VAR s: SyncState; outer: Step);
		VAR i: INTEGER;
	BEGIN
		IF AddLeaf(s, outer, i) THEN AsyncSelect.AltSignal(e.sig, s.ops[i]) END
	END Collect;

	PROCEDURE (e: AlwaysEvent) Collect (VAR s: SyncState; outer: Step);
		VAR i: INTEGER;
	BEGIN
		IF AddLeaf(s, outer, i) THEN AsyncSelect.AltSignal(alwaysSig, s.ops[i]) END
	END Collect;

	PROCEDURE (e: NeverEvent) Collect (VAR s: SyncState; outer: Step);
		VAR i: INTEGER;
	BEGIN
		IF AddLeaf(s, outer, i) THEN AsyncSelect.AltSignal(neverSig, s.ops[i]) END
	END Collect;

	(* --- комбинаторы ------------------------------------------------------- *)

	PROCEDURE (e: ChooseEvent) Collect (VAR s: SyncState; outer: Step);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(e.evs^)) & ~s.overflow DO
			e.evs[i].Collect(s, outer); INC(i)
		END
	END Collect;

	PROCEDURE (e: WrapEvent) Collect (VAR s: SyncState; outer: Step);
		VAR st: Step;
	BEGIN
		st := TakeStep(); st.kind := wrapStep; st.act := e.act; st.next := outer;
		st.scan := s.allSteps; s.allSteps := st;
		e.inner.Collect(s, st)
	END Collect;

	PROCEDURE (e: GuardEvent) Collect (VAR s: SyncState; outer: Step);
		VAR ev: Event;
	BEGIN
		IF s.overflow THEN RETURN END;
		e.builder.Make(ev);
		ASSERT(ev # NIL, 22);
		ev.Collect(s, outer)
	END Collect;

	PROCEDURE (e: WithNackEvent) Collect (VAR s: SyncState; outer: Step);
		VAR st: Step; t: Token;
	BEGIN
		IF s.overflow THEN RETURN END;
		t := TakeToken(); t.nack := e.nack; t.next := s.tokens; s.tokens := t;
		st := TakeStep(); st.kind := commitStep; st.token := t; st.next := outer;
		st.scan := s.allSteps; s.allSteps := st;
		e.inner.Collect(s, st)
	END Collect;

	PROCEDURE (e: NackEvent) Collect (VAR s: SyncState; outer: Step);
	(* Ожидание уведомления — сигнал Select: вооружается вместе с
	   остальными альтернативами и снимается общим снятием. *)
		VAR i: INTEGER; sig: NackSignal;
	BEGIN
		IF ~AddLeaf(s, outer, i) THEN RETURN END;
		sig := TakeSignal(); sig.owner := e;
		sig.scan := s.armed; s.armed := sig;
		AsyncSelect.AltSignal(sig, s.ops[i])
	END Collect;

	(* --- уведомление ------------------------------------------------------- *)

	PROCEDURE (s: NackSignal) Arm (ctx: AsyncSelect.Ctx; idx: INTEGER): BOOLEAN;
	BEGIN
		s.ctx := ctx; s.idx := idx; s.armed := TRUE;
		s.next := s.owner.waiters; s.owner.waiters := s;
		RETURN FALSE
	END Arm;

	PROCEDURE (s: NackSignal) Disarm;
		VAR p, q: NackSignal;
	BEGIN
		IF s.armed THEN
			p := NIL; q := s.owner.waiters;
			WHILE (q # NIL) & (q # s) DO p := q; q := q.next END;
			IF q = s THEN
				IF p = NIL THEN s.owner.waiters := q.next ELSE p.next := q.next END;
				q.next := NIL
			END;
			s.armed := FALSE; s.ctx := NIL
		END
	END Disarm;

	PROCEDURE (e: NackEvent) Fire, NEW;
	(* Зажечь уведомление: разбудить всех, кто ждёт его сейчас. Список
	   ожидающих очищается: событие без памяти. *)
		VAR sig, nx: NackSignal; ctx: AsyncSelect.Ctx; idx: INTEGER;
	BEGIN
		sig := e.waiters; e.waiters := NIL;
		WHILE sig # NIL DO
			nx := sig.next; sig.next := NIL;
			IF sig.armed & (sig.ctx # NIL) THEN
				ctx := sig.ctx; idx := sig.idx; sig.ctx := NIL; sig.armed := FALSE;
				AsyncSelect.SignalReady(ctx, idx)
			END;
			sig := nx
		END
	END Fire;

	(* --- Always и Never ---------------------------------------------------- *)

	PROCEDURE (s: AlwaysSignal) Arm (ctx: AsyncSelect.Ctx; idx: INTEGER): BOOLEAN;
	BEGIN
		RETURN TRUE
	END Arm;

	PROCEDURE (s: AlwaysSignal) Disarm;
	BEGIN
	END Disarm;

	PROCEDURE (s: NeverSignal) Arm (ctx: AsyncSelect.Ctx; idx: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END Arm;

	PROCEDURE (s: NeverSignal) Disarm;
	BEGIN
	END Disarm;

	(* --- сбор и исполнение ------------------------------------------------- *)

	PROCEDURE MarkCommits (st: Step);
	(* Отметить выбранные ветви раньше действий Wrap: трап в действии не
	   делает выбранную ветвь проигравшей. *)
	BEGIN
		WHILE st # NIL DO
			IF st.kind = commitStep THEN st.token.committed := TRUE END;
			st := st.next
		END
	END MarkCommits;

	PROCEDURE FireTokens (VAR s: SyncState);
	(* Зажечь уведомления непомеченных ветвей и вернуть отметки в список. *)
		VAR t, nx: Token;
	BEGIN
		t := s.tokens; s.tokens := NIL;
		WHILE t # NIL DO
			nx := t.next; t.next := NIL;
			IF ~t.committed THEN t.nack.Fire END;
			PutToken(t);
			t := nx
		END
	END FireTokens;

	PROCEDURE RunWraps (st: Step; x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	(* Действия идут от листа к корню: внутреннее Wrap раньше внешнего. *)
		VAR z: AsyncSelect.Msg;
	BEGIN
		y := x;
		WHILE st # NIL DO
			IF st.kind = wrapStep THEN
				z := y; st.act.Do(z, y)
			END;
			st := st.next
		END
	END RunWraps;

	PROCEDURE Apply (VAR s: SyncState; winner: INTEGER);
	(* Пост: отметки WithNack победившей ветви поставлены, уведомления
	   проигравших зажжены, действия Wrap исполнены, s.result — результат. *)
		VAR st: Step;
	BEGIN
		st := s.steps[winner];
		MarkCommits(st);
		FireTokens(s);	(* проигравшие узнают о решении до действий победителя *)
		RunWraps(st, s.ops[winner].got, s.result)
	END Apply;

	PROCEDURE Cleanup (VAR s: SyncState);
	(* Снять вооружённые уведомления и вернуть шаги и сигналы в списки.
	   Токены возвращает FireTokens. *)
		VAR sig, sn: NackSignal; st, stn: Step;
	BEGIN
		sig := s.armed; s.armed := NIL;
		WHILE sig # NIL DO
			sn := sig.scan; sig.scan := NIL;
			sig.Disarm;
			PutSignal(sig);
			sig := sn
		END;
		st := s.allSteps; s.allSteps := NIL;
		WHILE st # NIL DO
			stn := st.scan; st.scan := NIL; st.next := NIL;
			PutStep(st);
			st := stn
		END
	END Cleanup;

	PROCEDURE RunBody (a, b, c: S.ADDRESS);
	(* Тело синхронизации под Kernel.Try. a — адрес SyncState, b — адрес
	   Tasks.Ctx или 0, c — событие. *)
		VAR sp: POINTER TO SyncState; e: Event; ctxp: CtxPtr; i: INTEGER;
	BEGIN
		sp := S.VAL(SyncPtr, a);
		e := S.VAL(Event, c);
		sp.n := 0; sp.overflow := FALSE; sp.tokens := NIL; sp.allSteps := NIL;
		sp.armed := NIL; sp.winner := -1; sp.stopped := FALSE; sp.result := NIL;
		e.Collect(sp^, NIL);
		ASSERT(~sp.overflow, 23);
		ASSERT(sp.n > 0, 100);
		(* Select принимает длину массива, а не число собранных листьев:
		   хвост добиваем Never-сигналами, которые не срабатывают. *)
		FOR i := sp.n TO maxAlt - 1 DO sp.ops[i] := neverOp END;
		IF b = 0 THEN
			sp.winner := AsyncSelect.Select(sp.ops)
		ELSE
			ctxp := S.VAL(CtxPtr, b);
			sp.winner := AsyncSelect.SelectCtx(ctxp^, sp.ops)
		END;
		IF sp.winner >= 0 THEN
			ASSERT(sp.winner < sp.n, 100);
			Apply(sp^, sp.winner)
		ELSE
			sp.stopped := TRUE
		END;
		sp.done := TRUE
	END RunBody;

	PROCEDURE Raise (code: INTEGER);
	(* Передать трап дальше с тем же номером: HALT принимает только
	   константу. *)
	BEGIN
		IF (code < 0) OR (code > 128) THEN HALT(128) END;
		CASE code OF
		| 0: HALT(0) | 1: HALT(1) | 2: HALT(2) | 3: HALT(3) | 4: HALT(4) | 5: HALT(5) | 6: HALT(6) | 7: HALT(7)
		| 8: HALT(8) | 9: HALT(9) | 10: HALT(10) | 11: HALT(11) | 12: HALT(12) | 13: HALT(13) | 14: HALT(14) | 15: HALT(15)
		| 16: HALT(16) | 17: HALT(17) | 18: HALT(18) | 19: HALT(19) | 20: HALT(20) | 21: HALT(21) | 22: HALT(22) | 23: HALT(23)
		| 24: HALT(24) | 25: HALT(25) | 26: HALT(26) | 27: HALT(27) | 28: HALT(28) | 29: HALT(29) | 30: HALT(30) | 31: HALT(31)
		| 32: HALT(32) | 33: HALT(33) | 34: HALT(34) | 35: HALT(35) | 36: HALT(36) | 37: HALT(37) | 38: HALT(38) | 39: HALT(39)
		| 40: HALT(40) | 41: HALT(41) | 42: HALT(42) | 43: HALT(43) | 44: HALT(44) | 45: HALT(45) | 46: HALT(46) | 47: HALT(47)
		| 48: HALT(48) | 49: HALT(49) | 50: HALT(50) | 51: HALT(51) | 52: HALT(52) | 53: HALT(53) | 54: HALT(54) | 55: HALT(55)
		| 56: HALT(56) | 57: HALT(57) | 58: HALT(58) | 59: HALT(59) | 60: HALT(60) | 61: HALT(61) | 62: HALT(62) | 63: HALT(63)
		| 64: HALT(64) | 65: HALT(65) | 66: HALT(66) | 67: HALT(67) | 68: HALT(68) | 69: HALT(69) | 70: HALT(70) | 71: HALT(71)
		| 72: HALT(72) | 73: HALT(73) | 74: HALT(74) | 75: HALT(75) | 76: HALT(76) | 77: HALT(77) | 78: HALT(78) | 79: HALT(79)
		| 80: HALT(80) | 81: HALT(81) | 82: HALT(82) | 83: HALT(83) | 84: HALT(84) | 85: HALT(85) | 86: HALT(86) | 87: HALT(87)
		| 88: HALT(88) | 89: HALT(89) | 90: HALT(90) | 91: HALT(91) | 92: HALT(92) | 93: HALT(93) | 94: HALT(94) | 95: HALT(95)
		| 96: HALT(96) | 97: HALT(97) | 98: HALT(98) | 99: HALT(99) | 100: HALT(100) | 101: HALT(101) | 102: HALT(102) | 103: HALT(103)
		| 104: HALT(104) | 105: HALT(105) | 106: HALT(106) | 107: HALT(107) | 108: HALT(108) | 109: HALT(109) | 110: HALT(110) | 111: HALT(111)
		| 112: HALT(112) | 113: HALT(113) | 114: HALT(114) | 115: HALT(115) | 116: HALT(116) | 117: HALT(117) | 118: HALT(118) | 119: HALT(119)
		| 120: HALT(120) | 121: HALT(121) | 122: HALT(122) | 123: HALT(123) | 124: HALT(124) | 125: HALT(125) | 126: HALT(126) | 127: HALT(127)
		| 128: HALT(128)
		END
	END Raise;

	PROCEDURE Execute (e: Event; ctxAdr: S.ADDRESS; OUT res: AsyncSelect.Msg): INTEGER;
	(* Пред: e # NIL. Пост: исполнена одна ветвь, результат — её индекс;
	   ctxAdr = 0 — без пропуска. −1 — пропуск остановлен. Трап внутри
	   синхронизации передан вызывающему после зажигания уведомлений. *)
		VAR s: SyncState;
	BEGIN
		ASSERT(e # NIL, 20);
		s.done := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(RunBody)),
			S.ADR(s), ctxAdr, S.VAL(S.ADDRESS, e));
		IF ~s.done THEN
			(* Трап: выбранная ветвь уже отмечена, проигравшие получают
			   уведомление, затем трап уходит дальше. *)
			Cleanup(s); FireTokens(s); Raise(Kernel.err)
		END;
		IF s.stopped THEN
			Cleanup(s); FireTokens(s); res := NIL; RETURN -1
		END;
		Cleanup(s);		(* токены уже зажжены в Apply *)
		res := s.result;
		RETURN s.winner
	END Execute;

	(* --- конструкторы ------------------------------------------------------ *)

	PROCEDURE Send* (c: AsyncSelect.Core; m: AsyncSelect.Msg): Event;
	(** Пред: c # NIL — 20. Пост: событие отправки m в c. *)
		VAR e: SendEvent;
	BEGIN
		ASSERT(c # NIL, 20);
		NEW(e); e.core := c; e.msg := m;
		RETURN e
	END Send;

	PROCEDURE Recv* (c: AsyncSelect.Core): Event;
	(** Пред: c # NIL — 20. Пост: событие приёма из c. *)
		VAR e: RecvEvent;
	BEGIN
		ASSERT(c # NIL, 20);
		NEW(e); e.core := c;
		RETURN e
	END Recv;

	PROCEDURE Timeout* (ms: LONGINT): Event;
	(** Пост: событие «истекло ms мс от начала синхронизации»;
		ms <= 0 срабатывает сразу. *)
		VAR e: TimeoutEvent;
	BEGIN
		NEW(e); e.ms := ms;
		RETURN e
	END Timeout;

	PROCEDURE Signal* (sig: AsyncSelect.Signal): Event;
	(** Пред: sig # NIL — 20. Пост: событие готовности sig. *)
		VAR e: SignalEvent;
	BEGIN
		ASSERT(sig # NIL, 20);
		NEW(e); e.sig := sig;
		RETURN e
	END Signal;

	PROCEDURE Always* (): Event;
	(** Пост: событие, готовое сразу; в Choose выигрывает, когда до него
		дошла очередь проверки. *)
		VAR e: AlwaysEvent;
	BEGIN
		NEW(e);
		RETURN e
	END Always;

	PROCEDURE Never* (): Event;
	(** Пост: событие, которое не срабатывает никогда. *)
		VAR e: NeverEvent;
	BEGIN
		NEW(e);
		RETURN e
	END Never;

	PROCEDURE Choose* (events: ARRAY OF Event): Event;
	(** Пред: каждый элемент # NIL — 20; LEN(events) <= 32 (иначе трап 23
		при синхронизации за вычетом вложенных листьев выбора).
		Пост: событие выбора одной из ветвей по порядку: побеждает первая
		готовая. Пустой выбор — Never. *)
		VAR e: ChooseEvent; i: INTEGER;
	BEGIN
		FOR i := 0 TO LEN(events) - 1 DO ASSERT(events[i] # NIL, 20) END;
		IF LEN(events) = 0 THEN RETURN Never() END;
		NEW(e); NEW(e.evs, LEN(events));
		FOR i := 0 TO LEN(events) - 1 DO e.evs[i] := events[i] END;
		RETURN e
	END Choose;

	PROCEDURE Wrap* (e: Event; a: Action): Event;
	(** Пред: e # NIL — 20; a # NIL — 21. Пост: событие e, результат
		которого пропущен через a.Do, если ветвь e победила. *)
		VAR w: WrapEvent;
	BEGIN
		ASSERT(e # NIL, 20); ASSERT(a # NIL, 21);
		NEW(w); w.inner := e; w.act := a;
		RETURN w
	END Wrap;

	PROCEDURE Guard* (b: Builder): Event;
	(** Пред: b # NIL — 21. Пост: событие, которое строится b.Make при
		каждой синхронизации; Make зовётся один раз на вхождение. *)
		VAR e: GuardEvent;
	BEGIN
		ASSERT(b # NIL, 21);
		NEW(e); e.builder := b;
		RETURN e
	END Guard;

	PROCEDURE WithNack* (e: Event; OUT nack: Event): Event;
	(** Пред: e # NIL — 20. Пост: nack — событие-уведомление; оно
		срабатывает, если ветвь e проиграла выбор или синхронизация
		прервана отменой, сроком либо трапом. *)
		VAR w: WithNackEvent;
	BEGIN
		ASSERT(e # NIL, 20);
		NEW(w); w.inner := e; NEW(w.nack);
		nack := w.nack;
		RETURN w
	END WithNack;

	(* --- синхронизация ----------------------------------------------------- *)

	PROCEDURE Sync* (e: Event): AsyncSelect.Msg;
	(** Пред: вызов из задачи — 20 (AsyncSelect.Select); e # NIL — 20;
		не больше 32 атомарных операций в дереве — 23; Make вернул NIL — 22.
		Пост: исполнена ровно одна ветвь, её результат; у приёма — принятое
		сообщение, у отправки, тайм-аута, сигнала, Always и уведомления — NIL. *)
		VAR res: AsyncSelect.Msg;
	BEGIN
		IF Execute(e, 0, res) < 0 THEN ASSERT(FALSE, 100) END;
		RETURN res
	END Sync;

	PROCEDURE SyncCtx* (VAR ctx: Tasks.Ctx; e: Event; OUT x: AsyncSelect.Msg): BOOLEAN;
	(** Пред: вызов из задачи — 20; ctx этой задачи — 25; e # NIL — 20;
		не больше 32 операций — 23; Make вернул NIL — 22.
		Пост: TRUE — исполнена одна ветвь, x — её результат; FALSE — пропуск
		остановлен (отмена или срок) и ни одна ветвь не исполнилась, x = NIL.
		Уведомления WithNack проигравших ветвей зажжены в обоих случаях. *)
		VAR res: AsyncSelect.Msg;
	BEGIN
		IF Execute(e, S.ADR(ctx), res) < 0 THEN
			x := NIL; RETURN FALSE
		END;
		x := res; RETURN TRUE
	END SyncCtx;

BEGIN
	NEW(alwaysSig); NEW(neverSig);
	AsyncSelect.AltSignal(neverSig, neverOp)
END AsyncEvent.
