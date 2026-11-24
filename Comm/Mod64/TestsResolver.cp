MODULE CommTestsResolver;
(** Тест этапа 8: недоступный сервер имён не замораживает главный цикл.

	Запускается только из tools/test-resolver.sh: тот создаёт отдельное
	сетевое и mount-пространство без root (`unshare -rmn`), привязывает
	свой resolv.conf с nameserver на адрес, пакеты к которому молча
	теряются, и оставляет тайм-аут/попытки резолвера по умолчанию.

	ResolveJob зовёт getaddrinfo в рабочей нити; задача ждёт Execute,
	главный цикл всё это время делает шаги. Вне такого пространства
	ответ приходит за миллисекунды, и проверка «ждали тайм-аут» падает —
	случайный зелёный прогон невозможен. *)

	IMPORT S := SYSTEM, Kernel, Services, Tasks, AsyncWork, AsyncIO,
		UnitAssert, Console, Strings;

	TYPE
		AddrTask = POINTER TO RECORD (Tasks.Task) END;

	VAR
		req: AsyncWork.AddrReq;
		jobStatus: INTEGER;
		elapsed: LONGINT;
		done, steps: INTEGER;
		dlg: ARRAY 64 OF CHAR;

	PROCEDURE PumpUntil (ms: LONGINT; cond: PROCEDURE (): BOOLEAN): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~cond() & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step;
			AsyncIO.Poll(1);
			INC(steps)
		END;
		RETURN cond()
	END PumpUntil;

	PROCEDURE (t: AddrTask) Run;
		VAR t0: LONGINT;
	BEGIN
		req.host := "no-such-host.invalid";
		req.count := 0; req.status := -1;
		t0 := Kernel.Time();
		jobStatus := AsyncWork.Execute(S.ADR(AsyncWork.ResolveJob), S.ADR(req));
		elapsed := Kernel.Time() - t0;
		done := 1
	END Run;

	PROCEDURE DoneCond (): BOOLEAN;
	BEGIN
		RETURN done = 1
	END DoneCond;

	PROCEDURE TestProcResolveBlocks*;
	(** Имя не разрешается; резолвер ждёт свой тайм-аут в рабочей нити,
		а главный цикл продолжает шаги. *)
		VAR t: AddrTask;
	BEGIN
		done := 0; steps := 0; elapsed := 0; jobStatus := -2;
		NEW(t); Tasks.Start(t);
		UnitAssert.True(PumpUntil(120000, DoneCond), "resolve job finishes");
		Console.WriteStr("CommTestsResolver: wait ms="); Strings.IntToString(elapsed, dlg);
		Console.WriteStr(dlg); Console.WriteStr(" steps="); Strings.IntToString(steps, dlg);
		Console.WriteStr(dlg); Console.WriteLn;
		UnitAssert.True(elapsed >= 1000, "resolver waited for its timeout");
		UnitAssert.True(steps > 100, "main loop kept stepping while worker blocked");
		UnitAssert.True(req.status # 0, "nonexistent name does not resolve");
		UnitAssert.EqualInt(0, jobStatus, "worker job completed normally")
	END TestProcResolveBlocks;

END CommTestsResolver.
