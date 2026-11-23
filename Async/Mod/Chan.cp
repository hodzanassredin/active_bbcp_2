MODULE AsyncChan;
(** Каналы этапа 7 на расширении языка: [exclusive] + ACTIVE.AWAIT.

	Chan — встреча (отправитель ждёт, пока получатель заберёт); Buffer —
	ограниченная очередь; Mailbox — неограниченная. Все три — мониторы:
	методы [exclusive] держат монитор объекта, ожидание условия отпускает
	его. Alt/Select из AsyncSelect умеет ждать те же объекты: предложения
	лежат в очередях ядра AsyncSelect, а прямой Send/Recv их обслуживает.

	Сообщение (AsyncSelect.Msg) после отправки не меняется. *)

	IMPORT ACTIVE, AsyncSelect;

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

	PROCEDURE [exclusive] (c: Chan) Send* (m: AsyncSelect.Msg), NEW;
		VAR done: BOOLEAN;
	BEGIN
		done := FALSE;
		LOOP
			ACTIVE.AWAIT((c.waitR # NIL) OR ~c.deposit);
			AsyncSelect.Prune(c);
			IF AsyncSelect.DeliverToReceiver(c, m) THEN done := TRUE; EXIT END;
			IF ~c.deposit THEN AsyncSelect.DepositDirect(c, m); EXIT END
		END;
		IF ~done THEN
			(* вклад принят только когда получатель его забрал *)
			ACTIVE.AWAIT(c.taken OR ~c.deposit);
			IF c.taken THEN c.taken := FALSE END;
			c.item := NIL; c.direct := FALSE; c.directTask := NIL
		END
	END Send;

	PROCEDURE [exclusive] (c: Chan) Recv* (OUT m: AsyncSelect.Msg), NEW;
	BEGIN
		LOOP
			ACTIVE.AWAIT(c.deposit OR (c.waitS # NIL));
			IF AsyncSelect.TakeRendezvous(c, m) THEN EXIT END
		END
	END Recv;

	(* --- ограниченный канал ----------------------------------------------- *)

	PROCEDURE [exclusive] (c: Buffer) Send* (m: AsyncSelect.Msg), NEW;
	BEGIN
		ACTIVE.AWAIT(c.count < c.capacity);
		AsyncSelect.PutItem(c, m)
	END Send;

	PROCEDURE [exclusive] (c: Buffer) Recv* (OUT m: AsyncSelect.Msg), NEW;
	BEGIN
		ACTIVE.AWAIT(c.count > 0);
		IF ~AsyncSelect.TakeItem(c, m) THEN m := NIL END
	END Recv;

	(* --- почтовый ящик ---------------------------------------------------- *)

	PROCEDURE [exclusive] (c: Mailbox) Send* (m: AsyncSelect.Msg), NEW;
	BEGIN
		AsyncSelect.PutItem(c, m)
	END Send;

	PROCEDURE [exclusive] (c: Mailbox) Recv* (OUT m: AsyncSelect.Msg), NEW;
	BEGIN
		ACTIVE.AWAIT(c.count > 0);
		IF ~AsyncSelect.TakeItem(c, m) THEN m := NIL END
	END Recv;

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
