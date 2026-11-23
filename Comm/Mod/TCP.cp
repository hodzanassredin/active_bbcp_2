MODULE CommTCP;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT Kernel, Dialog, CommStreams;

	TYPE
		Hook* = POINTER TO ABSTRACT RECORD END;

	VAR
		hook: Hook;

	PROCEDURE SetHook* (h: Hook);
	BEGIN
		hook := h
	END SetHook;

	PROCEDURE (h: Hook) NewListener* (localAdr: ARRAY OF CHAR; OUT l: CommStreams.Listener; OUT res: INTEGER), NEW, ABSTRACT;

	PROCEDURE (h: Hook) NewStream* (localAdr, remoteAdr: ARRAY OF CHAR; OUT s: CommStreams.Stream; OUT res: INTEGER), NEW, ABSTRACT;

	PROCEDURE NewListener* (localAdr: ARRAY OF CHAR; OUT l: CommStreams.Listener; OUT res: INTEGER);
	BEGIN
		hook.NewListener(localAdr, l, res)
	END NewListener;

	PROCEDURE NewStream* (localAdr, remoteAdr: ARRAY OF CHAR; OUT s: CommStreams.Stream; OUT res: INTEGER);
	BEGIN
		hook.NewStream(localAdr, remoteAdr, s, res)
	END NewStream;

BEGIN
	IF Dialog.platform = Dialog.windows THEN
		Kernel.LoadMod("CommTCP__Win")
	ELSIF Dialog.platform = Dialog.linux THEN
		Kernel.LoadMod("CommTCP__Lin")
	ELSIF Dialog.platform = Dialog.openbsd THEN
		Kernel.LoadMod("CommTCP__Obsd")
	ELSIF Dialog.platform = Dialog.freebsd THEN
		Kernel.LoadMod("CommTCP__Fbsd")
	ELSIF Dialog.platform = Dialog.netbsd THEN
		Kernel.LoadMod("CommTCP__Nbsd")
	END
END CommTCP.
