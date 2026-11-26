# Обзор на примерах: ObxActor2

Банк: счета и переводы под супервизором, директива `Resume` и тайм-аут простоя.

Десять счетов и тысяча переводов между ними — акторы, дети одного банка. Каждый сотый перевод несёт
нулевую сумму, и счёт трапует на предусловии. Пример показывает, почему для счетов выбрана директива
`Resume`: сумма денег в конце равна начальной. Пример относится к руководству
`Actor/Docu/ru/Actors.md` (раздел 6).

```
MODULE ObxActor2;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorSup, ActorRun;

	CONST
		accounts = 10;
		transfers = 1000;
		initial = 10000;	(* начальный остаток счёта *)
		badEvery = 100;	(* каждый badEvery-й перевод — с нулевой суммой *)

	TYPE
		Msg = ActorCore.Msg;
		Withdraw = POINTER TO RECORD (Msg) amount: INTEGER END;
		Deposit = POINTER TO RECORD (Msg) amount: INTEGER END;
		Done = POINTER TO RECORD (Msg) END;
		Refused = POINTER TO RECORD (Msg) END;
		GetBalance = POINTER TO RECORD (Msg) END;
		Balance = POINTER TO RECORD (Msg) n: INTEGER END;
		Report = POINTER TO RECORD (Msg) ok: BOOLEAN END;

		Account = POINTER TO RECORD (ActorCore.Actor) balance: INTEGER END;
		Transfer = POINTER TO RECORD (ActorCore.Actor)
			from, to: ActorCore.Ref;
			amount, stage: INTEGER
		END;
		Bank = POINTER TO RECORD (ActorCore.Actor) END;

		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		acct: ARRAY accounts OF ActorCore.Ref;
		nextFrom, nextTo, nextAmount: INTEGER;	(* параметры следующего перевода для фабрики *)
		finished, succeeded: INTEGER;
		total: INTEGER;
		done: Done; refused: Refused; getBalance: GetBalance;

	PROCEDURE WriteInt (x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	(* --- счёт ------------------------------------------------------------- *)

	PROCEDURE (a: Account) Receive (c: ActorCore.Context; m: Msg);
		VAR b: Balance;
	BEGIN
		WITH m: Withdraw DO
			ASSERT(m.amount > 0, 20);
			IF a.balance >= m.amount THEN DEC(a.balance, m.amount); c.Respond(done)
			ELSE c.Respond(refused)
			END
		| m: Deposit DO
			ASSERT(m.amount > 0, 20);
			INC(a.balance, m.amount); c.Respond(done)
		| m: GetBalance DO
			NEW(b); b.n := a.balance; c.Respond(b)
		ELSE
		END
	END Receive;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.balance := initial; RETURN a
	END NewAccount;

	(* --- перевод ---------------------------------------------------------- *)

	PROCEDURE Finish (t: Transfer; c: ActorCore.Context; ok: BOOLEAN);
		VAR r: Report;
	BEGIN
		NEW(r); r.ok := ok; c.Tell(c.Parent(), r);
		ActorCore.StopActor(c)
	END Finish;

	PROCEDURE (t: Transfer) Receive (c: ActorCore.Context; m: Msg);
		VAR w: Withdraw; d: Deposit;
	BEGIN
		WITH m: ActorCore.Started DO
			c.SetReceiveTimeout(200);
			NEW(w); w.amount := t.amount; c.Tell(t.from, w); t.stage := 1
		| m: Done DO
			IF t.stage = 1 THEN NEW(d); d.amount := t.amount; c.Tell(t.to, d); t.stage := 2
			ELSE Finish(t, c, TRUE)
			END
		| m: Refused DO Finish(t, c, FALSE)
		| m: ActorCore.ReceiveTimeout DO
			ASSERT(t.stage = 1, 100);	(* зачисление не трапует: деньги не теряются *)
			Finish(t, c, FALSE)
		ELSE
		END
	END Receive;

	PROCEDURE NewTransfer (): ActorCore.Actor;
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := acct[nextFrom]; t.to := acct[nextTo]; t.amount := nextAmount; t.stage := 0;
		RETURN t
	END NewTransfer;

	(* --- банк ------------------------------------------------------------- *)

	PROCEDURE (b: Bank) Receive (c: ActorCore.Context; m: Msg);
		VAR i, seed: INTEGER; p: ActorCore.Props; name: ARRAY 16 OF CHAR;
	BEGIN
		WITH m: ActorCore.Started DO
			p := ActorCore.NewProps(NewAccount);
			FOR i := 0 TO accounts - 1 DO
				Strings.IntToString(i, name); acct[i] := ActorCore.Spawn(c, p, "acct-" + name)
			END;
			p := ActorCore.NewProps(NewTransfer);
			seed := 12345;
			FOR i := 1 TO transfers DO
				seed := (seed * 1103 + 12345) MOD 65536; nextFrom := seed MOD accounts;
				seed := (seed * 1103 + 12345) MOD 65536; nextTo := seed MOD accounts;
				IF i MOD badEvery = 0 THEN nextAmount := 0 ELSE nextAmount := 1 + seed MOD 100 END;
				ASSERT(ActorCore.Spawn(c, p, "") # NIL, 100)
			END
		| m: Report DO
			INC(finished); IF m.ok THEN INC(succeeded) END
		ELSE
		END
	END Receive;

	PROCEDURE NewBank (): ActorCore.Actor;
		VAR b: Bank;
	BEGIN
		NEW(b); RETURN b
	END NewBank;

	(* --- проверка и команда ----------------------------------------------- *)

	PROCEDURE (d: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; r: Msg;
	BEGIN
		WHILE finished < transfers DO Tasks.Sleep(10) END;
		total := 0;
		FOR i := 0 TO accounts - 1 DO
			IF ActorCore.Ask(acct[i], getBalance, 1000, r) = ActorCore.ok THEN INC(total, r(Balance).n)
			ELSE total := MIN(INTEGER)
			END
		END;
		Console.WriteStr("bank: transfers "); WriteInt(transfers);
		Console.WriteStr(", done "); WriteInt(succeeded);
		Console.WriteStr(", refused "); WriteInt(transfers - succeeded); Console.WriteLn;
		Console.WriteStr("bank: total "); WriteInt(total);
		Console.WriteStr(", at start "); WriteInt(accounts * initial);
		IF total = accounts * initial THEN Console.WriteStr(": kept") ELSE Console.WriteStr(": BROKEN") END;
		Console.WriteLn;
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR p: ActorCore.Props; d: Driver;
	BEGIN
		NEW(done); NEW(refused); NEW(getBalance);
		finished := 0; succeeded := 0;
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		p := ActorCore.NewProps(NewBank); p.strategy := ActorSup.NewOneForOne(ActorCore.Resume, 0, 1000);
		ASSERT(ActorCore.Spawn(NIL, p, "bank") # NIL, 100);
		NEW(d); Tasks.Start(d);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor2.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Actor2.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxActor2.Do
```

