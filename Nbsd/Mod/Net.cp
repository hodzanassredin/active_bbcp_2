MODULE NbsdNet ["libc.so.12"];

	(*
		NetBSD 10.1
		i386
	*)

	CONST
		NULL* = 0H;

		SOCKET_ERROR* = -1;
		INVALID_SOCKET* = -1;

		INADDR_NONE* = -1;
		INADDR_ANY* = 0;

		(* socket domains *)
		AF_DLI* = 13;
		PF_DLI* = AF_DLI;
		AF_DECnet* = 12;
		PF_DECnet* = AF_DECnet;
		AF_ISO* = 7;
		PF_OSI* = AF_ISO;
		AF_BLUETOOTH* = 31;
		PF_BLUETOOTH* = AF_BLUETOOTH;
		PF_KEY* = 29;
		AF_LAT* = 14;
		PF_LAT* = AF_LAT;
		AF_NATM* = 27;
		PF_NATM* = AF_NATM;
		AF_LOCAL* = 1;
		PF_LOCAL* = AF_LOCAL;
		PF_UNIX* = PF_LOCAL;
		AF_ARP* = 28;
		PF_ARP* = AF_ARP;
		AF_ISDN* = 26;
		PF_ISDN* = AF_ISDN;
		PF_PIP* = 25;
		AF_PUP* = 4;
		PF_PUP* = AF_PUP;
		AF_ECMA* = 8;
		PF_ECMA* = AF_ECMA;
		AF_UNSPEC* = 0;
		PF_UNSPEC* = AF_UNSPEC;
		AF_IPX* = 23;
		PF_IPX* = AF_IPX;
		PF_ISO* = AF_ISO;
		AF_COIP* = 20;
		PF_COIP* = AF_COIP;
		AF_MAX* = 37;
		PF_MAX* = AF_MAX;
		AF_INET6* = 24;
		PF_INET6* = AF_INET6;
		AF_E164* = AF_ISDN;
		PF_E164* = AF_E164;
		AF_OROUTE* = 17;
		PF_OROUTE* = AF_OROUTE;
		PF_XTP* = 19;
		AF_ROUTE* = 34;
		PF_ROUTE* = AF_ROUTE;
		AF_DATAKIT* = 9;
		PF_DATAKIT* = AF_DATAKIT;
		AF_CCITT* = 10;
		PF_CCITT* = AF_CCITT;
		AF_CHAOS* = 5;
		PF_CHAOS* = AF_CHAOS;
		AF_LINK* = 18;
		PF_LINK* = AF_LINK;
		AF_CAN* = 35;
		PF_CAN* = AF_CAN;
		AF_ETHER* = 36;
		PF_ETHER* = AF_ETHER;
		PF_RTIP* = 22;
		AF_SNA* = 11;
		PF_SNA* = AF_SNA;
		AF_IMPLINK* = 3;
		PF_IMPLINK* = AF_IMPLINK;
		AF_APPLETALK* = 16;
		PF_APPLETALK* = AF_APPLETALK;
		AF_INET* = 2;
		PF_INET* = AF_INET;
		AF_CNT* = 21;
		PF_CNT* = AF_CNT;
		AF_MPLS* = 33;
		PF_MPLS* = AF_MPLS;
		AF_HYLINK* = 15;
		PF_HYLINK* = AF_HYLINK;
		AF_NS* = 6;
		PF_NS* = AF_NS;

		(* socket types *)
		SOCK_RAW* = 3;
		SOCK_RDM* = 4;
		SOCK_CLOEXEC* = 10000000H;
		SOCK_FLAGS_MASK* = 0F0000000H;
		SOCK_CONN_DGRAM* = 6;
		SOCK_DCCP* = SOCK_CONN_DGRAM;
		SOCK_SEQPACKET* = 5;
		SOCK_DGRAM* = 2;
		SOCK_NONBLOCK* = 20000000H;
		SOCK_NOSIGPIPE* = 40000000H;
		SOCK_STREAM* = 1;

		(* socket protocols *)
		IPPROTO_IPCOMP* = 108;
		IPPROTO_ROUTING* = 43;
		IPPROTO_CARP* = 112;
		IPPROTO_L2TP* = 115;
		IPPROTO_EGP* = 8;
		IPPROTO_ETHERIP* = 97;
		IPPROTO_PIM* = 103;
		IPPROTO_EON* = 80;
		IPPROTO_ENCAP* = 98;
		IPPROTO_DONE* = 257;
		IPPROTO_ESP* = 50;
		IPPROTO_PUP* = 12;
		IPPROTO_IDP* = 22;
		IPPROTO_IPIP* = 4;
		IPPROTO_TCP* = 6;
		IPPROTO_RAW* = 255;
		IPPROTO_IPV4* = 4;
		IPPROTO_IPV6* = 41;
		IPPROTO_SCTP* = 132;
		IPPROTO_MAX* = 256;
		IPPROTO_HOPOPTS* = 0;
		IPPROTO_RSVP* = 46;
		IPPROTO_AH* = 51;
		IPPROTO_TP* = 29;
		IPPROTO_MOBILE* = 55;
		IPPROTO_VRRP* = 112;
		IPPROTO_UDP* = 17;
		IPPROTO_IP* = 0;
		IPPROTO_ICMP* = 1;
		IPPROTO_GGP* = 3;
		IPPROTO_FRAGMENT* = 44;
		IPPROTO_GRE* = 47;
		IPPROTO_DSTOPTS* = 60;
		IPPROTO_NONE* = 59;
		IPPROTO_IGMP* = 2;
		IPPROTO_ICMPV6* = 58;
		IPPROTO_IPV6_ICMP* = 58;
		IPPROTO_PFSYNC* = 240;
		IPPROTO_DCCP* = 33;

		(* socket options *)
		SO_NOSIGPIPE* = {11};
		SO_LINGER* = {7};
		SO_REUSEADDR* = {2};
		SO_USELOOPBACK* = {6};
		SO_DEBUG* = {0};
		SO_BROADCAST* = {5};
		SO_TYPE* = {3,12};
		SO_ACCEPTFILTER* = {12};
		SO_DONTROUTE* = {4};
		SO_NOHEADER* = {1,3,12};
		SO_RCVLOWAT* = {2,12};
		SO_SNDTIMEO* = {0,1,3,12};
		SO_KEEPALIVE* = {3};
		SO_OVERFLOWED* = {0,3,12};
		SO_OOBINLINE* = {8};
		SO_ACCEPTCONN* = {1};
		SO_RERROR* = {14};
		SO_REUSEPORT* = {9};
		SO_ERROR* = {0..2,12};
		SO_TIMESTAMP* = {13};
		SO_SNDBUF* = {0,12};
		SO_DEFOPTS* = {0,2..9,11,13,14};
		SO_RCVBUF* = {1,12};
		SO_SNDLOWAT* = {0,1,12};
		SO_RCVTIMEO* = {2,3,12};

		SHUT_RDWR* = 2;
		SHUT_RD* = 0;
		SHUT_WR* = 1;

		SOL_SOCKET* = 0FFFFH;

		MSG_NOSIGNAL* = {10};
		MSG_WAITALL* = {6};
		MSG_BCAST* = {8};
		MSG_PEEK* = {1};
		MSG_CMSG_CLOEXEC* = {11};
		MSG_WAITFORONE* = {13};
		MSG_LENUSRSPACE* = {27};
		MSG_CTRUNC* = {5};
		MSG_MCAST* = {9};
		MSG_DONTROUTE* = {2};
		MSG_NOTIFICATION* = {14};
		MSG_CONTROLMBUF* = {25};
		MSG_NBIO* = {12};
		MSG_TRUNC* = {4};
		MSG_IOVUSRSPACE* = {26};
		MSG_OOB* = {0};
		MSG_DONTWAIT* = {7};
		MSG_NAMEMBUF* = {24};
		MSG_USERFLAGS* = {0..23};
		MSG_EOR* = {3};

		SOMAXCONN* = 128;

		(* addrinfo.ai_flags *)
		AI_NUMERICSERV* = {3};
		AI_ADDRCONFIG* = {10};
		AI_SRV* = {11};
		AI_PASSIVE* = {0};
		AI_MASK* = {0..3,10,11};
		AI_NUMERICHOST* = {2};
		AI_CANONNAME* = {1};

		(* getaddrinfo/getnameinfo/gai_strerror error codes *)
		EAI_PROTOCOL* = 13;
		EAI_BADHINTS* = 12;
		EAI_AGAIN* = 2;
		EAI_NONAME* = 8;
		EAI_MAX* = 15;
		EAI_FAMILY* = 5;
		EAI_MEMORY* = 6;
		EAI_FAIL* = 4;
		EAI_SOCKTYPE* = 10;
		EAI_OVERFLOW* = 14;
		EAI_BADFLAGS* = 3;
		EAI_NODATA* = 7;
		EAI_ADDRFAMILY* = 1;
		EAI_SYSTEM* = 11;
		EAI_SERVICE* = 9;

		(* NetBSD 10.1 /usr/include/sys/fd_set.h *)
			FD_SETSIZE = 256;
			__NFDBITS = 32;


	TYPE
		uint8_t = SHORTCHAR;
		int8_t = SHORTCHAR;
		uint16_t = SHORTINT;
		(* PtrVoid, int, intFlags, short, socklen_t, size_t, ssize_t, in_addr_t, in_port_t, time_t, suseconds_t, nfds_t, sa_family_t *)
		PtrVoid* = INTEGER;
		short* = SHORTINT;
		int* = INTEGER;
		intFlags* = SET;
		size_t* = INTEGER;
		ssize_t* = INTEGER;
		time_t* = LONGINT;
		suseconds_t* = INTEGER;
		socklen_t* = INTEGER;
		in_addr_t* = INTEGER;
		in_port_t* = SHORTINT;
		nfds_t* = INTEGER;
		sa_family_t* = SHORTCHAR;

		PtrSTR* = POINTER [untagged] TO ARRAY [untagged] OF SHORTCHAR;

		SOCKET* = int;

