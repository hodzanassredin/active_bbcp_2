# Обзор на примерах: ObxBankObj

Счёт на мониторе в базовом классе: два покупателя ждут остатка.

Два покупателя хотят снять по 100 с пустого счёта; зарплата приходит двумя частями — 150 и
затем 50. Перед выдачей кассир сверяет подпись, и это ожидание. Счёт — объект; `Withdraw` и
`Deposit` — записи операций, а условие записано как `WHILE balance < amount DO account.Wait END`.
Модуль не использует расширений языка. Та же сцена в старой форме, с исключающей процедурой и
`ACTIVE.AWAIT`, — `ObxBank2`; разбор — `docs/object-form-study.md`.

```
MODULE ObxBankObj;
(** Счёт в банке на мониторе в базовом классе.

	Тот же сценарий, что ObxBank2: два покупателя ждут остатка на счёте,
	кассир проверяет подпись (ожидание), зарплата приходит двумя частями.
	Здесь монитор — объект Account: операции Withdraw и Deposit —
	записи-наследники Op, ожидание условия — WHILE balance < n DO Wait.
	Для сравнения со старым способом см. ObxBank2 ([exclusive] + AWAIT). *)

	IMPORT AsyncObject, Tasks, Console, Strings;

	TYPE
		WithdrawOp = RECORD (AsyncObject.Op)
			amount: INTEGER;
			ok: BOOLEAN
		END;
		DepositOp = RECORD (AsyncObject.Op)
			amount: INTEGER
		END;

		Account = POINTER TO RECORD (AsyncObject.Object)
			balance: INTEGER
		END;

		Buyer = POINTER TO RECORD (Tasks.Task) a: Account END;
		Payroll = POINTER TO RECORD (Tasks.Task) a: Account END;

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

	PROCEDURE (a: Account) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: WithdrawOp DO
			(* Проверка и списание — под одним монитором *)
			WHILE a.balance < op.amount DO a.Wait END;
			DEC(a.balance, op.amount); op.ok := TRUE
		| op: DepositOp DO
			INC(a.balance, op.amount)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE Withdraw (a: Account; n: INTEGER);
		VAR op: WithdrawOp;
	BEGIN
		op.amount := n; op.ok := FALSE; a.Do(op)
	END Withdraw;

	PROCEDURE Deposit (a: Account; n: INTEGER);
		VAR op: DepositOp;
	BEGIN
		op.amount := n; a.Do(op)
	END Deposit;

	PROCEDURE (b: Buyer) Run;
	BEGIN
		Check;
		Withdraw(b.a, 100);
		Say("paid 100, balance ", b.a.balance)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10); Deposit(p.a, 150);
		Say("deposit 150, balance ", p.a.balance);
		Tasks.Sleep(30); Deposit(p.a, 50);
		Say("deposit 50, balance ", p.a.balance)
	END Run;

	PROCEDURE Do*;
		VAR a: Account; b1, b2: Buyer; p: Payroll;
	BEGIN
		NEW(a); a.balance := 0;
		NEW(b1); b1.a := a; NEW(b2); b2.a := a; NEW(p); p.a := a;
		Tasks.Start(b1); Tasks.Start(b2); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("final balance ", a.balance)
	END Do;

END ObxBankObj.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/BankObj.cp`; только amd64). В окне
BlackBox команду выполняет коммандер:

```
   ObxBankObj.Do
```

В консоли:

```sh
echo 'ObxBankObj.Do' | tools/run64.sh
```

В журнале появится:

```
deposit 150, balance 150
paid 100, balance 50
deposit 50, balance 100
paid 100, balance 0
final balance 0
```

## Что произошло

1. Запускаются два покупателя и зарплата. Покупатель спит 5 мс (сверка подписи) и зовёт
   `Withdraw(account, 100)`; зарплата спит 10 мс и вносит 150.
2. `Withdraw` заводит `WithdrawOp` на своём стеке и зовёт `account.Do`. Монитор взят,
   `Exclusive` видит `balance < amount` и зовёт `account.Wait`; монитор отпущен, покупатель спит.
3. `Deposit` входит в тот же монитор и прибавляет 150. Его выход будит обоих покупателей; они
   сами перепроверяют условие.
4. Один покупатель снимает 100. Второму всё ещё мало, и он ждёт снова; второй вклад в 50 будит
   его, и он снимает остаток.
5. Итоговый остаток — ноль; вывод совпадает с частью `with monitor` примера `ObxBank2`.

В этом примере мы увидели ту же банковскую сцену без расширения языка: монитор — поле базового
класса, операция — запись на стеке, а условие ожидания — цикл.
