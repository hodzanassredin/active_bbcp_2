MODULE AsyncSelect;
(** Выбор из нескольких ожиданий (Alt/Select) по образцу Hopac/CML.

	Предложение (offer) — запись операции над «ядром» канала (Core).
	Все предложения одного Select делят флаг решения (Ctx). Первый канал,
	готовый исполнить предложение, под своим монитором ставит флаг и
	будит задачу через Tasks.Unpark; проигравшие предложения снимает
	владелец (nack). Активно положенное предложение отправителя
	откатывается, если решение принял другой канал: это делает любая
	операция, наткнувшаяся на предложение с решённым Ctx (Prune).

	Только одна нить ОС: все переходы состояния — под монитором ядра,
	между проверкой и Park нет точки уступки. *)

	IMPORT Kernel, Tasks, Services, Console, AsyncProto;

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
			decided: BOOLEAN;
			ready: BOOLEAN;	(* регистрация окончена, можно будить *)
			winner: INTEGER	(* индекс альтернативы; -1 — ещё нет *)
		END;

		Item = POINTER TO RItem;

		Offer* = POINTER TO RECORD
			next: Offer;
			ctx: Ctx;
			idx: INTEGER;
			kind: INTEGER;
			msg, got: Msg
		END;

		RItem = RECORD
			next: Item;
			msg: Msg
		END;

		Core* = POINTER TO EXTENSIBLE RECORD
			kind*: INTEGER;
			capacity*: INTEGER;
			count*: INTEGER;		(* для Bounded и Mailbox *)
			item*: Msg;				(* активный вклад Rendezvous *)
			deposit*: BOOLEAN;
			taken*: BOOLEAN;		(* забрали вклад прямого отправителя *)
			depOffer*: Offer;		(* вклад предложения Select *)
			direct*: BOOLEAN;		(* вклад прямого Send *)
			directTask*: Tasks.Task;
			waitS*, waitR*: Offer;	(* головы очередей ожидающих *)
			waitST, waitRT: Offer;
			ih, it: Item;				(* очередь Bounded/Mailbox *)
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

	PROCEDURE CheckSend* (c: Core; m: Msg);
	(** Проверить отправку по протоколу. Без протокола — один тест NIL. *)
		VAR label: Kernel.Name; ok: BOOLEAN; msg: AsyncProto.Message;
	BEGIN
		IF c.proto = NIL THEN RETURN END;
		MessageLabel(m, label);
		AsyncProto.Step(c.proto, c.protoState, label$, ok);
		IF ~ok THEN
			AsyncProto.LastViolation(c.proto, msg);
			Console.WriteStr("AsyncChan protocol: "); Console.WriteStr(msg$); Console.WriteLn;
			ASSERT(FALSE, 21)
		END
	END CheckSend;

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

	PROCEDURE PruneQueue (VAR head, tail: Offer);
		VAR o, nx: Offer;
	BEGIN
		o := head;
		WHILE o # NIL DO
			nx := o.next;
			IF ~OfferAlive(o) THEN RemoveOffer(head, tail, o) END;
			o := nx
		END
	END PruneQueue;

	PROCEDURE Prune* (c: Core);
	(** Убрать протухшие предложения и откатить чужой вклад. Под монитором. *)
	BEGIN
		IF c.deposit THEN
			IF c.depOffer # NIL THEN
				IF ~OfferAlive(c.depOffer) THEN
					c.deposit := FALSE; c.item := NIL; c.depOffer := NIL; c.taken := FALSE
				END
			ELSIF c.direct & TaskDone(c.directTask) THEN
				c.deposit := FALSE; c.item := NIL; c.direct := FALSE;
				c.directTask := NIL; c.taken := FALSE
			END
		END;
		PruneQueue(c.waitS, c.waitST);
		PruneQueue(c.waitR, c.waitRT)
	END Prune;

	PROCEDURE ReceiverWaiting (c: Core): BOOLEAN;
	(** Есть живое предложение-приём в очереди встречи. Под монитором;
		между проверкой и DeliverToReceiver нет точки уступки. *)
		VAR o: Offer;
	BEGIN
		o := c.waitR;
		WHILE o # NIL DO
			IF OfferAlive(o) & (o.kind = RecvKind) THEN RETURN TRUE END;
			o := o.next
		END;
		RETURN FALSE
	END ReceiverWaiting;

	PROCEDURE PopLive (VAR head, tail: Offer; wantSend: BOOLEAN): Offer;
	(** Снять первое живое предложение нужного вида. Под монитором. *)
		VAR o: Offer;
	BEGIN
		LOOP
			o := head;
			IF o = NIL THEN RETURN NIL END;
			head := o.next; o.next := NIL;
			IF head = NIL THEN tail := NIL END;
			IF OfferAlive(o) & ((o.kind = SendKind) = wantSend) THEN RETURN o END
		END
	END PopLive;

	(* --- очередь сообщений ------------------------------------------------ *)

	PROCEDURE AppendItem (c: Core; m: Msg);
		VAR x: Item;
	BEGIN
		NEW(x); x.msg := m; x.next := NIL;
		IF c.it = NIL THEN c.ih := x ELSE c.it.next := x END;
		c.it := x; c.count := c.count + 1
	END AppendItem;

	PROCEDURE PopItem (c: Core; OUT m: Msg): BOOLEAN;
		VAR x: Item;
	BEGIN
		x := c.ih;
		IF x = NIL THEN RETURN FALSE END;
		c.ih := x.next;
		IF c.ih = NIL THEN c.it := NIL END;
		m := x.msg; c.count := c.count - 1;
		RETURN TRUE
	END PopItem;

	PROCEDURE CompleteRecv (c: Core);
	(** Есть данные: исполнить ждущие предложения-приёма. *)
		VAR r: Offer; m: Msg;
	BEGIN
		LOOP
			IF c.kind = Rendezvous THEN
				IF ~c.deposit THEN RETURN END;
				r := PopLive(c.waitR, c.waitRT, FALSE);
				IF r = NIL THEN RETURN END;
				r.got := c.item; c.item := NIL; c.deposit := FALSE;
				IF c.depOffer # NIL THEN
					Decide(c.depOffer.ctx, c.depOffer.idx); c.depOffer := NIL
				ELSE
					c.taken := TRUE
				END;
				Decide(r.ctx, r.idx)
			ELSE
				IF c.count = 0 THEN RETURN END;
				r := PopLive(c.waitR, c.waitRT, FALSE);
				IF r = NIL THEN RETURN END;
				IF ~PopItem(c, m) THEN RETURN END;
				r.got := m;
				Decide(r.ctx, r.idx)
			END
		END
	END CompleteRecv;

	PROCEDURE CompleteSend (c: Core);
	(** Освободилось место: исполнить ждущие предложения-отправки. *)
		VAR s: Offer; m: Msg;
	BEGIN
		LOOP
			IF (c.kind = Rendezvous) OR (c.count >= c.capacity) THEN RETURN END;
			s := PopLive(c.waitS, c.waitST, TRUE);
			IF s = NIL THEN RETURN END;
			m := s.msg;
			CheckSend(c, m);	(* отложенная отправка совершается здесь *)
			AppendItem(c, m);
			Decide(s.ctx, s.idx);
			CompleteRecv(c)
		END
	END CompleteSend;

	PROCEDURE PutItem* (c: Core; m: Msg);
	(** Положить сообщение. Для Rendezvous — вклад (не завершает Send сам). *)
	BEGIN
		IF c.kind = Rendezvous THEN
			c.item := m; c.deposit := TRUE; c.depOffer := NIL; c.direct := FALSE;
			c.directTask := NIL; c.taken := FALSE
		ELSE
			AppendItem(c, m)
		END;
		CompleteRecv(c)
	END PutItem;

	PROCEDURE TakeItem* (c: Core; OUT m: Msg): BOOLEAN;
	(** Взять сообщение из очереди; разбудить ждущего отправителя. *)
	BEGIN
		IF ~PopItem(c, m) THEN RETURN FALSE END;
		CompleteSend(c);
		RETURN TRUE
	END TakeItem;

	(* --- Rendezvous ------------------------------------------------------- *)

	PROCEDURE DeliverToReceiver* (c: Core; m: Msg): BOOLEAN;
		VAR r: Offer;
	BEGIN
		r := PopLive(c.waitR, c.waitRT, FALSE);
		IF r = NIL THEN RETURN FALSE END;
		r.got := m;
		Decide(r.ctx, r.idx);
		RETURN TRUE
	END DeliverToReceiver;

	PROCEDURE DepositDirect* (c: Core; m: Msg);
	(** Прямой Send кладёт вклад и ждёт, пока его заберут (taken). *)
	BEGIN
		Prune(c);
		c.item := m; c.deposit := TRUE; c.depOffer := NIL;
		c.direct := TRUE; c.directTask := Tasks.Current(); c.taken := FALSE
	END DepositDirect;

	PROCEDURE TakeRendezvous* (c: Core; OUT m: Msg): BOOLEAN;
	(** Забрать вклад: прямого Send или предложения; или из очереди waitS. *)
		VAR s: Offer;
	BEGIN
		Prune(c);
		IF c.deposit THEN
			m := c.item; c.item := NIL; c.deposit := FALSE;
			IF c.depOffer # NIL THEN
				Decide(c.depOffer.ctx, c.depOffer.idx); c.depOffer := NIL
			ELSE
				c.taken := TRUE	(* условие прямого Send *)
			END;
			c.direct := FALSE; c.directTask := NIL;
			RETURN TRUE
		END;
		s := PopLive(c.waitS, c.waitST, TRUE);
		IF s # NIL THEN
			CheckSend(c, s.msg);	(* отправка предложения совершается здесь *)
			m := s.msg;
			Decide(s.ctx, s.idx);
			RETURN TRUE
		END;
		RETURN FALSE
	END TakeRendezvous;

	(* --- регистрация альтернатив ------------------------------------------ *)

	PROCEDURE Register (c: Core; o: Offer): BOOLEAN;
	(** Попробовать исполнить предложение. Под монитором.
		Протокол проверяется в тот момент, когда отправка действительно
		совершается: прямое исполнение здесь или снятие отложенного
		предложения в TakeRendezvous/CompleteSend. Проигравшее
		предложение состояние протокола не меняет. *)
	BEGIN
		IF o.kind = SendKind THEN
			IF c.kind = Rendezvous THEN
				IF ReceiverWaiting(c) THEN
					CheckSend(c, o.msg);
					IF DeliverToReceiver(c, o.msg) THEN RETURN TRUE END
				END;
				IF ~c.deposit THEN
					c.item := o.msg; c.deposit := TRUE; c.depOffer := o;
					c.direct := FALSE; c.directTask := NIL; c.taken := FALSE;
					RETURN FALSE
				END;
				Enqueue(c.waitS, c.waitST, o); RETURN FALSE
			ELSIF c.kind = Bounded THEN
				IF c.count < c.capacity THEN
					CheckSend(c, o.msg); PutItem(c, o.msg); RETURN TRUE
				END;
				Enqueue(c.waitS, c.waitST, o); RETURN FALSE
			ELSE
				CheckSend(c, o.msg); PutItem(c, o.msg); RETURN TRUE
			END
		ELSE
			IF c.kind = Rendezvous THEN
				IF TakeRendezvous(c, o.got) THEN RETURN TRUE END
			ELSIF TakeItem(c, o.got) THEN RETURN TRUE
			END;
			Enqueue(c.waitR, c.waitRT, o); RETURN FALSE
		END
	END Register;

	PROCEDURE Withdraw* (c: Core; o: Offer);
	(** Снять предложение в конце Select (idempotent). Под монитором. *)
	BEGIN
		IF c.deposit & (c.depOffer = o) THEN
			c.deposit := FALSE; c.item := NIL; c.depOffer := NIL; c.taken := FALSE
		END;
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

	(* --- Select ----------------------------------------------------------- *)

	PROCEDURE Select* (VAR ops: ARRAY OF Op): INTEGER;
	(** Дождаться ровно одной альтернативы; вернуть её индекс.
		Результат приёма — ops[i].got. Только из задачи. *)
		VAR ctx: Ctx; offers: ARRAY maxAlt OF Offer;
			i, n, tIdx, winner: INTEGER; tMs: LONGINT; timer: Timer; done: BOOLEAN;
	BEGIN
		ASSERT(Tasks.InTask(), 20);
		n := LEN(ops); ASSERT(n <= maxAlt, 21);
		tIdx := -1; tMs := -1;
		NEW(ctx); ctx.task := Tasks.Current(); ctx.decided := FALSE;
		ctx.ready := FALSE; ctx.winner := -1;
		FOR i := 0 TO n - 1 DO offers[i] := NIL END;
		i := 0;
		WHILE (i < n) & ~ctx.decided DO
			IF ops[i].kind = TimeoutKind THEN
				IF (tIdx < 0) OR (ops[i].ms < tMs) THEN tIdx := i; tMs := ops[i].ms END
			ELSIF ops[i].kind = SignalKind THEN
				IF ops[i].sig # NIL THEN
					IF ops[i].sig.Arm(ctx, i) THEN ctx.decided := TRUE; ctx.winner := i END
				END
			ELSE
				NEW(offers[i]);
				offers[i].ctx := ctx; offers[i].idx := i;
				offers[i].kind := ops[i].kind; offers[i].msg := ops[i].msg;
				offers[i].got := NIL; offers[i].next := NIL;
				Kernel.Lock(ops[i].core);
				(* решение могло быть принято, пока мы ждали монитор
				   другого канала: второй альтернативы не исполняем *)
				IF ~ctx.decided THEN
					Prune(ops[i].core);
					IF Register(ops[i].core, offers[i]) THEN
						ctx.decided := TRUE; ctx.winner := i
					END
				END;
				Kernel.Unlock(ops[i].core)
			END;
			INC(i)
		END;
		IF ~ctx.decided & (tIdx >= 0) & (tMs <= 0) THEN
			ctx.decided := TRUE; ctx.winner := tIdx
		END;
		IF ~ctx.decided THEN
			timer := NIL;
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
				Kernel.Lock(ops[i].core);
				Withdraw(ops[i].core, offers[i]);
				Kernel.Unlock(ops[i].core)
			ELSIF (ops[i].kind = SignalKind) & (ops[i].sig # NIL) THEN
				ops[i].sig.Disarm
			END
		END;
		winner := ctx.winner;
		done := (0 <= winner) & (winner < n);
		IF done THEN
			IF (ops[winner].kind = RecvKind) & (offers[winner] # NIL) THEN
				ops[winner].got := offers[winner].got
			END
		END;
		RETURN winner
	END Select;

END AsyncSelect.
