# Библиотека Async

Руководство по необязательной подсистеме `Async` для программиста на Component Pascal. Она даёт
каналы, однократную переменную, выбор из нескольких ожиданий, протокол диалога, ввод-вывод без
блокировки, пул рабочих нитей и компутоны. Языковая часть — исключающие процедуры,
`ACTIVE.AWAIT` и точки уступки — определена в `Docu/ru/Active-CP-Lang.md`; задачи — в
`Docu/ru/Tasks.md`.

Библиотека написана на расширении языка и не меняет каркас: модули каркаса компилируются без
ключа уступок и не ждут. Подсистема собирается только для AMD64; ядро расширения работает и на
i386. Экспорт каждого модуля с пред- и постусловиями перечислен в разделе 10.

## 1. Сообщение

*Сообщение* — объект типа `AsyncSelect.Msg`:

```
Msg* = POINTER TO ABSTRACT RECORD END;
```

Прикладной тип расширяет эту запись:

```
TYPE NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
```

Правила:

1. Канал передаёт указатель, а не копию значения.
2. После отправки сообщение не меняется. Это соглашение, а не проверка: библиотека не копирует
   объект и не запрещает запись.
3. Получатель различает вид сообщения через `WITH`; поле `n` доступно только после проверки типа.

```
VAR m: AsyncSelect.Msg;
...
WITH m: NumMsg DO total := total + m.n END
```

## 2. Однократная переменная

*Однократная переменная* `AsyncOnce.Once` хранит одно значение и выдаёт его после заполнения.

Операции:

```
o := AsyncOnce.New();
o.FillInt(x);            (* заполнить целым *)
x := o.ReadInt();        (* ждать и прочитать целое *)
o.FillMsg(m);            (* заполнить сообщением *)
o.ReadMsg(m);            (* ждать и прочитать сообщение *)
```

Правила:

1. Заполнение выполняется ровно один раз. Повторный `FillInt` трапает с кодом 21, повторный
   `FillMsg` — с кодом 22.
2. У переменной один признак `done` на оба вида значения. Поэтому `FillInt` после `FillMsg` и
   наоборот — тоже повторное заполнение и трап. Вид заполнения и вид чтения должны совпадать.
3. `ReadInt` и `ReadMsg` ждут заполнения; тайм-аута нет. Чтение до заполнения приостанавливает
   задачу в `ACTIVE.AWAIT(done)`.
4. Проверки вида при чтении нет: `ReadInt` после `FillMsg` вернёт 0, `ReadMsg` после `FillInt` —
   `NIL`.
5. Объект — монитор: методы `[exclusive]`. `Fill` не ждёт и может исполняться вне задачи, если
   монитор свободен; `Read` ждёт, поэтому вызывается только из задачи.

```
MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN; s: ARRAY 16 OF CHAR;

	TYPE Reader = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (r: Reader) Run;
	BEGIN got := o.ReadInt(); done := TRUE END Run;

	PROCEDURE Run*;
		VAR r: Reader;
	BEGIN
		o := AsyncOnce.New();
		o.FillInt(42);		(* вне задачи: монитор свободен, Fill не ждёт *)
		NEW(r); done := FALSE; Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(got, s);
		Console.WriteStr("ObxOnce: "); Console.WriteStr(s); Console.WriteLn
	END Run;

END ObxOnce.
```

`Fill` исполняется вне задачи, `ReadInt` — в задаче: чтение ждёт заполнения, а заполнение уже
произошло.

Ограничения: одно значение на объект; вида значения нет; ожидание бесконечно; повторное
заполнение — трап.

## 3. Каналы

*Канал* — объект с методами `Send` и `Recv`. Библиотека даёт три вида:

| Вид | Конструктор | Ёмкость | `Send` ждёт |
|---|---|---|---|
| встреча, `Chan` | `AsyncChan.NewChan()` | 0 | пока получатель не заберёт |
| ограниченный буфер, `Buffer` | `AsyncChan.NewBuffer(capacity)` | `capacity` | пока есть место |
| почтовый ящик, `Mailbox` | `AsyncChan.NewMailbox()` | не ограничена | никогда |

Все три вида — расширения одной записи `AsyncSelect.Core`; различие задают поля `kind` и
`capacity`. Канал — объект с монитором: если монитор держит задача (`Kernel.Lock`), `Send` и
`Recv` ждут его освобождения. Ожидание отправителя или получателя монитора не занимает.

### 3.1. Встреча

`Chan` не хранит сообщений: это ограниченный канал ёмкости 0. `Send` без ждущего получателя
встаёт в очередь отправителей и ждёт, пока получатель заберёт сообщение; `Recv` без ждущего
отправителя встаёт в очередь получателей. Отправка завершается только после приёма; отправители
обслуживаются в порядке прихода.

```
c := AsyncChan.NewChan();
c.Send(m);        (* вернётся после Recv *)
c.Recv(m);        (* ждёт отправителя *)
```

Пришедшая второй сторона исполняет предложение первой и будит её. Отправка и приём одного
`Select` на одном канале друг друга не исполняют.

### 3.2. Ограниченный буфер

`Buffer` хранит до `capacity` сообщений в порядке отправки. `Recv` на пустом буфере ждёт;
`Send` в полном ждёт. Ёмкость должна быть положительной: `NewBuffer(0)` трапает с кодом 20.
Верхней границы ёмкости нет.

### 3.3. Почтовый ящик

`Mailbox` не ограничен: `Send` не ждёт никогда, `Recv` ждёт, пока ящик пуст. Порядок сообщений —
порядок отправки.

### 3.4. Общие правила

1. Сообщение — указатель `AsyncSelect.Msg`; после отправки оно не меняется.
2. Прямые `Send` и `Recv` обслуживают и предложения `Select`: отправка в буфер завершает
   отложенный приём, приём из буфера освобождает место для отложенной отправки. Поэтому канал
   можно смешивать с выбором.
3. `Recv` всех видов и `Send` встречи и буфера — точки переключения. `Send` ящика ждёт, только
   если монитор канала держит другая задача. Отменённая задача трапает в ожидании с кодом 127.
4. У канала может быть необязательный протокол диалога (раздел 5).

```
MODULE ObxChan;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
	     Sender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;
	     Receiver = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (s: Sender) Run;
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO NEW(m); m.n := i; s.c.Send(m) END;
		s.c.Send(NIL)
	END Run;

	PROCEDURE (r: Receiver) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			r.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO r.sum := r.sum + m.n END
		END;
		done := TRUE
	END Run;

	PROCEDURE Run*;
		VAR c: AsyncChan.Chan; s: Sender; r: Receiver; d: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewChan(); NEW(s); s.c := c; NEW(r); r.c := c; done := FALSE;
		Tasks.Start(s); Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(r.sum, d);
		Console.WriteStr("ObxChan: sum="); Console.WriteStr(d); Console.WriteLn
	END Run;

END ObxChan.
```

Ограничения: встреча не буферизует; буфер требует положительной ёмкости; ящик не ограничен;
сообщение не копируется; канал живёт в одной нити ОС.

## 4. Выбор

*Альтернатива* — описание одной операции, которая может быть выполнена, или ожидания сигнала.
*Выбор* (`Select`) ждёт, пока выполнится ровно одна из альтернатив, и возвращает её индекс.

