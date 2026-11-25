MODULE DevRejExclusive;
	IMPORT Tasks;
	TYPE
		Op = RECORD (Tasks.Op) END;
		O = POINTER TO RECORD (Tasks.Object) END;
		J = POINTER TO RECORD (Tasks.Job) o: O END;
	PROCEDURE (o: O) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
	END Exclusive;
	PROCEDURE (j: J) Body- (VAR ctx: Tasks.Ctx);
		VAR op: Op;
	BEGIN
		j.o.Exclusive(ctx, op)
	END Body;
END DevRejExclusive.