В консоли:

```sh
echo 'ObxActor2.Do' | tools/run64.sh
```

В журнале появится:

```
bank: transfers 1000, done 983, refused 17
bank: total 100000, at start 100000: kept
```

## Что произошло

Банк — актор верхнего уровня со стратегией `OneForOne` и директивой `Resume`. Получив `Started`, он
создаёт десять счетов с остатком 10 000 и тысячу переводов. Параметры перевода выбирает
линейный конгруэнтный генератор с постоянным зерном, поэтому от запуска к запуску переводы одни и те
же.

Перевод — актор с тремя шагами. В `Started` он взводит тайм-аут простоя 200 мс и шлёт счёту-источнику
`Withdraw`. Ответ `Done` переводит его ко второму шагу: `Deposit` счёту-получателю. Второй `Done`
означает успех, `Refused` — отказ по недостатку денег. Итог перевод сообщает родителю (`c.Parent()`)
и останавливает себя.

Перевод с нулевой суммой роняет счёт на `ASSERT(m.amount > 0, 20)`. Банк отвечает `Resume`: ящик
счёта снимается с паузы, остаток сохранён, письмо-виновник пропущено. Перевод ответа не получает, и
через 200 мс без писем его обработчик получает `ReceiveTimeout`: перевод считается отказанным. Таких
переводов десять; ещё семь получили `Refused`, потому что генератор водит деньги по кругу неровно и
отдельные счета пустеют. Деньги при этом не теряются: тайм-аут возможен только на первом шаге, до
списания.

Когда все отчёты получены, задача-водитель спрашивает остатки через `Ask`, печатает сверку и зовёт
`ActorRun.Shutdown`. При `Restart` вместо `Resume` новый экземпляр счёта начал бы с 10 000, и сумма
изменилась бы на величину всех операций, прошедших через этот счёт.

В этом примере мы увидели супервизор над многими детьми, директиву `Resume`, которая сохраняет
состояние, и тайм-аут простоя как способ узнать, что ответа не будет.
