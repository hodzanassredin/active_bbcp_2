MODULE AsyncOnce;
(** Однократная переменная: Fill/Read.

	Fill — ровно один раз (повторный — трап 21); Read ждёт заполнения.
	Объект — монитор: [exclusive] + ACTIVE.AWAIT. Значение — INTEGER
	или сообщение AsyncSelect.Msg. *)

	IMPORT ACTIVE, AsyncSelect;

	TYPE
		Once* = POINTER TO RECORD
			done*: BOOLEAN;
			n: INTEGER;
			m: AsyncSelect.Msg
		END;

	PROCEDURE New* (): Once;
		VAR o: Once;
	BEGIN
		NEW(o); o.done := FALSE; o.n := 0; o.m := NIL;
		RETURN o
	END New;

	PROCEDURE [exclusive] (o: Once) FillInt* (x: INTEGER), NEW;
	BEGIN
		ASSERT(~o.done, 21);
		o.n := x; o.done := TRUE
	END FillInt;

	PROCEDURE [exclusive] (o: Once) ReadInt* (): INTEGER, NEW;
	BEGIN
		ACTIVE.AWAIT(o.done);
		RETURN o.n
	END ReadInt;

	PROCEDURE [exclusive] (o: Once) FillMsg* (m: AsyncSelect.Msg), NEW;
	BEGIN
		ASSERT(~o.done, 22);
		o.m := m; o.done := TRUE
	END FillMsg;

	PROCEDURE [exclusive] (o: Once) ReadMsg* (OUT m: AsyncSelect.Msg), NEW;
	BEGIN
		ACTIVE.AWAIT(o.done);
		m := o.m
	END ReadMsg;

END AsyncOnce.
