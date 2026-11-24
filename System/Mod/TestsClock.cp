MODULE SystemTestsClock;
(** Источник наносекунд для замеров SystemTests (i386; amd64-вариант —
	System/Mod64/TestsClock.cp поверх clock_gettime из libtest64.so).

	Здесь время строится по Kernel.Time: test64_ns возвращает long
	(32 бита), а наносекунды с начала эпохи переполнились бы на i386 за
	4 секунды. Источник нужен только LoadBench в SystemTestsTasks, точности
	миллисекунд хватает. Отдельный модуль, а не SystemTestsCo: задачи не
	должны зависеть от C-помощника libtest32.so. *)

	IMPORT Kernel;

	PROCEDURE now* (): LONGINT;
	BEGIN
		RETURN Kernel.Time() * 1000000
	END now;

END SystemTestsClock.
