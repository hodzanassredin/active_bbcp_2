# Обзор на примерах: ObxBufCtx

Ограниченный буфер на мониторе-объекте: две операции, флаги и условие.

Два производителя кладут предметы в буфер на четыре места, два потребителя их забирают. Один
производитель кладёт по два предмета, ожидая флага «есть место»; другой кладёт те же два предмета
разом, ожидая условия «места хватает на оба». Пример показывает, как задача описывает операцию над
общим объектом, как эта операция попадает в `Exclusive` монитора-объекта, что возвращает `Await` и
чем ожидание по флагам отличается от `AwaitCond`. Пример относится к руководству `Docu/ru/Tasks.md`;
устройство пропуска и монитора подробно разобрано в `docs/monitor-object.md`.

```
MODULE ObxBufCtx;
(** Ограниченный буфер на мониторе-объекте Tasks.Object.

	Операции Put и Get — записи-наследники Tasks.Op на стеке вызывающего.
	Заполненность и пустота — флаги Ready: ждущий говорит, какой флаг ему
	нужен, и просыпается только когда этот флаг появился. Второй способ —
	положить k предметов разом: условие-объект Room проверяет, что в
	буфере есть место для всех k. Первый способ — цикл по флагам, второй —
	AwaitCond. *)

	IMPORT Tasks, Console, Strings;

	CONST
		cap = 4;
		producers = 2;
		consumers = 2;
		per = 8;		(* предметов от каждого производителя *)
		k = 2;			(* сколько кладёт операция PutAll *)
		empty = 0;		(* флаг Ready: есть место *)
		full = 1;		(* флаг Ready: есть предмет *)

	TYPE
		PutOp = RECORD (Tasks.Op) id, seq: INTEGER END;
		GetOp = RECORD (Tasks.Op) item: INTEGER END;
		PutAllFlagOp = RECORD (Tasks.Op) id, seq: INTEGER END;
		PutAllCondOp = RECORD (Tasks.Op) id, seq: INTEGER; room: Room END;

		Room = POINTER TO RECORD (Tasks.Cond) k: INTEGER END;

		Buffer = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER
		END;

		ProducerFlag = POINTER TO RECORD (Tasks.Job) b: Buffer; id, n: INTEGER END;
		ProducerCond = POINTER TO RECORD (Tasks.Job) b: Buffer; id, n: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Job) b: Buffer; n: INTEGER END;

	VAR
		produced, consumed: INTEGER;
		nextSeq: ARRAY producers OF INTEGER;
		got: ARRAY producers OF INTEGER;
		broken: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE SayStr (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s); Console.WriteLn
	END SayStr;

	PROCEDURE (b: Buffer) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF b.count < cap THEN INCL(s, empty) END;
		IF b.count > 0 THEN INCL(s, full) END;
		RETURN s
	END Ready;

	PROCEDURE (r: Room) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: Buffer;
	BEGIN
		b := o(Buffer);
		RETURN b.count + r.k <= cap
	END Ready;

	PROCEDURE (b: Buffer) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR i: INTEGER; v: INTEGER;
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = cap DO
				IF ~b.Await(ctx, {empty}) THEN RETURN END
			END;
			b.ring[b.tail] := op.id * 100 + op.seq;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO
				IF ~b.Await(ctx, {full}) THEN RETURN END
			END;
			op.item := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		| op: PutAllFlagOp DO
			FOR i := 0 TO k - 1 DO
				WHILE b.count = cap DO
					IF ~b.Await(ctx, {empty}) THEN RETURN END
				END;
				b.ring[b.tail] := op.id * 100 + op.seq + i;
				b.tail := (b.tail + 1) MOD cap; INC(b.count)
			END
		| op: PutAllCondOp DO
			op.room.k := k;
			WHILE ~op.room.Ready(b) DO
				IF ~b.AwaitCond(ctx, op.room) THEN RETURN END
			END;
			FOR i := 0 TO k - 1 DO
				b.ring[b.tail] := op.id * 100 + op.seq + i;
				b.tail := (b.tail + 1) MOD cap; INC(b.count)
			END
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: ProducerFlag) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutAllFlagOp;
	BEGIN
		i := 1;
		WHILE i <= p.n DO
			op.id := p.id; op.seq := i;
			p.b.Do(ctx, op);
			INC(produced, k); INC(i, k)
		END
	END Body;

	PROCEDURE (p: ProducerCond) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutAllCondOp; room: Room;
	BEGIN
		NEW(room);
		i := 1;
		WHILE i <= p.n DO
			op.id := p.id; op.seq := i; op.room := room;
			p.b.Do(ctx, op);
			INC(produced, k); INC(i, k)
		END
	END Body;

	PROCEDURE (c: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR i, id, seq: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO c.n DO
			op.item := -1;
			c.b.Do(ctx, op);
			IF op.item < 0 THEN broken := TRUE
			ELSE
				id := op.item DIV 100; seq := op.item MOD 100;
				IF (id < 0) OR (id >= producers) OR (seq # nextSeq[id] + 1) THEN broken := TRUE END;
				INC(nextSeq[id]); INC(got[id])
			END;
			INC(consumed)
		END
	END Body;

	PROCEDURE Do*;
		VAR b: Buffer; pf: ProducerFlag; pc: ProducerCond; c: Consumer; i: INTEGER;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		produced := 0; consumed := 0; broken := FALSE;
		FOR i := 0 TO producers - 1 DO nextSeq[i] := 0; got[i] := 0 END;
		FOR i := 1 TO consumers DO
			NEW(c); c.b := b; c.n := producers * per DIV consumers;
			Tasks.Start(c)
		END;
		NEW(pf); pf.b := b; pf.id := 0; pf.n := per; Tasks.Start(pf);
		NEW(pc); pc.b := b; pc.id := 1; pc.n := per; Tasks.Start(pc);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("produced ", produced);
		Say("consumed ", consumed);
		IF broken OR (produced # consumers * (producers * per DIV consumers)) OR
				(consumed # produced) THEN
			SayStr("order broken")
		ELSE
			SayStr("order ok")
		END
	END Do;

END ObxBufCtx.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/BufCtx.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxBufCtx.Do
```

