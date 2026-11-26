# Обзор на примерах: ObxStm2

Выбор из двух транзакций: `AsyncStm.OrElse`.

Покупатель платит с первой карты, а если на ней не хватает денег — со второй. Каждая оплата —
готовая транзакция `Pay`, которая ждёт в `Retry`, пока денег мало. Пример показывает, как
`OrElse` складывает две такие транзакции в одну, не заглядывая в них. Пример относится к
руководству `Docu/ru/Async.md`, раздел «Транзакции».

```
MODULE ObxStm2;
	IMPORT Tasks, AsyncStm, Console, Strings;

	TYPE
		Pay = POINTER TO RECORD (AsyncStm.Tx) card: AsyncStm.IntVar; n: INTEGER END;
		Either = POINTER TO RECORD (AsyncStm.Tx) first, second: Pay END;
		Customer = POINTER TO RECORD (Tasks.Job) n: INTEGER END;
		Bank = POINTER TO RECORD (Tasks.Job) END;

	VAR card1, card2: AsyncStm.IntVar;

	PROCEDURE Show (IN what: ARRAY OF CHAR);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(what);
		Console.WriteStr(": card1 = "); Strings.IntToString(card1.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", card2 = "); Strings.IntToString(card2.Get(), s); Console.WriteStr(s);
		Console.WriteLn
	END Show;

	PROCEDURE (t: Pay) Do;
	BEGIN
		IF t.card.Get() < t.n THEN AsyncStm.Retry END;
		t.card.Set(t.card.Get() - t.n)
	END Do;

	PROCEDURE (t: Either) Do;
	BEGIN
		AsyncStm.OrElse(t.first, t.second)
	END Do;

	PROCEDURE (c: Customer) Body- (VAR ctx: Tasks.Ctx);
		VAR t: Either;
	BEGIN
		NEW(t); NEW(t.first); NEW(t.second);
		t.first.card := card1; t.first.n := c.n;
		t.second.card := card2; t.second.n := c.n;
		AsyncStm.Atomically(t);
		Show("paid")
	END Body;

	PROCEDURE (b: Bank) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(10);
		card1.Set(card1.Get() + 100);
		Show("deposit")
	END Body;

	PROCEDURE Do*;
		VAR c: Customer; b: Bank;
	BEGIN
		card1 := AsyncStm.NewInt(10); card2 := AsyncStm.NewInt(80);
		NEW(c); c.n := 50; Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		NEW(c); c.n := 50; Tasks.Start(c);
		NEW(b); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxStm2.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Stm2.cp`). В окне BlackBox команду
исполняет коммандер:

```
   ObxStm2.Do
```

В консоли:

```sh
echo 'ObxStm2.Do' | tools/run64.sh
```

В журнале появится:

```
paid: card1 = 10, card2 = 30
deposit: card1 = 110, card2 = 30
paid: card1 = 60, card2 = 30
```

## Что произошло

На первой карте 10, на второй 80. Первый покупатель платит 50. `OrElse` исполняет первую ветвь:
денег мало, ветвь зовёт `Retry`. `OrElse` откатывает записи первой ветви и исполняет вторую;
она снимает 50 со второй карты, и транзакция завершается: `card2 = 30`.

Второй покупатель тоже платит 50. Теперь не хватает на обеих картах: обе ветви делают `Retry`, и
транзакция целиком ждёт изменения любой переменной, прочитанной в любой ветви. Задача `Bank`
через 10 мс зачисляет 100 на первую карту. Покупатель просыпается, первая ветвь находит 110 и
снимает 50.

В этом примере мы увидели, как `OrElse` выбирает первую из двух транзакций, которая не ждёт, и как
ожидание `OrElse` охватывает переменные обеих ветвей.
