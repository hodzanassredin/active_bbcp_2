MODULE CheckWinNetLayout64;

	IMPORT SYSTEM, WinNet;

	(* Compile-time ABI checks: a mismatch makes the inner quotient zero. *)

	CONST
		checkNETRESOURCEA = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.NETRESOURCEA) - 48)));
		checkCONNECTDLGSTRUCTA = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.CONNECTDLGSTRUCTA) - 32)));
		checkDISCDLGSTRUCTA = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.DISCDLGSTRUCTA) - 40)));
		checkNETINFOSTRUCT = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.NETINFOSTRUCT) - 40)));
		checkNCB = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.NCB) - 96)));
		checkWSADATA = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.WSADATA) - 408)));
		checkfd_set = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.fd_set) - 520)));
		checkhostent = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.hostent) - 32)));
		checkservent = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.servent) - 32)));
		checkprotoent = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.protoent) - 24)));
		checkWSABUF = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.WSABUF) - 16)));
		checkWSAOVERLAPPED = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.WSAOVERLAPPED) - 32)));
		checkQOS = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.QOS) - 80)));
		checkWSAPROTOCOL_INFOA = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.WSAPROTOCOL_INFOA) - 372)));
		checkWSAPROTOCOL_INFOW = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.WSAPROTOCOL_INFOW) - 628)));
		checkTRANSMIT_FILE_BUFFERS = 1 DIV (1 DIV (1 + ABS(SIZE(WinNet.TRANSMIT_FILE_BUFFERS) - 32)));

END CheckWinNetLayout64.
