MODULE CheckPosition;

	(* Ivan Denisov, Alexander Shiryaev, 2016.09 *)

	IMPORT TextControllers, TextViews, StdCmds, Dialog;

	VAR
		pos*: INTEGER;

	PROCEDURE Guard0 (c: TextControllers.Controller): BOOLEAN;
	BEGIN
	RETURN (c # NIL) & (pos >= 0) & (pos <= c.text.Length())
	END Guard0;

	PROCEDURE Set*;
		VAR c: TextControllers.Controller;
	BEGIN
		c := TextControllers.Focus();
		IF Guard0(c) THEN
			c.SetCaret(pos);
			c.view.ShowRange(pos, pos, TextViews.focusOnly);
			StdCmds.CloseDialog
		END
	END Set;

	PROCEDURE SetGuard* (VAR par: Dialog.Par);
	BEGIN
		IF ~Guard0(TextControllers.Focus()) THEN par.disabled := TRUE END
	END SetGuard;

END CheckPosition.