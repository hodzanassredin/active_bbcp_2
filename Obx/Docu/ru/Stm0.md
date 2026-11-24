# Обзор на примерах: ObxStm0

Перевод между счетами: трап между «снять» и «зачислить» не теряет денег.

Перевод — два действия: снять деньги с одного счёта и зачислить на другой. Между ними нет точки
переключения, и другие задачи половины перевода не видят. Но между ними может случиться трап, и
тогда снятые деньги пропали бы. Пример показывает, как транзакция `AsyncStm.Atomically` откатывает
снятие. Пример относится к руководству `Docu/ru/Async.md`, раздел «Транзакции».

```
MODULE ObxStm0;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST limit = 150;	(* больше этого счёт b не принимает *)

	TYPE
		Transfer = POINTER TO RECORD (AsyncStm.Tx)
			from, to: AsyncStm.IntVar; n: INTEGER
		END;
		Clerk = POINTER TO RECORD (Tasks.Task) END;

	VAR a, b: AsyncStm.IntVar;

	PROCEDURE (t: Transfer) Do;
	BEGIN
		t.from.Set(t.from.Get() - t.n);	(* снять *)
		ASSERT(t.to.Get() + t.n <= limit, 30);	(* трап между «снять» и «зачислить» *)
		t.to.Set(t.to.Get() + t.n)	(* зачислить *)
	END Do;

	PROCEDURE Move (n: INTEGER);
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := a; t.to := b; t.n := n;
		AsyncStm.Atomically(t)
	END Move;

	PROCEDURE (c: Clerk) Run;
	BEGIN
		Move(100); Move(100)
	END Run;

	PROCEDURE Show;
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr("a = "); Strings.IntToString(a.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", b = "); Strings.IntToString(b.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", sum = "); Strings.IntToString(a.Get() + b.Get(), s); Console.WriteStr(s);
		Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR c: Clerk;
	BEGIN
		a := AsyncStm.NewInt(300); b := AsyncStm.NewInt(0);
		NEW(c); Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.Failed(c) THEN Console.WriteStr("clerk trapped") ELSE Console.WriteStr("clerk done") END;
		Console.WriteLn;
		Show
	END Do;

END ObxStm0.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Stm0.cp`). В окне BlackBox команду
исполняет коммандер:

```
   ObxStm0.Do
```

В консоли:

```sh
echo 'ObxStm0.Do' | tools/run64.sh
```

В журнале появится:

```
clerk trapped
a = 200, b = 100, sum = 300
```

## Что произошло

Команда `Do` заводит два счёта — транзакционные переменные `a = 300` и `b = 0` — и запускает задачу
`Clerk`. Задача делает два перевода по 100. Каждый перевод — объект `Transfer`, его метод `Do`
снимает, проверяет предел счёта `b` и зачисляет; `AsyncStm.Atomically` исполняет `Do` как одну
транзакцию.

Первый перевод проходит: `a = 200`, `b = 100`. Второй снимает ещё 100 с `a` и упирается в предел:
`b + 100 = 200` больше 150, и `ASSERT` трапает с кодом 30. В этот момент `a` уже равен 100. Перед
первой записью в `a` метод `Set` отдал прежнее значение в журнал отката транзакции; поймав трап,
`Atomically` возвращает из журнала `a = 200` и передаёт трап 30 дальше. Задача завершается с трапом,
`Tasks.Failed(c)` истинно, и команда печатает `clerk trapped`.

Сумма осталась 300: второй перевод не сделан целиком. Без `Atomically` метод `Do` вызывался бы
напрямую, и после трапа было бы `a = 100`, `b = 100`, сумма 200.

В этом примере мы увидели, как описать транзакцию объектом с методом `Do`, как завести
транзакционные переменные и как трап внутри транзакции откатывает её записи и доходит до
вызывающего с тем же номером.
