# Обзор на примерах: ObxEvent2

Пример показывает вызов со сроком и отмену проигравшей стороны, как в Concurrent ML (Reppy) и Hopac. Клиент оборачивает ожидание ответа в `WithNack`; запрос несёт событие-уведомление серверу. Клиент выбирает между ответом и сроком. Сервер выбирает между своей работой и уведомлением. Если побеждает срок, клиент зажигает уведомление, и сервер отменяет работу вместо ответа.

Исходный текст модуля `ObxEvent2` (`Obx/Mod64/Event2.cp`):

```
MODULE ObxEvent2;
(** Сервер и отмена клиента через WithNack.

	Идея из Concurrent ML (Reppy) и Hopac: клиент ждёт ответ не дольше
	срока. Ожидание ответа обёрнуто в WithNack; если выбор выиграл
	тайм-аут, клиент получает событие-уведомление и зажигает его. Сервер
	ждёт ответ своей работы и это уведомление одним выбором: уведомление
	отменяет работу. *)

	IMPORT Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console, Strings;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Request = POINTER TO RECORD (AsyncSelect.Msg)
			delay: LONGINT;			(* сколько сервер работает *)
			reply: AsyncChan.Mailbox;
			nack: AsyncEvent.Event	(* уведомление «клиент ушёл» *)
		END;

		Server = POINTER TO RECORD (Tasks.Task)
			ch: AsyncChan.Chan; n: INTEGER;
			replies, cancelled: INTEGER;
			done: BOOLEAN
		END;

		Client = POINTER TO RECORD (Tasks.Job)
			ch: AsyncChan.Chan; fast, slow, done: BOOLEAN
		END;

		ReplyAction = POINTER TO RECORD (AsyncEvent.Action)
			s: Server; req: Request
		END;

		CancelAction = POINTER TO RECORD (AsyncEvent.Action)
			s: Server
		END;

		TimeoutAction = POINTER TO RECORD (AsyncEvent.Action) END;

	VAR
		(** Итог последнего прогона. *)
		Fast*, Slow*: BOOLEAN; Cancelled*: INTEGER;

	PROCEDURE Num (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END Num;

	PROCEDURE (a: ReplyAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		a.req.reply.Send(Num(0)); INC(a.s.replies); y := NIL
	END Do;

	PROCEDURE (a: CancelAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		INC(a.s.cancelled); y := NIL
	END Do;

	PROCEDURE (a: TimeoutAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		y := Num(1)
	END Do;

	PROCEDURE (s: Server) Run;
		VAR req: Request; m: AsyncSelect.Msg; ra: ReplyAction; ca: CancelAction;
			evs: ARRAY 2 OF AsyncEvent.Event; i: INTEGER;
	BEGIN
		FOR i := 1 TO s.n DO
			s.ch.Recv(m);
			WITH m: Request DO
				req := m;
				NEW(ra); ra.s := s; ra.req := req;
				NEW(ca); ca.s := s;
				evs[0] := AsyncEvent.Wrap(AsyncEvent.Timeout(req.delay), ra);
				evs[1] := AsyncEvent.Wrap(req.nack, ca);
				IF AsyncEvent.Sync(AsyncEvent.Choose(evs)) = NIL THEN END
			END
		END;
		s.done := TRUE
	END Run;

	PROCEDURE Call (ch: AsyncChan.Chan; delay, deadline: LONGINT): BOOLEAN;
		(* TRUE — ответ пришёл раньше срока; FALSE — срок, nack зажжён. *)
		VAR reply: AsyncChan.Mailbox; nack: AsyncEvent.Event; req: Request;
			ev: AsyncEvent.Event; evs: ARRAY 2 OF AsyncEvent.Event;
			m: AsyncSelect.Msg; ta: TimeoutAction;
	BEGIN
		reply := AsyncChan.NewMailbox();
		ev := AsyncEvent.WithNack(AsyncEvent.Recv(reply), nack);
		NEW(req); req.delay := delay; req.reply := reply; req.nack := nack;
		ch.Send(req);	(* встреча: сервер взял запрос *)
		NEW(ta);
		evs[0] := ev;
		evs[1] := AsyncEvent.Wrap(AsyncEvent.Timeout(deadline), ta);
		m := AsyncEvent.Sync(AsyncEvent.Choose(evs));
		IF m = NIL THEN RETURN FALSE END;
		RETURN m(NumMsg).n = 0
	END Call;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		c.fast := Call(c.ch, 10, 500);
		c.slow := Call(c.ch, 500, 30);
		c.done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR ch: AsyncChan.Chan; s: Server; c: Client; buf: ARRAY 16 OF CHAR;
	BEGIN
		ch := AsyncChan.NewChan();
		NEW(s); s.ch := ch; s.n := 2; s.replies := 0; s.cancelled := 0;
		s.done := FALSE;
		NEW(c); c.ch := ch; c.fast := FALSE; c.slow := FALSE; c.done := FALSE;
		Tasks.Start(s); Tasks.Start(c);
		WHILE ~(c.done & s.done) DO Services.actionHook.Step END;
		Fast := c.fast; Slow := c.slow; Cancelled := s.cancelled;
		Console.WriteStr("ObxEvent2: fast=");
		IF Fast THEN Console.WriteStr("reply") ELSE Console.WriteStr("timeout") END;
		Console.WriteStr(" slow=");
		IF Slow THEN Console.WriteStr("reply") ELSE Console.WriteStr("timeout") END;
		Console.WriteStr(" cancelled=");
		Strings.IntToString(Cancelled, buf);
		Console.WriteStr(buf); Console.WriteLn
	END Do;

END ObxEvent2.
```

Команда (коммандер в оконном BlackBox, строка ввода в консоли):

```
   ObxEvent2.Do
```

```sh
echo 'ObxEvent2.Do' | tools/run64.sh
```

В журнале появится:

```
ObxEvent2: fast=reply slow=timeout cancelled=1
```

## Что произошло

1. Вызов создаёт почтовый ящик ответа, затем `WithNack(Recv(reply))`, который даёт событие приёма и событие-уведомление, и посылает запрос с обоими в канал запросов.
2. Сервер принимает запрос и предлагает выбор: `Timeout(work)` с действием-ответом или уведомление запроса с действием, считающим отмену.
3. В быстром вызове работа занимает 10 мс, срок клиента — 500 мс. Побеждает тайм-аут сервера, ответ ложится в ящик, приём клиента побеждает. Уведомление не зажигается: выбрана ветвь приёма.
4. В медленном вызове работа заняла бы 500 мс, срок клиента — 30 мс. Побеждает ветвь тайм-аута клиента; ветвь приёма проигрывает, и её уведомление зажигается.
5. Сервер ждёт то же уведомление как событие. Уведомление побеждает в его выборе, действие отмены исполняется, ответ не посылается. В журнале `cancelled=1`.
6. Клиент и сервер заканчивают работу; поздняя работа не исполняется. Пример ждёт конца всех задач, поэтому отмены спящей задачи в журнале нет.

В этом примере мы увидели, как `WithNack` уведомляет партнёра проигравшей стороны, как уведомление становится обычной альтернативой сервера и как один выбор охватывает клиента и сервер.

Подробнее — в [руководстве Async](../../Docu/ru/Async.md), раздел 5.1.
