MODULE AsyncSelect;
(** Выбор из нескольких ожиданий (Alt/Select) по образцу Hopac/CML.

	Предложение (offer) — запись операции над «ядром» канала (Core).
	Все предложения одного Select делят флаг решения (Ctx). Первый канал,
	готовый исполнить предложение, под своим монитором ставит флаг и
	будит задачу через Tasks.Unpark; проигравшие предложения снимает
	владелец (nack).

	Встреча — ограниченный канал ёмкости 0: отправка без получателя
	ждёт предложением в очереди waitS, приём без отправителя — в waitR.

	Инвариант: предложение не исполняется против предложения с тем же
	Ctx (Select не встречается сам с собой).

	Только одна нить ОС: все переходы состояния — под монитором ядра,
	между проверкой и Park нет точки уступки. *)

	IMPORT Kernel, Monitors, Tasks, Services, Console, AsyncProto;

	CONST
		maxAlt = 32;

		(** вид операции **)
		SendKind* = 0; RecvKind* = 1; TimeoutKind* = 2; SignalKind* = 3;
		(** вид ядра **)
		Rendezvous* = 0; Bounded* = 1; Mailbox* = 2;

	TYPE
		(** Сообщение канала. После отправки не меняется (дисциплина
			Clojure/Erlang): получателю достаётся копия или передача
			владения, но не общий изменяемый объект. *)
		Msg* = POINTER TO ABSTRACT RECORD END;

		Ctx* = POINTER TO RECORD
			task: Tasks.Task;
			nextFree: Ctx;	(* связь свободного списка *)
			decided: BOOLEAN;
			ready: BOOLEAN;	(* регистрация окончена, можно будить *)
			winner: INTEGER	(* индекс альтернативы; -1 — ещё нет *)
		END;

		Offer* = POINTER TO RECORD
			next: Offer;
			ctx: Ctx;
			idx: INTEGER;
			kind: INTEGER;
			refused: BOOLEAN;	(* отправка отвергнута протоколом; трап — у отправителя *)
			msg, got: Msg
		END;

		Core* = POINTER TO EXTENSIBLE RECORD
			kind*: INTEGER;
			capacity*: INTEGER;
			count*: INTEGER;		(* сообщений в кольце *)
			waitS*, waitR*: Offer;	(* головы очередей ожидающих *)
			waitST, waitRT: Offer;
			ring: POINTER TO ARRAY OF Msg;	(* очередь Bounded/Mailbox — кольцо *)
			rh: INTEGER;			(* индекс головы кольца *)
			proto*: AsyncProto.Protocol;	(* необязательный протокол диалога *)
			protoState*: INTEGER
		END;

		(** Внешний сигнал: Alt, который будит не канал, а владелец
			(например, готовность Pull компутона). *)
		Signal* = POINTER TO ABSTRACT RECORD END;

		(** Альтернатива Select. *)
		Op* = RECORD
			kind*: INTEGER;
			core*: Core;
			msg*: Msg;		(* для отправки *)
			got*: Msg;		(* результат приёма *)
			sig*: Signal;
			ms*: LONGINT	(* для тайм-аута *)
		END;

		Timer = POINTER TO RECORD (Services.Action) ctx: Ctx; idx: INTEGER END;

	(* Инвариант кольца: ring = NIL равносильно count = 0 до первого
	   сообщения; иначе сообщения лежат в ring[(rh + i) MOD LEN(ring)],
	   0 <= i < count <= LEN(ring), остальные ячейки — NIL. *)

	CONST
		ringMin = 16;	(* начальная длина кольца почтового ящика *)

	VAR
		(* Свободные записи решений и предложений: Select и ожидание Buffer
		   не выделяют памяти. Инвариант: запись свободного списка не лежит
		   ни в одной очереди ядра канала и не связана с таймером или сигналом. *)
		freeCtx: Ctx;
		freeOffers: Offer;

	PROCEDURE (s: Signal) Arm* (ctx: Ctx; idx: INTEGER): BOOLEAN, NEW, ABSTRACT;
	(** TRUE — сигнал уже готов; иначе владелец позовёт SignalReady. *)

	PROCEDURE (s: Signal) Disarm*, NEW, ABSTRACT;

	(* --- вспомогательное -------------------------------------------------- *)

	PROCEDURE TaskDone (t: Tasks.Task): BOOLEAN;
	BEGIN
		RETURN (t = NIL) OR (Tasks.State(t) IN {Tasks.Finished, Tasks.Cancelled})
	END TaskDone;

	PROCEDURE OfferAlive (o: Offer): BOOLEAN;
	BEGIN
		RETURN (o # NIL) & (o.ctx # NIL) & ~o.ctx.decided & ~TaskDone(o.ctx.task)
	END OfferAlive;

	PROCEDURE Wake (ctx: Ctx);
	(** Будить, только когда регистрация альтернатив окончена и задача
		действительно спит в Tasks.Park. Пока идёт регистрация, задача
		может стоять в Kernel.Lock на мониторе другого канала; Tasks.Unpark
		перевёл бы её в Ready без монитора, и Lock трапнул бы «woken
		without the monitor». Такая задача сама увидит ctx.decided,
		получив монитор. *)
	BEGIN
		IF (ctx # NIL) & ctx.ready THEN Tasks.Unpark(ctx.task) END
	END Wake;

	PROCEDURE Decide (ctx: Ctx; idx: INTEGER);
	BEGIN
		IF (ctx # NIL) & ~ctx.decided THEN
			ctx.decided := TRUE; ctx.winner := idx;
			Wake(ctx)
		END
	END Decide;

	PROCEDURE SignalReady* (ctx: Ctx; idx: INTEGER);
	(** Владелец сигнала: альтернатива готова. *)
	BEGIN
		Decide(ctx, idx)
	END SignalReady;

	PROCEDURE CtxTask* (ctx: Ctx): Tasks.Task;
	BEGIN
		RETURN ctx.task
	END CtxTask;

	(* --- протокол диалога ------------------------------------------------- *)

	PROCEDURE SetProtocol* (c: Core; p: AsyncProto.Protocol);
	(** Прикрепить протокол диалога: каждая прямая отправка проверяется
		(отладочный режим). NIL снимает протокол. *)
	BEGIN
		c.proto := p;
		IF p # NIL THEN c.protoState := AsyncProto.Start(p) ELSE c.protoState := 0 END
	END SetProtocol;

	PROCEDURE MessageLabel (m: Msg; OUT label: Kernel.Name);
	(** Вид сообщения — имя его динамического типа записи. В описании
		протокола — имя без служебного суффикса "^" (так записан тип
		указателя в метаданных BlackBox). *)
		VAR t: Kernel.Type; i: INTEGER;
	BEGIN
		IF m = NIL THEN label := "NIL"; RETURN END;
		t := Kernel.TypeOf(m^);
		IF t # NIL THEN Kernel.GetTypeName(t, label) ELSE label := "?" END;
		i := 0; WHILE (label[i] # 0X) DO INC(i) END;
		IF (i > 0) & (label[i-1] = "^") THEN label[i-1] := 0X END
	END MessageLabel;

	PROCEDURE SendAllowed (c: Core; m: Msg): BOOLEAN;
	(* Шаг протокола для сообщения m. Пост: TRUE — состояние протокола
	   продвинуто; FALSE — состояние прежнее, текст нарушения выведен и
	   доступен через AsyncProto.LastViolation. Без протокола — TRUE. *)
		VAR label: Kernel.Name; ok: BOOLEAN; msg: AsyncProto.Message;
	BEGIN
		IF c.proto = NIL THEN RETURN TRUE END;
		MessageLabel(m, label);
		AsyncProto.Step(c.proto, c.protoState, label$, ok);
		IF ~ok THEN
			AsyncProto.LastViolation(c.proto, msg);
			Console.WriteStr("AsyncChan protocol: "); Console.WriteStr(msg$); Console.WriteLn
		END;
		RETURN ok
	END SendAllowed;

	PROCEDURE CheckSend* (c: Core; m: Msg);
	(** Проверить отправку по протоколу; нарушение — трап 21 у вызывающего. *)
	BEGIN
		IF ~SendAllowed(c, m) THEN ASSERT(FALSE, 21) END
	END CheckSend;

	PROCEDURE Refuse (o: Offer);
	(* Пред: o — предложение-отправка, снятое с очереди; протокол отверг
	   его сообщение. Пост: решение принято, отправитель разбужен и трапует
	   сам (трап 21): нарушение — ошибка отправителя, а не задачи, которая
	   исполняет его предложение. *)
	BEGIN
		o.refused := TRUE; Decide(o.ctx, o.idx)
	END Refuse;

	PROCEDURE Enqueue (VAR head, tail: Offer; o: Offer);
	BEGIN
		o.next := NIL;
		IF tail = NIL THEN head := o ELSE tail.next := o END;
		tail := o
	END Enqueue;

	PROCEDURE RemoveOffer (VAR head, tail: Offer; o: Offer);
		VAR p, q: Offer;
	BEGIN
		p := NIL; q := head;
		WHILE (q # NIL) & (q # o) DO p := q; q := q.next END;
		IF q = o THEN
			IF p = NIL THEN head := q.next ELSE p.next := q.next END;
			IF tail = q THEN tail := p END;
			q.next := NIL
		END
	END RemoveOffer;

	PROCEDURE ReceiverWaiting (c: Core; except: Ctx): BOOLEAN;
	(* Есть живое предложение-приём, чьё решение не except. Под монитором;
	   между проверкой и PopLive нет точки уступки. *)
		VAR o: Offer;
	BEGIN
		o := c.waitR;
		WHILE (o # NIL) & ~(OfferAlive(o) & (o.ctx # except)) DO o := o.next END;
		RETURN o # NIL
	END ReceiverWaiting;

	PROCEDURE PopLive (VAR head, tail: Offer; except: Ctx): Offer;
	(* Снять первое живое предложение, чьё решение не except; мёртвые
	   предложения по пути удаляются. Под монитором.
	   Инвариант цикла: p — предшественник o в очереди (NIL — o голова),
	   до o нет подходящих предложений. *)
		VAR p, o, nx: Offer;
	BEGIN
		p := NIL; o := head;
		WHILE (o # NIL) & ~(OfferAlive(o) & (o.ctx # except)) DO
			nx := o.next;
			IF OfferAlive(o) THEN p := o
			ELSE
				IF p = NIL THEN head := nx ELSE p.next := nx END;
				IF tail = o THEN tail := p END;
				o.next := NIL
			END;
			o := nx
		END;
		IF o # NIL THEN
			IF p = NIL THEN head := o.next ELSE p.next := o.next END;
			IF tail = o THEN tail := p END;
			o.next := NIL
		END;
		RETURN o
	END PopLive;

	(* --- очередь сообщений ------------------------------------------------ *)

	PROCEDURE GrowRing (c: Core);
	(* Пред: кольцо полно или не создано. Пост: в кольце есть свободная
	   ячейка, порядок сообщений сохранён, rh = 0. *)
		VAR new: POINTER TO ARRAY OF Msg; i, n: INTEGER;
	BEGIN
		IF c.ring = NIL THEN n := 0 ELSE n := LEN(c.ring^) END;
		ASSERT(c.count = n, 20);
		IF n = 0 THEN
			IF c.kind = Bounded THEN NEW(new, c.capacity) ELSE NEW(new, ringMin) END
		ELSE
			NEW(new, 2 * n);
			FOR i := 0 TO n - 1 DO new[i] := c.ring[(c.rh + i) MOD n] END
		END;
		c.ring := new; c.rh := 0
	END GrowRing;

	PROCEDURE AppendItem (c: Core; m: Msg);
	(* Пред: для Bounded count < capacity. Пост: m — последнее в очереди. *)
	BEGIN
		IF (c.ring = NIL) OR (c.count = LEN(c.ring^)) THEN GrowRing(c) END;
		c.ring[(c.rh + c.count) MOD LEN(c.ring^)] := m;
		c.count := c.count + 1
	END AppendItem;

	PROCEDURE PopItem (c: Core; OUT m: Msg): BOOLEAN;
	BEGIN
		IF c.count = 0 THEN m := NIL; RETURN FALSE END;
		m := c.ring[c.rh]; c.ring[c.rh] := NIL;
		c.rh := (c.rh + 1) MOD LEN(c.ring^);
		c.count := c.count - 1;
		RETURN TRUE
	END PopItem;

	PROCEDURE CompleteSend (c: Core; except: Ctx);
	(* Освободилось место: исполнить ждущие предложения-отправки, чьё
	   решение не except, пока в кольце есть место. Отвергнутые протоколом
	   будятся с отказом.
	   Инвариант цикла: s — снятое живое предложение-отправка, в кольце
	   есть место. *)
		VAR s: Offer;
	BEGIN
		IF c.count < c.capacity THEN s := PopLive(c.waitS, c.waitST, except) ELSE s := NIL END;
		WHILE s # NIL DO
			IF SendAllowed(c, s.msg) THEN	(* отложенная отправка совершается здесь *)
				AppendItem(c, s.msg); Decide(s.ctx, s.idx)
			ELSE
				Refuse(s)
			END;
			IF c.count < c.capacity THEN s := PopLive(c.waitS, c.waitST, except) ELSE s := NIL END
		END
	END CompleteSend;

	PROCEDURE Put (c: Core; m: Msg; except: Ctx; OUT refused: BOOLEAN): BOOLEAN;
	(* Пред: под монитором; except — решение отправителя (NIL у прямой
	   отправки). Пост: TRUE — отправка совершена: m отдано ждущему
	   получателю или положено в кольцо, протокол продвинут; FALSE и
	   refused — протокол отверг m, канал и протокол прежние; FALSE и
	   ~refused — места нет, канал не изменён. *)
		VAR r: Offer; ready: BOOLEAN;
	BEGIN
		refused := FALSE;
		ready := ReceiverWaiting(c, except);
		IF ~ready & (c.kind # Mailbox) & (c.count >= c.capacity) THEN RETURN FALSE END;
		IF ~SendAllowed(c, m) THEN refused := TRUE; RETURN FALSE END;
		IF ready THEN
			r := PopLive(c.waitR, c.waitRT, except);
			r.got := m; Decide(r.ctx, r.idx)
		ELSE
			AppendItem(c, m)
		END;
		RETURN TRUE
	END Put;

	PROCEDURE Take (c: Core; except: Ctx; OUT m: Msg): BOOLEAN;
	(* Пред: под монитором; except — решение получателя (NIL у прямого
	   приёма). Пост: TRUE — m взято из кольца или у ждущего отправителя,
	   чьё решение не except; FALSE — брать нечего, m = NIL. Отправители,
	   чьё сообщение протокол отверг, разбужены с отказом.
	   Инвариант цикла: s — снятое живое предложение-отправка. *)
		VAR s: Offer;
	BEGIN
		IF PopItem(c, m) THEN CompleteSend(c, except); RETURN TRUE END;
		(* кольцо пусто: ждущий отправитель бывает только у встречи *)
		s := PopLive(c.waitS, c.waitST, except);
		WHILE (s # NIL) & ~SendAllowed(c, s.msg) DO
			Refuse(s); s := PopLive(c.waitS, c.waitST, except)
		END;
		IF s = NIL THEN RETURN FALSE END;
		m := s.msg; Decide(s.ctx, s.idx);	(* отправка предложения совершается здесь *)
		RETURN TRUE
	END Take;

	(* --- регистрация альтернатив ------------------------------------------ *)

	PROCEDURE Register (c: Core; o: Offer): BOOLEAN;
	(* Исполнить предложение или поставить его в очередь. Под монитором.
	   Протокол проверяется в тот момент, когда отправка действительно
	   совершается: здесь или при снятии отложенного предложения в
	   Take/CompleteSend. Проигравшее предложение состояние протокола не
	   меняет. Пост: TRUE — предложение исполнено; FALSE — оно в очереди. *)
		VAR refused: BOOLEAN;
	BEGIN
		IF o.kind = SendKind THEN
			IF Put(c, o.msg, o.ctx, refused) THEN RETURN TRUE END;
			ASSERT(~refused, 21);
			Enqueue(c.waitS, c.waitST, o)
		ELSE
			IF Take(c, o.ctx, o.got) THEN RETURN TRUE END;
			Enqueue(c.waitR, c.waitRT, o)
		END;
		RETURN FALSE
	END Register;

	PROCEDURE Withdraw (c: Core; o: Offer);
	(* Снять предложение в конце Select; идемпотентно. Под монитором. *)
	BEGIN
		RemoveOffer(c.waitS, c.waitST, o);
		RemoveOffer(c.waitR, c.waitRT, o)
	END Withdraw;

	(* --- тайм-аут --------------------------------------------------------- *)

	PROCEDURE (a: Timer) Do;
	BEGIN
		IF ~a.ctx.decided THEN
			a.ctx.decided := TRUE; a.ctx.winner := a.idx;
			Wake(a.ctx)
		END
	END Do;

	(* --- альтернативы ----------------------------------------------------- *)

	PROCEDURE AltSend* (core: Core; msg: Msg; OUT op: Op);
	BEGIN
		op.kind := SendKind; op.core := core; op.msg := msg; op.got := NIL;
		op.sig := NIL; op.ms := 0
	END AltSend;

	PROCEDURE AltRecv* (core: Core; OUT op: Op);
	BEGIN
		op.kind := RecvKind; op.core := core; op.msg := NIL; op.got := NIL;
		op.sig := NIL; op.ms := 0
	END AltRecv;

	PROCEDURE AltTimeout* (ms: LONGINT; OUT op: Op);
	BEGIN
		op.kind := TimeoutKind; op.core := NIL; op.msg := NIL; op.got := NIL;
		op.sig := NIL; op.ms := ms
	END AltTimeout;

	PROCEDURE AltSignal* (sig: Signal; OUT op: Op);
	BEGIN
		op.kind := SignalKind; op.core := NIL; op.msg := NIL; op.got := NIL;
		op.sig := sig; op.ms := 0
	END AltSignal;

	(* --- записи решений и предложений ------------------------------------ *)

	PROCEDURE TakeCtx (): Ctx;
	(* Пост: решение текущей задачи, не принято, регистрация не окончена. *)
		VAR ctx: Ctx;
	BEGIN
		IF freeCtx # NIL THEN ctx := freeCtx; freeCtx := ctx.nextFree; ctx.nextFree := NIL
		ELSE NEW(ctx)
		END;
		ctx.task := Tasks.Current(); ctx.decided := FALSE; ctx.ready := FALSE; ctx.winner := -1;
		RETURN ctx
	END TakeCtx;

	PROCEDURE FreeCtx (ctx: Ctx);
	(* Пред: на ctx не ссылаются предложения в очередях, таймер и сигналы. *)
	BEGIN
		ctx.task := NIL; ctx.nextFree := freeCtx; freeCtx := ctx
	END FreeCtx;

	PROCEDURE TakeOffer (ctx: Ctx; idx, kind: INTEGER; msg: Msg): Offer;
	(* Пост: предложение вне очередей. *)
		VAR o: Offer;
	BEGIN
		IF freeOffers # NIL THEN o := freeOffers; freeOffers := o.next ELSE NEW(o) END;
		o.next := NIL; o.ctx := ctx; o.idx := idx; o.kind := kind; o.refused := FALSE;
		o.msg := msg; o.got := NIL;
		RETURN o
	END TakeOffer;

	PROCEDURE FreeOffer (o: Offer);
	(* Пред: o снято со всех очередей. *)
	BEGIN
		o.ctx := NIL; o.msg := NIL; o.got := NIL; o.next := freeOffers; freeOffers := o
	END FreeOffer;

	(* --- ожидание одной операции очереди ---------------------------------- *)

	PROCEDURE Enter (c: Core): BOOLEAN;
	(* Операция очереди между двумя точками ожидания исполняется без
	   переключений: одна нить и вытеснения нет, поэтому исключение
	   обеспечено и без монитора. Монитор берётся, только если его держит
	   кто-то (например, задача, взявшая Kernel.Lock на канал): тогда
	   операция ждёт его освобождения. Пост: TRUE — монитор взят. *)
	BEGIN
		IF Monitors.IsLocked(c) THEN Kernel.Lock(c); RETURN TRUE ELSE RETURN FALSE END
	END Enter;

	PROCEDURE Leave (c: Core; locked: BOOLEAN);
	BEGIN
		IF locked THEN Kernel.Unlock(c) END
	END Leave;

	PROCEDURE WaitOne (c: Core; o: Offer; locked: BOOLEAN);
	(* Пред: операция над c начата Enter (locked — его результат), o —
	   предложение текущей задачи, не исполненное. Пост: монитор отпущен,
	   o исполнено другой стороной и снято с очереди. *)
	BEGIN
		IF o.kind = SendKind THEN Enqueue(c.waitS, c.waitST, o) ELSE Enqueue(c.waitR, c.waitRT, o) END;
		Leave(c, locked);
		o.ctx.ready := TRUE;
		WHILE ~o.ctx.decided DO Tasks.Park END
	END WaitOne;

	PROCEDURE QueueSend* (c: Core; m: Msg);
	(** Отправить: ждущему получателю или в кольцо; иначе ждать получателя
		или места. Ждущий отправитель — предложение в очереди waitS:
		получатель забирает его сообщение и будит задачу без передачи
		монитора, поэтому отправитель не стоит на входе в монитор, пока
		получатель работает. Нарушение протокола — трап 21. *)
		VAR ctx: Ctx; o: Offer; locked, refused: BOOLEAN;
	BEGIN
		locked := Enter(c);
		IF Put(c, m, NIL, refused) OR refused THEN
			Leave(c, locked)
		ELSE
			ctx := TakeCtx(); o := TakeOffer(ctx, 0, SendKind, m);
			WaitOne(c, o, locked);	(* протокол проверяется в момент отправки *)
			refused := o.refused;
			FreeOffer(o); FreeCtx(ctx)
		END;
		ASSERT(~refused, 21)
	END QueueSend;

	PROCEDURE QueueRecv* (c: Core; OUT m: Msg);
	(** Принять; нет отправителя или данных — ждать. Данные или ждущий
		отправитель уже есть — ожидания нет, годится и главный стек. *)
		VAR ctx: Ctx; o: Offer; locked: BOOLEAN;
	BEGIN
		locked := Enter(c);
		IF Take(c, NIL, m) THEN
			Leave(c, locked)
		ELSE
			ctx := TakeCtx(); o := TakeOffer(ctx, 0, RecvKind, NIL);
			WaitOne(c, o, locked);
			m := o.got;
			FreeOffer(o); FreeCtx(ctx)
		END
	END QueueRecv;

	(* --- Select ----------------------------------------------------------- *)

	PROCEDURE Select* (VAR ops: ARRAY OF Op): INTEGER;
	(** Дождаться ровно одной альтернативы; вернуть её индекс.
		Результат приёма — ops[i].got. Только из задачи. *)
		VAR ctx: Ctx; offers: ARRAY maxAlt OF Offer;
			i, n, tIdx, winner: INTEGER; tMs: LONGINT; timer: Timer;
			done, signals, locked, refused: BOOLEAN;
	BEGIN
		ASSERT(Tasks.InTask(), 20);
		n := LEN(ops); ASSERT(n <= maxAlt, 21);
		tIdx := -1; tMs := -1;
		ctx := TakeCtx(); signals := FALSE; timer := NIL;
		FOR i := 0 TO n - 1 DO offers[i] := NIL END;
		i := 0;
		WHILE (i < n) & ~ctx.decided DO
			IF ops[i].kind = TimeoutKind THEN
				IF (tIdx < 0) OR (ops[i].ms < tMs) THEN tIdx := i; tMs := ops[i].ms END
			ELSIF ops[i].kind = SignalKind THEN
				signals := TRUE;
				IF ops[i].sig # NIL THEN
					IF ops[i].sig.Arm(ctx, i) THEN ctx.decided := TRUE; ctx.winner := i END
				END
			ELSE
				offers[i] := TakeOffer(ctx, i, ops[i].kind, ops[i].msg);
				locked := Enter(ops[i].core);
				(* решение могло быть принято, пока мы ждали монитор
				   другого канала: второй альтернативы не исполняем *)
				IF ~ctx.decided THEN
					IF Register(ops[i].core, offers[i]) THEN
						ctx.decided := TRUE; ctx.winner := i
					END
				END;
				Leave(ops[i].core, locked)
			END;
			INC(i)
		END;
		IF ~ctx.decided & (tIdx >= 0) & (tMs <= 0) THEN
			ctx.decided := TRUE; ctx.winner := tIdx
		END;
		IF ~ctx.decided THEN
			IF tIdx >= 0 THEN
				NEW(timer); timer.ctx := ctx; timer.idx := tIdx;
				Services.DoLater(timer, Services.Ticks() + tMs)
			END;
			ctx.ready := TRUE;
			WHILE ~ctx.decided DO Tasks.Park END;
			IF timer # NIL THEN Services.RemoveAction(timer) END
		END;
		(* снять проигравшие (и победителя — Withdraw идемпотентен) *)
		FOR i := 0 TO n - 1 DO
			IF offers[i] # NIL THEN
				locked := Enter(ops[i].core);
				Withdraw(ops[i].core, offers[i]);
				Leave(ops[i].core, locked)
			ELSIF (ops[i].kind = SignalKind) & (ops[i].sig # NIL) THEN
				ops[i].sig.Disarm
			END
		END;
		winner := ctx.winner;
		done := (0 <= winner) & (winner < n);
		refused := done & (offers[winner] # NIL) & offers[winner].refused;
		IF done THEN
			IF (ops[winner].kind = RecvKind) & (offers[winner] # NIL) THEN
				ops[winner].got := offers[winner].got
			END
		END;
		FOR i := 0 TO n - 1 DO
			IF offers[i] # NIL THEN FreeOffer(offers[i]) END
		END;
		IF ~signals & (timer = NIL) THEN FreeCtx(ctx) END;	(* сигнал может помнить ctx *)
		ASSERT(~refused, 21);	(* протокол отверг отложенную отправку этой задачи *)
		RETURN winner
	END Select;

END AsyncSelect.
