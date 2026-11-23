Loop

DEFINITION Loop;
	
	TYPE
		ExitGuard = PROCEDURE (): BOOLEAN;
	
	VAR
		quantum: LONGINT;
		
	PROCEDURE Start (eventIterator, viewsValidator: PROCEDURE; exitGuard: ExitGuard);

END Loop.

Module Loop provides the main event loop for event-driven applications.

The BlackBox framework, as of 2.0, supports the development of two kinds of applications:
	* Event-loop applications (window applications, server applications, or a combination thereof)
	* Batch aplications

Event-loop applications do have an event loop, which is implemented in module Loop; the loop ends normally (thus the application exits) once exit conditions have been met. In principle, the loop is:
	REPEAT
		ObtainEvent;
		ProcessEvent;
		InvokeImmediateActions;
		ValidateViews;
		InvokeDueActions
	UNTIL ExitAllowed()
Events are usually obtained from the host platform; they may be user input events, timer events, network events, etc. For window applications, the exit condition is that all windows have been closed; for server applications, the exit condition is that the server app has somehow received a signal to exit. A combination is possible, when a server app starts 'in the background' (without any windows); but may at some point present some windows; for such apps, the exit condition may be a conjunction of 'all windows closed' and 'exit signal received'.
The main event loop is started with Loop.Start; it should be provided with three procedures: eventIterator to obtain and process a host platform event; exitGuard to establish the exit condition; and viewsValidator (optional) to validate (restore) views. Normally, Start is called from WinInit/LinInit.
Simple server apps may call Dialog.RequestExit(~Dialog.exitWithoutWindows) to tell the framework NOT to exit even if there are no (more) windows; or Dialog.RequestExit(Dialog.exitWithoutWindows) to tell the framework that the server has received an exit signal and is ready to exit. If your server app requires a more complicated exit condition, you will need to provide (and statically link) your own <App>Init module that will call Loop.Start and provide it with your own exit guard procedure.

Batch applications do not have an event loop and should not invoke Loop.Start; they require a (statically linked) <App>Init module. The latter should invoke all required input processing from it's BEGIN section. Once such processing is complete, and the BEGIN section returns control, the batch application exits normally.

TYPE ExitGuard = PROCEDURE (): BOOLEAN
Type of procedure used in Start to check whether the main event loop should exit.

VAR quantum: INTEGER
At least quantum milliseconds are guaranteed to have elapsed between consecutive invocations of scheduled actions.
That is, If all due actions had been invoked at t0, and actions A0..An are due at t0 + delta, it is guaranteed that A0..An won't be invoked until t0 + quantum.

PROCEDURE Start (eventIterator, viewsValidator: PROCEDURE; exitGuard: ExitGuard);
Starts the main event loop of application (typically called in LinInit/WinInit or a custom <App>Init module). When exitGuard returns TRUE, the event loop exits, and Start returns control. eventIterator  is called to let the host platform process/dispatch/deliver an event. If viewsValidator is provided, it's called to validate (restore) views (may be NIL).

Pre
eventIterator # NIL	20
exitGuard # NIL	21

Post
exitGuard has just returned TRUE
