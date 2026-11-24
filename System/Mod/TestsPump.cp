MODULE SystemTestsPump;
(** Проверка консольного шага действий (этап 1): ConsInterp.Run крутит
	Services.actionHook.Step, пока ждёт ввода. Команда Start ставит действие
	в очередь с задержкой 200 мс; если цикл работает, строка CONSOLE-ACTION-RAN
	напечатается ДО следующей команды, хотя ввод задерживается.

	Ручная проверка:
	  ( echo 'SystemTestsPump.Start'; sleep 1; echo 'SystemTestsPump.Stop' ) | tools/run64.sh
	В манифест не входит: нужен интервал ожидания ввода. *)

	IMPORT Console, Services, Strings;

	TYPE
		Act = POINTER TO RECORD (Services.Action) END;

	VAR
		act: Act; ran: INTEGER;

	PROCEDURE (a: Act) Do;
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		INC(ran);
		Strings.IntToString(ran, s);
		Console.WriteStr("CONSOLE-ACTION-RAN "); Console.WriteStr(s);
		Console.WriteLn
	END Do;

	PROCEDURE Start*;
	BEGIN
		NEW(act); ran := 0;
		Services.DoLater(act, Services.Ticks() + 200)
	END Start;

	PROCEDURE Stop*;
	BEGIN
		Services.RemoveAction(act);
		IF ran > 0 THEN
			Console.WriteStr("CONSOLE-ACTION-OK"); Console.WriteLn
		ELSE
			Console.WriteStr("CONSOLE-ACTION-MISSING"); Console.WriteLn
		END
	END Stop;

END SystemTestsPump.
