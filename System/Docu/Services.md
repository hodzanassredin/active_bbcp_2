Services

DEFINITION Services;

	CONST
		immediately = -1; now = 0;
		resolution = 1000;

	TYPE
		Action = POINTER TO ABSTRACT RECORD
			(a: Action) Do-, NEW, ABSTRACT
		END;
		InsistentAction = POINTER TO ABSTRACT RECORD (Action) END;
		
		SafeAction = ABSTRACT RECORD 
			trapped-: BOOLEAN;
			(VAR a: SafeAction) Do, NEW, ABSTRACT
		END;

		SafeRecAction = ABSTRACT RECORD (SafeAction)
			(VAR a: SafeRecAction) Do;
			(VAR a: SafeRecAction) DoRec (VAR rec: ANYREC), NEW, ABSTRACT
		END;

	VAR
		itemsInTheQueue-: INTEGER;

	PROCEDURE DoLater (a: Action; notBefore: LONGINT);
	PROCEDURE RemoveAction (a: Action);
	PROCEDURE Ticks (): LONGINT;
	PROCEDURE GetTypeName (IN rec: ANYREC; OUT type: ARRAY OF CHAR);
	PROCEDURE SameType (IN ra, rb: ANYREC): BOOLEAN;
	PROCEDURE IsExtensionOf (IN ra, rb: ANYREC): BOOLEAN;
	PROCEDURE Is (IN rec: ANYREC; IN type: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE Extends (IN type, base: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE Level (IN type: ARRAY OF CHAR): INTEGER;
	PROCEDURE TypeLevel (IN rec: ANYREC): INTEGER;
	PROCEDURE AdrOf (IN rec: ANYREC): INTEGER;
	PROCEDURE Collect;
	PROCEDURE Try (VAR a: SafeAction);
	PROCEDURE TryRec (VAR a: SafeRecAction; VAR rec: ANYREC);

END Services.

Green color denote new features for version 2.0.

This module provides a variety of low-level services. Currently, only a timing service, a background processing service, and a reflection service is provided.
Actions are objects whose Do procedures are executed in a delayed fashion, when the system is idle. An action which re-installs itself whenever it is invoked operates as a non-preemptive background task.
The reflection service, unlike module Meta, only works on records, and it is also applicable to temporary variables on the stack (while Meta items typically denote long-lived variables).

Examples:
ObxActions  docu
ObxCubes  docu


CONST immediately
This value can be passed to DoLater as notBefore parameter, if the action must be executed as part of the currently executing command.

CONST now
This value can be passed to DoLater as notBefore parameter, if the action should be executed as quickly as possible, after the currently executing command.

CONST resolution
Time resolution in ticks per second. The current time can be inquired by procedure Ticks.

VAR actionsInTheQueue- : INTEGER;
Current abount of actions in the queue.

TYPE Action
ABSTRACT
Actions are objects whose Do procedures are called in a deferred way, when the system is idle again.

PROCEDURE (a: Action) Do-
NEW, ABSTRACT
For a registered action a, a.Do is called eventually, depending how it has been registered (see DoLater).

TYPE InsistentAction (Action)
ABSTRACT
Extension of basic Action, wich has higher prioryty then the mouse tracking loop proceed.
This action is not recommended for calling any graphical commands (Views.Update etc), because this can lead to instability of the framework.

TYPE SafeAction
ABSTRACT
SafeActions are objects whose Do procedures can be called in a 'protected' fashion: if a trap occurs during the execution of Do, control returns to the caller (as opposed to terminating the whole running command).

trapped-: BOOLEAN
TRUE if the last execution of Do ended in a trap; FALSE if Do returned successfully. 
Valid only after Try.

PROCEDURE (a: SafeAction) Do
NEW, ABSTRACT
Do is called in a protected fashion by Try.

TYPE SafeRecAction
ABSTRACT
SafeRecActions are like SafeActions, but their DoRec procedure has a reference record variable parameter.

trapped-: BOOLEAN
TRUE if the last execution of DoRec ended in a trap; FALSE if DoRec returned successfully. 
Valid only after TryRec.

PROCEDURE (a: SafeRecAction) DoRec (VAR rec: ANYREC)
NEW, ABSTRACT
DoRec is called in a protected fashion by TryRec.


PROCEDURE DoLater (a: Action; notBefore: LONGINT)
Register an action. Its Do procedure will be executed once, later when time permits. a.Do is called eventually after Ticks() has reached notBefore, or if notBefore = now. If the action's Do procedure should be executed more than once, it may call DoLater to re-install itself. A particular action can only be registered once; additional attempts have no effect.

Pre
a # NIL	20

PROCEDURE RemoveAction (a: Action)
Remove a registered action. If action a is not registered or NIL, nothing happens.

PROCEDURE Ticks (): LONGINT
Returns the current time in clock ticks. These ticks have a resolution of resolution., i.e., resolution ticks per second.

PROCEDURE GetTypeName (IN rec: ANYREC; OUT type: ARRAY OF CHAR)
Returns the name of a record type, in the form "module.type". If a pointer is passed, its record type is returned. If the record is anonymous, the pointer's type name is returned, e.g., "FormViews.StdView".

PROCEDURE SameType (IN ra, rb: ANYREC): BOOLEAN
Determines whether two record variables have (exactly!) the same type.

PROCEDURE IsExtensionOf (IN ra, rb: ANYREC): BOOLEAN
Determines whether the record type of ra is the same or an extension of the type of rb.

PROCEDURE Is (IN rec: ANYREC; IN type: ARRAY OF CHAR): BOOLEAN
Determines whether the record type of rec is the same or an extension of the type given in type (e.g., as "FormViews.View"). If type type was not found, FALSE is returned.

Pre
type # ""	20
type has form "module.type"	21

PROCEDURE Extends (IN type, base: ARRAY OF CHAR): BOOLEAN
Determines whether the record type type is the same or an extension of the type base. If type type or base was not found, FALSE is returned.

Pre
type # ""  &  base # ""	20
type and base have form "module.type"	21

PROCEDURE Level (IN type: ARRAY OF CHAR): INTEGER
Determines the extension level of a record type type. A newly introduced type has level 0. If type type was not found, -1 is returned.

Pre
type # ""	20
type has form "module.type"	21

PROCEDURE TypeLevel (IN rec: ANYREC): INTEGER
Determines the extension level of a record variable rec's type. A newly introduced type has level 0.

PROCEDURE AdrOf (IN rec: ANYREC): INTEGER
Returns the address of a record variable.

PROCEDURE Collect
Forces a garbage collection.

PROCEDURE Try (VAR a: SafeAction)
Calls a.Do. Try returns even if a trap occurs during the execution of Do. a.trapped can be analyzed to check if Do trapped or returned successfully.

PROCEDURE TryRec (VAR a: SafeAction; VAR rec: ANYREC)
Calls a.DoRec(rec). TryRec returns even if a trap occurs during the execution of DoRec. a.trapped can be analyzed to check if Do trapped or returned successfully.
