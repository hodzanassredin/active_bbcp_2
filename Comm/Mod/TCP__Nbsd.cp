MODULE CommTCP__Nbsd;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	references	= "see the CommStreams documentation for details on the semantics of this driver module"
	changes	= ""
	issues	= ""

**)

	(*
		Alexander Shiryaev, 2012.11, 2016.11:
			NetBSD CommTCP
			Based on 1.6 Windows CommTCP
	*)

	IMPORT
		SYSTEM, Libc := NbsdLibc, Net := NbsdNet, Ioctl := NbsdIoctl,
			Strings, Dialog, CommStreams, CommTCP;

	CONST
		hostSpecificError = -1;	(* host-specific error codes *)
		INVALID_SOCKET = Net.INVALID_SOCKET;
		SOCKET_ERROR = Net.SOCKET_ERROR;

	TYPE
		Stream = POINTER TO RECORD (CommStreams.Stream)
			sock: Net.SOCKET;	(* socket must be in non-blocking mode, otherwise recv() or send() could block *)
			remoteAdr: CommStreams.Adr	(* remoteAdr # NIL *)
		END;

		Listener = POINTER TO RECORD (CommStreams.Listener)
			sock: Net.SOCKET;
			localAdr: CommStreams.Adr	(* localAdr # NIL *)
		END;

		Hook* = POINTER TO RECORD (CommTCP.Hook) END;

	VAR
		debug*: BOOLEAN;
		hook: Hook;

	PROCEDURE Errno (): INTEGER;
		VAR errno: INTEGER;
	BEGIN
		SYSTEM.GET(Libc.__errno_location(), errno);
		RETURN errno
	END Errno;

	(* auxiliary procedures *)

	PROCEDURE CopyOfAdrString (a: CommStreams.Adr): CommStreams.Adr;
		VAR b: CommStreams.Adr;
	BEGIN
		NEW(b, LEN(a)); b := a;
		RETURN b
	END CopyOfAdrString;

	PROCEDURE Error (msg: ARRAY OF CHAR; errno: INTEGER);
		VAR s: ARRAY 128 OF CHAR; errnostr: ARRAY 8 OF CHAR;
	BEGIN
		IF debug THEN
			CASE errno OF
			  Libc.EINTR: s := "EINTR, interrupted system call"
			| Libc.EBADF: s := "EBADF, bad file descriptor"
			| Libc.EACCES: s := "EACCES, permission denied"
			| Libc.EFAULT: s := "EFAULT, bad address"
			| Libc.EINVAL: s := "EINVAL, invalid argument"
			| Libc.ENFILE: s := "ENFILE, too many open files in system"
			| Libc.EMFILE: s := "EMFILE, too many open files"
			| Libc.EPIPE: s := "EPIPE, broken pipe"
			| Libc.EAGAIN: s := "EAGAIN, resource temporarily unavailable"
			| Libc.EINPROGRESS: s := "EINPROGRESS, operation now in progress"
			| Libc.EALREADY: s := "EALREADY, operation already in progress"
			| Libc.ENOTSOCK: s := "ENOTSOCK, socket operation on non-socket"
			| Libc.EDESTADDRREQ: s := "EDESTADDRREQ, destination address required"
			| Libc.EMSGSIZE: s := "EMSGSIZE, message too long"
			| Libc.ENOPROTOOPT: s := "ENOPROTOOPT, protocol not available"
			| Libc.EPROTONOSUPPORT: s := "EPROTONOSUPPORT, protocol not supported"
			| Libc.EAFNOSUPPORT: s := "EAFNOSUPPORT, address family not supported by protocol family"
			| Libc.EADDRINUSE: s := "EADDRINUSE, address already in use"
			| Libc.EADDRNOTAVAIL: s := "EADDRNOTAVAIL, can't assign requested address"
			| Libc.ENETDOWN: s := "ENETDOWN, network is down"
			| Libc.ENETUNREACH: s := "ENETUNREACH, network is unreachable"
			| Libc.ENOBUFS: s := "ENOBUFS, no buffer space available"
			| Libc.EISCONN: s := "EISCONN, socket is already connected"
			| Libc.ENOTCONN: s := "ENOTCONN, socket is not connected"
			| Libc.ETIMEDOUT: s := "ETIMEDOUT, operation timed out"
			| Libc.ECONNREFUSED: s := "ECONNREFUSED, connection refused"
			| Libc.EHOSTDOWN: s := "EHOSTDOWN, host is down"
			| Libc.EHOSTUNREACH: s := "EHOSTUNREACH, no route to host"
			ELSE s := "(please look up the error number in the NbsdLibc symbol file and in the NetBSD documentation)"
			END;
			Strings.IntToString(errno, errnostr);
			Dialog.ShowParamMsg("^0: error ^1 (^2)", msg, errnostr, s)
		END
	END Error;

	PROCEDURE GAIError (msg: ARRAY OF CHAR; errno: INTEGER);
		VAR s: ARRAY 128 OF CHAR; errnostr: ARRAY 12 OF CHAR;
	BEGIN
		IF debug THEN
			CASE errno OF
			  Net.EAI_ADDRFAMILY: s := "EAI_ADDRFAMILY, address family for hostname not supported"
			| Net.EAI_AGAIN: s := "EAI_AGAIN, temporary failure in name resolution"
			| Net.EAI_BADFLAGS: s := "EAI_BADFLAGS, invalid value for ai_flags"
			| Net.EAI_BADHINTS: s := "EAI_BADHINTS, invalid value for hints"
			| Net.EAI_FAIL: s := "EAI_FAIL, non-recoverable failure in name resolution"
			| Net.EAI_FAMILY: s := "EAI_FAMILY, ai_family not supported"
			| Net.EAI_MEMORY: s := "EAI_MEMORY, memory allocation failure"
			| Net.EAI_NODATA: s := "EAI_NODATA, no address associated with hostname"
			| Net.EAI_NONAME: s := "EAI_NONAME, hostname or servname not provided, or not known"
			| Net.EAI_OVERFLOW: s := "EAI_OVERFLOW, argument buffer overflow"
			| Net.EAI_PROTOCOL: s := "EAI_PROTOCOL, resolved protocol is unknown"
			| Net.EAI_SERVICE: s := "EAI_SERVICE, servname not supported for ai_socktype"
			| Net.EAI_SOCKTYPE: s := "EAI_SOCKTYPE, ai_socktype not supported"
			| Net.EAI_SYSTEM: s := "EAI_SYSTEM, system error returned in errno"
			ELSE s := "(please look up the error number in the NbsdNet symbol file and in the NetBSD documentation)"
			END;
			Strings.IntToString(errno, errnostr);
			Dialog.ShowParamMsg("^0: error ^1 (^2)", msg, errnostr, s)
		END
	END GAIError;

	PROCEDURE IsIPAddress (name: ARRAY OF CHAR): BOOLEAN;
		(* returns TRUE iff name only contains decimal digits and "." characters *)
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE ("0" <= name[i]) & (name[i] <= "9") OR (name[i] = ".") DO INC(i) END;
		RETURN name[i] = 0X
	END IsIPAddress;

	PROCEDURE ParseAdr (adr: ARRAY OF CHAR; OUT addr: ARRAY OF CHAR; OUT port: INTEGER; OUT ok: BOOLEAN);
		(* parse address specification with syntax addr [ ":" portnumber ] *)
		VAR i, j, res: INTEGER; portstr: ARRAY 16 OF CHAR;
	BEGIN
		i := 0; j := 0; port := 0; ok := TRUE;
		WHILE (adr[i] # 0X) & (adr[i] # ":") DO
			IF j < LEN(addr) - 1 THEN addr[j] := adr[i]; INC(j) END;
			INC(i)
		END;
		ok := i = j;
		addr[j] := 0X;
		IF ok & (adr[i] = ":") THEN
			INC(i); j := 0;
			WHILE adr[i] # 0X DO
				portstr[j] := adr[i]; INC(i);
				IF j < LEN(portstr) - 1 THEN INC(j) END
			END;
			portstr[j] := 0X;
			Strings.StringToInt(portstr, port, res);
			ok := res = 0
		END
	END ParseAdr;

	PROCEDURE ParseLocalAdr (IN adr: ARRAY OF CHAR;
		OUT peername: ARRAY OF CHAR; OUT port: INTEGER; OUT ok: BOOLEAN
	);
		VAR i, res: INTEGER;
	BEGIN
		IF adr = "" THEN
			port := 0; peername := ""; ok := TRUE	(* default port number 0 means 'don't care which port is used' *)
		ELSE
			i := 0;
			WHILE (adr[i] # 0X) & ((adr[i] >= "0") & (adr[i] <= "9")) OR (adr[i] = " ") DO INC(i) END;
			IF adr[i] # 0X THEN ParseAdr(adr, peername, port, ok)
			ELSE Strings.StringToInt(adr, port, res); peername := ""; ok := res = 0
			END
		END
	END ParseLocalAdr;

	PROCEDURE NameToAdr (IN peername: ARRAY OF CHAR; VAR inaddr: Net.in_addr; OUT ok: BOOLEAN);
		VAR shortPName: ARRAY 64 OF SHORTCHAR;
			res: Net.int;
			result: Net.Ptraddrinfo;
			hints: Net.addrinfo;
			addr: Net.Ptrsockaddr_in;
	BEGIN
		shortPName := SHORT(peername$);
		IF IsIPAddress(peername) THEN
			IF shortPName = "" THEN inaddr.S_un.S_addr := Net.INADDR_ANY
			ELSE inaddr.S_un.S_addr := Net.inet_addr(shortPName)
			END;
			ok := inaddr.S_un.S_addr # Net.INADDR_NONE
		ELSE
			hints.ai_flags := {};
			hints.ai_family := Net.AF_INET;
			hints.ai_socktype := Net.SOCK_STREAM;
			hints.ai_protocol := Net.IPPROTO_TCP;
			hints.ai_addrlen := 0;
			hints.ai_addr := NIL;
			hints.ai_canonname := NIL;
			hints.ai_next := NIL;
			res := Net.getaddrinfo(shortPName, NIL, hints, result);
			ok := res = 0;
			IF ok THEN
				ASSERT(result # NIL);
				IF (result.ai_family = Net.AF_INET)
						& (result.ai_socktype = Net.SOCK_STREAM)
						& (result.ai_protocol = Net.IPPROTO_TCP)
						& (result.ai_addr # NIL) THEN
					addr := SYSTEM.VAL(Net.Ptrsockaddr_in, result.ai_addr);
					inaddr := addr.sin_addr
				ELSE ok := FALSE
				END;
				Net.freeaddrinfo(result);
				ASSERT(ok)
			ELSE GAIError("getaddrinfo()", res)
			END
		END
	END NameToAdr;

	PROCEDURE MakeAdr (adr: Net.sockaddr_in; OUT s: CommStreams.Adr);
		VAR ipadr, n: INTEGER; temp, buf: ARRAY 64 OF CHAR;
	BEGIN
		ipadr := adr.sin_addr.S_un.S_addr; temp := "";
		IF ipadr # 0 THEN
			n := ipadr MOD 256;
			Strings.IntToString(n, temp);
			n := SYSTEM.LSH(ipadr, -8); n := n MOD 256;
			Strings.IntToString(n, buf); temp := temp + "."+ buf;
			n := SYSTEM.LSH(ipadr, -16); n := n MOD 256;
			Strings.IntToString(n, buf); temp := temp + "."+ buf;
			n := SYSTEM.LSH(ipadr, -24); n := n MOD 256;
			Strings.IntToString(n, buf); temp := temp + "." + buf + ":"
		END;
		n := (adr.sin_port MOD 256) * 256 + (adr.sin_port DIV 256) MOD 256;
		Strings.IntToString(n, buf); temp := temp + buf;
		NEW(s, LEN(temp$) + 1); s^ := temp$
	END MakeAdr;

	(* Linux, FreeBSD, OpenBSD, 32-bit *)
		PROCEDURE FD_ZERO (VAR set: Net.fd_set);
			VAR i: INTEGER;
		BEGIN
			i := LEN(set); REPEAT DEC(i); set[i] := {} UNTIL i = 0
		END FD_ZERO;

		PROCEDURE FD_SET (fd: Net.SOCKET; VAR set: Net.fd_set);
		BEGIN
			INCL(set[fd DIV 32], fd MOD 32)
		END FD_SET;

	PROCEDURE MakeFdSet (socket: Net.SOCKET; OUT set: Net.fd_set);
	BEGIN
		FD_ZERO(set); FD_SET(socket, set)
	END MakeFdSet;

	(* Listener *)

	PROCEDURE (l: Listener) LocalAdr (): CommStreams.Adr;
	BEGIN
		RETURN CopyOfAdrString(l.localAdr)
	END LocalAdr;

	PROCEDURE (l: Listener) Accept (OUT s: CommStreams.Stream);
		VAR timeout: Net.timeval; set: Net.fd_set; res, namelen: INTEGER;
			sock: Net.SOCKET; tcpstream: Stream;
			adr: Net.sockaddr_storage;
	BEGIN
		timeout.tv_sec := 0; timeout.tv_usec := 0;
		MakeFdSet(l.sock, set);
		res := Net.select(l.sock + 1, set, NIL, NIL, timeout);
		ASSERT(res # SOCKET_ERROR, 100);
		IF res > 0 THEN
			namelen := SIZE(Net.sockaddr_storage);
			sock := Net.accept(l.sock, SYSTEM.VAL(Net.sockaddr, adr), namelen);
			ASSERT(sock # INVALID_SOCKET, 101);
			namelen := 1;	(* = 'true' *)
			res := Ioctl.ioctl1(sock, Ioctl.FIONBIO, namelen);	(* set to non-blocking mode *)
			ASSERT(res = 0, 102);
			NEW(tcpstream); tcpstream.sock := sock;
			MakeAdr(SYSTEM.VAL(Net.sockaddr_in, adr), tcpstream.remoteAdr);
			s := tcpstream
		END
	END Accept;

	PROCEDURE (l: Listener) Close;
		VAR res: INTEGER;
	BEGIN
		IF l.sock # INVALID_SOCKET THEN
			res := Libc.close(l.sock);
			l.sock := INVALID_SOCKET
		END
	END Close;

	PROCEDURE (l: Listener) FINALIZE-;
	BEGIN
		WITH l: Listener DO
			IF l.sock # INVALID_SOCKET THEN
				l.Close
			END
		END
	END FINALIZE;

	PROCEDURE NewListener* (localAdr: ARRAY OF CHAR; OUT l: CommStreams.Listener; OUT res: INTEGER);
		(* localAdr must contain a port number *)
		CONST SOMAXCONN = 5;	(* use default length of listener backlog queue *)
		VAR portnr, namelen, optval: INTEGER; ok: BOOLEAN; tcplistener: Listener;
			adr: Net.sockaddr_in; sock: Net.SOCKET;
			peername: ARRAY 64 OF CHAR;
	BEGIN
		l := NIL;
		ParseLocalAdr(localAdr, peername, portnr, ok);
		IF ok & (portnr >= 0) THEN	(* only non-negative port numbers are legal *)
			sock := Net.socket(Net.AF_INET, Net.SOCK_STREAM, Net.IPPROTO_TCP);
			ASSERT(sock # INVALID_SOCKET, 100);
			optval := 1;
			res := Net.setsockopt(sock, Net.SOL_SOCKET, Net.SO_REUSEADDR,
				SYSTEM.ADR(optval), SIZE(INTEGER));
			ASSERT(res = 0, 101);
			adr.sin_family := SHORT(CHR(Net.AF_INET));
			adr.sin_port := Net.htons(SHORT(portnr));
			NameToAdr(peername, adr.sin_addr, ok);
			IF ok THEN
				res := Net.bind(sock, SYSTEM.VAL(Net.sockaddr, adr), SIZE(Net.sockaddr_in));
				IF res = 0 THEN
					res := Net.listen(sock, SOMAXCONN);
					ASSERT(res = 0, 102);
					NEW(tcplistener); tcplistener.sock := sock;
					namelen := SIZE(Net.sockaddr_in);
					res := Net.getsockname(sock, SYSTEM.VAL(Net.sockaddr, adr), namelen);
					ASSERT(res = 0, 103);
					MakeAdr(adr, tcplistener.localAdr);
					l := tcplistener;
					res := CommStreams.done
				ELSE 
					res := CommStreams.localAdrInUse
				END
			ELSE
				res := Libc.close(sock);
				res := CommStreams.invalidLocalAdr
			END
		ELSE 
			res := CommStreams.invalidLocalAdr
		END
	END NewListener;


	(* Stream *)

	PROCEDURE (s: Stream) RemoteAdr (): CommStreams.Adr;
	BEGIN
		RETURN CopyOfAdrString(s.remoteAdr)
	END RemoteAdr;

	PROCEDURE (s: Stream) IsConnected (): BOOLEAN;
		(* Give an educated guess on whether the peer has closed the connection. *)
		(* This is not a guarantee that data sent on s will arrive at the peer. *)
		VAR timeout: Net.timeval; set: Net.fd_set; n, res, avail: INTEGER;
	BEGIN
		IF s.sock = INVALID_SOCKET THEN
			RETURN FALSE
		ELSE
			timeout.tv_sec := 0; timeout.tv_usec := 0;
			MakeFdSet(s.sock, set);
			n := Net.select(s.sock + 1, set, NIL, NIL, timeout);
			ASSERT(n # SOCKET_ERROR, 100);
			IF n = 1 THEN	(* a recv on s.sock would not block; find out whether there is data queued *)
				res := Ioctl.ioctl1(s.sock, Ioctl.FIONREAD, avail);
				ASSERT(res = 0, 101);
				(* if there is data queued, we assume the socket is still open.
					if no data is queued, then we know that a recv can only return with zero bytes
					read, telling us that the socket has been closed. *)
				RETURN avail > 0
			ELSE (* a recv on s.sock would block, so the peer has not closed the socket yet or the connect failed entirely *)
				timeout.tv_sec := 0; timeout.tv_usec := 0;
				MakeFdSet(s.sock, set);
				n := Net.select(s.sock + 1, NIL, NIL, set, timeout);
				ASSERT(n # SOCKET_ERROR, 102);
				IF n = 1 THEN s.Close END;
				RETURN n = 0
			END
		END
	END IsConnected;

	PROCEDURE (s: Stream) WriteBytes (IN x: ARRAY OF BYTE; beg, len: INTEGER; OUT written: INTEGER);
		VAR res: INTEGER;
	BEGIN
		ASSERT(beg >= 0, 20);
		ASSERT(len > 0, 21);
		ASSERT(LEN(x) >= beg + len, 22);
		written := Net.send(s.sock, SYSTEM.VAL(Libc.PtrVoid, SYSTEM.ADR(x) + beg), len, Net.MSG_NOSIGNAL);
		IF written = SOCKET_ERROR THEN
			res := Errno();
			IF (res # Libc.EAGAIN) & (res # Libc.ENOTCONN) THEN Error("send()", res) END;
			written := 0
		END
	END WriteBytes;

	PROCEDURE (s: Stream) ReadBytes (VAR x: ARRAY OF BYTE; beg, len: INTEGER; OUT read: INTEGER);
		VAR res: INTEGER;
	BEGIN
		ASSERT(beg >= 0, 20);
		ASSERT(len > 0, 21);
		ASSERT(LEN(x) >= beg + len, 22);
		read := Net.recv(s.sock, SYSTEM.VAL(Libc.PtrVoid, SYSTEM.ADR(x) + beg), len, {});
		IF read = SOCKET_ERROR THEN
			res := Errno();
			IF (res = Libc.EAGAIN) OR (res = Libc.ENOTCONN) THEN
				read := 0	 (* there is nothing to be read *)
			ELSIF res = Libc.ECONNRESET THEN
				read := 0;
				s.Close	(* prevent trap *)
			ELSE
				Error("recv()", res);
				read := 0; s.Close
			END
		END
	END ReadBytes;

	PROCEDURE (s: Stream) Close;
		VAR res: INTEGER;
	BEGIN
		res := Libc.close(s.sock);
		s.sock := INVALID_SOCKET
	END Close;

	PROCEDURE (s: Stream) FINALIZE-;
	BEGIN
		IF s.sock # INVALID_SOCKET THEN
			s.Close
		END
	END FINALIZE;

	PROCEDURE CreateStream (
		OUT s: CommStreams.Stream; sock: Net.SOCKET; IN remoteAdr: ARRAY OF CHAR
	);
		VAR stream: Stream;
	BEGIN
		NEW(stream); stream.sock := sock;
		NEW(stream.remoteAdr, LEN(remoteAdr$)+1); stream.remoteAdr^ := remoteAdr$;
		s := stream
	END CreateStream;

	PROCEDURE NewStream* (
		localAdr, remoteAdr: ARRAY OF CHAR; OUT s: CommStreams.Stream; OUT res: INTEGER
	);
		(* localAdr may contain a port number *)
		(* remoteAdr must contain an address in the format  ( ip-address | hostname ) [ ":" portnumber ] *)
		VAR adr: Net.sockaddr_in;
			rpeername, lpeername: ARRAY 64 OF CHAR;
			inaddr: Net.in_addr; lport, rport: INTEGER; ok: BOOLEAN;
			sock: Net.SOCKET;
	BEGIN
		s := NIL;
		ParseAdr(remoteAdr, rpeername, rport, ok);
		IF ok THEN
			sock := Net.socket(Net.AF_INET, Net.SOCK_STREAM, Net.IPPROTO_TCP);
			IF sock # INVALID_SOCKET THEN
				ParseLocalAdr(localAdr, lpeername, lport, ok);
				IF ok & (lport >= 0) THEN	(* only non-negative port numbers are legal *)
					adr.sin_family := SHORT(CHR(Net.AF_INET));
					adr.sin_port := Net.htons(SHORT(lport));
					NameToAdr(lpeername, adr.sin_addr, ok);
					res := Net.bind(sock, SYSTEM.VAL(Net.sockaddr, adr), SIZE(Net.sockaddr_in));
					IF res = 0 THEN
						NameToAdr(rpeername, inaddr, ok);
						IF ok THEN
							adr.sin_family := SHORT(CHR(Net.AF_INET));
							adr.sin_port := Net.htons(SHORT(rport));
							adr.sin_addr := inaddr;
							res := 1;	(* = 'true' *)
							res := Ioctl.ioctl1(sock, Ioctl.FIONBIO, res);	(* set to non-blocking mode *)
							ASSERT(res = 0, 101);
							res := Net.connect(sock, SYSTEM.VAL(Net.sockaddr, adr), SIZE(Net.sockaddr_in));
							IF res = 0 THEN
								CreateStream(s, sock, remoteAdr); res := CommStreams.done
							ELSE
								res := Errno();
								IF res = Libc.EINPROGRESS THEN
									CreateStream(s, sock, remoteAdr); res := CommStreams.done
								ELSE
									Error("connect()", res);
									res := Libc.close(sock);
									res := hostSpecificError
								END
							END
						ELSE
							res := Libc.close(sock);
							res := CommStreams.invalidRemoteAdr
						END
					ELSE
						res := Libc.close(sock);
						res := CommStreams.invalidLocalAdr
					END
				ELSE
					res := CommStreams.invalidLocalAdr
				END
			ELSE
				Error("socket()", Errno());
				res := hostSpecificError
			END
		ELSE
			res := CommStreams.invalidRemoteAdr
		END
	END NewStream;

	PROCEDURE  (hook: Hook) NewListener* (
		localAdr: ARRAY OF CHAR; OUT l: CommStreams.Listener; OUT res: INTEGER
	);
	BEGIN
		NewListener(localAdr, l, res)
	END NewListener;
	
	PROCEDURE  (hook: Hook) NewStream* (
		localAdr, remoteAdr: ARRAY OF CHAR; OUT s: CommStreams.Stream; OUT res: INTEGER
	);
	BEGIN
		NewStream(localAdr, remoteAdr, s, res)
	END NewStream;


BEGIN
	NEW(hook);
	CommTCP.SetHook(hook);
	debug := TRUE
END CommTCP__Nbsd.
