MODULE CheckWinV24Layout64;

	IMPORT SYSTEM, WinApi;

	CONST
		checkDCB = 1 DIV (1 DIV (1 + ABS(SIZE(WinApi.DCB) - 28)));
		checkCOMSTAT = 1 DIV (1 DIV (1 + ABS(SIZE(WinApi.COMSTAT) - 12)));
		checkCOMMTIMEOUTS = 1 DIV (1 DIV (1 + ABS(SIZE(WinApi.COMMTIMEOUTS) - 20)));

END CheckWinV24Layout64.
