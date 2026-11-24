# Обзор на примерах: ObxBank6

Кассир ждёт заявку, сигнал закрытия или тишину; ответ — однократная переменная.

Кассир ждёт сразу трёх событий выбором `AsyncChan.Select`: заявки в окошке, сигнала закрытия и
50 мс тишины. Клиент получает ответ через однократную переменную `AsyncOnce.Once` — обещание
результата. Пример относится к главе 6 учебника `Docu/ru/Tut-Active-6.md`.

```
MODULE ObxBank6;
	IMPORT AsyncChan, AsyncSelect, AsyncOnce, AsyncIO, Services, Tasks, Console, Strings;

	TYPE
		Request = POINTER TO RECORD (AsyncSelect.Msg)
			sum: INTEGER;
			answer: AsyncOnce.Once	(* обещание: остаток после снятия или -1 *)
		END;
		Cashier = POINTER TO RECORD (Tasks.Task) END;
		Client = POINTER TO RECORD (Tasks.Task) id, delay, sum: INTEGER END;
		Manager = POINTER TO RECORD (Tasks.Task) END;

	VAR window, closing: AsyncChan.Mailbox; done: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (c: Cashier) Run;
		VAR balance: INTEGER; ops: ARRAY 3 OF AsyncSelect.Op; r: AsyncSelect.Msg;
	BEGIN
		balance := 100;
		LOOP
			AsyncChan.RecvAlt(window, ops[0]);	(* заявка *)
			AsyncChan.RecvAlt(closing, ops[1]);	(* касса закрывается *)
			AsyncChan.TimeoutAlt(50, ops[2]);	(* 50 мс никого *)
			CASE AsyncChan.Select(ops) OF
			| 0:
				r := ops[0].got;
				WITH r: Request DO
					IF balance >= r.sum THEN
						balance := balance - r.sum; r.answer.FillInt(balance)
					ELSE
						r.answer.FillInt(-1)
					END
				END
			| 1: EXIT
			| 2: Console.WriteStr("cashier: nobody for 50 ms"); Console.WriteLn
			END
		END;
		Say("cashier: closed, balance ", balance);
		done := TRUE
	END Run;

	PROCEDURE (c: Client) Run;
		VAR r: Request; rest: INTEGER;
	BEGIN
		Tasks.Sleep(c.delay);
		NEW(r); r.sum := c.sum; r.answer := AsyncOnce.New();
		window.Send(r);
		rest := r.answer.ReadInt();	(* ждать исполнения обещания *)
		IF rest >= 0 THEN Say("client got money, left ", rest) ELSE Say("client refused, id ", c.id) END
	END Run;

	PROCEDURE (m: Manager) Run;
		VAR stop: Request;
	BEGIN
		Tasks.Sleep(110); NEW(stop); closing.Send(stop)
	END Run;

	PROCEDURE Do*;
		VAR k: Cashier; c1, c2: Client; m: Manager;
	BEGIN
		window := AsyncChan.NewMailbox(); closing := AsyncChan.NewMailbox(); done := FALSE;
		NEW(k); Tasks.Start(k);
		NEW(c1); c1.id := 1; c1.delay := 0; c1.sum := 70; Tasks.Start(c1);
		NEW(c2); c2.id := 2; c2.delay := 80; c2.sum := 70; Tasks.Start(c2);
		NEW(m); Tasks.Start(m);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END
	END Do;

END ObxBank6.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Bank6.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxBank6.Do
```

В консоли:

```sh
echo 'ObxBank6.Do' | tools/run64.sh
```

В журнале появится:

```
client got money, left 30
cashier: nobody for 50 ms
client refused, id 2
cashier: closed, balance 30
```

## Что произошло

Кассир заполняет три альтернативы: `RecvAlt(window)`, `RecvAlt(closing)` и `TimeoutAlt(50)`, и
зовёт `Select`. Первый клиент приходит сразу; `Select` возвращает 0, кассир снимает 70 и выполняет
обещание `r.answer.FillInt(30)`. Клиент, который ждал в `ReadInt`, просыпается.

Следующие 50 мс никого нет, `Select` возвращает 2. Через 80 мс приходит второй клиент, денег не
хватает, и обещание заполняется числом −1. Через 110 мс управляющий кладёт сообщение в `closing`,
`Select` возвращает 1, кассир выходит из цикла.

Тайм-аут выбора исполняет действие `Services`, поэтому команда крутит главный цикл
`Services.actionHook.Step` и между проходами ждёт ввода-вывода `AsyncIO.Poll(1)`.

В этом примере мы увидели, как одна задача ждёт нескольких событий сразу и узнаёт, какое
случилось, и как однократная переменная передаёт один результат.
