# Обзор на примерах: ObxBank1

Три кассира переводят деньги, управляющий ждёт их и останавливает часы.

Три задачи-кассира переводят деньги по кругу между тремя счетами и отклоняют перевод, на который
не хватает денег. Управляющий запускает их в группу, ждёт каждого через `Tasks.Join`, печатает итог
и отменяет задачу-часы. Пример показывает запуск, ожидание, сон, отмену и то, почему перевод без
точек переключения неделим. Пример относится к руководству `Docu/ru/Tasks.md`.

```
MODULE ObxBank1;
(** Задачи нижнего уровня: пример показывает Run, Join, Sleep, Cancel и
	группу Task.Group. *)
	IMPORT Tasks, Console, Strings;

	CONST n = 3;

	TYPE
		Clerk = POINTER TO RECORD (Tasks.Task) from, to, sum: INTEGER END;
		Clock = POINTER TO RECORD (Tasks.Task) END;
		Manager = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: ARRAY n OF INTEGER; refused: INTEGER; clock: Clock;

	PROCEDURE Total (): INTEGER;
		VAR i, s: INTEGER;
	BEGIN
		s := 0; FOR i := 0 TO n - 1 DO s := s + balance[i] END;
		RETURN s
	END Total;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	(* Перевод: проверка и обе записи без точки переключения между ними *)
	PROCEDURE Transfer (from, to, sum: INTEGER);
	BEGIN
		IF balance[from] >= sum THEN
			balance[from] := balance[from] - sum;
			balance[to] := balance[to] + sum
		ELSE
			INC(refused)
		END
	END Transfer;

	PROCEDURE (c: Clerk) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO Transfer(c.from, c.to, c.sum); Tasks.Yield END
	END Run;

	PROCEDURE (c: Clock) Run;
	BEGIN
		LOOP Tasks.Sleep(1000) END	(* «часы на стене»: работают, пока их не отменят *)
	END Run;

	PROCEDURE (m: Manager) Run;
		VAR g: Tasks.Group; c: ARRAY n OF Clerk; i: INTEGER;
	BEGIN
		g := Tasks.NewGroup();
		FOR i := 0 TO n - 1 DO
			NEW(c[i]); c[i].from := i; c[i].to := (i + 1) MOD n; c[i].sum := 40 + 30 * i;
			Tasks.StartIn(c[i], g)
		END;
		FOR i := 0 TO n - 1 DO Tasks.Join(c[i]) END;
		FOR i := 0 TO n - 1 DO Say("balance ", balance[i]) END;
		Say("refused ", refused);
		Say("total ", Total());
		Tasks.Cancel(clock)
	END Run;

	PROCEDURE Do*;
		VAR m: Manager;
	BEGIN
		balance[0] := 100; balance[1] := 100; balance[2] := 100; refused := 0;
		NEW(clock); Tasks.Start(clock);
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("clock state ", Tasks.State(clock))
	END Do;

END ObxBank1.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Bank1.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxBank1.Do
```

В консоли:

```sh
echo 'ObxBank1.Do' | tools/run64.sh
```

В журнале появится:

```
balance 200
balance 20
balance 80
refused 3
total 300
Tasks: cancelled
clock state 7
```

## Что произошло

Команда `Do` запускает часы и управляющего и крутит планировщик `Tasks.Step`, пока живы задачи.
Управляющий создаёт группу `Tasks.NewGroup` и запускает в неё трёх кассиров `Tasks.StartIn`; суммы
переводов — 40, 70 и 100.

Кассиры чередуются: после каждого перевода кассир зовёт `Tasks.Yield`. В процедуре `Transfer` нет
точки переключения, поэтому проверка остатка и обе записи неделимы: три перевода из пятнадцати
отклонены, ни один счёт не ушёл в минус, сумма осталась 300.

Управляющий спит в `Tasks.Join`, пока каждый кассир не закончит, печатает остатки и зовёт
`Tasks.Cancel(clock)`. Часы спят в `Tasks.Sleep(1000)`; отмена будит их, `Sleep` трапует 127, и
система печатает `Tasks: cancelled`. Состояние часов — 7, `Tasks.Cancelled`.

В этом примере мы увидели, как запустить задачи в группу и дождаться их, как отменить задачу,
которая работает вечно, и почему код между точками переключения обходится без замка.
