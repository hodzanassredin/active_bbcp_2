MODULE AsyncMpi;
(** MPI-транспорт компутонов (этап 9) поверх MPICH.

	AsyncComputon не импортирует этот модуль: тест сам зовёт
	AsyncMpi.Install и SetTransport("mpi"), поэтому обычные прогоны не
	требуют MPI-библиотеки. Обёртка libbbmpi.so (tools/bbmpi.c, mpicc)
	скрывает структуры MPI; адреса функций берутся через LinDl.

	Раскладка мира: ранг 0 — управляющий (клиент), ранги 1..n — процессы
	BlackBox с командой AsyncMpi.WorkerLoop. Кадры — те же: 12-байтовый
	заголовок (длина, tag, вид) и тело, но через MPI_Send/MPI_Recv.
	Готовность ответов главный цикл проверяет MPI_Iprobe в действии
	AsyncComputon; блокирующее ожидание остаётся в задаче Pull.

	Ограничения: гибель ранга не повторяется (mpiexec обычно гасит мир),
	Unload по MPI возвращает отказ -1, WorkerPid неизвестен. *)

	IMPORT S := SYSTEM, Kernel, Services, Dialog, Strings, Dl := LinDl,
		AsyncComputon;

	CONST
		maxFrame = 60000;
		tagHdr = 1000; tagBody = 1001; tagResp = 2000;
		kindCheck = 0; kindStop = 1; kindUnload = 2;
		libName = "libbbmpi.so";

	TYPE
		InitProc = PROCEDURE [ccall] (): INTEGER;
		RankProc = PROCEDURE [ccall] (): INTEGER;
		SendProc = PROCEDURE [ccall] (dest, tag: INTEGER; buf: S.ADDRESS; len: INTEGER): INTEGER;
		RecvProc = PROCEDURE [ccall] (src, tag: INTEGER; buf: S.ADDRESS; len: INTEGER): INTEGER;
		ProbeProc = PROCEDURE [ccall] (src, tag: INTEGER): INTEGER;

		MpiTransport = POINTER TO RECORD (AsyncComputon.Transport)
			n: INTEGER;
			active: BOOLEAN
		END;

	VAR
		lib: Dl.HANDLE;
		initP: InitProc; finalizeP, rankP, sizeP: RankProc;
		sendP, recvP: SendProc; probeP: ProbeProc;
		bound, initialized: BOOLEAN;
		world, me: INTEGER;
		hdr, respHdr: ARRAY 12 OF SHORTCHAR;
		payload, outBuf: ARRAY maxFrame OF SHORTCHAR;

	PROCEDURE Short (IN s: ARRAY OF CHAR; VAR d: ARRAY OF SHORTCHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(d) - 1) & (s[i] # 0X) DO d[i] := SHORT(s[i]); INC(i) END;
		d[i] := 0X
	END Short;

	PROCEDURE Sym (IN name: ARRAY OF CHAR): Dl.HANDLE;
		VAR s: ARRAY 32 OF SHORTCHAR;
	BEGIN
		Short(name, s);
		RETURN Dl.dlsym(lib, S.VAL(Dl.PtrSTR, S.ADR(s[0])))
	END Sym;

	PROCEDURE Bind (): BOOLEAN;
		VAR s: ARRAY 32 OF SHORTCHAR;
	BEGIN
		IF bound THEN RETURN TRUE END;
		Short(libName, s);
		lib := Dl.dlopen(S.VAL(Dl.PtrSTR, S.ADR(s[0])), Dl.RTLD_LAZY + Dl.RTLD_GLOBAL);
		IF lib = Dl.NULL THEN RETURN FALSE END;
		initP := S.VAL(InitProc, Sym("bbmpi_init"));
		finalizeP := S.VAL(RankProc, Sym("bbmpi_finalize"));
		rankP := S.VAL(RankProc, Sym("bbmpi_rank"));
		sizeP := S.VAL(RankProc, Sym("bbmpi_size"));
		sendP := S.VAL(SendProc, Sym("bbmpi_send"));
		recvP := S.VAL(SendProc, Sym("bbmpi_recv"));
		probeP := S.VAL(ProbeProc, Sym("bbmpi_probe"));
		IF (initP = NIL) OR (finalizeP = NIL) OR (rankP = NIL) OR (sizeP = NIL)
				OR (sendP = NIL) OR (recvP = NIL) OR (probeP = NIL) THEN RETURN FALSE END;
		bound := TRUE;
		RETURN TRUE
	END Bind;

	PROCEDURE PutInt (VAR b: ARRAY OF SHORTCHAR; pos, x: INTEGER);
	BEGIN
		b[pos] := S.VAL(SHORTCHAR, x MOD 256);
		b[pos+1] := S.VAL(SHORTCHAR, (x DIV 256) MOD 256);
		b[pos+2] := S.VAL(SHORTCHAR, (x DIV 65536) MOD 256);
		b[pos+3] := S.VAL(SHORTCHAR, (x DIV 16777216) MOD 256)
	END PutInt;

	PROCEDURE GetInt (VAR b: ARRAY OF SHORTCHAR; pos: INTEGER): INTEGER;
	BEGIN
		RETURN (ORD(b[pos]) MOD 256)
			+ 256 * (ORD(b[pos+1]) MOD 256)
			+ 65536 * (ORD(b[pos+2]) MOD 256)
			+ 16777216 * (ORD(b[pos+3]) MOD 256)
	END GetInt;

	(* --- транспорт управляющего процесса ---------------------------------- *)

	PROCEDURE (t: MpiTransport) Start (n: INTEGER; OUT size: INTEGER);
	BEGIN
		size := 0;
		IF ~Bind() THEN RETURN END;
		IF ~initialized THEN
			IF initP() # 0 THEN RETURN END;
			initialized := TRUE
		END;
		world := sizeP(); me := rankP();
		IF (me # 0) OR (world # n + 1) THEN RETURN END;
		t.n := n; t.active := TRUE; size := n
	END Start;

	PROCEDURE (t: MpiTransport) SendFrame (rank, tag, kind: INTEGER;
		buf: POINTER TO ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR ok: BOOLEAN;
	BEGIN
		IF ~t.active THEN RETURN FALSE END;
		PutInt(hdr, 0, len); PutInt(hdr, 4, tag); PutInt(hdr, 8, kind);
		ok := sendP(rank + 1, tagHdr, S.ADR(hdr[0]), 12) = 0;
		IF ok & (len > 0) & (buf # NIL) THEN
			ok := sendP(rank + 1, tagBody, S.ADR(buf^[0]), len) = 0
		END;
		RETURN ok
	END SendFrame;

	PROCEDURE (t: MpiTransport) Poll;
		VAR r, n, len, tag, st: INTEGER;
	BEGIN
		IF ~t.active THEN RETURN END;
		FOR r := 0 TO t.n - 1 DO
			LOOP
				n := probeP(r + 1, tagResp);
				IF n <= 0 THEN EXIT END;
				IF recvP(r + 1, tagResp, S.ADR(respHdr[0]), 12) # 0 THEN EXIT END;
				len := GetInt(respHdr, 0); tag := GetInt(respHdr, 4); st := GetInt(respHdr, 8);
				IF (len < 0) OR (len > maxFrame) THEN EXIT END;
				IF len > 0 THEN
					IF recvP(r + 1, tagBody, S.ADR(payload[0]), len) # 0 THEN EXIT END
				END;
				AsyncComputon.DeliverFrame(r, tag, st, payload, len)
			END
		END
	END Poll;

	PROCEDURE (t: MpiTransport) Stop;
		VAR r: INTEGER; ok: BOOLEAN;
	BEGIN
		IF ~t.active THEN RETURN END;
		t.active := FALSE;
		PutInt(hdr, 0, 0); PutInt(hdr, 4, 0); PutInt(hdr, 8, kindStop);
		FOR r := 0 TO t.n - 1 DO
			ok := sendP(r + 1, tagHdr, S.ADR(hdr[0]), 12) = 0
		END;
		t.n := 0;
		IF initialized THEN
			initialized := FALSE;
			ok := finalizeP() = 0
		END
	END Stop;

	PROCEDURE (t: MpiTransport) Alive (rank: INTEGER): BOOLEAN;
	BEGIN
		RETURN t.active & (rank >= 0) & (rank < t.n)
	END Alive;

	PROCEDURE Install* (): BOOLEAN;
	(** Привязать libbbmpi.so и поставить транспорт в AsyncComputon. *)
		VAR t: MpiTransport;
	BEGIN
		IF ~Bind() THEN RETURN FALSE END;
		NEW(t); t.n := 0; t.active := FALSE;
		AsyncComputon.SetTransportObject(t);
		RETURN TRUE
	END Install;

	PROCEDURE Available* (): BOOLEAN;
	(** Библиотека MPICH и обёртка найдены (без MPI_Init). *)
	BEGIN
		RETURN Bind()
	END Available;

	PROCEDURE Rank* (): INTEGER;
	(** Ранг MPI текущего процесса (после Start в управляющем). *)
	BEGIN
		IF ~initialized THEN RETURN -1 END;
		RETURN rankP()
	END Rank;

	PROCEDURE WorldSize* (): INTEGER;
	BEGIN
		IF ~initialized THEN RETURN 0 END;
		RETURN sizeP()
	END WorldSize;

	(* --- процесс-исполнитель ---------------------------------------------- *)

	PROCEDURE NoopViewer;
	BEGIN
	END NoopViewer;

	PROCEDURE WorkerLoop*;
	(** Точка входа процесса-исполнителя ранга > 0; запускается mpiexec
		с командой AsyncMpi.WorkerLoop на stdin. *)
		VAR rank, len, tag, kind, err, outLen, i: INTEGER;
			c: AsyncComputon.Computon; ok: BOOLEAN;
			tmp: POINTER TO ARRAY OF SHORTCHAR;
	BEGIN
		Kernel.InstallTrapViewer(NoopViewer);
		IF ~Bind() THEN RETURN END;
		IF initP() # 0 THEN RETURN END;
		rank := rankP();
		IF rank = 0 THEN	(* управляющий не должен попадать сюда *)
			ok := finalizeP() = 0;
			RETURN
		END;
		LOOP
			IF recvP(0, tagHdr, S.ADR(hdr[0]), 12) # 0 THEN EXIT END;
			len := GetInt(hdr, 0); tag := GetInt(hdr, 4); kind := GetInt(hdr, 8);
			IF kind = kindStop THEN EXIT END;
			IF (len < 0) OR (len > maxFrame) THEN EXIT END;
			IF kind = kindUnload THEN
				err := -1; outLen := 0	(* MPI не поддерживает выгрузку кода *)
			ELSE
				IF (len > 0) & (recvP(0, tagBody, S.ADR(payload[0]), len) # 0) THEN EXIT END;
				c := AsyncComputon.Deserialize(payload, len);
				IF c = NIL THEN err := -1 ELSE AsyncComputon.RunDo(c, err) END;
				outLen := 0;
				IF err = 0 THEN
					AsyncComputon.Serialize(c, tmp, outLen);
					IF (tmp # NIL) & (outLen <= maxFrame) THEN
						FOR i := 0 TO outLen - 1 DO outBuf[i] := tmp^[i] END
					ELSE err := -6; outLen := 0
					END
				END
			END;
			PutInt(hdr, 0, outLen); PutInt(hdr, 4, tag); PutInt(hdr, 8, err);
			ok := sendP(0, tagResp, S.ADR(hdr[0]), 12) = 0;
			IF ok & (outLen > 0) THEN ok := sendP(0, tagBody, S.ADR(outBuf[0]), outLen) = 0 END;
			c := NIL;
			IF ~ok THEN EXIT END
		END;
		ok := finalizeP() = 0
	END WorkerLoop;

END AsyncMpi.