Тип альтернативы — `AsyncSelect.Op`. Альтернативы строят:

```
AsyncChan.RecvAlt(c, op);          (* принять из канала *)
AsyncChan.SendAlt(c, m, op);       (* отправить в канал *)
AsyncChan.TimeoutAlt(ms, op);      (* истечение срока *)
AsyncSelect.AltSignal(sig, op);    (* внешний сигнал *)
```

`AsyncChan` оборачивает первые три; `Select` — та же операция:

```
i := AsyncChan.Select(ops);        (* индекс сработавшей альтернативы *)
m := ops[i].got;                   (* результат приёма *)
```

Правила:

1. `Select` вызывается только из задачи; иначе трап 20.
2. Число альтернатив — не более 32; иначе трап 21. Каждая альтернатива заполнена
   `AltSend`/`AltRecv`/`AltTimeout`/`AltSignal` (иначе трап 22); у отправки и приёма есть канал
   (иначе трап 23), у сигнала — сигнал (иначе трап 24). Предусловия проверяются до регистрации.
   Трап 22 возникает, только если вид альтернативы вне допустимого диапазона. Незаполненная
   альтернатива с нулевым видом считается отправкой и даёт трап 23.
3. Альтернативы проверяются в порядке индексов. Побеждает первая, которая может выполниться
   немедленно.
4. Из нескольких тайм-аутов учитывается наименьший `ms`; при равенстве — первый по индексу.
   Тайм-аут с `ms <= 0` срабатывает сразу, если ни одна альтернатива не готова.
5. Внешний сигнал, готовый при регистрации (`Arm` вернул `TRUE`), побеждает сразу. Иначе
   владелец сигнала позже зовёт `SignalReady`.
6. После пробуждения задача снимает проигравшие предложения. Предложение не исполняется против
   предложения того же `Select`. Поле `got` заполняется только для победившего приёма.
7. Отмена задачи во время ожидания трапает её в `Tasks.Park` с кодом 127. Предложения остаются в
   каналах и снимаются лениво: канал считает предложение мёртвым, когда задача достигла
   `Finished` или `Cancelled`.
8. Если протокол отверг отправку этой задачи, `Select` снимает все предложения и затем трапает
   с кодом 21.

```
VAR ops: ARRAY 3 OF AsyncSelect.Op;
AsyncChan.RecvAlt(a, ops[0]);
AsyncChan.RecvAlt(b, ops[1]);
AsyncChan.TimeoutAlt(100, ops[2]);
i := AsyncChan.Select(ops);
IF i = 0 THEN ... ELSIF i = 1 THEN ... ELSE ... END
```

Ограничения: не более 32 альтернатив; только из задачи; порядок при готовности нескольких
альтернатив — порядок индексов; отмена не снимает предложения немедленно.

## 5. Протокол канала

*Протокол* — детерминированный автомат состояний, построенный из текстового описания. Канал
может нести его как необязательную проверку последовательности сообщений.

Описание состоит из пунктов:

```
start: RequestMsg > wait; wait: ResponseMsg > wait;
wait: EndMsg > done; done:
```

Правила описания:

1. Пункт — `состояние: Сообщение > состояние`.
2. Стрелка записывается `>` или `->`; пункты разделяются `;` или переводом строки.
3. `состояние:` без стрелки объявляет состояние без исходящих переходов. Конечность —
   вычисляемый признак: состояние конечно, пока из него нет ни одного перехода.
4. Первое упомянутое состояние — начальное.
5. Два перехода из одного состояния по одному имени — ошибка описания.
6. Имя сообщения — имя динамического типа записи `AsyncSelect.Msg`; суффикс `^` срезается.
   `NIL` даёт метку `NIL`, неизвестный тип — `?`.

Операции `AsyncProto` и `AsyncChan.SetProtocol` перечислены в разделе 10.

Проверка:

1. Протокол проверяет сообщение в момент, когда отправка совершается. Это одинаково для прямого
   `Send` и для `SendAlt`.
2. Отправка совершается в одном из трёх мест: при входе, если получатель ждёт или есть место;
   когда получатель забирает ждущее предложение; когда в буфере освобождается место.
3. Проигравшее предложение `Select` состояние протокола не меняет.
4. Нарушение печатает в консоль `AsyncChan protocol: <текст>`. Отправитель трапает с кодом 21, а
   сообщение не доставляется. Если отправитель ждал, трап получает он, а не задача-получатель.
5. Отвергнутая отправка состояние протокола не меняет. Текст нарушения остаётся в
   `LastViolation` и перечисляет ожидаемые сообщения.
6. Без протокола проверка выходит после одной проверки `NIL`.
7. Прикрепление протокола сбрасывает состояние в начальное.

Ограничения: не более 32 состояний и 128 переходов; имя состояния и метки — до 31 символа;
текст нарушения — до 255 символов. Автомат детерминированный; операторов `*`, `+`, `?`, `|` нет.

## 6. Ввод-вывод

*Ожидание готовности* строится на реестре `Poller` и неблокирующих потоках `CommStreams`.

```
AsyncIO.WaitFd(fd, events);       (* ждать готовности дескриптора *)
AsyncIO.Read(s, buf, beg, len, read);      (* читать хотя бы байт *)
AsyncIO.Write(s, buf, beg, len, written);  (* записать хотя бы байт *)
AsyncIO.Accept(l, s, timeout);    (* принять соединение *)
AsyncIO.Poll(timeout);            (* прогнать ожидания, для тестового насоса *)
```

Правила:

1. `Read` возвращает `read > 0`, как только прочитан хотя бы байт; `read = 0` означает конец или
   обрыв. `Write` возвращает `written > 0`, как только записан хотя бы байт; `written = 0` —
   обрыв.
2. Пока данных или места нет, задача регистрирует дескриптор в `Poller` и спит в `Tasks.Park`.
   Регистрация одноразовая: после готовности запись снимается, и ожидающий регистрируется снова,
   если готовности не хватило.
3. `Accept` с `timeout < 0` ждёт бесконечно; с неотрицательным — возвращает `FALSE` по истечении
   срока. Срок проверяется между итерациями, отдельного таймера нет.
4. Если поток не имеет дескриптора (`Handle() = -1`), ожидание вырождается в `Tasks.Sleep(1)`.
   Это медленно, но не блокирует главный цикл.
5. `WaitFd` вызывается только из задачи; иначе трап 20.
6. Главный цикл (`LinConsole.WaitInput`) опрашивает stdin и зарегистрированные дескрипторы одним
   `poll`.

`CommStreams.Stream` и `CommStreams.Listener` имеют метод `Handle()`, по умолчанию
возвращающий −1. Его перекрывает только `CommTCP__Lin`: там `Handle` возвращает сокет. На
остальных платформах `AsyncIO` работает через `Sleep(1)`.

Ограничения: не более 4096 одновременных ожиданий (`LinConsole`); при переполнении ожидающий
немедленно получает `Ready` и повторяет попытку. `CommTCP__Lin` работает по IPv4, очередь
accept — 128. Реестр использует `poll`, а не `select`: `fd_set` ограничен 1024 дескрипторами.

## 7. Рабочие нити

