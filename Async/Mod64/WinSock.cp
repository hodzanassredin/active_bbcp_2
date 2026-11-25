MODULE AsyncWinSock ["WS2_32.dll"];
(** Объявления WinSock для пула рабочих нитей.

	Отдельный interface-модуль, как WinNet: в модуле с телом нельзя
	указывать библиотеку у отдельной процедуры (это разрешено только
	interface-модулям). Нужны только три вызова для ResolveJob;
	getaddrinfo в WinNet отсутствует.

	addrinfo — раскладка Windows x64: четыре INTEGER, size_t и три
	указателя; поэтому [align8]. WSAData — непрозрачный буфер: важна
	только успешность WSAStartup. *)

	IMPORT SYSTEM;

	CONST
		version* = 514;	(* 2.2 *)

		pfUnspec* = 0; sockStream* = 1; ipProtoTcp* = 6;

	TYPE
		WSAData* = RECORD [align8] data: ARRAY 512 OF BYTE END;

		AddrInfo* = RECORD [align8]
			flags*, family*, socktype*, protocol*: INTEGER;
			addrlen*: SYSTEM.ADDRESS;	(* size_t *)
			canonname*, addr*, next*: SYSTEM.ADDRESS
		END;
		AddrInfoPtr* = POINTER [untagged] TO AddrInfo;

	PROCEDURE [ms64] WSAStartup* (version: INTEGER; VAR [nil] data: WSAData): INTEGER;
	PROCEDURE [ms64] getaddrinfo* (node, service: SYSTEM.ADDRESS;
		VAR [nil] hints: AddrInfo; VAR [nil] res: AddrInfoPtr): INTEGER;
	PROCEDURE [ms64] freeaddrinfo* (res: AddrInfoPtr);

END AsyncWinSock.
