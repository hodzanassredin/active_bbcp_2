# Обзор на примерах: ObxBankCtx

Перевод между счетами: повторный вход в монитор и два монитора в руках.

Два покупателя снимают по сто со счёта A, касса кладёт на него две суммы, а одна задача переводит
пятьдесят со счёта A на счёт B. Перевод идёт операцией над счётом A; внутри неё та же задача входит
в монитор A ещё раз и берёт монитор B, не отпуская A. Пример показывает повторный вход, работу с
двумя мониторами сразу и то, как ожидание остатка записано внутри операции. Общие понятия пропуска,
операции и `Await` разобраны в `Obx/Docu/ru/BufCtx.md`; пример относится к руководству
`Docu/ru/Tasks.md`.

```
MODULE ObxBankCtx;
(** Счёт на мониторе-объекте: перевод повторным входом.

	Перевод — операция Transfer над счётом A. Её Exclusive уже идёт под
	монитором A и зовёт A.Do со списанием: это повторный вход, глубина
	становится 2. Затем под тем же внешним монитором берётся монитор B.
	Снятие ждёт остатка, как обычная операция; на глубине 1 монитор
	отпускается при выходе. *)

	IMPORT Tasks, Console, Strings;

	TYPE
		WithdrawOp = RECORD (Tasks.Op) amount: INTEGER; ok: BOOLEAN END;
		DepositOp = RECORD (Tasks.Op) amount: INTEGER END;
		TransferOp = RECORD (Tasks.Op) to: Account; amount: INTEGER; ok: BOOLEAN END;

		Account = POINTER TO RECORD (Tasks.Object)
			balance: INTEGER
		END;

		Buyer = POINTER TO RECORD (Tasks.Job) a: Account; amount: INTEGER END;
		Payroll = POINTER TO RECORD (Tasks.Job) a: Account; first, second: INTEGER END;
		Transfer = POINTER TO RECORD (Tasks.Job) from, to: Account; amount: INTEGER END;

	VAR
		transferDone: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (a: Account) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF a.balance > 0 THEN INCL(s, 0) END;
		RETURN s
	END Ready;

	PROCEDURE (a: Account) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR w: WithdrawOp; d: DepositOp;
	BEGIN
		WITH op: WithdrawOp DO
			(* Цикл нужен: флаг 0 значит «есть хоть что-то», а не «хватает суммы». *)
			WHILE a.balance < op.amount DO
				IF ~a.Await(ctx, {0}) THEN op.ok := FALSE; RETURN END
			END;
			DEC(a.balance, op.amount); op.ok := TRUE
		| op: DepositOp DO
			INC(a.balance, op.amount)
		| op: TransferOp DO
			(* Повторный вход в тот же монитор: глубина 2. *)
			w.amount := op.amount; w.ok := FALSE;
			a.Do(ctx, w);
			IF ~w.ok THEN RETURN END;
			(* Монитор A остаётся у задачи, пока берётся монитор B. *)
			d.amount := op.amount;
			op.to.Do(ctx, d);
			op.ok := TRUE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (b: Buyer) Body- (VAR ctx: Tasks.Ctx);
		VAR op: WithdrawOp;
	BEGIN
		Tasks.Sleep(5);
		op.amount := b.amount; op.ok := FALSE;
		b.a.Do(ctx, op)
	END Body;

	PROCEDURE (p: Payroll) Body- (VAR ctx: Tasks.Ctx);
		VAR op: DepositOp;
	BEGIN
		op.amount := p.first; p.a.Do(ctx, op);
		Tasks.Sleep(10);
		op.amount := p.second; p.a.Do(ctx, op)
	END Body;

	PROCEDURE (t: Transfer) Body- (VAR ctx: Tasks.Ctx);
		VAR op: TransferOp;
	BEGIN
		Tasks.Sleep(15);
		op.to := t.to; op.amount := t.amount; op.ok := FALSE;
		t.from.Do(ctx, op);
		transferDone := op.ok
	END Body;

	PROCEDURE Do*;
		VAR a, b: Account; x, y: Buyer; p: Payroll; t: Transfer;
	BEGIN
		NEW(a); a.balance := 200;
		NEW(b); b.balance := 0;
		NEW(x); x.a := a; x.amount := 100;
		NEW(y); y.a := a; y.amount := 100;
		NEW(p); p.a := a; p.first := 50; p.second := 50;
		NEW(t); t.from := a; t.to := b; t.amount := 50;
		transferDone := FALSE;
		Tasks.Start(t); Tasks.Start(x); Tasks.Start(y); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("A = ", a.balance);
		Say("B = ", b.balance);
		IF transferDone THEN
			Console.WriteStr("transfer ok"); Console.WriteLn
		ELSE
			Console.WriteStr("transfer failed"); Console.WriteLn
		END
	END Do;

END ObxBankCtx.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/BankCtx.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxBankCtx.Do
```

