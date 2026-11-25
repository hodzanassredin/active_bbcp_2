MODULE DevRejCtxCopy;
	IMPORT Tasks;
	TYPE J = POINTER TO RECORD (Tasks.Job) END;
	PROCEDURE (j: J) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		ctx := ctx
	END Body;
END DevRejCtxCopy.