В консоли:

```sh
echo 'ObxBufCtx.Do' | tools/run64.sh
```

В журнале появится:

```
produced 16
consumed 16
order ok
```

## Что произошло

Пропуск. Задача — это последовательное исполнение процедуры на своём стеке. Когда задача зовёт
операцию общего объекта, объект должен знать, какая задача его зовёт: чтобы пустить ту же задачу
повторно, поставить её в очередь, отпустить монитор на время ожидания и заметить отмену. Эти
сведения несёт пропуск `Tasks.Ctx`. Создать пропуск, расширить или скопировать его вне `Tasks`
нельзя: это `LIMITED RECORD`, и за этим следит компилятор. `Job.Run` заводит пропуск на стеке
задачи на каждый запуск и передаёт его телу по `VAR`; тело передаёт тот же пропуск дальше в `Do`,
`Await`, `Spawn`, `JoinAll` и `SetDeadline`. Копии нет, поэтому все вызовы видят одно состояние:
отменён ли пропуск, истёк ли срок, какие мониторы задача держит.

Операция. Операция — запись-наследник `Tasks.Op`, объявленная на стеке вызывающего: здесь это
`PutAllFlagOp`, `GetOp` и `PutAllCondOp`. Вызывающий заполняет поля и зовёт `p.b.Do(ctx, op)`. `Do`
берёт монитор (или ставит задачу в очередь входа) и зовёт `o.Exclusive(ctx, op)`. `Exclusive`
разбирает вид записи через `WITH op: PutAllFlagOp DO … | op: GetOp DO … END`. Поля записи несут
данные в обе стороны: потребитель кладёт `op.item := -1`, после `Do` читает предмет. Памяти на
операцию не выделяется: запись лежит на стеке. Ветка `ELSE HALT(20)` ловит чужой вид записи, который
объект исполнять не умеет.

Ожидание. `Await(ctx, {empty})` вызывают только из `Exclusive`, под монитором на глубине 1. Если
нужный флаг уже есть в `Ready()`, `Await` возвращает `TRUE` сразу. Иначе отпускает монитор, усыпляет
задачу, после пробуждения снова берёт монитор и повторяет проверку. `TRUE` — условие выполнено,
монитор у задачи; `FALSE` — пропуск остановлен отменой или сроком. На `FALSE` автор объекта выходит
из `Exclusive` через `RETURN`, и вызывающий видит обычное `Do`, которое ничего не сделало.

Почему у вызывающего нет цикла `WHILE`. Тело производителя зовёт `Do` на каждый предмет и не
проверяет заполненность буфера. Цикл `WHILE b.count = cap` стоит внутри `Exclusive`, у автора
объекта, и написан один раз для всех вызывающих. Так и задумано: вызывающий формулирует операцию, а
ожидание и повторную проверку пишет владелец состояния. В примере `ObxBank2`
(`Obx/Docu/ru/Bank2.md`) наивная версия, наоборот, опрашивает остаток в теле покупателя — и теряет
неделимость.

Флаги и условие. `Ready()` возвращает множество битов; ждущий называет те, которые ему нужны:
`{empty}` или `{full}`. Монитор будит первого, у кого `need * Ready() # {}`. Этого достаточно, когда
условие — один бит: «есть место», «есть предмет». `PutAllCondOp` кладёт два предмета, и ему нужно
«места хватает на оба». Это уже предикат с параметром `k`, а не бит состояния; его считает
условие-объект `Room.Ready(o)`. `AwaitCond(ctx, op.room)` отпускает монитор, спит и после
пробуждения заново спрашивает `op.room.Ready(b)`. Флаги отвечают на вопрос одним битом; `Cond` —
общий случай: условие смотрит на всё состояние объекта и на поля ждущего.

Отмена и срок. `Tasks.Cancel(t)` и `SetDeadline(ctx, ms)` не трапуют в `Await`: он возвращает
`FALSE`, и `Exclusive` выходит. `Do` на остановленном пропуске не исполняет операцию вовсе. В этом
примере проверка стоит до изменения буфера, поэтому остановка не оставляет половины операции. Если
задача в этот момент спит в `Tasks.Sleep`, отмена прерывает сон трапом 127 и печатает
`Tasks: cancelled`; так ведут себя примеры `ObxDownCtx` и `ObxDownChan`.

Повторный запуск. Завершённую задачу `Tasks.Job` можно запустить снова: `Job.Run` заводит новый
пропуск и зовёт `Body` заново. Стек возвращается в пул и берётся оттуда; после прогрева запуск не
выделяет памяти. Это показывает `ObxRestart`.

В этом прогоне `Do` создаёт буфер, двух потребителей, производителя на флагах и производителя на
условии. Потребители ждут `{full}`; производитель 0 кладёт по два предмета, ожидая `{empty}`,
производитель 1 — по два, ожидая `Room.Ready`. Каждый производитель делает четыре шага и кладёт
восемь предметов; два потребителя забирают по восемь. Отсюда `produced 16` и `consumed 16`.
Потребитель сверяет номер производителя и ожидает следующую последовательность номеров; проверка
проходит, отсюда `order ok`.

В этом примере мы увидели, зачем задаче пропуск, как запись-операция попадает в `Exclusive`, что
возвращает `Await`, почему вызывающий не пишет цикл ожидания и чем `AwaitCond` отличается от флагов.
