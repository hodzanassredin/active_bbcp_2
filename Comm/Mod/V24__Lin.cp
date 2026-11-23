MODULE CommV24__Lin;

	(*
		Alexander Shiryaev, 2012.11

		Linux, OpenBSD, FreeBSD, NetBSD
		32-bit

		Interface corresponds to original BlackBox 1.6 CommV24
	*)

	IMPORT SYSTEM, Libc := LinLibc, Termios := LinTermios, Ioctl := LinIoctl, CommV24;

	CONST
		bits4* = 0; bits5* = 1; bits6* = 2; bits7* = 3; stop15* = 4; stop2* = 5;
		even* = 6; odd* = 7;
		inXonXoff* = 8; outXonXoff* = 9;
		inRTS* = 10; inDTR* = 11; outCTS* = 12; outDSR* = 13;
		nonblocking* = 31;

	TYPE
		Hook* = POINTER TO RECORD (CommV24.Hook) END;

		Connection* = POINTER TO RECORD (CommV24.Connection)
			hnd: INTEGER;	(* # -1: open *)
			opts: SET
		END;

	VAR
		hook: Hook;

	PROCEDURE DecodeBaud (baud: INTEGER; OUT speed: Termios.speed_t);
	BEGIN
		IF baud = 50 THEN speed := Termios.B50
		ELSIF baud = 75 THEN speed := Termios.B75
		ELSIF baud = 110 THEN speed := Termios.B110
		ELSIF baud = 134 THEN speed := Termios.B134
		ELSIF baud = 150 THEN speed := Termios.B150
		ELSIF baud = 200 THEN speed := Termios.B200
		ELSIF baud = 300 THEN speed := Termios.B300
		ELSIF baud = 600 THEN speed := Termios.B600
		ELSIF baud = 1200 THEN speed := Termios.B1200
		ELSIF baud = 1800 THEN speed := Termios.B1800
		ELSIF baud = 2400 THEN speed := Termios.B2400
		ELSIF baud = 4800 THEN speed := Termios.B4800
		(* ELSIF baud = 7200 THEN speed := Termios.B7200 *) (* not present in Linux *)
		ELSIF baud = 9600 THEN speed := Termios.B9600
		(* ELSIF baud = 14400 THEN speed := Termios.B14400 *) (* not present in Linux *)
		ELSIF baud = 19200 THEN speed := Termios.B19200
		(* ELSIF baud = 28800 THEN speed := Termios.B28800 *) (* not present in Linux *)
		ELSIF baud = 38400 THEN speed := Termios.B38400
		ELSIF baud = 57600 THEN speed := Termios.B57600
		(* ELSIF baud = 76800 THEN speed := Termios.B76800 *) (* not present in Linux *)
		ELSIF baud = 115200 THEN speed := Termios.B115200
		ELSIF baud = 230400 THEN speed := Termios.B230400
		ELSE HALT(100)
		END
	END DecodeBaud;
	
	PROCEDURE (hook: Hook) IsConnected* (c: CommV24.Connection): BOOLEAN;
	BEGIN
		ASSERT(c # NIL, 20);
		RETURN (c(Connection).hnd # 0) & (c(Connection).hnd # -1)
	END IsConnected;

	PROCEDURE (hook: Hook) Open* (device: ARRAY OF CHAR; baud: INTEGER; opts: SET; OUT conn: CommV24.Connection);
		VAR c: Connection;
			fd: INTEGER;
			ss: ARRAY Libc.NAME_MAX + 1 OF SHORTCHAR;
			res: INTEGER;
			t: Termios.termios;
			speed: Termios.speed_t;
			flags: SET;
	BEGIN
		conn := NIL;

		ASSERT(opts * {even,odd} # {even,odd}, 20);

		IF bits4 IN opts THEN ASSERT(opts * {bits5,bits6,bits7} = {}, 21) END;
		IF bits5 IN opts THEN ASSERT(opts * {bits4,bits6,bits7} = {}, 22) END;
		IF bits6 IN opts THEN ASSERT(opts * {bits4,bits5,bits7} = {}, 23) END;
		IF bits7 IN opts THEN ASSERT(opts * {bits4,bits5,bits6} = {}, 24) END;

		ASSERT(opts * {stop15,stop2} # {stop15,stop2}, 25);
		IF stop15 IN opts THEN ASSERT(opts * {bits4,bits5} # {}, 26) END;
		IF stop2 IN opts THEN ASSERT(opts * {bits4,bits5} = {}, 27) END;

		DecodeBaud(baud, speed);

		ss := SHORT(device$);

		flags := Libc.O_RDWR;
		IF nonblocking IN opts THEN flags := flags + Libc.O_NONBLOCK END;
		fd := Libc.open(ss, flags, {1,4} (* 022 *));
		IF fd # -1 THEN
			res := Termios.tcgetattr(fd, t);
			IF res = 0 THEN
				t.c_iflag := Termios.IGNBRK (* ignore BREAK condition *)
					+ Termios.IGNPAR (* ignore (discard) parity errors *);
				IF opts * {even,odd} # {} THEN
					t.c_iflag := t.c_iflag + Termios.INPCK (* enable input parity checking *)
				END;

				t.c_oflag := 0;

				t.c_cflag := Termios.CREAD (* enable receiver *)
					+ Termios.CLOCAL (* ignore modem status lines *);
				IF bits4 IN opts THEN HALT(126) (* not implemented *)
				ELSIF bits5 IN opts THEN t.c_cflag := t.c_cflag + Termios.CS5
				ELSIF bits6 IN opts THEN t.c_cflag := t.c_cflag + Termios.CS6
				ELSIF bits7 IN opts THEN t.c_cflag := t.c_cflag + Termios.CS7
				ELSE t.c_cflag := t.c_cflag + Termios.CS8
				END;
				IF opts * {stop15,stop2} # {} THEN t.c_cflag := t.c_cflag + Termios.CSTOPB END;
				IF even IN opts THEN t.c_cflag := t.c_cflag + Termios.PARENB
				ELSIF odd IN opts THEN t.c_cflag := t.c_cflag + Termios.PARENB + Termios.PARODD
				(* ELSE parity none *)
				END;
				IF outCTS IN opts THEN HALT(126) END; (* CTS out flow control *)
				IF outDSR IN opts THEN HALT(126) END; (* DSR out flow control *)
				IF inDTR IN opts THEN HALT(126) END; (* DTR flow control handshake *)
				IF outXonXoff IN opts THEN HALT(126) END; (* Xon/Xoff out flow control *)
				IF inXonXoff IN opts THEN HALT(126) END; (* Xon/Xoff in flow control *)
				IF inRTS IN opts THEN HALT(126) END; (* RTS flow control handshake *)

				t.c_lflag := 0;

				res := Termios.cfsetispeed(t, speed);
				IF res = 0 THEN
					res := Termios.cfsetospeed(t, speed);
					IF res = 0 THEN
						res := Termios.tcsetattr(fd, Termios.TCSANOW, t);
						IF res = 0 THEN
							res := Termios.tcflush(fd, Termios.TCIOFLUSH);
							IF res = 0 THEN
								NEW(c); c.hnd := fd; c.opts := opts; conn := c
							ELSE res := Libc.close(fd)
							END
						ELSE res := Libc.close(fd)
						END
					ELSE res := Libc.close(fd)
					END
				ELSE res := Libc.close(fd)
				END
			ELSE res := Libc.close(fd)
			END
		END
	END Open;

	PROCEDURE (hook: Hook) Close* (c: CommV24.Connection);
		VAR res: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20);
		IF c(Connection).hnd # -1 THEN
			res := Libc.close(c(Connection).hnd);
			c(Connection).hnd := -1
		END
	END Close;

	PROCEDURE (c: Connection) FINALIZE-;
	VAR res: INTEGER;
	BEGIN
		IF c.hnd # -1 THEN
			res := Libc.close(c(Connection).hnd);
			c(Connection).hnd := -1
		END
	END FINALIZE;

	PROCEDURE (hook: Hook) SendByte* (c: CommV24.Connection; x: BYTE);
		VAR res: Libc.ssize_t;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		res := Libc.write(c(Connection).hnd, SYSTEM.ADR(x), 1);
		CommV24.isLastSendDone := res = 1;
		IF ~(nonblocking IN c(Connection).opts) THEN ASSERT(res = 1, 100) END
	END SendByte;

	PROCEDURE (hook: Hook) SendBytes* (c: CommV24.Connection; IN x: ARRAY OF BYTE; beg, len: INTEGER);
		VAR res: Libc.ssize_t;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		ASSERT(LEN(x) >= beg + len, 22);
		ASSERT(len > 0, 23);
		res := Libc.write(c(Connection).hnd, SYSTEM.ADR(x) + beg, len);
		CommV24.isLastSendDone := res = len;
		IF ~(nonblocking IN c(Connection).opts) THEN ASSERT(res = len, 100) END
	END SendBytes;

	PROCEDURE (hook: Hook) Available* (c: CommV24.Connection): INTEGER;
		VAR res: INTEGER;
			read: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		read := -1;
		res := Ioctl.ioctl1(c(Connection).hnd, Ioctl.FIONREAD, read);
		IF res = -1 THEN
			CommV24.Close(c);
			read := -1
		END;
		RETURN read
	END Available;

	PROCEDURE (hook: Hook) ReceiveByte* (c: CommV24.Connection; OUT x: BYTE);
		VAR res: Libc.ssize_t;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		res := Libc.read(c(Connection).hnd, SYSTEM.ADR(x), 1);
		ASSERT(res = 1, 100)
	END ReceiveByte;

	PROCEDURE (hook: Hook) ReceiveBytes* (c: CommV24.Connection; OUT x: ARRAY OF BYTE; beg, len: INTEGER);
		VAR res: Libc.ssize_t;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		ASSERT(LEN(x) >= beg + len, 22);
		ASSERT(len > 0, 23);
		res := Libc.read(c(Connection).hnd, SYSTEM.ADR(x) + beg, len);
		ASSERT(res = len, 100)
	END ReceiveBytes;

	PROCEDURE (hook: Hook) SetBuffers* (c: CommV24.Connection; inpBufSize, outBufSize: INTEGER);
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		(* HALT(126) *)
	END SetBuffers;

	PROCEDURE (hook: Hook) SetDTR* (c: CommV24.Connection; on: BOOLEAN);
		VAR res: INTEGER;
			modembits: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		ASSERT(~(inDTR IN c(Connection).opts), 22);
		res := Ioctl.ioctl1(c(Connection).hnd, Ioctl.TIOCMGET, modembits);
		ASSERT(res # -1, 100);
		IF on THEN
			modembits := ORD(BITS(modembits) + Ioctl.TIOCM_DTR)
		ELSE
			modembits := ORD(BITS(modembits) - Ioctl.TIOCM_DTR)
		END;
		res := Ioctl.ioctl1(c(Connection).hnd, Ioctl.TIOCMSET, modembits);
		ASSERT(res # -1, 101)
	END SetDTR;

	PROCEDURE (hook: Hook) SetRTS* (c: CommV24.Connection; on: BOOLEAN);
		VAR res: INTEGER;
			modembits: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		ASSERT(~(inRTS IN c(Connection).opts), 22);
		res := Ioctl.ioctl1(c(Connection).hnd, Ioctl.TIOCMGET, modembits);
		ASSERT(res # -1, 100);
		IF on THEN
			modembits := ORD(BITS(modembits) + Ioctl.TIOCM_RTS)
		ELSE
			modembits := ORD(BITS(modembits) - Ioctl.TIOCM_RTS)
		END;
		res := Ioctl.ioctl1(c(Connection).hnd, Ioctl.TIOCMSET, modembits);
		ASSERT(res # -1, 101)
	END SetRTS;

	PROCEDURE (hook: Hook) SetBreak* (c: CommV24.Connection; on: BOOLEAN);
		VAR res: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		IF on THEN
			res := Ioctl.ioctl0(c(Connection).hnd, Ioctl.TIOCSBRK)
		ELSE
			res := Ioctl.ioctl0(c(Connection).hnd, Ioctl.TIOCCBRK)
		END;
		ASSERT(res # -1, 100)
	END SetBreak;

	PROCEDURE (hook: Hook) CTSState* (c: CommV24.Connection): BOOLEAN;
		VAR res: INTEGER;
			modembits: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		res := Ioctl.ioctl1(c(Connection).hnd, Ioctl.TIOCMGET, modembits);
		ASSERT(res # -1, 100);
		RETURN BITS(modembits) * Ioctl.TIOCM_CTS = Ioctl.TIOCM_CTS
	END CTSState;

	PROCEDURE (hook: Hook) DSRState* (c: CommV24.Connection): BOOLEAN;
		VAR res: INTEGER;
			modembits: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		res := Ioctl.ioctl1(c(Connection).hnd, Ioctl.TIOCMGET, modembits);
		ASSERT(res # -1, 100);
		RETURN BITS(modembits) * Ioctl.TIOCM_DSR = Ioctl.TIOCM_DSR
	END DSRState;

	PROCEDURE (hook: Hook) CDState* (c: CommV24.Connection): BOOLEAN;
		VAR res, errno: INTEGER;
			modembits: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(c(Connection).hnd # -1, 21);
		res := Ioctl.ioctl1(c(Connection).hnd, Ioctl.TIOCMGET, modembits);
		ASSERT(res # -1, 100);
		RETURN BITS(modembits) * Ioctl.TIOCM_CAR = Ioctl.TIOCM_CAR
	END CDState;

BEGIN
	NEW(hook);
	CommV24.SetHook(hook)

END CommV24__Lin.
