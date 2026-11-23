MODULE CheckTimedExecute;

	IMPORT Kernel, Dialog, StdLog;

	PROCEDURE Run*(cmd: ARRAY OF CHAR);
		VAR t0: LONGINT; res: INTEGER; str: Dialog.String;
	BEGIN
		t0 := Kernel.Time();
		Dialog.Call(cmd, "", res);
		StdLog.Int(SHORT((Kernel.Time() - t0) * 1000 DIV Kernel.timeResolution));
		Dialog.MapString("#Dev:msec", str);
		StdLog.Char(" "); StdLog.String(str);
		StdLog.Ln
	END Run;

END CheckTimedExecute.