В консоли:

```sh
echo 'ObxBankCtx.Do' | tools/run64.sh
```

В журнале появится:

```
A = 50
B = 50
transfer ok
```

## Что произошло

Счёт — монитор-объект `Account` с полем `balance`. Операции над ним — записи `WithdrawOp`,
`DepositOp` и `TransferOp`; каждая объявлена на стеке вызывающего. Обычный вход в монитор один и тот
же: `a.Do(ctx, op)` берёт монитор и зовёт `Account.Exclusive`, а тот разбирает вид записи через
`WITH`.

Перевод устроен сложнее. `Transfer` вызывает `t.from.Do(ctx, op)`, то есть операцию `TransferOp` над
A. `Exclusive` уже исполняется под монитором A на глубине 1, а внутри зовёт `a.Do(ctx, w)` со
списанием. Это повторный вход той же задачи: `Do` видит, что владелец монитора — она сама, и не
ставит задачу в очередь, а увеличивает глубину до 2. Поэтому пропуск и несёт сведения о задаче:
иначе объект не отличил бы повторный вход от чужого. Выход с глубины 2 уменьшает счётчик, и только
выход с глубины 1 отпускает монитор.

После успешного списания перевод берёт монитор B: `op.to.Do(ctx, d)`. Монитор A при этом остаётся у
задачи: `Exclusive` ещё не вернулся. Так задача держит два монитора сразу. Правило о сне с
мониторами в руках разрешает ждать, только если каждый удерживаемый объект ответил `TRUE` на
`HoldsWhileWaiting`; по умолчанию ответ `FALSE`, и ожидание трапует 26. Здесь задача не ждёт, пока
держит оба монитора, поэтому правило не срабатывает. Порядок захвата один: сначала A, потом B. Если
бы другая задача брала их в обратном порядке, две задачи могли бы застрять; это разобрано в
`ObxBank3`.

Списание внутри перевода может ждать остатка в `Await`, но `Await` на глубине 2 того же монитора
трапует 23. Пример рассчитан так, что к моменту перевода на A уже хватает денег, и вложенное
списание не ждёт. Ждать остатка умеет обычная операция на глубине 1: она отпускает монитор, спит и
после пробуждения проверяет остаток снова.

Расписание прогона такое. Задача перевода спит 15 мс. Покупатели спят 5 мс и затем снимают по 100.
Касса сразу кладёт 50, спит 10 мс и кладёт ещё 50. На счету A лежит 200; после двух взносов и двух
снятий остаётся 100. Перевод в 15 мс снимает 50 с A и кладёт 50 на B. Итог: `A = 50`, `B = 50`.
Каждая операция проходит целиком, потому что проверка и изменение стоят внутри одной исключающей
процедуры; `transfer ok` подтверждает, что перевод получил `TRUE` от обеих операций.

Снятие и зачисление — две операции над двумя объектами. Если пропуск остановлен между ними, `Do` на
счёте B не исполнит зачисление, и снятые деньги не придут на B: перевод по двум мониторам не
атомарен. Пример этот случай не проверяет; перевод одной транзакцией с откатом показывает `ObxStm0`.

В этом примере мы увидели повторный вход в тот же монитор через пропуск, захват второго монитора без
отпускания первого и то, что ожидание остатка живёт внутри операции, а не в теле вызывающего.
