MODULE CheckCmds;

	IMPORT Dialog, Windows, Controllers;
	
	PROCEDURE WindowFlagsGuard* (VAR par: Dialog.Par);
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(Controllers.targetPath);
		IF w # NIL THEN
			par.label := '';
			IF ~(Windows.isAux IN w.flags) THEN par.label := par.label + '~' END; 
			par.label := par.label + 'Aux ';
			IF ~(Windows.isTool IN w.flags) THEN par.label := par.label + '~' END; 
			par.label := par.label + 'Tool ';
			IF Windows.noResize IN w.flags THEN par.label := par.label + 'no' END; 
			par.label := par.label + 'Resize ';
			IF ~(Windows.allowDuplicates IN w.flags) THEN par.label := par.label + '~' END; 
			par.label := par.label + 'allowDuplicates ';
			IF ~(Windows.neverDirty IN w.flags) THEN par.label := par.label + '~' END; 
			par.label := par.label + 'neverDirty '
		END
	END WindowFlagsGuard;
	
END CheckCmds.