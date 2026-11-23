StdConfig

DEFINITION StdConfig;

	PROCEDURE Setup;

END StdConfig.

BlackBox attempts to call the command StdConfig.Setup during start-up. The call allows to customize the configuration of BlackBox. Module Config is provided in source form and can be changed by the programmer arbitrarily. The default implementation looks like this:

	MODULE StdConfig;

		IMPORT Dialog;

		PROCEDURE Setup*;
			VAR res: INTEGER;
		BEGIN

			(* ... various file and clipboard converters are installed here ... *)
			
			(* creating workspace *)
			
			Dialog.Call("StdLog.Open", "", res)
		END Setup;

	END StdConfig.

This configuration causes the log window to be opened upon startup. The command is called after the complete BlackBox library, framework, and standard subsystems are loaded. StdConfig may import any BlackBox module. If it isn't needed, module StdConfig can be deleted.

Current Configuration


PROCEDURE Setup
This procedure implemented in order to customize the initial BlackBox configuration after start-up.
It is called after all standard services and menus have been installed.


Note that similar to StdConfig, there may (but need not) be a module Startup with a command Setup. It has a similar purpose as StdConfig, but is called before the higher levels (text subsystem, form subsystem, etc.) are loaded. Consequently, Startup may not import the higher levels of BlackBox. Normally, Startup does not exist. It is only used under special circumstances, e.g., to overwrite the variable Dialog.appName.
