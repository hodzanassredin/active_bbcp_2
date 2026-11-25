# Обзор на примерах: ObxBank3

Два кассира входят в мониторы двух счетов в разном порядке и попадают в тупик.

Перевод — исключающий метод счёта: он входит в монитор своего счёта, а затем в монитор счёта
получателя. Один кассир переводит с `a` на `b`, другой — с `b` на `a`. Пример показывает тупик и
журнал, который печатает планировщик. Пример относится к главе 3 учебника
`Docu/ru/Tasks.md`.

```
MODULE ObxBank3;
	IMPORT Tasks, Console;

	TYPE
		DepositOp = RECORD (Tasks.Op) n: INTEGER END;
		TransferOp = RECORD (Tasks.Op) to: Account; n: INTEGER END;

		Account = POINTER TO RECORD (Tasks.Object)
			balance: INTEGER
		END;

		Clerk = POINTER TO RECORD (Tasks.Job) from, to: Account END;

	VAR a, b: Account; c1, c2: Clerk;

	PROCEDURE (acc: Account) HoldsWhileWaiting (): BOOLEAN;
	(* Перевод держит монитор счёта, пока берёт монитор второго счёта. *)
	BEGIN
		RETURN TRUE
	END HoldsWhileWaiting;

	PROCEDURE (acc: Account) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR dep: DepositOp;
	BEGIN
		WITH op: DepositOp DO
			INC(acc.balance, op.n)
		| op: TransferOp DO
			DEC(acc.balance, op.n);
			Tasks.Yield;	(* держит монитор acc и уступает *)
			dep.n := op.n;
			op.to.Do(ctx, dep)	(* вход во второй монитор под монитором acc *)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (c: Clerk) Body- (VAR ctx: Tasks.Ctx);
		VAR op: TransferOp;
	BEGIN
		op.to := c.to; op.n := 10;
		c.from.Do(ctx, op)
	END Body;

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
