MODULE AsyncOnce;
(** Однократная переменная: Fill/Read.

	Fill — ровно один раз (повторный — трап 21/22); Read ждёт заполнения.
	Объект — монитор-объект Tasks.Object: операции Fill и Read — записи
	Tasks.Op на стеке вызывающего, их исполняет Once.Exclusive. Значение —
	INTEGER или сообщение AsyncSelect.Msg.

	Операции требуют задачу с пропуском (Tasks.Ctx): Do входит в монитор
	и ждёт в Await, а ждать можно только из задачи Job. Вызов с главного
	стека трапует 22 (нет задачи); Read, остановленный пропуском,
	возвращает FALSE (INTEGER-вариант — значение по умолчанию 0). *)

	IMPORT Tasks, AsyncSelect;

	CONST
		filled = 0;	(* флаг Ready: значение записано *)

	TYPE
		Once* = POINTER TO RECORD (Tasks.Object)
			done-: BOOLEAN;
			n: INTEGER;
			m: AsyncSelect.Msg
		END;

		FillIntOp = RECORD (Tasks.Op) x: INTEGER END;
		ReadIntOp = RECORD (Tasks.Op) x: INTEGER; ok: BOOLEAN END;
		FillMsgOp = RECORD (Tasks.Op) m: AsyncSelect.Msg END;
		ReadMsgOp = RECORD (Tasks.Op) m: AsyncSelect.Msg; ok: BOOLEAN END;

	PROCEDURE New* (): Once;
		VAR o: Once;
	BEGIN
		NEW(o); o.done := FALSE; o.n := 0; o.m := NIL;
		RETURN o
	END New;

	PROCEDURE (o: Once) Ready* (): SET;
	BEGIN
		IF o.done THEN RETURN {filled} ELSE RETURN {} END
	END Ready;

	PROCEDURE (o: Once) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: FillIntOp DO
			ASSERT(~o.done, 21);
			o.n := op.x; o.done := TRUE
		| op: ReadIntOp DO
			WHILE ~o.done DO
				IF ~o.Await(ctx, {filled}) THEN op.ok := FALSE; RETURN END
			END;
			op.x := o.n; op.ok := TRUE
		| op: FillMsgOp DO
			ASSERT(~o.done, 22);
			o.m := op.m; o.done := TRUE
		| op: ReadMsgOp DO
			WHILE ~o.done DO
				IF ~o.Await(ctx, {filled}) THEN op.ok := FALSE; RETURN END
			END;
			op.m := o.m; op.ok := TRUE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (o: Once) FillInt* (VAR ctx: Tasks.Ctx; x: INTEGER), NEW;
	(** Пред: значение ещё не записано (иначе трап 21). Пост: записано x,
		ждущие Read разбужены. *)
		VAR op: FillIntOp;
	BEGIN
		op.x := x; o.Do(ctx, op)
	END FillInt;

	PROCEDURE (o: Once) ReadInt* (VAR ctx: Tasks.Ctx): INTEGER, NEW;
	(** Пост: прочитанное значение; 0 — пропуск остановлен до заполнения. *)
		VAR op: ReadIntOp;
	BEGIN
		op.ok := FALSE; o.Do(ctx, op);
		IF op.ok THEN RETURN op.x ELSE RETURN 0 END
	END ReadInt;

	PROCEDURE (o: Once) FillMsg* (VAR ctx: Tasks.Ctx; m: AsyncSelect.Msg), NEW;
	(** Пред: значение ещё не записано (иначе трап 22). Пост: записано m. *)
		VAR op: FillMsgOp;
	BEGIN
		op.m := m; o.Do(ctx, op)
	END FillMsg;

	PROCEDURE (o: Once) ReadMsg* (VAR ctx: Tasks.Ctx; OUT m: AsyncSelect.Msg), NEW;
	(** Пост: m — записанное сообщение; NIL — пропуск остановлен до
		заполнения. *)
		VAR op: ReadMsgOp;
	BEGIN
		op.m := NIL; op.ok := FALSE; o.Do(ctx, op);
		m := op.m
	END ReadMsg;

END AsyncOnce.