*Рабочая нить* — нить операционной системы, исполняющая блокирующий вызов вне главного цикла.
Пул `AsyncWork` уводит такой вызов из задачи, чтобы главный цикл и остальные задачи
продолжали работать.

```
status := AsyncWork.Execute(S.ADR(SomeJob), S.VAL(S.ADDRESS, arg));  (* из задачи; ждёт *)

id := AsyncWork.Submit(S.ADR(SomeJob), S.VAL(S.ADDRESS, arg));   (* не ждёт *)
IF AsyncWork.Done(id) THEN status := AsyncWork.Result(id); AsyncWork.Release(id) END;
```

`Execute` сам освобождает слот и возвращает статус задания. Операции перечислены в разделе 10.

Готовые задания: `ReadFileJob`, `WriteFileJob`, `ResolveJob`. Записи `FileReq` и `AddrReq`
объявлены в фасаде; вызывающий выделяет их заранее, рабочая нить получает только адрес.

Правила:

1. Делегат ставится лениво при первом `Execute` или `Submit`: `AsyncWork.Ensure` пробует
   `Dialog.Call("AsyncWorkLin.Install")`, затем `AsyncWorkWin.Install`. Базовый init пул не
   заводит.
2. Пул — до четырёх нитей. На Linux готовность приходит байтом в pipe; `Poller` будит главный
   цикл, `Scan` будит задачу через `Tasks.Unpark`. На Windows готовность забирает действие
   `Services`, опрашивающее счётчик `done` каждые 5 мс, пока есть занятые слоты.
3. Рабочая нить не имеет права обращаться к куче, вызывать `NEW`, `Kernel.Try`, журнал и каркас.
   Это правило дисциплины, а не барьер.
4. `Execute` — точка переключения задачи; вне задачи он трапает с кодом 21. `Submit`, `Done`,
   `Result`, `Release` не ждут.
5. После `Submit` вызов `Release` обязателен: иначе слот занят до конца процесса.
6. На Windows пул работает, пока крутится цикл `Services`; блокирующий консольный ввод
   останавливает доставку готовности.

Коды заданий: `ReadFileJob` возвращает −30, если чтение не дало байт; `WriteFileJob` — −20 при
короткой записи; `ResolveJob` — −1 при ошибке. `Submit` и `Execute` возвращают −1, если нет
свободного слота.

Замер (Linux, рабочая нить, задание `usleep` на 300 мс): задача ждёт `Execute`, главный цикл
делает 286 шагов, тест занимает около 301 мс. Команда — `tools/test.sh Async_devWorkers`.

Ограничения: пул есть только на Linux AMD64 и Windows x64; путь — до 255 байт, буфер файла —
4096 байт, хост — до 127 байт; задания делают один системный вызов чтения или записи;
`ResolveJob` только считает адреса и не сохраняет их.

## 8. Компутоны

*Компутон* — сериализуемый объект `Stores.Store` с методом `Do`, который исполняется в отдельном
процессе BlackBox со своей кучей и сборщиком. Поэтому `Do` может вызывать `NEW`.

```
TYPE Computon* = POINTER TO ABSTRACT RECORD (Stores.Store) END;
     PROCEDURE (c: Computon) Do*, NEW, ABSTRACT;
```

Тип компутона должен реализовать `Externalize` и `Internalize`: объект передаётся через `Stores`.

### 8.1. Операции

Операции `AsyncComputon` перечислены в разделе 10. Правила:

1. `Start(n)` запускает `n` исполнителей; `n` — от 1 до 16, иначе трап 20. `Rank()` управляющего —
   0.
2. `Push` сериализует компутон и отправляет его исполнителю `rank`; ошибки видны через
   `LastError` или `Hook.Failed`.
3. `Pull` ждёт результат не дольше 10 000 мс; ожидание — `Tasks.Sleep(10)`, то есть точка
   переключения. Вне задачи `Pull` трапает с кодом 21. По тайм-ауту `Pull` возвращает `NIL`,
   `LastError = -4`.
4. `Try` не ждёт: возвращает `TRUE`, если запрос завершён, и результат или `NIL` с кодом в
   `LastError`.
5. Запрос сопоставляется **только по `tag`**; `rank` в `Pull` используется лишь для `Hook`, в
   `Try` и `AltPull` не используется. Одинаковые `tag` у одновременных запросов конфликтуют.
6. При гибели исполнителя запрос повторяется на живом. Повтор возможен после каждой следующей
   смерти; счётчика попыток нет. Семантика — «хотя бы один раз»: если исполнитель умер после
   вычисления, но до ответа, `Do` будет исполнен повторно.
7. `AltPull` даёт сигнал для `Select`: выбор может ждать результат компутона вместе с каналами
   и тайм-аутом. Сам запрос `AltPull` не потребляет; после выбора нужен `Pull` или `Try`.
8. `Unload(module)` просит исполнителей выгрузить код модуля; следующий `Push` загрузит новый
   `.ocf`. Возвращает `FALSE` и `LastError = 20`, если модуль занят. Незагруженный модуль —
   успех. По MPI выгрузка не поддерживается: `FALSE`, `LastError = -1`.
9. `Stop` убивает исполнителей (`SIGKILL`) и закрывает каналы; `waitpid` не вызывается. `Stop`
   не помечает незавершённые запросы: после остановки они остаются занятыми.

Коды ошибок: `-1` — нет запроса, пустая сериализация или нет слота; `-2` — не осталось живых
исполнителей; `-3` — отправка не удалась; `-4` — тайм-аут; `-5` — не удалось разобрать ответ;
`-6` — результат не сериализуется; `20` — модуль занят при выгрузке; `1` — трап в `Do`.

### 8.2. Транспорты

```
ok := AsyncComputon.SetTransport("pipe");   (* по умолчанию *)
ok := AsyncComputon.SetTransport("tcp");
ok := AsyncComputon.SetTransport("mpi");
```

Транспорт выбирается до `Start`; внешняя переменная `ASYNC_TRANSPORT` задаёт то же значение.

1. `pipe` — `fork` и `exec` текущего процесса (`/proc/self/exe`) с тремя каналами на
   исполнителя: команда на stdin, запросы, ответы. В консольном хосте это тот же
   `blackbox-console`; в GUI-процессе `exec` запустит другой бинарник.
2. `tcp` — управляющий слушает `127.0.0.1:0`, исполнитель подключается к адресу из
   `--ASYNC_TCP=...`. Формат кадров и повтор те же. Сокет исполнителя блокирующий, управляющего —
   неблокирующий для `Poller`.
3. `mpi` — внешний транспорт `AsyncMpi`; см. 8.4.

Кадр — 12-байтовый заголовок (длина, `tag`, вид; ответ — длина, `tag`, код) и тело. Тело ответа
по pipe и TCP ограничено так, что практический предел результата — около 59 988 байт.

### 8.3. Пример

