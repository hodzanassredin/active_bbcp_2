# Обзор на примерах: ObxStm1

Ожидание условия над двумя переменными: `AsyncStm.Retry`.

Покупатель ждёт, пока на его счёте наберётся 250 и откроется магазин. Счёт и признак «открыто» —
разные объекты; `ACTIVE.AWAIT` ждёт условия только внутри одного монитора и здесь не подходит.
Пример показывает, как транзакция ждёт условия над двумя переменными. Пример относится к
руководству `Docu/ru/Async.md`, раздел «Транзакции».

```
MODULE ObxStm1;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST price = 250;

	TYPE
		Buy = POINTER TO RECORD (AsyncStm.Tx) tries: INTEGER END;
		Buyer = POINTER TO RECORD (Tasks.Task) END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;
		Opener = POINTER TO RECORD (Tasks.Task) END;

	VAR
		money: AsyncStm.IntVar;	(* счёт покупателя *)
		open: AsyncStm.BoolVar;	(* магазин открыт *)

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (t: Buy) Do;
	BEGIN
		INC(t.tries);	(* обычное поле: откат его не трогает *)
		IF (money.Get() < price) OR ~open.Get() THEN AsyncStm.Retry END;
		money.Set(money.Get() - price)
	END Do;

	PROCEDURE (b: Buyer) Run;
		VAR t: Buy;
	BEGIN
		NEW(t); t.tries := 0;
		AsyncStm.Atomically(t);
		Say("bought, money = ", money.Get());
		Say("transaction ran times: ", t.tries)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10);
		money.Set(money.Get() + 200);
		Say("salary paid, money = ", money.Get())
	END Run;

	PROCEDURE (o: Opener) Run;
	BEGIN
		Tasks.Sleep(20);
		open.Set(TRUE);
		Console.WriteStr("shop opened"); Console.WriteLn
	END Run;

	PROCEDURE Do*;
		VAR b: Buyer; p: Payroll; o: Opener;
	BEGIN
		money := AsyncStm.NewInt(100); open := AsyncStm.NewBool(FALSE);
		NEW(b); NEW(p); NEW(o);
		Tasks.Start(b); Tasks.Start(p); Tasks.Start(o);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxStm1.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Stm1.cp`). В окне BlackBox команду
исполняет коммандер:

```
   ObxStm1.Do
```

В консоли:

```sh
echo 'ObxStm1.Do' | tools/run64.sh
```

В журнале появится:

```
salary paid, money = 300
shop opened
bought, money = 50
transaction ran times: 3
```

## Что произошло

Команда `Do` заводит счёт `money = 100` и признак `open = FALSE` и запускает три задачи. Задача
`Buyer` исполняет транзакцию `Buy`. Её метод `Do` читает обе переменные; денег 100, магазин закрыт,
и `Do` зовёт `AsyncStm.Retry`. `Retry` откатывает транзакцию, и `Atomically` усыпляет задачу до
изменения одной из прочитанных переменных — `money` или `open`.

Через 10 мс задача `Payroll` прибавляет 200 к счёту вне транзакции. `Set` будит покупателя.
`Atomically` исполняет `Do` заново: денег 300, но магазин закрыт, и задача снова ждёт. Через 20 мс
задача `Opener` открывает магазин; третье исполнение `Do` снимает 250, и транзакция завершается.

Счётчик `tries` — обычное поле объекта, не транзакционная переменная, поэтому откат его не трогает и
он показывает, что `Do` исполнялся три раза.

В этом примере мы увидели, как `Retry` ждёт условия над несколькими переменными и как изменение
любой прочитанной переменной будит ждущую транзакцию.
