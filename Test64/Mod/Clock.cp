MODULE Test64Clock;
(** Источник наносекунд для замеров Test64 (i386; amd64-вариант —
	Test64/Mod64/Clock.cp поверх clock_gettime из libtest64.so).

	Здесь время строится по Kernel.Time: test64_ns возвращает long
	(32 бита), а наносекунды с начала эпохи переполнились бы на i386 за
	4 секунды. Источник нужен только LoadBench в Test64Tasks, точности
	миллисекунд хватает. Отдельный модуль, а не Test64Co: задачи не
	должны зависеть от C-помощника libtest32.so. *)

	IMPORT Kernel;

	PROCEDURE now* (): LONGINT;
	BEGIN
		RETURN Kernel.Time() * 1000000
	END now;

END Test64Clock.
