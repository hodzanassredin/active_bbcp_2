# Обзор на примерах: ObxEvent3

Пример показывает мультикаст, как в Concurrent ML (Reppy). Процесс-вещатель читает вход и выбирает, кому отдать значение первым; действие после выбора отдаёт его второму получателю. Если один получатель отстаёт, вещатель ждёт его внутри действия, поэтому значение не теряется.

Исходный текст модуля `ObxEvent3` (`Obx/Mod64/Event3.cp`):

```
MODULE ObxEvent3;
(** Мультикаст: одно значение — двум получателям.

	Идея из Concurrent ML (Reppy): процесс читает вход и выбирает, кому
	отдать значение первым; действие Wrap после выбора отдаёт его второму
	получателю. Если один получатель отстаёт, отправитель ждёт его в
	действии — значение не теряется. *)

	IMPORT Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console, Strings;

	CONST
		total = 5;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		SendOther = POINTER TO RECORD (AsyncEvent.Action)
			c: AsyncChan.Chan;
			m: AsyncSelect.Msg
		END;

		Producer = POINTER TO RECORD (Tasks.Task)
			c: AsyncChan.Chan; done: BOOLEAN
		END;

		Consumer = POINTER TO RECORD (Tasks.Task)
			c: AsyncChan.Chan; sum, count: INTEGER; done: BOOLEAN
		END;

		Broadcaster = POINTER TO RECORD (Tasks.Task)
			inCh, a, b: AsyncChan.Chan; n: INTEGER; done: BOOLEAN
		END;

	VAR
		(** Итоги последнего вещания. *)
		SumA*, SumB*, CountA*, CountB*: INTEGER;

	PROCEDURE Num (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END Num;

	PROCEDURE (a: SendOther) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		a.c.Send(a.m);	(* встреча со вторым получателем *)
		y := m
	END Do;

	PROCEDURE (p: Producer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO total DO p.c.Send(Num(i)) END;
		p.done := TRUE
	END Run;

	PROCEDURE (c: Consumer) Run;
		VAR i: INTEGER; m: AsyncSelect.Msg;
	BEGIN
		c.sum := 0; c.count := 0;
		FOR i := 1 TO total DO
			c.c.Recv(m);
			WITH m: NumMsg DO c.sum := c.sum + m.n END;
			INC(c.count)
		END;
		c.done := TRUE
	END Run;

	PROCEDURE (b: Broadcaster) Run;
		VAR i: INTEGER; m: AsyncSelect.Msg;
			s1, s2: SendOther; evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		FOR i := 1 TO b.n DO
			b.inCh.Recv(m);
			NEW(s1); s1.c := b.b; s1.m := m;
			NEW(s2); s2.c := b.a; s2.m := m;
			evs[0] := AsyncEvent.Wrap(AsyncEvent.Send(b.a, m), s1);
			evs[1] := AsyncEvent.Wrap(AsyncEvent.Send(b.b, m), s2);
			IF AsyncEvent.Sync(AsyncEvent.Choose(evs)) = NIL THEN END
		END;
		b.done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR inCh, a, b: AsyncChan.Chan; p: Producer; ca, cb: Consumer;
			br: Broadcaster; buf: ARRAY 16 OF CHAR;
	BEGIN
		inCh := AsyncChan.NewChan(); a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(p); p.c := inCh; p.done := FALSE;
		NEW(ca); ca.c := a; ca.done := FALSE;
		NEW(cb); cb.c := b; cb.done := FALSE;
		NEW(br); br.inCh := inCh; br.a := a; br.b := b; br.n := total; br.done := FALSE;
		Tasks.Start(ca); Tasks.Start(cb); Tasks.Start(br); Tasks.Start(p);
		WHILE ~(p.done & ca.done & cb.done & br.done) DO Services.actionHook.Step END;
		SumA := ca.sum; SumB := cb.sum; CountA := ca.count; CountB := cb.count;
		Console.WriteStr("ObxEvent3: a=");
		Strings.IntToString(SumA, buf); Console.WriteStr(buf);
		Console.WriteStr(" b=");
		Strings.IntToString(SumB, buf); Console.WriteStr(buf);
		Console.WriteStr(" each=");
		Strings.IntToString(CountA, buf); Console.WriteStr(buf);
		Console.WriteStr("/");
		Strings.IntToString(CountB, buf); Console.WriteStr(buf);
		Console.WriteLn
	END Do;

END ObxEvent3.
```

Команда (коммандер в оконном BlackBox, строка ввода в консоли):

```
   ObxEvent3.Do
```

```sh
echo 'ObxEvent3.Do' | tools/run64.sh
```

В журнале появится:

```
ObxEvent3: a=15 b=15 each=5/5
```

## Что произошло

1. Два потребителя запускаются и ждут значения в своих каналах.
2. Производитель посылает 1, 2, 3, 4, 5 во входной канал; вещатель принимает значения по одному.
3. Для каждого значения вещатель строит выбор из двух отправок: первому потребителю и второму. Обе альтернативы обёрнуты действием, которое посылает то же значение другому потребителю.
4. Побеждает та отправка, которая готова первой; действие исполняет вторую. Победившая отправка — встреча, а вторая отправка внутри действия — точка переключения: вещатель ждёт, пока отстающий потребитель возьмёт значение.
5. Каждый потребитель получает все пять значений по порядку. Обе суммы — 15, и каждый получил пять значений.

В этом примере мы увидели, как выбор решает, кому доставить значение первым, а действие доводит вторую доставку, не теряя значение при медленном получателе.

Подробнее — в [руководстве Async](../../../Docu/ru/Async.md), раздел 5.1.