(*
		in_addr* = RECORD [untagged]
			s_addr*: in_addr_t;
		END;
*)
		in_addr* = RECORD [untagged]
			S_un*: RECORD [union]
				S_un_b*: RECORD [untagged]
					s_b1*: SHORTCHAR;
					s_b2*: SHORTCHAR;
					s_b3*: SHORTCHAR;
					s_b4*: SHORTCHAR;
				END;
				S_un_w*: RECORD [untagged]
					s_w1*: SHORTINT;
					s_w2*: SHORTINT;
				END;
				S_addr*: in_addr_t;
			END;
		END;

		Ptrsockaddr* = POINTER [untagged] TO sockaddr;
		sockaddr* = RECORD [untagged]
			(* NetBSD 10.1 /usr/include/sys/socket.h *)
				sa_len*: SHORTCHAR; (* total length *)
				sa_family*: sa_family_t; (* address family *)
				sa_data*: ARRAY [untagged] 14 OF SHORTCHAR; (* actually longer; address value *)
		END;

		Ptrsockaddr_storage = POINTER [untagged] TO sockaddr_storage;
		sockaddr_storage* = RECORD [untagged]
			(* NetBSD 10.1 /usr/include/sys/socket.h *)
				ss_len*: SHORTCHAR; (* address length *)
				ss_family*: sa_family_t; (* address family *)
				__ss_pad1: ARRAY [untagged] 8 - 2 OF SHORTCHAR;
				__ss_align: LONGINT;
				__ss_pad2: ARRAY [untagged] 128 - 2 - (8 - 2) - 8 OF SHORTCHAR;
		END;

		Ptrsockaddr_in* = POINTER [untagged] TO sockaddr_in;
		sockaddr_in* = RECORD [untagged]
			(* NetBSD 10.1 /usr/include/netinet/in.h *)
				sin_len*: uint8_t;
				sin_family*: sa_family_t;
				sin_port*: in_port_t;
				sin_addr*: in_addr;
				sin_zero*: ARRAY [untagged] 8 OF int8_t;
		END;

		Ptraddrinfo* = POINTER [untagged] TO addrinfo;
		addrinfo* = RECORD [untagged]
			(* NetBSD 10.1 /usr/include/netdb.h *)
				ai_flags*: intFlags;
				ai_family*: int;
				ai_socktype*: int;
				ai_protocol*: int;
				ai_addrlen*: socklen_t;
				ai_canonname*: PtrSTR;
				ai_addr*: Ptrsockaddr;
				ai_next*: Ptraddrinfo;
		END;

		timeval* = RECORD [untagged]
			(* Linux, FreeBSD, OpenBSD *)
				tv_sec*: time_t; (* seconds *)
				tv_usec*: suseconds_t; (* and microseconds *)
		END;

		pollfd* = RECORD [untagged]
			(* Linux, FreeBSD, OpenBSD *)
				fd*: int;
				events*: short;
				revents*: short;
		END;

		(* NetBSD 10.1 /usr/include/sys/fd_set.h *)
			__fd_mask = SET;
			fd_set* = ARRAY [untagged] (FD_SETSIZE + (__NFDBITS - 1)) DIV __NFDBITS OF __fd_mask;


		PROCEDURE [ccall] socket* ["__socket30"] (domain: int; type: int; protocol: int): SOCKET;

		PROCEDURE [ccall] select* ["__select50"] (nfds: INTEGER; VAR [nil] readfds: fd_set; VAR [nil] writefds: fd_set; VAR [nil] exceptfds: fd_set; VAR timeout: timeval): int;


	(* POSIX.1 *)
