# Обзор на примерах: ObxBank3

Два кассира входят в мониторы двух счетов в разном порядке и попадают в тупик.

Перевод — исключающий метод счёта: он входит в монитор своего счёта, а затем в монитор счёта
получателя. Один кассир переводит с `a` на `b`, другой — с `b` на `a`. Пример показывает тупик и
журнал, который печатает планировщик. Пример относится к главе 3 учебника
`Docu/ru/Tut-Active-3.md`.

```
MODULE ObxBank3;
	IMPORT ACTIVE, Tasks, Console;

	TYPE
		Account = POINTER TO RECORD balance: INTEGER END;
		Clerk = POINTER TO RECORD (Tasks.Task) from, to: Account END;

	VAR a, b: Account; c1, c2: Clerk;

	PROCEDURE [exclusive] (acc: Account) Deposit (n: INTEGER), NEW;
	BEGIN
		acc.balance := acc.balance + n
	END Deposit;

	PROCEDURE [exclusive] (acc: Account) TransferTo (to: Account; n: INTEGER), NEW;
	BEGIN
		acc.balance := acc.balance - n;
		Tasks.Yield;	(* держит монитор acc и уступает *)
		to.Deposit(n)	(* вход во второй монитор *)
	END TransferTo;

	PROCEDURE (c: Clerk) Run;
	BEGIN
		c.from.TransferTo(c.to, 10)
	END Run;

	PROCEDURE Do*;
	BEGIN
		NEW(a); a.balance := 100; NEW(b); b.balance := 100;
		NEW(c1); c1.from := a; c1.to := b; Tasks.SetName(c1, "clerk a->b");
		NEW(c2); c2.from := b; c2.to := a; Tasks.SetName(c2, "clerk b->a");
		Tasks.Start(c1); Tasks.Start(c2);
		WHILE (Tasks.Count() > 0) & ~Tasks.Deadlocked() DO Tasks.Step END;
		IF Tasks.Deadlocked() THEN
			Console.WriteStr("deadlock, cancelling"); Console.WriteLn;
			Tasks.Cancel(c1); Tasks.Cancel(c2);
			WHILE Tasks.Count() > 0 DO Tasks.Step END
		END
	END Do;

END ObxBank3.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Bank3.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxBank3.Do
```

В консоли:

```sh
echo 'ObxBank3.Do' | tools/run64.sh   # вывод не сверяется
```

В журнале появится (вместо `…` печатается адрес монитора, он меняется от запуска к запуску,
поэтому строку запуска выше `tools/check-examples.sh` не исполняет):

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Kernel: monitor dump
  monitor …: held by clerk a->b count=1
    enters clerk b->a
  monitor …: held by clerk b->a count=1
    enters clerk a->b
deadlock, cancelling
Tasks: cancelled
Tasks: cancelled
```

## Что произошло

Первый кассир входит в монитор `a`, списывает 10 и зовёт `Tasks.Yield`, не выходя из монитора.
Второй кассир входит в монитор `b` и тоже уступает. Первый вызывает `b.Deposit` — монитор `b` занят,
он ждёт у входа. Второй вызывает `a.Deposit` — монитор `a` занят, он ждёт тоже.

Готовых задач нет, и никто не спит со сроком. Шаг планировщика печатает журнал и поднимает
`Tasks.Deadlocked()`: каждый монитор, его владелец и кто стоит у входа. Имена задачам дал
`Tasks.SetName`. Команда видит признак и отменяет обоих кассиров; отмена трапует их с кодом 127,
мониторы отпускаются.

Тупика не будет, если переводы входят в мониторы в одном порядке, например по возрастанию номера
счёта.

В этом примере мы увидели, как возникает тупик на двух мониторах, как его показывает журнал
планировщика и как программа может из него выйти отменой.
