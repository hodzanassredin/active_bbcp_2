[← Глава 2](Tut-Active-2.md) · [Оглавление](Tut-Active-TOC.md) · [Глава 4 →](Tut-Active-4.md)

# 3. Общие данные и монитор

## 3.1. Ожидание посреди перевода

На счёте пусто. Два покупателя хотят снять по 100 и готовы подождать, пока деньги появятся. Перед
выдачей денег кассир сверяет подпись — это занимает время, и на это время задача засыпает. Через
10 мс приходит зарплата 150, ещё через 30 мс — 50.

Наивное решение: подождать, пока денег хватит, сверить подпись, списать.

```
WHILE balance < n DO Tasks.Sleep(1) END;
Check;	(* сверка подписи: Tasks.Sleep(5) *)
balance := balance - n
```

После зарплаты оба покупателя видят 150 и оба идут сверять подпись. Пока первый ждёт в `Check`,
второй тоже проверил остаток и тоже ждёт. Оба списывают по 100, и на счёте −50. Правило главы 2
здесь не помогает: между проверкой и списанием стоит ожидание, а ожидание — точка переключения.

## 3.2. Монитор и AWAIT

Нужно, чтобы проверка «денег хватает» и списание были одним неделимым шагом, даже если перед этим
кто-то ждал. Для этого есть *исключающая процедура* — процедура с флагом `[exclusive]`. С ней связан
*монитор*: объект, который в каждый момент принадлежит не больше чем одной задаче. Задача, которая
вызвала исключающую процедуру, пока монитор занят, ждёт у входа. Все исключающие процедуры уровня
модуля делят один монитор модуля; у метода монитор — его запись, и у разных записей мониторы разные.

Внутри исключающей процедуры можно ждать условия: `ACTIVE.AWAIT(balance >= n)`. Если условие ложно,
задача отпускает монитор и засыпает; когда другая задача выйдет из монитора, условие проверят снова,
и ждущая получит монитор обратно — уже с истинным условием. Это и есть «подождать, пока на счёте
появятся деньги», записанное одной строкой. Флаг и `AWAIT` доступны модулю, который импортирует
`ACTIVE`; правила — в разделах 5 и 6 сообщения о языке `Docu/ru/Active-CP-Lang.md`.

Модуль `ObxBank2` проигрывает обе версии:

```
MODULE ObxBank2;
	IMPORT ACTIVE, Tasks, Console, Strings;

	TYPE
		Buyer = POINTER TO RECORD (Tasks.Task) safe: BOOLEAN END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: INTEGER;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE Check;
	BEGIN
		Tasks.Sleep(5)	(* кассир сверяет подпись: это ожидание *)
	END Check;

	(* Без монитора: между проверкой и списанием стоит ожидание *)
	PROCEDURE NaiveWithdraw (n: INTEGER);
	BEGIN
		WHILE balance < n DO Tasks.Sleep(1) END;
		Check;
		balance := balance - n
	END NaiveWithdraw;

	(* С монитором: проверка и списание в одной исключающей процедуре *)
	PROCEDURE [exclusive] Withdraw (n: INTEGER);
	BEGIN
		ACTIVE.AWAIT(balance >= n);
		balance := balance - n
	END Withdraw;

	PROCEDURE [exclusive] Deposit (n: INTEGER);
	BEGIN
		balance := balance + n
	END Deposit;

	PROCEDURE (b: Buyer) Run;
	BEGIN
		IF b.safe THEN Check; Withdraw(100) ELSE NaiveWithdraw(100) END;
		Say("paid 100, balance ", balance)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10); Deposit(150); Say("deposit 150, balance ", balance);
		Tasks.Sleep(30); Deposit(50); Say("deposit 50, balance ", balance)
	END Run;

	PROCEDURE Run (safe: BOOLEAN);
		VAR b1, b2: Buyer; p: Payroll;
	BEGIN
		balance := 0;
		NEW(b1); b1.safe := safe; NEW(b2); b2.safe := safe; NEW(p);
		Tasks.Start(b1); Tasks.Start(b2); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("final balance ", balance)
	END Run;

	PROCEDURE Do*;
	BEGIN
		Console.WriteStr("without monitor:"); Console.WriteLn; Run(FALSE);
		Console.WriteStr("with monitor:"); Console.WriteLn; Run(TRUE)
	END Do;

END ObxBank2.
```

Команда печатает:

```
without monitor:
deposit 150, balance 150
paid 100, balance 50
paid 100, balance -50
deposit 50, balance 0
final balance 0
with monitor:
deposit 150, balance 150
paid 100, balance 50
deposit 50, balance 100
paid 100, balance 0
final balance 0
```

Итог в обоих случаях 0, но по дороге наивная версия ушла в минус, а версия с монитором — нет: второй
покупатель дождался второго взноса. Сверка подписи в безопасной версии стоит до входа в монитор.
Держать монитор, пока задача спит, можно, но тогда все остальные кассиры ждут у входа; монитор
держат коротко.

Зачем `Deposit` исключающая, если в ней нет ожидания? Затем, что `AWAIT` перепроверяет условия,
когда кто-то выходит из монитора. Взнос через исключающую процедуру будит ждущего покупателя сразу.
Монитор защищает данные только от тех, кто сам заходит в монитор; поле, которое меняют в обход
исключающих процедур, он не защищает, и компилятор этого не проверяет.

## 3.3. Тупик

Теперь у каждого счёта свой монитор: перевод — метод счёта, исключающий по своему счёту. Перевод с
`a` на `b` входит в монитор `a`, списывает, затем вызывает `b.Deposit` и входит в монитор `b`. Два
кассира одновременно переводят: один с `a` на `b`, другой с `b` на `a`. Первый держит `a` и ждёт
`b`; второй держит `b` и ждёт `a`. Ни один не продолжит никогда. Такое положение называется
*тупиком*.

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

Нить операционной системы в таком положении просто висела бы. Планировщик видит тупик: живые
задачи есть, готовых нет, и никто не спит со сроком. Он один раз печатает журнал «кто чего ждёт» и
поднимает признак `Tasks.Deadlocked()`:

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

Журнал читается так: монитор первого счёта держит `clerk a->b`, а у входа в него стоит
`clerk b->a`; со вторым — наоборот. Имена задачам дал `Tasks.SetName`; без имён журнал понять
труднее. Вместо `…` печатается адрес монитора, он меняется от запуска к запуску. Команда замечает
тупик и отменяет обоих кассиров; отмена снимает их с очередей и отпускает мониторы.

Лекарство от такого тупика — входить в мониторы всегда в одном порядке. Например, перевод сначала
входит в монитор счёта с меньшим номером, потом с большим, куда бы ни шли деньги. Язык порядка не
проверяет. Планировщик ловит только полный тупик: если хоть одна задача спит со сроком, журнала не
будет, хотя часть задач стоит навсегда (`Docu/ru/Tasks.md`, раздел 6).

## 3.4. Цена и границы

Вход и выход свободного монитора стоят 62–78 нс (`docs/measurements.md`, раздел 2.2). Это дороже,
чем в Go, но для перевода денег незаметно.

Монитор хорош, пока данные одного действия лежат под одним монитором. Как только перевод затрагивает
два объекта со своими мониторами, появляется порядок входа и угроза тупика. И монитор не спасает от
другой беды: если перевод трапнет между списанием и зачислением, деньги пропадут — монитор
отпустится, а списанное не вернётся. Обе беды снимает транзакция, о ней следующая глава.
