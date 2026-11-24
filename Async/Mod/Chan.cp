MODULE AsyncChan;
(** Каналы.

	Chan — встреча (отправитель ждёт, пока получатель заберёт); Buffer —
	ограниченная очередь; Mailbox — неограниченная. Send и Recv —
	AsyncSelect.QueueSend/QueueRecv: ждущая сторона кладёт предложение в
	очередь ядра канала и спит в Tasks.Park, другая сторона исполняет его и
	будит задачу. Канал — объект с монитором: если монитор держит задача
	(Kernel.Lock), операции ждут его освобождения. Alt/Select из
	AsyncSelect ждёт те же объекты через те же очереди.

	Сообщение (AsyncSelect.Msg) после отправки не меняется. *)

	IMPORT AsyncSelect, AsyncProto;

	TYPE
		Chan* = POINTER TO RECORD (AsyncSelect.Core) END;
		Buffer* = POINTER TO RECORD (AsyncSelect.Core) END;
		Mailbox* = POINTER TO RECORD (AsyncSelect.Core) END;

	PROCEDURE Init (c: AsyncSelect.Core; kind, capacity: INTEGER);
	BEGIN
		(* NEW обнуляет запись; здесь только параметры вида *)
		c.kind := kind; c.capacity := capacity
	END Init;

	PROCEDURE NewChan* (): Chan;
		VAR c: Chan;
	BEGIN
		NEW(c); Init(c, AsyncSelect.Rendezvous, 0);
		RETURN c
	END NewChan;

	PROCEDURE NewBuffer* (capacity: INTEGER): Buffer;
		VAR c: Buffer;
	BEGIN
		ASSERT(capacity > 0, 20);
		NEW(c); Init(c, AsyncSelect.Bounded, capacity);
		RETURN c
	END NewBuffer;

	PROCEDURE NewMailbox* (): Mailbox;
		VAR c: Mailbox;
	BEGIN
		NEW(c); Init(c, AsyncSelect.Mailbox, 0);
		RETURN c
	END NewMailbox;

	(* --- встреча ---------------------------------------------------------- *)

	PROCEDURE (c: Chan) Send* (m: AsyncSelect.Msg), NEW;
	(** Отправка завершается, когда получатель забрал сообщение. *)
	BEGIN
		AsyncSelect.QueueSend(c, m)
	END Send;

	PROCEDURE (c: Chan) Recv* (OUT m: AsyncSelect.Msg), NEW;
	BEGIN
		AsyncSelect.QueueRecv(c, m)
	END Recv;

	(* --- ограниченный канал ----------------------------------------------- *)

	PROCEDURE (c: Buffer) Send* (m: AsyncSelect.Msg), NEW;
	(** Монитор берётся на время операции; ждущий отправитель — предложение
		в очереди канала, монитор во время ожидания свободен. *)
	BEGIN
		AsyncSelect.QueueSend(c, m)
	END Send;

	PROCEDURE (c: Buffer) Recv* (OUT m: AsyncSelect.Msg), NEW;
	BEGIN
		AsyncSelect.QueueRecv(c, m)
	END Recv;

	(* --- почтовый ящик ---------------------------------------------------- *)

	PROCEDURE (c: Mailbox) Send* (m: AsyncSelect.Msg), NEW;
	BEGIN
		AsyncSelect.QueueSend(c, m)
	END Send;

	PROCEDURE (c: Mailbox) Recv* (OUT m: AsyncSelect.Msg), NEW;
	BEGIN
		AsyncSelect.QueueRecv(c, m)
	END Recv;

	(* --- протокол -------------------------------------------------------- *)

	PROCEDURE SetProtocol* (c: AsyncSelect.Core; p: AsyncProto.Protocol);
	(** Проверять прямые Send по протоколу диалога (отладочный режим).
		NIL снимает проверку; без протокола накладных расходов нет.
		Отправки через Select проверяются в момент, когда предложение
		действительно исполняется. *)
	BEGIN
		AsyncSelect.SetProtocol(c, p)
	END SetProtocol;

	(* --- альтернативы ----------------------------------------------------- *)

	PROCEDURE SendAlt* (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op);
	BEGIN
		AsyncSelect.AltSend(c, m, op)
	END SendAlt;

	PROCEDURE RecvAlt* (c: AsyncSelect.Core; OUT op: AsyncSelect.Op);
	BEGIN
		AsyncSelect.AltRecv(c, op)
	END RecvAlt;

	PROCEDURE TimeoutAlt* (ms: LONGINT; OUT op: AsyncSelect.Op);
	BEGIN
		AsyncSelect.AltTimeout(ms, op)
	END TimeoutAlt;

	PROCEDURE Select* (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER;
	BEGIN
		RETURN AsyncSelect.Select(ops)
	END Select;

END AsyncChan.
