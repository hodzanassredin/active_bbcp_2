MODULE CommV24__Win;
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

	IMPORT SYSTEM, WinApi, CommV24;

	CONST
		bits4* = 0; bits5* = 1; bits6* = 2; bits7* = 3; stop15* = 4; stop2* = 5;
		even* = 6; odd* = 7;
		inXonXoff* = 8; outXonXoff* = 9;
		inRTS* = 10; inDTR* = 11; outCTS* = 12; outDSR* = 13;
		inDTRon* = 14; inRTSon* = 15;
		nonblocking* = 31;

	TYPE
		Hook* = POINTER TO RECORD (CommV24.Hook) END;
		
		Connection* = POINTER TO RECORD (CommV24.Connection)
			hnd-: WinApi.HANDLE;	(* # 0: open *)
			opts: SET
		END;

	VAR
		hook: Hook;

	PROCEDURE (hook: Hook) IsConnected* (c: CommV24.Connection): BOOLEAN;
	BEGIN
		ASSERT(c # NIL, 20);
		RETURN (c(Connection).hnd # 0) & (c(Connection).hnd # -1)
	END IsConnected;

	PROCEDURE (hook: Hook) Open* (device: ARRAY OF CHAR; baud: INTEGER; opts: SET; OUT conn: CommV24.Connection);
		VAR c: Connection; h: WinApi.HANDLE; res: INTEGER; dcb: WinApi.DCB; to: WinApi.COMMTIMEOUTS;
		s: ARRAY WinApi.MAX_PATH OF CHAR;
	BEGIN
		ASSERT(opts * {even, odd} # {even, odd}, 20);
		IF bits4 IN opts THEN ASSERT(opts * {bits5, bits6, bits7} = {}, 21) END;
		IF bits5 IN opts THEN ASSERT(opts * {bits4, bits6, bits7} = {}, 21) END;
		IF bits6 IN opts THEN ASSERT(opts * {bits4, bits5, bits7} = {}, 21) END;
		IF bits7 IN opts THEN ASSERT(opts * {bits4, bits5, bits6} = {}, 21) END;
		ASSERT(opts * {inDTR, inDTRon} # {inDTR, inDTRon}, 22);
		ASSERT(opts * {inRTS, inRTSon} # {inRTS, inRTSon}, 23);
		IF (device[0] # "\") & (device[0] # "/") THEN
			s := "\\.\" + device
		ELSE
			s := device$
		END;
		h := WinApi.CreateFileW(
			s,  WinApi.GENERIC_READ +  WinApi.GENERIC_WRITE,
			{}, NIL,  WinApi.OPEN_EXISTING, {}, 0
		);
		IF h # -1 THEN
			dcb.DCBlength := SIZE(WinApi.DCB);
			res := WinApi.GetCommState(h, dcb);
			IF res # 0 THEN
				dcb.BaudRate := baud;
				dcb.fBits0 := {0};	(* binary *)
				IF opts * {even, odd} # {} THEN INCL(dcb.fBits0, 1) END;	(* check parity *)
				IF outCTS IN opts THEN INCL(dcb.fBits0, 2) END;	(* CTS out flow control *)
				IF outDSR IN opts THEN INCL(dcb.fBits0, 3) END;	(* DSR out flow control *)
				IF inDTR IN opts THEN INCL(dcb.fBits0, 5)	(* DTR flow control handshake *)
				ELSIF inDTRon IN opts THEN INCL(dcb.fBits0, 4) END;	(* DTR enable*)
				IF outXonXoff IN opts THEN INCL(dcb.fBits0, 8) END;	(* Xon/Xoff out flow control *)
				IF inXonXoff IN opts THEN INCL(dcb.fBits0, 9) END;	(* Xob/Xoff in flow control *)
				IF inRTS IN opts THEN INCL(dcb.fBits0, 13)	(* RTS flow control handshake *)
				ELSIF inRTSon IN opts THEN INCL(dcb.fBits0, 12) END;	(* RTS enable*)
				IF bits4 IN opts THEN dcb.ByteSize := 4X
				ELSIF bits5 IN opts THEN dcb.ByteSize := 5X
				ELSIF bits6 IN opts THEN dcb.ByteSize := 6X
				ELSIF bits7 IN opts THEN dcb.ByteSize := 7X
				ELSE dcb.ByteSize := 8X
				END;
				IF stop15 IN opts THEN dcb.StopBits := 1X
				ELSIF stop2 IN opts THEN dcb.StopBits := 2X
				ELSE dcb.StopBits := 0X
				END;
				IF even IN opts THEN dcb.Parity := 2X
				ELSIF odd IN opts THEN dcb.Parity := 1X
				ELSE dcb.Parity := 0X
				END;
				res := WinApi.SetCommState(h, dcb);
				IF res # 0 THEN
					to.ReadIntervalTimeout := 0;
					to.ReadTotalTimeoutMultiplier := 0;
					to.ReadTotalTimeoutConstant := 0;
					to.WriteTotalTimeoutMultiplier := 0;
					to.WriteTotalTimeoutConstant := 0;
					res := WinApi.SetCommTimeouts(h, to);
					IF res # 0 THEN
						NEW(c); c.hnd := h; c.opts := opts; conn := c
					END
				END
			END
		END
	END Open;

	PROCEDURE (hook: Hook) Close* (c: CommV24.Connection);
		VAR res: INTEGER;
	BEGIN
		IF hook.IsConnected(c) THEN
			ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
			res := WinApi.CloseHandle(c(Connection).hnd);
			c(Connection).hnd := -1
		END
	END Close;

	PROCEDURE (hook: Hook) SendByte* (c: CommV24.Connection; x: BYTE);
		VAR res, written: INTEGER;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		written := 0;
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		res := WinApi.WriteFile(c(Connection).hnd, SYSTEM.ADR(x), 1, written, NIL);
		CommV24.isLastSendDone := written = 1;
		IF ~(nonblocking IN c(Connection).opts) THEN
			ASSERT(res # 0, 100);
			ASSERT(written = 1, 101)
		END
	END SendByte;

	PROCEDURE (hook: Hook) SendBytes* (c: CommV24.Connection; IN x: ARRAY OF BYTE; beg, len: INTEGER);
		VAR res, written: INTEGER;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		ASSERT(LEN(x) >= beg + len, 22);
		ASSERT(len > 0, 23);
		res := WinApi.WriteFile(c(Connection).hnd, SYSTEM.ADR(x) + beg, len, written, NIL);
		CommV24.isLastSendDone := written = len;
		IF ~(nonblocking IN c(Connection).opts) THEN
			ASSERT(res # 0, 100);
			ASSERT(written = len, 101)
		END
	END SendBytes;

	PROCEDURE (hook: Hook) Available* (c: CommV24.Connection): INTEGER;
		VAR res: INTEGER; errors: SET; status: WinApi.COMSTAT;
	BEGIN
		IF hook.IsConnected(c) THEN
			errors := {};
			status.cbInQue := 0;
			res := WinApi.ClearCommError(c(Connection).hnd, errors, status);
			IF res = 0 THEN
				RETURN -1
			ELSE
				RETURN status.cbInQue
			END
		ELSE
			RETURN -1
		END
	END Available;

	PROCEDURE (hook: Hook) ReceiveByte* (c: CommV24.Connection; OUT x: BYTE);
		VAR res, read: INTEGER;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		read := 0;
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		res := WinApi.ReadFile(c(Connection).hnd, SYSTEM.ADR(x), 1, read, NIL);
		ASSERT(res # 0, 100);
		ASSERT(read = 1, 101)
	END ReceiveByte;

	PROCEDURE (hook: Hook) ReceiveBytes* (c: CommV24.Connection; OUT x: ARRAY OF BYTE; beg, len: INTEGER);
		VAR res, read: INTEGER;
	BEGIN
		IF len = 0 THEN RETURN END;
		ASSERT(hook.IsConnected(c), 21);
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		ASSERT(LEN(x) >= beg + len, 22);
		ASSERT(len > 0, 23);
		read := 0;
		res := WinApi.ReadFile(c(Connection).hnd, SYSTEM.ADR(x) + beg, len, read, NIL);
		ASSERT(res # 0, 100);
		ASSERT(read = len, 101)
	END ReceiveBytes;

	PROCEDURE (hook: Hook) SetBuffers* (c: CommV24.Connection; inpBufSize, outBufSize: INTEGER);
		VAR res: INTEGER;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		res := WinApi.SetupComm(c(Connection).hnd, inpBufSize, outBufSize);
		ASSERT(res # 0, 100)
	END SetBuffers;

	PROCEDURE (hook: Hook) SetDTR* (c: CommV24.Connection; on: BOOLEAN);
		VAR res: INTEGER;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		ASSERT(~(inDTR IN c(Connection).opts), 22);
		IF on THEN res := WinApi.EscapeCommFunction(c(Connection).hnd, WinApi.SETDTR)
		ELSE res := WinApi.EscapeCommFunction(c(Connection).hnd, WinApi.CLRDTR)
		END;
		ASSERT(res # 0, 100)
	END SetDTR;

	PROCEDURE (hook: Hook) SetRTS* (c: CommV24.Connection; on: BOOLEAN);
		VAR res: INTEGER;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		ASSERT(~(inRTS IN c(Connection).opts), 22);
		IF on THEN res := WinApi.EscapeCommFunction(c(Connection).hnd, WinApi.SETRTS)
		ELSE res := WinApi.EscapeCommFunction(c(Connection).hnd, WinApi.CLRRTS)
		END;
		ASSERT(res # 0, 100)
	END SetRTS;

	PROCEDURE (hook: Hook) SetBreak* (c: CommV24.Connection; on: BOOLEAN);
		VAR res: INTEGER;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		IF on THEN res := WinApi.EscapeCommFunction(c(Connection).hnd, WinApi.SETBREAK)
		ELSE res := WinApi.EscapeCommFunction(c(Connection).hnd, WinApi.CLRBREAK)
		END;
		ASSERT(res # 0, 100)
	END SetBreak;

	PROCEDURE (hook: Hook) CTSState* (c: CommV24.Connection): BOOLEAN;
		VAR res: INTEGER; s: SET;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		s := {};
		res := WinApi.GetCommModemStatus(c(Connection).hnd, s);
		ASSERT(res # 0, 100);
		RETURN s * WinApi.MS_CTS_ON # {}
	END CTSState;

	PROCEDURE (hook: Hook) DSRState* (c: CommV24.Connection): BOOLEAN;
		VAR res: INTEGER; s: SET;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		s := {};
		res := WinApi.GetCommModemStatus(c(Connection).hnd, s);
		ASSERT(res # 0, 100);
		RETURN s * WinApi.MS_DSR_ON # {}
	END DSRState;

	PROCEDURE (hook: Hook) CDState* (c: CommV24.Connection): BOOLEAN;
		VAR res: INTEGER; s: SET;
	BEGIN
		ASSERT(hook.IsConnected(c), 21);
		s := {};
		res := WinApi.GetCommModemStatus(c(Connection).hnd, s);
		ASSERT(res # 0, 100);
		RETURN s * WinApi.MS_RLSD_ON # {}
	END CDState;

BEGIN
	NEW(hook);
	CommV24.SetHook(hook)

END CommV24__Win.
