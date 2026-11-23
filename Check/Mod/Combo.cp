MODULE CheckCombo;	(* ComboBox checks *)

	IMPORT Dialog;
	
	VAR
		rec*: RECORD
			combo*: Dialog.Combo;
		END;
	
	PROCEDURE Init;
	BEGIN
		rec.combo.SetLen(3);
		rec.combo.SetItem(0, 'The rain in Spain');
		rec.combo.SetItem(1, 'Falls mainly');
		rec.combo.SetItem(2, 'In the plain');
		Dialog.Update(rec)
	END Init;

BEGIN Init

END CheckCombo.


