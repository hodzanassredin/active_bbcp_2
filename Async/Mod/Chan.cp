MODULE AsyncChan;
(** Каналы.

	Chan — встреча (отправитель ждёт, пока получатель заберёт); Buffer —
	ограниченная очередь; Mailbox — неограниченная. Send и Recv —
	AsyncSelect.QueueSend/QueueRecv: ждущая сторона кладёт предложение в
	очередь ядра канала и спит в Tasks.Park, другая сторона исполняет его и
	будит задачу. Очередь канала защищена не монитором, а самой моделью
	задач: одна нить ОС, вытеснения нет, операция над очередью между
	точками ожидания неделима. Alt/Select из AsyncSelect ждёт те же объекты
	через те же очереди.

	Сообщение (AsyncSelect.Msg) после отправки не меняется. *)

	IMPORT Tasks, AsyncSelect, AsyncProto;

	TYPE
		Chan* = POINTER TO RECORD (AsyncSelect.Core) END;
		Buffer* = POINTER TO RECORD (AsyncSelect.Core) END;
		Mailbox* = POINTER TO RECORD (AsyncSelect.Core) END;

	(* --- операции с пропуском --------------------------------------------- *)

	PROCEDURE SendCtxCore (c: AsyncSelect.Core; VAR ctx: Tasks.Ctx;
		m: AsyncSelect.Msg): BOOLEAN;
	(* Отправка одним предложением SelectCtx: решение, снятие и пробуждение
	   по остановке — там же. *)
		VAR ops: ARRAY 1 OF AsyncSelect.Op;
	BEGIN
		AsyncSelect.AltSend(c, m, ops[0]);
		RETURN AsyncSelect.SelectCtx(ctx, ops) = 0
	END SendCtxCore;

	PROCEDURE RecvCtxCore (c: AsyncSelect.Core; VAR ctx: Tasks.Ctx;
		OUT m: AsyncSelect.Msg): BOOLEAN;
	(* Приём одним предложением SelectCtx. FALSE — приём не состоялся,
	   m = NIL. *)
		VAR ops: ARRAY 1 OF AsyncSelect.Op;
	BEGIN
		AsyncSelect.AltRecv(c, ops[0]);
		IF AsyncSelect.SelectCtx(ctx, ops) = 0 THEN
			m := ops[0].got; RETURN TRUE
		ELSE
			m := NIL; RETURN FALSE
		END
	END RecvCtxCore;

	PROCEDURE NewChan* (): Chan;
	(** Пост: новая пустая встреча без протокола. *)
		VAR c: Chan;
	BEGIN
		NEW(c); AsyncSelect.InitCore(c, AsyncSelect.Rendezvous, 0);
		RETURN c
	END NewChan;

	PROCEDURE NewBuffer* (capacity: INTEGER): Buffer;
	(** Пред: capacity > 0 (трап 20). Пост: новый пустой буфер на capacity
		сообщений без протокола. *)
		VAR c: Buffer;
	BEGIN
		ASSERT(capacity > 0, 20);
		NEW(c); AsyncSelect.InitCore(c, AsyncSelect.Bounded, capacity);
		RETURN c
	END NewBuffer;

	PROCEDURE NewMailbox* (): Mailbox;
	(** Пост: новый пустой неограниченный ящик без протокола. *)
		VAR c: Mailbox;
	BEGIN
		NEW(c); AsyncSelect.InitCore(c, AsyncSelect.Mailbox, 0);
		RETURN c
	END NewMailbox;

	(* --- встреча ---------------------------------------------------------- *)

	PROCEDURE (c: Chan) Send* (m: AsyncSelect.Msg), NEW;
	(** Пред: ждать можно только из задачи. Пост: получатель забрал m;
		нарушение протокола — трап 21, m не отправлено. *)
	BEGIN
		AsyncSelect.QueueSend(c, m)
	END Send;

	PROCEDURE (c: Chan) Recv* (OUT m: AsyncSelect.Msg), NEW;
	(** Пред: ждать можно только из задачи. Пост: m — сообщение первого
		ждущего отправителя. *)
	BEGIN
		AsyncSelect.QueueRecv(c, m)
	END Recv;

	(* --- ограниченный канал ----------------------------------------------- *)

	PROCEDURE (c: Buffer) Send* (m: AsyncSelect.Msg), NEW;
	(** Пред: ждать можно только из задачи. Пост: m — последнее в буфере;
		полный буфер — ждать места; нарушение протокола — трап 21. Ждущий
		отправитель — предложение в очереди канала, монитор во время
		ожидания свободен. *)
	BEGIN
		AsyncSelect.QueueSend(c, m)
	END Send;

	PROCEDURE (c: Buffer) Recv* (OUT m: AsyncSelect.Msg), NEW;
	(** Пред: ждать можно только из задачи. Пост: m — первое сообщение
		буфера; пустой буфер — ждать отправителя. *)
	BEGIN
		AsyncSelect.QueueRecv(c, m)
	END Recv;

	(* --- почтовый ящик ---------------------------------------------------- *)

	PROCEDURE (c: Mailbox) Send* (m: AsyncSelect.Msg), NEW;
	(** Пост: m — последнее в ящике, ожидания нет; нарушение протокола —
		трап 21, m не отправлено. *)
	BEGIN
		AsyncSelect.QueueSend(c, m)
	END Send;

	PROCEDURE (c: Mailbox) Recv* (OUT m: AsyncSelect.Msg), NEW;
	(** Пред: ждать можно только из задачи. Пост: m — первое сообщение
		ящика; пустой ящик — ждать отправителя. *)
	BEGIN
		AsyncSelect.QueueRecv(c, m)
	END Recv;

	(* --- те же операции с пропуском --------------------------------------- *)

	PROCEDURE (c: Chan) SendCtx* (VAR ctx: Tasks.Ctx; m: AsyncSelect.Msg): BOOLEAN, NEW;
	(** Пред: вызов из задачи, ctx этой задачи (20, 25). Пост: TRUE — m
		отправлено (встреча состоялась целиком); FALSE — пропуск остановлен,
		m не отправлено и ожидания не было. *)
	BEGIN
		RETURN SendCtxCore(c, ctx, m)
	END SendCtx;

	PROCEDURE (c: Chan) RecvCtx* (VAR ctx: Tasks.Ctx; OUT m: AsyncSelect.Msg): BOOLEAN, NEW;
	(** Пост: TRUE — m получено; FALSE — пропуск остановлен, m = NIL и
		приёма не было. *)
	BEGIN
		RETURN RecvCtxCore(c, ctx, m)
	END RecvCtx;

	PROCEDURE (c: Buffer) SendCtx* (VAR ctx: Tasks.Ctx; m: AsyncSelect.Msg): BOOLEAN, NEW;
	(** Пост: TRUE — m в буфере; FALSE — пропуск остановлен, m не отправлено.
		Полный буфер — ждать места с пропуском. *)
	BEGIN
		RETURN SendCtxCore(c, ctx, m)
	END SendCtx;

	PROCEDURE (c: Buffer) RecvCtx* (VAR ctx: Tasks.Ctx; OUT m: AsyncSelect.Msg): BOOLEAN, NEW;
	(** Пост: TRUE — m из буфера; FALSE — пропуск остановлен, m = NIL. *)
	BEGIN
		RETURN RecvCtxCore(c, ctx, m)
	END RecvCtx;

	PROCEDURE (c: Mailbox) SendCtx* (VAR ctx: Tasks.Ctx; m: AsyncSelect.Msg): BOOLEAN, NEW;
	(** Пост: TRUE — m в ящике; FALSE — пропуск остановлен, m не отправлено.
		Ящик не ждёт места, но вызов всё равно требует задачу и пропуск. *)
	BEGIN
		RETURN SendCtxCore(c, ctx, m)
	END SendCtx;

	PROCEDURE (c: Mailbox) RecvCtx* (VAR ctx: Tasks.Ctx; OUT m: AsyncSelect.Msg): BOOLEAN, NEW;
	(** Пост: TRUE — m из ящика; FALSE — пропуск остановлен, m = NIL. *)
	BEGIN
		RETURN RecvCtxCore(c, ctx, m)
	END RecvCtx;

	(* --- протокол -------------------------------------------------------- *)

	PROCEDURE SetProtocol* (c: AsyncSelect.Core; p: AsyncProto.Protocol);
	(** Пред: c # NIL. Пост: каждая отправка в c — прямая и через Select —
		проверяется протоколом p в момент, когда совершается (отладочный
		режим); состояние — начальное. p = NIL снимает проверку; без
		протокола накладных расходов нет. *)
	BEGIN
		AsyncSelect.SetProtocol(c, p)
	END SetProtocol;

	(* --- альтернативы ----------------------------------------------------- *)

	PROCEDURE SendAlt* (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op);
	(** Пред: c # NIL (иначе трап 23 в Select). Пост: op — отправка m в c. *)
	BEGIN
		AsyncSelect.AltSend(c, m, op)
	END SendAlt;

	PROCEDURE RecvAlt* (c: AsyncSelect.Core; OUT op: AsyncSelect.Op);
	(** Пред: c # NIL (иначе трап 23 в Select). Пост: op — приём из c. *)
	BEGIN
		AsyncSelect.AltRecv(c, op)
	END RecvAlt;

	PROCEDURE TimeoutAlt* (ms: LONGINT; OUT op: AsyncSelect.Op);
	(** Пост: op — тайм-аут ms мс; ms <= 0 срабатывает сразу. *)
	BEGIN
		AsyncSelect.AltTimeout(ms, op)
	END TimeoutAlt;

	PROCEDURE Select* (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER;
	(** То же, что AsyncSelect.Select: пред- и постусловия там. *)
	BEGIN
		RETURN AsyncSelect.Select(ops)
	END Select;

	PROCEDURE SelectCtx* (VAR ctx: Tasks.Ctx; VAR ops: ARRAY OF AsyncSelect.Op): INTEGER;
	(** То же, что AsyncSelect.SelectCtx: −1 — пропуск остановлен и ни одна
		альтернатива не исполнена. *)
	BEGIN
		RETURN AsyncSelect.SelectCtx(ctx, ops)
	END SelectCtx;

END AsyncChan.
