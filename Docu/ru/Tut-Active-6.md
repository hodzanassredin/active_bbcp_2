[← Глава 5](Tut-Active-5.md) · [Оглавление](Tut-Active-TOC.md) · [Глава 7 →](Tut-Active-7.md)

# 6. Выбор

## 6.1. Ждать сразу нескольких вещей

Кассир из главы 5 умеет только одно: ждать заявку в окошке. В жизни ему надо следить за тремя
вещами сразу. Пришла заявка — исполнить. Управляющий сказал «закрываемся» — закрыть кассу. Никого
нет 50 мс — записать в журнал, что касса простаивает. Что из этого случится первым, заранее
неизвестно.

`window.Recv(m)` ждёт только окошко. Если кассир уснёт в нём, сигнал о закрытии он не услышит, пока
не придёт следующая заявка, а её может не быть вовсе. Можно опрашивать по очереди: заглянуть в
окошко, не ждя, заглянуть в ящик управляющего, поспать миллисекунду, повторить. Это плохо дважды.
Кассир тратит процессор на пустые проверки, и он опаздывает: событие, случившееся сразу после
проверки, он заметит только через круг.

Нужна операция «жди, пока случится хоть одно из этих событий, и скажи, какое». Она называется
*выбором*. Каждый вариант описывают заранее в записи `AsyncSelect.Op` — её называют
*альтернативой* (отсюда `Alt` в именах процедур):

- `AsyncChan.RecvAlt(c, op)` — «получить сообщение из канала `c`»;
- `AsyncChan.SendAlt(c, m, op)` — «отправить `m` в канал `c`»;
- `AsyncChan.TimeoutAlt(ms, op)` — «прошло `ms` миллисекунд».

Затем `AsyncChan.Select(ops)` ждёт и возвращает номер альтернативы, которая сработала. Сработала
ровно одна: если выбрано получение, сообщение лежит в `ops[i].got`, а остальные альтернативы не
сделали ничего — сообщение из другого канала не потеряно, отправка в другой канал не состоялась.

## 6.2. Обещание результата

Клиент главы 5 приносил свою встречу для ответа. Ответ нужен ровно один раз, и для этого есть
вещь проще канала — *однократная переменная* `AsyncOnce.Once`. Её заполняют один раз (`FillInt`,
`FillMsg`); читать (`ReadInt`, `ReadMsg`) может кто угодно и сколько угодно раз, а кто читает
раньше заполнения — ждёт. Это обещание: «результат будет здесь». Клиент кладёт пустую однократную
переменную в заявку, кассир выполняет обещание, клиент забирает результат. Повторное заполнение
трапует: обещание выполняют один раз.

## 6.3. Пример

Кассир ждёт заявку, сигнал закрытия или 50 мс тишины. Первый клиент приходит сразу, второй — через
80 мс, управляющий закрывает кассу через 110 мс. На счёте 100, оба клиента хотят по 70.

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

Команда печатает:

```
client got money, left 30
cashier: nobody for 50 ms
client refused, id 2
cashier: closed, balance 30
```

Первый `Select` возвращает 0 — заявку первого клиента. Следующий выбор ждёт 50 мс и возвращает 2:
никого. Через 80 мс от начала приходит второй клиент, ему не хватает денег. На 110-й мс управляющий
кладёт сообщение в `closing`, `Select` возвращает 1, и кассир выходит из цикла. Тайм-аут
перезапускается при каждом вызове `Select`: это «50 мс с последнего события», а не «каждые 50 мс».

Тайм-аут выбора — действие `Services`, а не задача, и `Tasks.Step` его не исполняет. Поэтому
команда `Do` крутит не `Tasks.Step`, а весь главный цикл (`Services.actionHook.Step`), и между
проходами зовёт `AsyncIO.Poll(1)` — ждёт событий ввода-вывода не дольше миллисекунды, чтобы не
крутиться вхолостую. Кассир, который ждёт тайм-аут, ждёт события извне задач, и планировщик не
считает такое положение тупиком. В окне BlackBox главный цикл делает всё это сам.

## 6.4. Где ещё нужен выбор

Выбор встречается всякий раз, когда задача служит нескольким сторонам. Сервер ждёт новое
соединение или команду остановки. Клиент ждёт ответ или тайм-аут, после которого спросит другой
сервер (`ObxSelect0`). Задача-посредник одновременно ждёт сообщение от производителя и свободное
место у потребителя — выбор из получения и отправки. Результат тяжёлого расчёта в другом процессе
тоже можно ждать в выборе рядом с каналами: для этого `AsyncComputon.AltPull` (глава 7).

Своё событие тоже можно сделать альтернативой — расширить `AsyncSelect.Signal` и подать его в
`AsyncSelect.AltSignal`. Это нужно редко; устройство — раздел 5 руководства `Docu/ru/Async.md`.

## 6.5. Нарушения и цена

Порядок альтернатив важен, когда готовы сразу несколько. `Select` проверяет их по возрастанию
номера, и побеждает первая, которая может исполниться немедленно; тайм-аут готов, только когда не
готово ничего другое. Поэтому кассир ставит окошко на место 0, а закрытие — на место 1: если в
окошке лежит заявка и одновременно пришёл сигнал, сначала будет обслужена заявка. Если поставить
закрытие первым, заявки, пришедшие одновременно с ним, останутся в окошке необслуженными. Больше 32
альтернатив в одном выборе нельзя (трап 21), выбор вне задачи трапует 20; остальные номера — раздел 5
руководства `Docu/ru/Async.md`.

Выбор из двух встреч стоит 315–400 нс, из восьми — 568–653 нс (Linux x86-64, одна нить, под
нагрузкой; `docs/measurements.md`, раздел 2.2): дороже одного `Recv`, но дешевле любого опроса.

Этой главой кончается часть I. Всё, о чём в ней шла речь, работает на одной нити: задачи делят её
между собой и никогда не исполняются одновременно. Часть II о том, что делать, когда одной нити
мало.