```
MODULE ObxComputon;
	IMPORT AsyncComputon, Stores, Tasks, Console, Strings;

	TYPE Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
	     Client = POINTER TO RECORD (Tasks.Task) END;

	VAR done: BOOLEAN; d: ARRAY 16 OF CHAR;

	PROCEDURE (c: Square) Do*;
	BEGIN c.y := c.x * c.x END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN wr.WriteInt(c.x); wr.WriteInt(c.y) END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
	BEGIN rd.ReadInt(c.x); rd.ReadInt(c.y) END Internalize;

	PROCEDURE (cl: Client) Run;
		VAR c: Square; r: AsyncComputon.Computon;
	BEGIN
		NEW(c); c.x := 12; c.y := 0;
		AsyncComputon.Push(c, 0, 1);
		r := AsyncComputon.Pull(0, 1);
		WITH r: Square DO Strings.IntToString(r.y, d) END;
		done := TRUE
	END Run;

	PROCEDURE Run*;
		VAR cl: Client;
	BEGIN
		AsyncComputon.Start(1);
		NEW(cl); done := FALSE; Tasks.Start(cl);
		WHILE ~done DO Tasks.Step END;
		Console.WriteStr("ObxComputon: "); Console.WriteStr(d); Console.WriteLn;
		AsyncComputon.Stop
	END Run;

END ObxComputon.
```

`Do` исполняется в отдельном процессе, поэтому `NEW` внутри него разрешён. `Pull` — точка
переключения и вызывается из задачи; команда `Run` крутит `Tasks.Step`, пока клиент не получит
результат.

### 8.4. MPI

`AsyncMpi` ставит внешний транспорт поверх MPICH. Порядок:

```
ok := AsyncMpi.Install();                 (* загрузить libbbmpi.so, поставить транспорт *)
ok := AsyncComputon.SetTransport("mpi");
AsyncComputon.Start(n);                   (* мир из n+1 рангов *)
```

Раскладка мира: ранг 0 — управляющий BlackBox, ранги 1..n исполняют `AsyncMpi.WorkerLoop`.
Кадры те же; заголовок и тело идут MPI-тегами 1000, 1001, ответы — 2000. Мир должен быть ровно
`n+1`; иначе `Start` даёт размер 0.

Ограничения: гибель ранга не обнаруживается и не повторяется (mpiexec обычно гасит мир);
`Unload` не поддерживается; `WorkerPid` возвращает 0, `WorkerDead` — `TRUE`; повторный `Start`
после `Stop` снова вызывает `MPI_Init` после `MPI_Finalize`.

### 8.5. Фасад RobustParallel

`RobustParallel` повторяет опубликованное определение из доклада И. Денисова (2017, с. 19):

```
CONST tag* = 1;
TYPE Computon* = AsyncComputon.Computon;
     Hook* = AsyncComputon.Hook;
VAR rank-, size-: INTEGER;
PROCEDURE LogMsg* (a: ARRAY OF CHAR);
PROCEDURE Push* (store: Computon; rank, tag: INTEGER);
PROCEDURE Pull* (rank, tag: INTEGER): Computon;
PROCEDURE Try* (rank, tag: INTEGER): Computon;
PROCEDURE SetHook* (h: Hook);
PROCEDURE Wait*;
PROCEDURE Stop*;
PROCEDURE Unload* (module: ARRAY OF CHAR);
```

Правила:

1. Ранги как в MPI: 0 — управляющий, 1..size−1 — исполнители; `Push`, `Pull`, `Try` сдвигают
   ранг на −1.
2. Число исполнителей задаёт внешняя переменная `ROBUST_SIZE`: `n > 1` запускает `n−1` локальных
   исполнителей `AsyncComputon`; при `n = 1` пул не запускается.
3. `ROBUST_SIZE` не связан с MPI: транспорт — локальный `pipe` (или тот, что задан
   `ASYNC_TRANSPORT`), а не MPI-мир.
4. В процессе-исполнителе (задана любая из `ASYNC_IN`, `ASYNC_OUT`, `ASYNC_TCP`) фасад свой пул
   не запускает.
5. `Unload` не возвращает результат: отказ печатается в консоль.
6. `Wait` — пустая процедура; все ожидания выражены `Pull` и `Try`.
7. `Push`, `Pull` и `Try` с рангом вне `1..size−1` трапают с кодом 20.

## 9. Общие ограничения

1. Библиотека собирается только для AMD64. Ядро языка (задачи, мониторы, `ACTIVE.AWAIT`, ключ
   `^`) работает и на i386.
2. Одна нить операционной системы исполняет код Component Pascal. Рабочие нити и процессы
   компутонов не исполняют общий код на общей куче.
3. `Select`, `AsyncIO` и `Pull` вызываются только из задачи. `Send` в почтовый ящик, `Fill`,
   `Push`, `Try`, `Unload` задачи не требуют.
4. `Once` — монитор: методы `[exclusive]`, ожидание — `ACTIVE.AWAIT`. Каналы ждут в `Tasks.Park`
   и уважают монитор объекта канала, если его держит задача.
5. Отмена задачи во время ожидания трапает её с кодом 127; предложения `Select` снимаются лениво, а
   незавершённые запросы компутона остаются занятыми до `Stop`.

## 10. Интерфейс

Раздел перечисляет экспорт модулей в форме документации BlackBox: блок `DEFINITION`, затем
описание каждой процедуры. Строка «Пред» называет условие и номер трапа при его нарушении.
Условие с пометкой «не проверяется» код не проверяет; его нарушение даёт неопределённый результат
или трап разыменования `NIL`. Строка «Пост» называет состояние после возврата.

### 10.1. AsyncSelect

```
DEFINITION AsyncSelect;

	IMPORT AsyncProto;

	CONST
		Rendezvous = 0; Bounded = 1; Mailbox = 2;

	TYPE
		Msg = POINTER TO ABSTRACT RECORD END;

		Ctx = POINTER TO RECORD END;

		Core = POINTER TO EXTENSIBLE RECORD
			count-: INTEGER;
			protoState-: INTEGER
		END;

		Signal = POINTER TO ABSTRACT RECORD
			(s: Signal) Arm (ctx: Ctx; idx: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(s: Signal) Disarm, NEW, ABSTRACT
		END;

		Op = RECORD
			got-: Msg
		END;

	PROCEDURE InitCore (c: Core; kind, capacity: INTEGER);
	PROCEDURE SetProtocol (c: Core; p: AsyncProto.Protocol);
	PROCEDURE QueueSend (c: Core; m: Msg);
	PROCEDURE QueueRecv (c: Core; OUT m: Msg);
	PROCEDURE AltSend (core: Core; msg: Msg; OUT op: Op);
	PROCEDURE AltRecv (core: Core; OUT op: Op);
	PROCEDURE AltTimeout (ms: LONGINT; OUT op: Op);
	PROCEDURE AltSignal (sig: Signal; OUT op: Op);
	PROCEDURE Select (VAR ops: ARRAY OF Op): INTEGER;
	PROCEDURE SignalReady (ctx: Ctx; idx: INTEGER);

END AsyncSelect.
```

`Msg` — базовый тип сообщения. `Core` — ядро канала: `count` — число сообщений в кольце,
`protoState` — состояние протокола (0 без протокола). `Op` — альтернатива `Select`; `got` —
сообщение победившего приёма. `Ctx` — решение одного `Select` или одного ожидания.

**PROCEDURE InitCore (c: Core; kind, capacity: INTEGER)**
Задаёт вид и ёмкость нового ядра канала.
Пред: `c` только что создано `NEW` — не проверяется; `kind` — `Rendezvous`, `Bounded` или
`Mailbox` — трап 20; `capacity > 0` для `Bounded`, иначе `capacity = 0` — трап 21.
Пост: `c` — пустой канал вида `kind` без протокола.

