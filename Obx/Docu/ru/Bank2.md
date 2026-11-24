# Обзор на примерах: ObxBank2

Два покупателя ждут денег на счёте: без монитора и с монитором.

Два покупателя хотят снять по 100 с пустого счёта и ждут, пока придут деньги; перед выдачей
кассир сверяет подпись, и это ожидание. Пример проигрывает наивную версию, где между проверкой и
списанием стоит ожидание, и версию с исключающей процедурой и `ACTIVE.AWAIT`. Пример относится к
главе 3 учебника `Docu/ru/Tut-Active-3.md`.

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

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Bank2.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxBank2.Do
```

В консоли:

```sh
echo 'ObxBank2.Do' | tools/run64.sh
```

В журнале появится:

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

## Что произошло

В наивной версии покупатели опрашивают остаток через `Tasks.Sleep(1)`. Через 10 мс приходит 150.
Оба видят, что денег хватает, и оба уходят сверять подпись в `Check`. Сверка — точка переключения,
и пока первый в ней спит, второй тоже проходит проверку. Оба списывают по 100, остаток −50.

В версии с монитором покупатель сначала сверяет подпись, затем вызывает исключающую процедуру
`Withdraw`. `ACTIVE.AWAIT(balance >= n)` отпускает монитор модуля и усыпляет покупателя, пока
условие ложно. Взнос `Deposit` — тоже исключающая процедура; выходя из монитора, он перепроверяет
условия ждущих. После взноса 150 проходит первый покупатель, условие второго снова ложно (50), и он
ждёт второго взноса.

В этом примере мы увидели, почему ожидание между проверкой и изменением ломает неделимость и как
исключающая процедура с `AWAIT` делает проверку и изменение одним шагом.
