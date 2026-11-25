MODULE LinThreads ["libc.so.6"];
(** Рабочие нити ОС для Linux x64: только объявления.

	Модуль — чистый набор [ccall], как LinNet/LinIoctl: тела нитей и
	обёртки живут в AsyncWork, чтобы здесь не было ни кучи, ни
	инициализации. Мьютекс и условная переменная glibc — 40 и 48 байт;
	здесь с запасом 64 байта, выравнивание 8.

	pthread_* с glibc 2.34 лежат в libc.so.6, отдельная библиотека не
	нужна. *)

	IMPORT S := SYSTEM;

	CONST
		SIGINT* = 2; SIGTERM* = 15;
		SIG_BLOCK* = 0;
		sizeofMutex = 64; sizeofCond = 64; sizeofSigSet = 128;

	TYPE
		Thread* = S.ADDRESS;
		Mutex* = RECORD [align8] data: ARRAY sizeofMutex OF SHORTCHAR END;
		Cond* = RECORD [align8] data: ARRAY sizeofCond OF SHORTCHAR END;
		SigSet* = RECORD [align8] data: ARRAY sizeofSigSet OF SHORTCHAR END;

	PROCEDURE [ccall] pthread_create* (VAR t: Thread; attr: S.ADDRESS; start: S.ADDRESS; arg: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] pthread_join* (t: Thread; retval: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] pthread_mutex_init* (m: S.ADDRESS; attr: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] pthread_mutex_lock* (m: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] pthread_mutex_unlock* (m: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] pthread_cond_init* (c: S.ADDRESS; attr: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] pthread_cond_wait* (c: S.ADDRESS; m: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] pthread_cond_signal* (c: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] pthread_cond_broadcast* (c: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] sigemptyset* (set: S.ADDRESS): INTEGER;
	PROCEDURE [ccall] sigaddset* (set: S.ADDRESS; sig: INTEGER): INTEGER;
	PROCEDURE [ccall] pthread_sigmask* (how: INTEGER; set: S.ADDRESS; oldset: S.ADDRESS): INTEGER;

END LinThreads.