(*
		PROCEDURE [ccall] socket* (domain: int; type: int; protocol: int): SOCKET;
*)
		PROCEDURE [ccall] accept* (s: SOCKET; VAR addr: sockaddr; VAR addrlen: socklen_t): SOCKET;
		PROCEDURE [ccall] bind* (s: SOCKET; VAR name: sockaddr; namelen: socklen_t): int;
		PROCEDURE [ccall] connect* (s: SOCKET; VAR name: sockaddr; namelen: socklen_t): int;
		PROCEDURE [ccall] getsockname* (s: SOCKET; VAR name: sockaddr; VAR namelen: socklen_t): int;
		PROCEDURE [ccall] listen* (s: SOCKET; backlog: int): int;
		PROCEDURE [ccall] recv* (s: SOCKET; buf: PtrVoid; len: size_t; flags: intFlags): ssize_t;
		PROCEDURE [ccall] recvfrom* (s: SOCKET; buf: PtrVoid; len: size_t; flags: intFlags; VAR from: sockaddr; VAR fromlen: socklen_t): ssize_t;
		PROCEDURE [ccall] send* (s: SOCKET; msg: PtrVoid; len: size_t; flags: intFlags): ssize_t;
		PROCEDURE [ccall] sendto* (s: SOCKET; msg: PtrVoid; len: size_t; flags: intFlags; VAR to: sockaddr; tolen: socklen_t): ssize_t;
		PROCEDURE [ccall] shutdown* (s: SOCKET; how: int): int;
		PROCEDURE [ccall] getsockopt* (s: SOCKET; level: int; optname: intFlags; optval: PtrVoid; VAR [nil] optlen: socklen_t): int;
		PROCEDURE [ccall] setsockopt* (s: SOCKET; level: int; optname: intFlags; optval: PtrVoid; optlen: socklen_t): int;

		PROCEDURE [ccall] inet_addr* (cp: PtrSTR): in_addr_t;
		PROCEDURE [ccall] htons* (host16: uint16_t): uint16_t;

(*
		PROCEDURE [ccall] select* (nfds: INTEGER; VAR [nil] readfds: fd_set; VAR [nil] writefds: fd_set; VAR [nil] exceptfds: fd_set; VAR timeout: timeval): int;
*)

		PROCEDURE [ccall] poll* (VAR fds: ARRAY [untagged] OF pollfd; nfds: nfds_t; timeout: int): int;

(*
	PROCEDURE FD_ZERO (VAR set: Net.fd_set);
		VAR i: INTEGER;
	BEGIN
		i := LEN(set); REPEAT DEC(i); set[i] := {} UNTIL i = 0
	END FD_ZERO;

	PROCEDURE FD_SET (fd: Net.SOCKET; VAR set: Net.fd_set);
	BEGIN
		INCL(set[fd DIV 32], fd MOD 32)
	END FD_SET;
*)

	(* POSIX.1g *)
		PROCEDURE [ccall] getaddrinfo* (hostname: PtrSTR; servname: PtrSTR; VAR [nil] hints: addrinfo; VAR res: Ptraddrinfo): int;
		PROCEDURE [ccall] freeaddrinfo* (ai: Ptraddrinfo);

		PROCEDURE [ccall] gai_strerror* (ecode: int): PtrSTR;

END NbsdNet.