**PROCEDURE SetProtocol (c: Core; p: AsyncProto.Protocol)**
Прикрепляет протокол к каналу или снимает его.
Пред: `c # NIL` — не проверяется.
Пост: каждая отправка в `c` проверяется протоколом `p` в момент, когда совершается;
`protoState` — начальное состояние `p`. При `p = NIL` протокола нет, `protoState = 0`.

**PROCEDURE QueueSend (c: Core; m: Msg)**
Отправляет `m` в канал; ждёт, если получателя или места нет.
Пред: `c # NIL` — не проверяется; ожидание только из задачи — трап 20 в `Tasks.Park`.
Пост: `m` отдано получателю или лежит в кольце. Протокол отверг `m` — трап 21, `m` не
отправлено.

**PROCEDURE QueueRecv (c: Core; OUT m: Msg)**
Принимает сообщение; ждёт, если сообщений и ждущих отправителей нет.
Пред: `c # NIL` — не проверяется; ожидание только из задачи — трап 20 в `Tasks.Park`.
Пост: `m` — первое сообщение кольца или сообщение первого ждущего отправителя.

**PROCEDURE AltSend (core: Core; msg: Msg; OUT op: Op)**
Строит альтернативу отправки.
Пред: `core # NIL` — трап 23 в `Select`.
Пост: `op` — отправка `msg` в `core`.

**PROCEDURE AltRecv (core: Core; OUT op: Op)**
Строит альтернативу приёма.
Пред: `core # NIL` — трап 23 в `Select`.
Пост: `op` — приём из `core`.

**PROCEDURE AltTimeout (ms: LONGINT; OUT op: Op)**
Строит альтернативу тайм-аута.
Пост: `op` — тайм-аут `ms` мс от начала ожидания; `ms <= 0` срабатывает сразу.

**PROCEDURE AltSignal (sig: Signal; OUT op: Op)**
Строит альтернативу внешнего сигнала.
Пред: `sig # NIL` — трап 24 в `Select`.
Пост: `op` — ожидание сигнала `sig`.

**PROCEDURE Select (VAR ops: ARRAY OF Op): INTEGER**
Ждёт, пока исполнится ровно одна альтернатива, и возвращает её индекс.
Пред: вызов из задачи — трап 20; `LEN(ops) <= 32` — трап 21; вид каждой альтернативы допустим —
трап 22; у отправки и приёма канал не `NIL` — трап 23; у сигнала сигнал не `NIL` — трап 24.
Пост: исполнена ровно одна альтернатива `i`, результат — `i`. У приёма `ops[i].got` — сообщение.
Проигравшие предложения сняты, сигналы разоружены. Протокол отверг отправку — трап 21 после
снятия всех предложений.

**PROCEDURE SignalReady (ctx: Ctx; idx: INTEGER)**
Сообщает о готовности внешнего сигнала.
Пред: вызывает владелец сигнала; `ctx` и `idx` получены в `Arm`; `Disarm` ещё не вызван — не
проверяется.
Пост: если решения не было, победила альтернатива `idx`; ждущая задача разбужена.

**PROCEDURE (s: Signal) Arm (ctx: Ctx; idx: INTEGER): BOOLEAN**
Абстрактный метод; его вызывает `Select` при регистрации.
Пост: `TRUE` — сигнал готов. `FALSE` — владелец позже вызовет `SignalReady(ctx, idx)` не более
одного раза, если до того не вызван `Disarm`.

**PROCEDURE (s: Signal) Disarm**
Абстрактный метод; его вызывает `Select` после решения.
Пост: владелец сигнала больше не использует `ctx` из `Arm` и не вызывает с ним `SignalReady`.

### 10.2. AsyncChan

```
DEFINITION AsyncChan;

	IMPORT AsyncSelect, AsyncProto;

	TYPE
		Chan = POINTER TO RECORD (AsyncSelect.Core)
			(c: Chan) Send (m: AsyncSelect.Msg), NEW;
			(c: Chan) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

		Buffer = POINTER TO RECORD (AsyncSelect.Core)
			(c: Buffer) Send (m: AsyncSelect.Msg), NEW;
			(c: Buffer) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

		Mailbox = POINTER TO RECORD (AsyncSelect.Core)
			(c: Mailbox) Send (m: AsyncSelect.Msg), NEW;
			(c: Mailbox) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

	PROCEDURE NewChan (): Chan;
	PROCEDURE NewBuffer (capacity: INTEGER): Buffer;
	PROCEDURE NewMailbox (): Mailbox;
	PROCEDURE SetProtocol (c: AsyncSelect.Core; p: AsyncProto.Protocol);
	PROCEDURE SendAlt (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op);
	PROCEDURE RecvAlt (c: AsyncSelect.Core; OUT op: AsyncSelect.Op);
	PROCEDURE TimeoutAlt (ms: LONGINT; OUT op: AsyncSelect.Op);
	PROCEDURE Select (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER;

END AsyncChan.
```

**PROCEDURE NewChan (): Chan**
Пост: новая пустая встреча без протокола.

**PROCEDURE NewBuffer (capacity: INTEGER): Buffer**
Пред: `capacity > 0` — трап 20.
Пост: новый пустой буфер на `capacity` сообщений без протокола.

**PROCEDURE NewMailbox (): Mailbox**
Пост: новый пустой неограниченный ящик без протокола.

**PROCEDURE (c: Chan) Send (m: AsyncSelect.Msg)**
**PROCEDURE (c: Buffer) Send (m: AsyncSelect.Msg)**
**PROCEDURE (c: Mailbox) Send (m: AsyncSelect.Msg)**
Отправляют `m`; то же, что `AsyncSelect.QueueSend(c, m)`.
Пред: ожидание только из задачи — трап 20 в `Tasks.Park`. `Mailbox.Send` ждёт, только если
монитор канала держит другая задача.
Пост: встреча — получатель забрал `m`; буфер и ящик — `m` последнее в очереди. Протокол отверг
`m` — трап 21, `m` не отправлено.

**PROCEDURE (c: Chan) Recv (OUT m: AsyncSelect.Msg)**
**PROCEDURE (c: Buffer) Recv (OUT m: AsyncSelect.Msg)**
**PROCEDURE (c: Mailbox) Recv (OUT m: AsyncSelect.Msg)**
Принимают сообщение; то же, что `AsyncSelect.QueueRecv(c, m)`.
Пред: ожидание только из задачи — трап 20 в `Tasks.Park`.
Пост: `m` — первое сообщение очереди или первого ждущего отправителя.

**PROCEDURE SetProtocol (c: AsyncSelect.Core; p: AsyncProto.Protocol)**
То же, что `AsyncSelect.SetProtocol`.

