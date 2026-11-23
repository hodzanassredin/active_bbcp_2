MODULE LinTermios ["libc.so.6"];

	(*
		GNU/Linux
		i386
	*)

	CONST
		B0* = 0;
		B1200* = 9;
		B110* = 3;
		B1000000* = 4104;
		B134* = 4;
		B150* = 5;
		B115200* = 4098;
		B19200* = 14;
		B1800* = 10;
		B1152000* = 4105;
		B1500000* = 4106;
		B2500000* = 4108;
		B230400* = 4099;
		B2400* = 11;
		B200* = 6;
		B2000000* = 4107;
		B3000000* = 4109;
		B38400* = 15;
		B3500000* = 4110;
		B300* = 7;
		B460800* = 4100;
		B4800* = 12;
		B4000000* = 4111;
		B576000* = 4102;
		B500000* = 4101;
		B57600* = 4097;
		B50* = 1;
		B600* = 8;
		B75* = 2;
		B9600* = 13;
		B921600* = 4103;

		TCSANOW* = 0;
		TCSADRAIN* = 1;
		TCSAFLUSH* = 2;

		TCOOFF* = 0;
		TCOON* = 1;
		TCIOFF* = 2;
		TCION* = 3;
		TCIFLUSH* = 0;
		TCOFLUSH* = 1;
		TCIOFLUSH* = 2;

		VEOF* = 4;
		VEOL* = 11;
		VEOL2* = 16;
		VERASE* = 2;
		VWERASE* = 14;
		VKILL* = 3;
		VREPRINT* = 12;
		VINTR* = 0;
		VQUIT* = 1;
		VSUSP* = 10;
		VSTART* = 8;
		VSTOP* = 9;
		VLNEXT* = 15;
		VDISCARD* = 13;
		VMIN* = 6;
		VTIME* = 5;

		IGNBRK* = 1;
		BRKINT* = 2;
		IGNPAR* = 4;
		PARMRK* = 8;
		INPCK* = 16;
		ISTRIP* = 32;
		INLCR* = 64;
		IGNCR* = 128;
		ICRNL* = 256;
		IXON* = 1024;
		IXOFF* = 4096;
		IXANY* = 2048;
		IMAXBEL* = 8192;
		IUCLC* = 512;

		CSIZE* = 48;
		CS5* = 0;
		CS6* = 16;
		CS7* = 32;
		CS8* = 48;
		CSTOPB* = 64;
		CREAD* = 128;
		PARENB* = 256;
		PARODD* = 512;
		HUPCL* = 1024;
		CLOCAL* = 2048;
		CRTSCTS* = 2147483648;

		ECHOKE* = 2048;
		ECHOE* = 16;
		ECHOK* = 32;
		ECHO* = 8;
		ECHONL* = 64;
		ECHOPRT* = 1024;
		ECHOCTL* = 512;
		ISIG* = 1;
		ICANON* = 2;
		IEXTEN* = 32768;
		EXTPROC* = 65536;
		TOSTOP* = 256;
		FLUSHO* = 4096;
		PENDIN* = 16384;
		NOFLSH* = 128;
		XCASE* = 4;

		NCCS* = 32;

	TYPE
		int* = INTEGER;
		tcflag_t* = INTEGER;
		cc_t* = SHORTCHAR;
		speed_t* = INTEGER;

		termios* = RECORD [untagged]
			c_iflag*: tcflag_t; (* input mode flags *)
			c_oflag*: tcflag_t; (* output mode flags *)
			c_cflag*: tcflag_t; (* control mode flags *)
			c_lflag*: tcflag_t; (* local mode flags *)
			c_line*: cc_t; (* line discipline *)
			cc_c*: ARRAY [untagged] NCCS OF cc_t; (* control chars *)
			c_ispeed*: speed_t; (* input speed *)
			c_ospeed*: speed_t; (* output speed *)
		END;

	(* POSIX.1 *)
		PROCEDURE [ccall] cfgetispeed* (VAR tp: termios): speed_t;
		PROCEDURE [ccall] cfsetispeed* (VAR tp: termios; ispeed: speed_t): int;
		PROCEDURE [ccall] cfgetospeed* (VAR tp: termios): speed_t;
		PROCEDURE [ccall] cfsetospeed* (VAR tp: termios; ospeed: speed_t): int;
		PROCEDURE [ccall] tcgetattr* (fd: int; VAR tp: termios): int;
		PROCEDURE [ccall] tcsetattr* (fd: int; action: int; VAR tp: termios): int;

		PROCEDURE [ccall] tcdrain* (fd: int): int;
		PROCEDURE [ccall] tcflow* (fd: int; action: int): int;
		PROCEDURE [ccall] tcflush* (fd: int; action: int): int;
		PROCEDURE [ccall] tcsendbreak* (fd: int; len: int): int;

	(* extensions *)
(*
		PROCEDURE [ccall] cfsetspeed* (VAR tp: termios; speed: speed_t): int;
		PROCEDURE [ccall] cfmakeraw* (VAR tp: termios);
*)

END LinTermios.
