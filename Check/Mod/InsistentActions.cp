MODULE CheckInsistentActions;
	
	IMPORT Services, Dialog, Con := CheckLog;
	
	TYPE
		Insistent = POINTER TO RECORD (Services.InsistentAction) END;
		Action = POINTER TO RECORD (Services.Action) END;
	
	VAR
		action: Action;
		insistent: Insistent;
	
	PROCEDURE (a: Action) Do; 
	BEGIN 
		Con.String("Simple action"); Con.Ln; 
		IF action # NIL THEN Services.DoLater(a, Services.Ticks() + 1000) END
	END Do;

	PROCEDURE (a: Insistent) Do; 
	BEGIN 
		Con.String("Insistent action"); Con.Ln; 
		IF insistent # NIL THEN Services.DoLater(a, Services.Ticks() + 1000) END
	END Do;
	
	PROCEDURE Toggle*;
	BEGIN
		IF action # NIL THEN Services.RemoveAction(action); action := NIL
		ELSE NEW(action); Services.DoLater(action, Services.now)
		END;
		IF insistent # NIL THEN Services.RemoveAction(insistent); insistent := NIL
		ELSE NEW(insistent); Services.DoLater(insistent, Services.now)
		END
	END Toggle;
	
	PROCEDURE ToggleGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := FALSE;
		par.checked := (action # NIL) & (insistent # NIL)
	END ToggleGuard;


END CheckInsistentActions.
