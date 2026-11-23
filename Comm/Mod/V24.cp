MODULE CommV24;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems, Alexander Iljin, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT Kernel, Dialog;

	CONST
		bits4* = 0; bits5* = 1; bits6* = 2; bits7* = 3; stop15* = 4; stop2* = 5;
		even* = 6; odd* = 7;
		inXonXoff* = 8; outXonXoff* = 9;
		inRTS* = 10; inDTR* = 11; outCTS* = 12; outDSR* = 13;
		inDTRon* = 14; inRTSon* = 15;
		nonblocking* = 31;

	TYPE
		Hook* = POINTER TO ABSTRACT RECORD END;
		Connection* = POINTER TO EXTENSIBLE RECORD END;

	VAR
		hook: Hook;
		isLastSendDone*: BOOLEAN;

	PROCEDURE SetHook* (h: Hook);
	BEGIN
		hook := h
	END SetHook;

	PROCEDURE (h: Hook) IsConnected* (c: Connection): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (h: Hook) Open* (device: ARRAY OF CHAR; baud: INTEGER; opts: SET; OUT conn: Connection), NEW, ABSTRACT;
	PROCEDURE (h: Hook) Close* (c: Connection), NEW, ABSTRACT;
	PROCEDURE (h: Hook) SendByte* (c: Connection; x: BYTE), NEW, ABSTRACT;
	PROCEDURE (h: Hook) SendBytes* (c: Connection; IN x: ARRAY OF BYTE; beg, len: INTEGER), NEW, ABSTRACT;
	PROCEDURE (h: Hook) Available* (c: Connection): INTEGER, NEW, ABSTRACT;
	PROCEDURE (h: Hook) ReceiveByte* (c: Connection; OUT x: BYTE), NEW, ABSTRACT;
	PROCEDURE (h: Hook) ReceiveBytes* (c: Connection; OUT x: ARRAY OF BYTE; beg, len: INTEGER), NEW, ABSTRACT;
	PROCEDURE (h: Hook) SetBuffers* (c: Connection; inpBufSize, outBufSize: INTEGER), NEW, ABSTRACT;
	PROCEDURE (h: Hook) SetDTR* (c: Connection; on: BOOLEAN), NEW, EMPTY;
	PROCEDURE (h: Hook) SetRTS* (c: Connection; on: BOOLEAN), NEW, EMPTY;
	PROCEDURE (h: Hook) SetBreak* (c: Connection; on: BOOLEAN), NEW, ABSTRACT;
	PROCEDURE (h: Hook) CTSState* (c: Connection): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (h: Hook) DSRState* (c: Connection): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (h: Hook) CDState* (c: Connection): BOOLEAN, NEW, ABSTRACT;

	PROCEDURE IsConnected* (c: Connection): BOOLEAN;
	BEGIN
		ASSERT(c # NIL, 20);
		RETURN hook.IsConnected(c)
	END IsConnected;

	PROCEDURE Open* (device: ARRAY OF CHAR; baud: INTEGER; opts: SET; OUT conn: Connection);
	BEGIN
		hook.Open(device, baud, opts, conn)
	END Open;

	PROCEDURE Close* (c: Connection);
	BEGIN
		hook.Close(c)
	END Close;

	PROCEDURE SendByte* (c: Connection; x: BYTE);
	BEGIN
		hook.SendByte(c, x)
	END SendByte;

	PROCEDURE SendBytes* (c: Connection; IN x: ARRAY OF BYTE; beg, len: INTEGER);
	BEGIN
		hook.SendBytes(c, x, beg, len)
	END SendBytes;

	PROCEDURE Available* (c: Connection): INTEGER;
	BEGIN
		RETURN hook.Available(c)
	END Available;

	PROCEDURE ReceiveByte* (c: Connection; OUT x: BYTE);
	BEGIN
		hook.ReceiveByte(c, x)
	END ReceiveByte;

	PROCEDURE ReceiveBytes* (c: Connection; OUT x: ARRAY OF BYTE; beg, len: INTEGER);
	BEGIN
		hook.ReceiveBytes(c, x, beg, len)
	END ReceiveBytes;

	PROCEDURE SetBuffers* (c: Connection; inpBufSize, outBufSize: INTEGER);
	BEGIN
		hook.SetBuffers(c, inpBufSize, outBufSize)
	END SetBuffers;

	PROCEDURE SetDTR* (c: Connection; on: BOOLEAN);
	BEGIN
		hook.SetDTR(c, on)
	END SetDTR;

	PROCEDURE SetRTS* (c: Connection; on: BOOLEAN);
	BEGIN
		hook.SetRTS(c, on)
	END SetRTS;

	PROCEDURE SetBreak* (c: Connection; on: BOOLEAN);
	BEGIN
		hook.SetBreak(c, on)
	END SetBreak;

	PROCEDURE CTSState* (c: Connection): BOOLEAN;
	BEGIN
		RETURN hook.CTSState(c) 
	END CTSState;

	PROCEDURE DSRState* (c: Connection): BOOLEAN;
	BEGIN
		RETURN hook.DSRState(c)
	END DSRState;

	PROCEDURE CDState* (c: Connection): BOOLEAN;
	BEGIN
		RETURN hook.CDState(c)
	END CDState;

BEGIN
	IF Dialog.platform = Dialog.windows THEN
		Kernel.LoadMod("CommV24__Win")
	ELSIF Dialog.platform = Dialog.linux THEN
		Kernel.LoadMod("CommV24__Lin")
	ELSIF Dialog.platform = Dialog.openbsd THEN
		Kernel.LoadMod("CommV24__Obsd")
	ELSIF Dialog.platform = Dialog.freebsd THEN
		Kernel.LoadMod("CommV24__Fbsd")
	ELSIF Dialog.platform = Dialog.netbsd THEN
		Kernel.LoadMod("CommV24__Nbsd")
	END
END CommV24.
