# Обзор на примерах: DemoSupport

Общий помощник примеров `Demo`.

Команда, запущенная через `tools/run64.sh`, исполняется без окна, а значит без главного цикла,
который крутит планировщик задач и таймеры `Services`. Каждому примеру нужно дождаться своих задач
и напечатать итог. `DemoSupport` делает это за всех; своей команды у него нет.

```
MODULE DemoSupport;
(** Общие средства примеров Demo: прогон задач без окна и вывод в консоль.

	Примеры запускаются командой из консольного хоста, где главного цикла
	окон нет. Pump крутит действие планировщика Services — те же шаги,
	которые делает главный цикл, поэтому работают и таймеры Select. *)

	IMPORT Kernel, Services, Tasks, Console, Strings;

	CONST
		defaultTimeout = 30000;

	VAR
		message: ARRAY 256 OF CHAR;

	PROCEDURE Pump* (ms: LONGINT): BOOLEAN;
	(** Крутить действия Services, пока есть живые задачи, но не дольше ms.
		TRUE — все задачи завершились. *)
		VAR t0: LONGINT;
	BEGIN
		IF ms <= 0 THEN ms := defaultTimeout END;
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE Log* (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END Log;

	PROCEDURE LogInt* (IN label: ARRAY OF CHAR; x: LONGINT);
	BEGIN
		Strings.IntToString(x, message);
		Console.WriteStr(label); Console.WriteStr(message)
	END LogInt;

	PROCEDURE LogLn*;
	BEGIN
		Console.WriteLn
	END LogLn;

	PROCEDURE LogLine* (IN label: ARRAY OF CHAR; x: LONGINT);
	BEGIN
		LogInt(label, x); LogLn
	END LogLine;

END DemoSupport.
```

`Pump(ms)` вызывает `Services.actionHook.Step` — тот же шаг, что делает главный цикл окна. Поэтому
тайм-ауты `Select` и `Tasks.Sleep` работают и в консольной команде. `Pump` возвращает `TRUE`,
когда все задачи завершились, и `FALSE` по истечении `ms`. `Log`, `LogInt`, `LogLn` и `LogLine`
пишут в `Console` (stdout); примеры печатают ими одну итоговую строку, а тесты проверяют
экспортированные счётчики, а не текст.

В этом модуле мы увидели, как консольная команда заменяет главный цикл окна вызовами шага
`Services`.
