MODULE ObxBufCtx;
(** Ограниченный буфер на мониторе-объекте Tasks.Object.

	Операции Put и Get — записи-наследники Tasks.Op на стеке вызывающего.
	Заполненность и пустота — флаги Ready: ждущий говорит, какой флаг ему
	нужен, и просыпается только когда этот флаг появился. Второй способ —
	положить k предметов разом: условие-объект Room проверяет, что в
	буфере есть место для всех k. Первый способ — цикл по флагам, второй —
	AwaitCond. *)

	IMPORT Tasks, Console, Strings;

	CONST
		cap = 4;
		producers = 2;
		consumers = 2;
		per = 8;		(* предметов от каждого производителя *)
		k = 2;			(* сколько кладёт операция PutAll *)
		empty = 0;		(* флаг Ready: есть место *)
		full = 1;		(* флаг Ready: есть предмет *)

	TYPE
		PutOp = RECORD (Tasks.Op) id, seq: INTEGER END;
		GetOp = RECORD (Tasks.Op) item: INTEGER END;
		PutAllFlagOp = RECORD (Tasks.Op) id, seq: INTEGER END;
		PutAllCondOp = RECORD (Tasks.Op) id, seq: INTEGER; room: Room END;

		Room = POINTER TO RECORD (Tasks.Cond) k: INTEGER END;

		Buffer = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER
		END;

		ProducerFlag = POINTER TO RECORD (Tasks.Job) b: Buffer; id, n: INTEGER END;
		ProducerCond = POINTER TO RECORD (Tasks.Job) b: Buffer; id, n: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Job) b: Buffer; n: INTEGER END;

	VAR
		produced, consumed: INTEGER;
		nextSeq: ARRAY producers OF INTEGER;
		got: ARRAY producers OF INTEGER;
		broken: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE SayStr (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s); Console.WriteLn
	END SayStr;

	PROCEDURE (b: Buffer) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF b.count < cap THEN INCL(s, empty) END;
		IF b.count > 0 THEN INCL(s, full) END;
		RETURN s
	END Ready;

	PROCEDURE (r: Room) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: Buffer;
	BEGIN
		b := o(Buffer);
		RETURN b.count + r.k <= cap
	END Ready;

	PROCEDURE (b: Buffer) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR i: INTEGER; v: INTEGER;
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = cap DO
				IF ~b.Await(ctx, {empty}) THEN RETURN END
			END;
			b.ring[b.tail] := op.id * 100 + op.seq;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO
				IF ~b.Await(ctx, {full}) THEN RETURN END
			END;
			op.item := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		| op: PutAllFlagOp DO
			FOR i := 0 TO k - 1 DO
				WHILE b.count = cap DO
					IF ~b.Await(ctx, {empty}) THEN RETURN END
				END;
				b.ring[b.tail] := op.id * 100 + op.seq + i;
				b.tail := (b.tail + 1) MOD cap; INC(b.count)
			END
		| op: PutAllCondOp DO
			op.room.k := k;
			WHILE ~op.room.Ready(b) DO
				IF ~b.AwaitCond(ctx, op.room) THEN RETURN END
			END;
			FOR i := 0 TO k - 1 DO
				b.ring[b.tail] := op.id * 100 + op.seq + i;
				b.tail := (b.tail + 1) MOD cap; INC(b.count)
			END
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: ProducerFlag) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutAllFlagOp;
	BEGIN
		i := 1;
		WHILE i <= p.n DO
			op.id := p.id; op.seq := i;
			p.b.Do(ctx, op);
			INC(produced, k); INC(i, k)
		END
	END Body;

	PROCEDURE (p: ProducerCond) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutAllCondOp; room: Room;
	BEGIN
		NEW(room);
		i := 1;
		WHILE i <= p.n DO
			op.id := p.id; op.seq := i; op.room := room;
			p.b.Do(ctx, op);
			INC(produced, k); INC(i, k)
		END
	END Body;

	PROCEDURE (c: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR i, id, seq: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO c.n DO
			op.item := -1;
			c.b.Do(ctx, op);
			IF op.item < 0 THEN broken := TRUE
			ELSE
				id := op.item DIV 100; seq := op.item MOD 100;
				IF (id < 0) OR (id >= producers) OR (seq # nextSeq[id] + 1) THEN broken := TRUE END;
				INC(nextSeq[id]); INC(got[id])
			END;
			INC(consumed)
		END
	END Body;

	PROCEDURE Do*;
		VAR b: Buffer; pf: ProducerFlag; pc: ProducerCond; c: Consumer; i: INTEGER;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		produced := 0; consumed := 0; broken := FALSE;
		FOR i := 0 TO producers - 1 DO nextSeq[i] := 0; got[i] := 0 END;
		FOR i := 1 TO consumers DO
			NEW(c); c.b := b; c.n := producers * per DIV consumers;
			Tasks.Start(c)
		END;
		NEW(pf); pf.b := b; pf.id := 0; pf.n := per; Tasks.Start(pf);
		NEW(pc); pc.b := b; pc.id := 1; pc.n := per; Tasks.Start(pc);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("produced ", produced);
		Say("consumed ", consumed);
		IF broken OR (produced # consumers * (producers * per DIV consumers)) OR
				(consumed # produced) THEN
			SayStr("order broken")
		ELSE
			SayStr("order ok")
		END
	END Do;

END ObxBufCtx.