**PROCEDURE SendAlt (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op)**
**PROCEDURE RecvAlt (c: AsyncSelect.Core; OUT op: AsyncSelect.Op)**
**PROCEDURE TimeoutAlt (ms: LONGINT; OUT op: AsyncSelect.Op)**
**PROCEDURE Select (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER**
То же, что `AsyncSelect.AltSend`, `AltRecv`, `AltTimeout` и `Select` (раздел 10.1).

### 10.3. AsyncOnce

```
DEFINITION AsyncOnce;

	IMPORT AsyncSelect;

	TYPE
		Once = POINTER TO RECORD
			done: BOOLEAN;
			(o: Once) FillInt (x: INTEGER), NEW;
			(o: Once) ReadInt (): INTEGER, NEW;
			(o: Once) FillMsg (m: AsyncSelect.Msg), NEW;
			(o: Once) ReadMsg (OUT m: AsyncSelect.Msg), NEW
		END;

	PROCEDURE New (): Once;

END AsyncOnce.
```

Все четыре метода — исключающие процедуры (`[exclusive]`). Поле `done` экспортировано для
чтения и записи; запись извне нарушает однократность, и код её не запрещает.

**PROCEDURE New (): Once**
Пост: незаполненная переменная, `done = FALSE`.

**PROCEDURE (o: Once) FillInt (x: INTEGER)**
Пред: `~o.done` — трап 21.
Пост: `o.done`; значение — `x`.

**PROCEDURE (o: Once) ReadInt (): INTEGER**
Ждёт заполнения в `ACTIVE.AWAIT(o.done)`.
Пред: ожидание только из задачи — трап 24 монитора, если переменная не заполнена.
Пост: результат — целое из `FillInt`; после `FillMsg` — 0.

**PROCEDURE (o: Once) FillMsg (m: AsyncSelect.Msg)**
Пред: `~o.done` — трап 22.
Пост: `o.done`; значение — `m`.

**PROCEDURE (o: Once) ReadMsg (OUT m: AsyncSelect.Msg)**
Ждёт заполнения в `ACTIVE.AWAIT(o.done)`.
Пред: ожидание только из задачи — трап 24 монитора, если переменная не заполнена.
Пост: `m` — сообщение из `FillMsg`; после `FillInt` — `NIL`.

### 10.4. AsyncProto

```
DEFINITION AsyncProto;

	TYPE
		Message = ARRAY 256 OF CHAR;
		Protocol = POINTER TO RECORD END;

	PROCEDURE New (IN desc: ARRAY OF CHAR): Protocol;
	PROCEDURE LastError (OUT msg: Message);
	PROCEDURE Start (p: Protocol): INTEGER;
	PROCEDURE StateName (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR);
	PROCEDURE IsFinal (p: Protocol; state: INTEGER): BOOLEAN;
	PROCEDURE Step (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN);
	PROCEDURE LastViolation (p: Protocol; OUT msg: Message);

END AsyncProto.
```

**PROCEDURE New (IN desc: ARRAY OF CHAR): Protocol**
Строит автомат из описания (раздел 5).
Пост: автомат или `NIL` при ошибке описания; текст ошибки — в `LastError`.

**PROCEDURE LastError (OUT msg: Message)**
Пост: `msg` — текст последней ошибки разбора.

**PROCEDURE Start (p: Protocol): INTEGER**
Пост: индекс начального состояния; при `p = NIL` — 0.

**PROCEDURE StateName (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR)**
Пост: `name` — имя состояния; `""`, если `p = NIL` или индекса нет.

**PROCEDURE IsFinal (p: Protocol; state: INTEGER): BOOLEAN**
Пред: `p # NIL` — не проверяется.
Пост: `TRUE`, если из `state` нет исходящих переходов.

**PROCEDURE Step (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN)**
Проверяет очередное сообщение с меткой `label`.
Пост: `ok` — `state` переведено по переходу `label`. `~ok` — `state` прежнее, текст нарушения — в
`LastViolation`. При `p = NIL` — `ok`.

**PROCEDURE LastViolation (p: Protocol; OUT msg: Message)**
Пост: `msg` — текст последнего нарушения; при `p = NIL` — `""`.

### 10.5. AsyncIO

```
DEFINITION AsyncIO;

	IMPORT CommStreams;

	PROCEDURE WaitFd (fd: INTEGER; events: SET);
	PROCEDURE Poll (timeout: LONGINT);
	PROCEDURE Read (s: CommStreams.Stream; VAR x: ARRAY OF BYTE; beg, len: INTEGER;
		OUT read: INTEGER);
	PROCEDURE Write (s: CommStreams.Stream; IN x: ARRAY OF BYTE; beg, len: INTEGER;
		OUT written: INTEGER);
	PROCEDURE Accept (l: CommStreams.Listener; OUT s: CommStreams.Stream;
		timeout: LONGINT): BOOLEAN;

END AsyncIO.
```

**PROCEDURE WaitFd (fd: INTEGER; events: SET)**
Ждёт готовности дескриптора; регистрация одноразовая.
Пред: вызов из задачи — трап 20.
Пост: `fd` готов к `events`; при `fd < 0` прошёл `Tasks.Sleep(1)`.

**PROCEDURE Poll (timeout: LONGINT)**
Прогоняет ожидания `Poller` для тестового насоса.
Пост: готовые ожидающие не дольше `timeout` мс разбужены.

**PROCEDURE Read (s; VAR x; beg, len: INTEGER; OUT read: INTEGER)**
Пред: ожидание только из задачи — трап 20 в `WaitFd` или `Tasks.Sleep`.
Пост: `read > 0` — прочитано `read` байт; `read = 0` — конец или обрыв.

**PROCEDURE Write (s; IN x; beg, len: INTEGER; OUT written: INTEGER)**
Пред: ожидание только из задачи — трап 20 в `WaitFd` или `Tasks.Sleep`.
Пост: `written > 0` — записано `written` байт; `written = 0` — обрыв.

**PROCEDURE Accept (l; OUT s; timeout: LONGINT): BOOLEAN**
Пред: ожидание только из задачи — трап 20 в `WaitFd` или `Tasks.Sleep`.
Пост: `TRUE` — `s` принятое соединение; `FALSE` — истёк `timeout` мс, `s = NIL`. При
`timeout < 0` срока нет.

### 10.6. AsyncWork

```
DEFINITION AsyncWork;

	IMPORT SYSTEM;

	CONST
		maxPath = 256; maxBuf = 4096; maxHost = 128;

	TYPE
		Proc = PROCEDURE [ccall] (arg: SYSTEM.ADDRESS);

		FileReq = RECORD [untagged]
			path: ARRAY maxPath OF SHORTCHAR;
			buf: ARRAY maxBuf OF SHORTCHAR;
			len, status: INTEGER
		END;

		AddrReq = RECORD [untagged]
			host: ARRAY maxHost OF SHORTCHAR;
			count, status: INTEGER
		END;

		FileReqPtr = POINTER [untagged] TO FileReq;
		AddrReqPtr = POINTER [untagged] TO AddrReq;

		Impl = POINTER TO ABSTRACT RECORD
			(i: Impl) Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER, NEW, ABSTRACT;
			(i: Impl) Done (id: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(i: Impl) Result (id: INTEGER): INTEGER, NEW, ABSTRACT;
			(i: Impl) Release (id: INTEGER), NEW, ABSTRACT;
			(i: Impl) Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER, NEW, ABSTRACT;
			(i: Impl) Workers (): INTEGER, NEW, ABSTRACT;
			(i: Impl) Scan, NEW, ABSTRACT;
			(i: Impl) ReadFileJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT;
			(i: Impl) WriteFileJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT;
			(i: Impl) ResolveJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT
		END;

	PROCEDURE Install (i: Impl);
	PROCEDURE Active (): BOOLEAN;
	PROCEDURE Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE Done (id: INTEGER): BOOLEAN;
	PROCEDURE Result (id: INTEGER): INTEGER;
	PROCEDURE Release (id: INTEGER);
	PROCEDURE Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE Workers (): INTEGER;
	PROCEDURE Scan;
	PROCEDURE [ccall] ReadFileJob (arg: SYSTEM.ADDRESS);
	PROCEDURE [ccall] WriteFileJob (arg: SYSTEM.ADDRESS);
	PROCEDURE [ccall] ResolveJob (arg: SYSTEM.ADDRESS);

END AsyncWork.
```

`Impl` — платформенный делегат; его ставят `AsyncWorkLin` и `AsyncWorkWin`. Номера трапов
делегата приведены для Linux и Windows; они совпадают.

**PROCEDURE Install (i: Impl)**
Ставит делегата. Его вызывает `Install` делегата.
Пост: пул обслуживает `i`.

**PROCEDURE Active (): BOOLEAN**
Пост: `TRUE`, если делегат поставлен.

**PROCEDURE Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER**
Ставит задание в очередь; при необходимости ставит делегата.
Пред: делегат найден — трап 22.
Пост: номер слота; −1 — нет свободного слота или очередь полна.

**PROCEDURE Done (id: INTEGER): BOOLEAN**
Пред: делегат поставлен — трап 21; `0 <= id <` числа слотов — трап 20 в делегате.
Пост: `TRUE`, если задание `id` завершено.

**PROCEDURE Result (id: INTEGER): INTEGER**
Пред: те же, что у `Done`.
Пост: статус задания `id`.

**PROCEDURE Release (id: INTEGER)**
Пред: те же, что у `Done`.
Пост: слот `id` свободен.

**PROCEDURE Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER**
Исполняет задание в рабочей нити и ждёт его; точка переключения.
Пред: делегат найден — трап 22; вызов из задачи — трап 21 в делегате.
Пост: статус задания, слот освобождён; −1 — нет свободного слота.

**PROCEDURE Workers (): INTEGER**
Пост: число созданных рабочих нитей; без делегата — 0.

**PROCEDURE Scan**
Разбирает завершённые задания; его вызывает главный цикл.
Пост: задачи, чьи задания завершены, разбужены.

**PROCEDURE ReadFileJob (arg: SYSTEM.ADDRESS)**
**PROCEDURE WriteFileJob (arg: SYSTEM.ADDRESS)**
**PROCEDURE ResolveJob (arg: SYSTEM.ADDRESS)**
Задания для рабочей нити; `arg` — адрес `FileReq` или `AddrReq`.
Пред: делегат поставлен — трап 21.
Пост: статус задания — в поле `status` записи (коды — в разделе 7).

### 10.7. AsyncComputon

```
DEFINITION AsyncComputon;

	IMPORT Stores, AsyncSelect;

	TYPE
		Computon = POINTER TO ABSTRACT RECORD (Stores.Store)
			(c: Computon) Do, NEW, ABSTRACT
		END;

		Hook = POINTER TO ABSTRACT RECORD
			(h: Hook) Failed (rank, tag, err: INTEGER), NEW, ABSTRACT
		END;

		Transport = POINTER TO ABSTRACT RECORD
			(t: Transport) Start (n: INTEGER; OUT size: INTEGER), NEW, ABSTRACT;
			(t: Transport) SendFrame (rank, tag, kind: INTEGER;
				buf: POINTER TO ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(t: Transport) Poll, NEW, ABSTRACT;
			(t: Transport) Stop, NEW, ABSTRACT;
			(t: Transport) Alive (rank: INTEGER): BOOLEAN, NEW, ABSTRACT
		END;

	PROCEDURE Serialize (c: Computon; OUT buf: POINTER TO ARRAY OF SHORTCHAR; OUT len: INTEGER);
	PROCEDURE Deserialize (VAR b: ARRAY OF SHORTCHAR; len: INTEGER): Computon;
	PROCEDURE SetTransport (IN name: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE SetTransportObject (t: Transport);
	PROCEDURE AltPull (rank, tag: INTEGER): AsyncSelect.Signal;
	PROCEDURE Push (c: Computon; rank, tag: INTEGER);
	PROCEDURE PullTimeout (rank, tag: INTEGER; ms: LONGINT): Computon;
	PROCEDURE Pull (rank, tag: INTEGER): Computon;
	PROCEDURE Try (rank, tag: INTEGER; OUT c: Computon): BOOLEAN;
	PROCEDURE Wait;
	PROCEDURE LastError (): INTEGER;
	PROCEDURE SetHook (h: Hook);
	PROCEDURE LogMsg (IN msg: ARRAY OF CHAR);
	PROCEDURE Unload (IN module: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE Size (): INTEGER;
	PROCEDURE Rank (): INTEGER;
	PROCEDURE WorkerPid (rank: INTEGER): INTEGER;
	PROCEDURE WorkerDead (rank: INTEGER): BOOLEAN;
	PROCEDURE Stop;
	PROCEDURE Start (n: INTEGER);
	PROCEDURE MarkDead (idx: INTEGER);
	PROCEDURE DeliverFrame (rank, tag, status: INTEGER; VAR b: ARRAY OF SHORTCHAR; len: INTEGER);
	PROCEDURE RunDo (c: Computon; OUT err: INTEGER);
	PROCEDURE WorkerLoop;

END AsyncComputon.
```

Коды `LastError` перечислены в разделе 8.1. `Transport`, `SetTransportObject`, `MarkDead`,
`DeliverFrame`, `RunDo`, `Serialize` и `Deserialize` служат транспортам и исполнителю.

**PROCEDURE Start (n: INTEGER)**
Останавливает прежний пул и запускает `n` исполнителей.
Пред: `1 <= n <= 16` — трап 20; для транспорта `mpi` поставлен внешний транспорт — трап 21.
Пост: `Size()` — число запущенных исполнителей.

**PROCEDURE Stop**
Пост: исполнители убиты, каналы закрыты, `Size() = 0`.

**PROCEDURE SetTransport (IN name: ARRAY OF CHAR): BOOLEAN**
Пост: `TRUE` — выбран транспорт `"pipe"` (и `""`), `"tcp"` или `"mpi"`; `FALSE` — имя неизвестно.

**PROCEDURE Push (c: Computon; rank, tag: INTEGER)**
Сериализует `c` и отправляет исполнителю `rank`.
Пост: запрос `tag` поставлен; при ошибке `LastError = -1`, отправка не удалась — повтор на живом.

**PROCEDURE PullTimeout (rank, tag: INTEGER; ms: LONGINT): Computon**
**PROCEDURE Pull (rank, tag: INTEGER): Computon**
Ждут результат запроса `tag`; `Pull` — с `ms = 10000`.
Пред: вызов из задачи — трап 21.
Пост: результат или `NIL` с кодом в `LastError`; запрос освобождён.

**PROCEDURE Try (rank, tag: INTEGER; OUT c: Computon): BOOLEAN**
Пост: `TRUE` — запрос завершён и освобождён, `c` — результат или `NIL` с кодом в `LastError`;
`FALSE` — результата нет, `c = NIL`.

**PROCEDURE AltPull (rank, tag: INTEGER): AsyncSelect.Signal**
Пост: сигнал для `Select`, готовый, когда завершён запрос `tag`.

**PROCEDURE Unload (IN module: ARRAY OF CHAR): BOOLEAN**
Пост: `TRUE` — все живые исполнители выгрузили модуль или пул не запущен; `FALSE` — отказ, код в
`LastError`.

**PROCEDURE Size (): INTEGER**, **Rank (): INTEGER**, **WorkerPid (rank): INTEGER**,
**WorkerDead (rank): BOOLEAN**, **LastError (): INTEGER**
Пост: число исполнителей; 0; pid исполнителя или −1; признак гибели (`TRUE` вне диапазона);
код последней ошибки.

**PROCEDURE SetHook (h: Hook)**, **LogMsg (IN msg)**, **Wait**
Ставят обработчик ошибок, пишут строку в консоль; `Wait` ничего не делает.

### 10.8. AsyncMpi

```
DEFINITION AsyncMpi;

	PROCEDURE Install (): BOOLEAN;
	PROCEDURE Available (): BOOLEAN;
	PROCEDURE Rank (): INTEGER;
	PROCEDURE WorldSize (): INTEGER;
	PROCEDURE WorkerLoop;

END AsyncMpi.
```

**PROCEDURE Install (): BOOLEAN**
Пост: `TRUE` — `libbbmpi.so` привязана, транспорт поставлен в `AsyncComputon`.

**PROCEDURE Available (): BOOLEAN**
Пост: `TRUE` — библиотека MPICH и обёртка найдены; `MPI_Init` не вызывается.

**PROCEDURE Rank (): INTEGER**, **WorldSize (): INTEGER**
Пост: ранг и размер мира MPI; до инициализации — −1 и 0.

**PROCEDURE WorkerLoop**
Точка входа процесса-исполнителя ранга больше 0.

## 11. Схема реализации

Раздел не определяет поведение; он описывает, как оно получено.

### 11.1. Канал и выбор

`AsyncSelect.Core` — расширяемая запись с видом, ёмкостью, очередью сообщений и головами двух
очередей предложений (`waitS`, `waitR`). Предложение (`Offer`) хранит общий контекст `Ctx`,
индекс альтернативы, вид операции и сообщение.

Предложения и контексты берутся из свободных списков `freeOffers` и `freeCtx` и возвращаются
туда после операции. Запись свободного списка не лежит ни в одной очереди канала. Выбор без
тайм-аута памяти не выделяет. Очередь сообщений `Buffer` и `Mailbox` — кольцо в массиве; у `Buffer` его длина
равна ёмкости, у `Mailbox` кольцо удваивается при заполнении.

Операция над каналом между двумя точками переключения исполняется без переключений: нить одна,
вытеснения нет. Поэтому `Enter` берёт монитор канала, только если его кто-то держит
(`Monitors.IsLocked`). Тогда операция ждёт освобождения монитора; иначе исключение обеспечено без
монитора.

Две процедуры исполняют операцию над каналом. `Put` отдаёт сообщение ждущему получателю или
кладёт его в кольцо и проверяет протокол. `Take` берёт сообщение из кольца или у ждущего
отправителя. После `Take` из кольца `CompleteSend` исполняет ждущие отправки, пока есть место.
Обе процедуры пропускают предложения с решением вызывающего: предложение не исполняется против
предложения с тем же `Ctx`.

`Select` обходит альтернативы в порядке индексов. Для отправки и приёма он берёт предложение и
вызывает `Register`. `Register` исполняет операцию через `Put` или `Take` либо ставит предложение
в `waitS` или `waitR`. Тайм-аут с наименьшим `ms` ставит действие `Services`. Задача спит в
`Tasks.Park`, пока `Ctx.decided` не станет истинным.

Решение принимает сторона, которая первой исполнила предложение. Она записывает `ctx.decided` и
`ctx.winner`, забирает или отдаёт сообщение и будит задачу через `Tasks.Unpark`. Монитор канала
при этом взят, только если его держал кто-то другой. `Wake` будит только при `ctx.ready = TRUE`.
Во время регистрации задача может ждать монитор другого канала, и `Unpark` перевёл бы её в
`Ready` без монитора. После пробуждения владелец снимает предложения (`Withdraw`). Снятие
идемпотентно, поэтому победитель снимается тем же вызовом.

Прямые `Send` и `Recv` всех трёх видов — `QueueSend` и `QueueRecv`. Операция исполняется сразу
через `Put` или `Take`, иначе предложение встаёт в `waitS` или `waitR`, а задача спит в
`Tasks.Park`. Другая сторона исполняет предложение и будит задачу без передачи монитора. Встреча —
канал ёмкости 0: отправка без получателя всегда ждёт в `waitS`. Мёртвые предложения снимаются при
просмотре очереди. Мёртвым считается предложение задачи в состоянии `Finished` или `Cancelled`.
### 11.2. Ввод-вывод

`Poller` хранит только указатель на платформенный хук. Реестр дескрипторов и `poll` лежат в
`LinConsole`: массив на 4096 записей, stdin первым элементом, одноразовая регистрация. Готовые
записи снимаются до вызова `Waiter.Ready`, чтобы повторная регистрация не испортила обход.

### 11.3. Рабочие нити

`AsyncWork` — фасад; `AsyncWorkLin` и `AsyncWorkWin` — делегаты. Linux: до четырёх pthread,
статический массив слотов под мьютексом и условной переменной, кольцевая очередь на 256 заданий,
байт в pipe как сигнал готовности. Windows: до четырёх `CreateThread`, слоты под
`CRITICAL_SECTION`, событие очереди; готовность забирает действие `Services`, опрашивающее
`done` каждые 5 мс.

Примитивы синхронизации лежат в куче, а не в глобальных записях модуля: глобальные записи не
получают гарантированного выравнивания, а futex и `CRITICAL_SECTION` его требуют.

### 11.4. Компутоны

`AsyncComputon` держит до 16 исполнителей и до 128 запросов. Запрос (`Req`) хранит `tag`, ранг,
буфер, задачу и контекст `Select`. `Push` сериализует объект через `Stores` во временный файл и
отправляет кадр. Ответ читает главный цикл: на Linux — через `Poller`, на Windows — действием
`Services`; `Pull` ждёт `Tasks.Sleep(10)` с дедлайном.

`MarkDead` закрывает канал умершего исполнителя и повторяет его незавершённые запросы на живом.
`Resend` выбирает следующего живого; если живых нет, запрос получает код `-2`. `Unload`
отправляет вид `kindUnload` и ждёт ответов; исполнитель вызывает `Kernel.UnloadMod` и очищает
указатель на объект выгруженного модуля, чтобы следующая сборка не разыменовала его таблицу
методов.

MPI-транспорт (`AsyncMpi`) загружает `libbbmpi.so` через `LinDl` и привязывает семь функций.
Управляющий проверяет готовность ответов `MPI_Iprobe` из действия `Services`; исполнитель
`WorkerLoop` принимает кадры на stdin-команду и отвечает через MPI.
