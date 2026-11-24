# Хранилище по содержимому Cas

Программа раз в минуту сохраняет состояние сотни объектов. За минуту меняются два-три из них, но
копия всего состояния пишется целиком, и диск заполняется одинаковыми байтами. Хуже другое: две
программы сохраняют свои версии под одним именем, и та, что пишет второй, молча затирает работу
первой. Подсистема `Cas` решает обе задачи одним приёмом: блок данных называется своим хешем.
Одинаковые блоки получают одно имя и хранятся один раз; имя нельзя подделать, не изменив содержимое;
а единственное изменяемое место — ссылка на последнюю версию — меняется только при условии, что её
никто не успел изменить.

Подсистема состоит из шести модулей. `CasHash` считает хеш SHA-256. `CasStore` описывает хранилище
блоков и реализует его в памяти, `CasFiles` — в каталоге файлов. `CasChunk` режет длинный поток на
куски по содержимому, `CasTree` собирает из кусков дерево. `CasChange` хранит наборы изменений и
публикует их. Модули собираются для amd64 и i386 из общего каталога `Cas/Mod`. Нитей в подсистеме
нет, файловые операции синхронные.

## 1. Блок и его имя

Хранилище держит *блоки* — массивы байт длиной от одного байта. Имя блока — его хеш SHA-256, 32 байта
типа `CasHash.Id`. Отсюда три свойства. Запись идемпотентна: `Put` того же блока даёт тот же `Id`, и
вторая запись ничего не добавляет. Одинаковые данные хранятся один раз, кто бы и сколько бы раз их
ни записал. Прочитанный блок проверяется: хеш его байт должен совпасть с именем, иначе это не тот
блок.

Хранилище — абстрактный тип `CasStore.Store` с методами `Put`, `Get` и `Has`, по образцу
`Files.Directory`. Реализация в памяти — `CasStore.NewMem()`; её поля `blocks` и `bytes` считают
разные блоки и их байты. Пример кладёт три слова, из которых два одинаковы:

```
MODULE ObxCas0;
	IMPORT Console, Strings, CasHash, CasStore;

	PROCEDURE Put (s: CasStore.Store; IN text: ARRAY OF CHAR; OUT id: CasHash.Id);
		VAR b: ARRAY 64 OF BYTE; n, res: INTEGER; hex: ARRAY 65 OF CHAR;
	BEGIN
		n := 0;
		WHILE text[n] # 0X DO b[n] := SHORT(SHORT(ORD(text[n]))); INC(n) END;
		s.Put(b, n, id, res);
		CasHash.ToHex(id, hex); hex[16] := 0X;	(* первые 16 знаков из 64 *)
		Console.WriteStr(text); Console.WriteStr(" -> "); Console.WriteStr(hex); Console.WriteLn
	END Put;

	PROCEDURE Do*;
		VAR s: CasStore.Mem; a, b, c: CasHash.Id; data: CasStore.Block; res, i: INTEGER;
			n: ARRAY 16 OF CHAR;
	BEGIN
		s := CasStore.NewMem();
		Put(s, "hello", a); Put(s, "world", b); Put(s, "hello", c);
		Strings.IntToString(s.blocks, n);
		Console.WriteStr("blocks: "); Console.WriteStr(n); Console.WriteLn;
		s.Get(c, data, res);
		Console.WriteStr("get: ");
		FOR i := 0 TO LEN(data) - 1 DO Console.WriteChar(CHR(data[i])) END;
		Console.WriteLn
	END Do;

END ObxCas0.
```

Слово `hello` записано дважды и получило одно имя, поэтому в хранилище два блока, а не три. Разбор
вывода — страница примера `Obx/Docu/ru/Cas0.md`.

Пустого блока нет: `Put` с длиной 0 трапует с номером 20. Причина в языке: открытый массив длины 0
в BlackBox не создать (`NEW(b, 0)` трапует), а `Get` возвращает блок как `POINTER TO ARRAY OF BYTE`.
Пустой поток `CasTree` хранит узлом без детей, поэтому запрет ничего не отнимает.

Ошибка среды — не трап. Блока нет — `Get` возвращает `res = absent` и `b = NIL`; блок прочитан, но
хеш не сошёлся — `res = corrupt` и тоже `NIL`; файл не записан или не прочитан — `res = failed`. Так
устроен и `Files`: программа решает сама, что делать с пропажей, а трап остаётся для ошибок в самой
программе.

SHA-256 на amd64 считает 24–31 МБ в секунду в одной нити; условия и команда — `docs/measurements.md`.
Слова хеша — `INTEGER`, 32 бита на обеих платформах. Сложение `INTEGER` в BlackBox не проверяет
переполнение и отбрасывает старшие биты — это сложение по модулю 2^32, которое и нужно SHA-256.
Повороты — `SYSTEM.ROT`, сдвиги — `SYSTEM.LSH`; оба генератора кода переводят их в машинный поворот
32-битного слова. Векторы NIST в `Cas_devHash` проверяют это на amd64 и i386.

## 2. Хранилище в каталоге

`CasFiles.New(путь, res)` хранит блок файлом, имя которого — 64 шестнадцатеричных знака `Id`.
Программу могут прервать посреди записи. Если бы блок писался сразу под своим именем, после сбоя в
каталоге лежал бы файл с правильным именем и половиной данных. Поэтому `CasFiles` пишет файл под
временным именем `tmp-write` и затем переименовывает его; переименование в Linux заменяет имя
целиком. Недописанный блок остаётся под временным именем, и `CasFiles.New` удаляет его при открытии.

При чтении `Get` заново считает хеш. Тест `Cas_devStore.TestFilesCorrupt` портит первый байт файла
блока; `Get` возвращает `corrupt`, а не испорченные данные. Без этой проверки повреждение диска
прошло бы дальше молча: имя блока обещает содержимое, которого нет.

Хранилищем в каталоге пользуется один процесс. Сравнение и замена ссылки (раздел 3) неделимы только
внутри процесса: между чтением ссылки и записью нового файла другой процесс может вклиниться.
Компилятор и `CasFiles` этого не проверяют.

## 3. Ссылки и публикация

Блоки неизменяемы, но программе нужно знать, какая версия последняя. Для этого есть *ссылка* —
изменяемое имя, указывающее на `Id`. `GetRef(имя)` читает её; ссылки нет — пустой `Id`
(`CasHash.IsNil`). Имя ссылки — от 1 до 63 знаков из букв, цифр, `_`, `-` и `.`; другое имя трапует
с номером 22.

Ссылку меняет только `CasRef(имя, ожидаемый, новый)`: если ссылка равна ожидаемому `Id`, она
получает новый, и результат `TRUE`; иначе ссылка не меняется, и результат `FALSE`. Это сравнение
с заменой и есть публикация. Пусть Аня и Боря прочитали вершину `h` и каждый подготовил свою версию.
Аня публикует первой: `CasRef("main", h, ann)` даёт `TRUE`. Боря зовёт `CasRef("main", h, bob)` и
получает `FALSE`, потому что вершина уже `ann`. Без сравнения Боря записал бы `bob` поверх `ann`, и
работа Ани пропала бы из истории без единой ошибки. С проверкой Боря узнаёт об этом сразу,
перечитывает вершину и повторяет. Проверка оптимистичная: никто никого не ждёт, а за столкновение
платит тот, кто опоздал.

`FALSE` возвращается и тогда, когда запись ссылки не удалась. Вызывающий отличает этот случай по
`res` следующего `GetRef`.

## 4. Нарезка по содержимому

Блок в 10 МБ невыгоден: изменился один байт — записан новый блок в 10 МБ. Длинный поток режут на
куски, и каждый кусок — отдельный блок. Вопрос в том, где резать. Если резать через каждые 8192
байта, вставка десяти байт в середину сдвигает все границы после места вставки, и все последующие
куски получают новые имена. Пример `ObxCas1` режет два входа по 300 000 байт, второй — с десятью
байтами в середине: при нарезке по 8 КБ общих кусков 18 из 37, при нарезке по содержимому — 30 из 31.

`CasChunk.Cut` ставит границу там, где хеш последних 48 байт (`window`) делится на маску: `hash *
mask = {}`. Хеш скользящий, Buzhash: при сдвиге окна на байт он поворачивается на один бит и
получает исключающее «или» со словом из таблицы для входящего байта и для уходящего. Хеш зависит
только от 48 байт под окном, поэтому граница привязана к содержимому, а не к позиции: после места
вставки окно видит те же байты и находит те же границы. Таблица из 256 слов строится в теле модуля
генератором xorshift32 с постоянным зерном; тест `Cas_devChunk.TestMatchesReference` сверяет точки
разреза с эталоном, и изменённая таблица его не пройдёт.

Длину куска ограничивают `Params`: `min` (2048 по умолчанию), `max` (65 536) и `mask` (8191 —
граница в среднем через 8 КБ после `min`). Короче `min` кусок не бывает: окно начинает считать с
`min - window`, и до `min` граница не проверяется. Длиннее `max` кусок не бывает: на однородных
данных хеш постоянен, граница не находится, и `Cut` режет по `max`. Маска должна иметь вид 2^k − 1;
иначе, как и при `min < window` или `max < min`, `Cut` трапует с номером 21.

Нарезка на amd64 идёт со скоростью 570–640 МБ в секунду, в двадцать раз быстрее SHA-256, поэтому
запись потока упирается в хеш, а не в нарезку.

## 5. Большой объект как дерево

`CasTree.PutStream(хранилище, читатель, корень, res)` режет поток `Files.Reader` на куски и
записывает их блоками. Из имён кусков он собирает *узлы*: узел — блок, в котором байт уровня и до 64
(`fan`) имён детей. Узлы уровня 1 перечисляют куски, узлы уровня 2 — узлы уровня 1, и так, пока не
останется один узел — корень. Корень всегда узел, поэтому пустой поток — узел без детей.

Имя корня зависит от имён детей, а те — от содержимого, поэтому корень однозначно определяет
содержимое, как имя блока определяет блок. Изменился один байт в середине — изменились кусок,
путь узлов от него до корня и сам корень; остальные блоки те же. Тест
`Cas_devTree.TestRootIdentifiesContent` меняет байт в потоке на 200 000 байт и видит не больше трёх
новых блоков.

`GetStream(хранилище, корень, писатель, res)` обходит дерево и пишет куски по порядку. Блока нет —
`res = absent`; блок не похож на узел (длина не 1 + 32·n, уровень вне 1..16) — `res = corrupt`. В
обоих случаях в писателе может остаться начало содержимого, поэтому писать стоит во временный файл.

## 6. Наборы изменений

Ссылка указывает на версию, но не говорит, что в ней изменилось и кем. *Набор изменений*
`CasChange.Change` хранит родителя (`Id` предыдущего набора или пустой `Id`), отображение «имя →
`Id`», автора, время и пояснение. Набор записывается блоком (`Put`), поэтому он неизменяем, и его
`Id` — имя версии. Цепочка наборов от вершины к первому — история; `Log` обходит её и связывает
наборы полем `older`.

`Commit(хранилище, ссылка, набор, id, res)` записывает набор и публикует его через `CasRef` от
`parent` набора к его `Id`. Если вершина ушла вперёд, `res = conflict`, и ссылка не меняется.
Тогда `Rebase(хранилище, набор, вершина, новый, имя, res)` переносит набор на новую вершину: проходит
цепочку от вершины назад до родителя набора и смотрит, какие имена изменили другие. Имя, которому
другие дали иное значение, чем этот набор, — конфликт: `res = conflict`, имя возвращается в
`clash`. Если конфликтов нет, `Rebase` строит новый набор с теми же записями, автором, временем и
пояснением и родителем — вершиной. Пример `ObxCas2` показывает это на двух авторах.

Содержимое `Rebase` не сливает. Если Аня и Боря изменили один файл по-разному, решать, как
соединить правки, должен человек или программа, которая знает формат файла. Набор, который тихо
выбрал бы одну из версий, потерял бы другую так же, как запись ссылки без сравнения. Если оба дали
имени одно и то же значение, конфликта нет. Родитель набора, которого нет в цепочке вершины, —
`res = unrelated`: такой набор относится к другой истории.

Записанный набор менять нельзя: `Set` после `Put` трапует с номером 23. Новая версия — новый набор.

## 7. Цена и границы

Хранилище только растёт: блоки, на которые больше не ведёт ни одна ссылка, не удаляются. Сборки
мусора по хранилищу нет. Хранилище в памяти ищет блок по первому байту `Id` в одной из 256 цепочек;
хранилище в каталоге — открытием файла. Набор изменений читает всю цепочку до родителя при каждом
`Rebase`, поэтому длинная чужая история между родителем и вершиной стоит по одному `Get` на набор.

## Интерфейс

```
DEFINITION CasHash;

	CONST size = 32;

	TYPE Id = ARRAY 32 OF BYTE;

	PROCEDURE Of (IN data: ARRAY OF BYTE; beg, len: INTEGER; OUT id: Id);
	PROCEDURE Equal (IN a, b: Id): BOOLEAN;
	PROCEDURE Clear (OUT id: Id);
	PROCEDURE IsNil (IN id: Id): BOOLEAN;
	PROCEDURE ToHex (IN id: Id; OUT s: ARRAY OF CHAR);
	PROCEDURE FromHex (IN s: ARRAY OF CHAR; OUT id: Id; OUT ok: BOOLEAN);

END CasHash.
```

**PROCEDURE Of (IN data: ARRAY OF BYTE; beg, len: INTEGER; OUT id: Id)**
Хеш SHA-256 отрезка `data[beg .. beg + len - 1]`.

```
Пред: beg >= 0, len >= 0 — 20
Пред: beg + len <= LEN(data) — 21
Пост: id — SHA-256 отрезка
```

**PROCEDURE Equal (IN a, b: Id): BOOLEAN**, **IsNil (IN id: Id): BOOLEAN**, **Clear (OUT id: Id)**
Пост: `Equal` — `a` и `b` совпадают побайтно; `IsNil` — все байты 0; после `Clear` верно `IsNil(id)`.
Пустой `Id` означает «нет блока».

**PROCEDURE ToHex (IN id: Id; OUT s: ARRAY OF CHAR)**

```
Пред: LEN(s) > 64 — 20
Пост: s — 64 знака 0-9a-f и 0X
```

**PROCEDURE FromHex (IN s: ARRAY OF CHAR; OUT id: Id; OUT ok: BOOLEAN)**
Разбирает запись `ToHex`; заглавные буквы допустимы.
Пост: `ok` — `s` из ровно 64 шестнадцатеричных знаков; иначе `id` пуст.

```
DEFINITION CasStore;

	IMPORT CasHash;

	CONST
		ok = 0; absent = 1; corrupt = 2; failed = 3;
		maxRef = 64;

	TYPE
		Block = POINTER TO ARRAY OF BYTE;
		RefName = ARRAY 64 OF CHAR;

		Store = POINTER TO ABSTRACT RECORD
			(s: Store) Put (IN data: ARRAY OF BYTE; len: INTEGER; OUT id: CasHash.Id;
				OUT res: INTEGER), NEW, ABSTRACT;
			(s: Store) Get (IN id: CasHash.Id; OUT b: Block; OUT res: INTEGER), NEW, ABSTRACT;
			(s: Store) Has (IN id: CasHash.Id): BOOLEAN, NEW, ABSTRACT;
			(s: Store) GetRef (IN name: ARRAY OF CHAR; OUT id: CasHash.Id; OUT res: INTEGER),
				NEW, ABSTRACT;
			(s: Store) CasRef (IN name: ARRAY OF CHAR; IN old, new: CasHash.Id): BOOLEAN,
				NEW, ABSTRACT
		END;

		Mem = POINTER TO RECORD (Store)
			blocks-, bytes-: INTEGER
		END;

	PROCEDURE ValidRef (IN name: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE NewMem (): Mem;

END CasStore.
```

**PROCEDURE (s: Store) Put (IN data: ARRAY OF BYTE; len: INTEGER; OUT id: CasHash.Id; OUT res: INTEGER)**
Записывает блок `data[0 .. len-1]`.

```
Пред: 0 < len <= LEN(data) — 20
Пост: res = ok: id = SHA-256 блока, Has(id); блок, который уже был, не перезаписан
Пост: res = failed: id — тот же хеш, блок мог не сохраниться
```

**PROCEDURE (s: Store) Get (IN id: CasHash.Id; OUT b: Block; OUT res: INTEGER)**

```
Пост: res = ok: b # NIL, SHA-256(b) = id
Пост: res = absent, corrupt или failed: b = NIL
```

**PROCEDURE (s: Store) Has (IN id: CasHash.Id): BOOLEAN**
Пост: блок `id` записан; содержимое не проверяется.

**PROCEDURE (s: Store) GetRef (IN name: ARRAY OF CHAR; OUT id: CasHash.Id; OUT res: INTEGER)**

```
Пред: ValidRef(name) — 22
Пост: res = ok: id — значение ссылки; ссылки нет — id пуст
Пост: res = failed или corrupt: id пуст
```

**PROCEDURE (s: Store) CasRef (IN name: ARRAY OF CHAR; IN old, new: CasHash.Id): BOOLEAN**
Сравнивает и заменяет: ссылка, равная `old` (пустой `Id` — ссылки нет), получает значение `new`.

```
Пред: ValidRef(name) — 22
Пост: TRUE — ссылка равна new
Пост: FALSE — ссылка не равна old или запись не удалась; ссылка не изменилась
```

**PROCEDURE ValidRef (IN name: ARRAY OF CHAR): BOOLEAN**
Пост: `name` — от 1 до 63 знаков `A-Z a-z 0-9 _ - .`.

**PROCEDURE NewMem (): Mem**
Пост: пустое хранилище в памяти: `blocks = 0`, `bytes = 0`, ссылок нет.

```
DEFINITION CasFiles;

	IMPORT CasStore;

	TYPE Store = POINTER TO RECORD (CasStore.Store) END;

	PROCEDURE New (IN path: ARRAY OF CHAR; OUT res: INTEGER): Store;

END CasFiles.
```

**PROCEDURE New (IN path: ARRAY OF CHAR; OUT res: INTEGER): Store**
Хранилище в каталоге `path`; каталог создаётся при первой записи. Методы — как у `CasStore.Store`;
`Get` заново проверяет хеш, пустой файл — `corrupt`.
Пост: `res = CasStore.ok`, результат `# NIL`; временный файл прерванной записи удалён.

```
DEFINITION CasChunk;

	CONST window = 48;

	TYPE Params = RECORD
		min, mask, max: INTEGER
	END;

	PROCEDURE Default (OUT p: Params);
	PROCEDURE Cut (IN data: ARRAY OF BYTE; beg, end: INTEGER; IN p: Params): INTEGER;

END CasChunk.
```

**PROCEDURE Default (OUT p: Params)**
Пост: `p.min = 2048`, `p.mask = 8191`, `p.max = 65536`.

**PROCEDURE Cut (IN data: ARRAY OF BYTE; beg, end: INTEGER; IN p: Params): INTEGER**
Конец куска, который начинается в `beg`.

```
Пред: 0 <= beg < end <= LEN(data) — 20
Пред: window <= p.min <= p.max, p.mask + 1 — степень двойки — 21
Пост: beg < результат <= end; результат - beg <= p.max — 60
Пост: результат = end или результат - beg >= p.min
```

```
DEFINITION CasTree;

	IMPORT Files, CasHash, CasStore;

	CONST fan = 64;

	PROCEDURE PutStream (s: CasStore.Store; r: Files.Reader; OUT root: CasHash.Id;
		OUT res: INTEGER);
	PROCEDURE GetStream (s: CasStore.Store; IN root: CasHash.Id; w: Files.Writer;
		OUT res: INTEGER);

END CasTree.
```

**PROCEDURE PutStream (s: CasStore.Store; r: Files.Reader; OUT root: CasHash.Id; OUT res: INTEGER)**
Записывает поток от текущей позиции `r` до конца файла.

```
Пред: s # NIL — 20
Пред: r # NIL — 21
Пост: res = ok: root — корень; GetStream(root) даёт те же байты
Пост: res # ok — запись блока не удалась, root пуст
```

**PROCEDURE GetStream (s: CasStore.Store; IN root: CasHash.Id; w: Files.Writer; OUT res: INTEGER)**

```
Пред: s # NIL — 20
Пред: w # NIL — 21
Пост: res = ok: в w записаны байты, из которых PutStream построил root
Пост: res = absent или corrupt: в w может остаться начало содержимого
```

```
DEFINITION CasChange;

	IMPORT CasHash, CasStore;

	CONST conflict = 10; unrelated = 11;

	TYPE
		Name = ARRAY 64 OF CHAR;
		Note = ARRAY 256 OF CHAR;

		Entry = POINTER TO RECORD
			next-: Entry;
			name-: Name;
			id-: CasHash.Id
		END;

		Change = POINTER TO RECORD
			id-, parent-: CasHash.Id;
			author-: Name;
			time-: LONGINT;
			note-: Note;
			entries-: Entry;
			older-: Change;
			(c: Change) Set (IN name: ARRAY OF CHAR; IN id: CasHash.Id), NEW;
			(c: Change) Find (IN name: ARRAY OF CHAR; OUT id: CasHash.Id): BOOLEAN, NEW
		END;

	PROCEDURE New (IN parent: CasHash.Id; IN author: ARRAY OF CHAR; time: LONGINT;
		IN note: ARRAY OF CHAR): Change;
	PROCEDURE Put (s: CasStore.Store; c: Change; OUT id: CasHash.Id; OUT res: INTEGER);
	PROCEDURE Get (s: CasStore.Store; IN id: CasHash.Id; OUT c: Change; OUT res: INTEGER);
	PROCEDURE Commit (s: CasStore.Store; IN ref: ARRAY OF CHAR; c: Change; OUT id: CasHash.Id;
		OUT res: INTEGER);
	PROCEDURE Rebase (s: CasStore.Store; c: Change; IN head: CasHash.Id; OUT new: Change;
		OUT clash: Name; OUT res: INTEGER);
	PROCEDURE Log (s: CasStore.Store; IN head: CasHash.Id; OUT first: Change; OUT res: INTEGER);

END CasChange.
```

Записи набора идут по возрастанию имён, имена разные. Поле `id` пусто, пока набор не записан;
`older` заполняет `Log`.

**PROCEDURE New (IN parent: CasHash.Id; IN author: ARRAY OF CHAR; time: LONGINT; IN note: ARRAY OF CHAR): Change**

```
Пред: author короче 64 знаков — 20
Пред: note короче 256 знаков — 21
Пост: результат # NIL, entries = NIL, id пуст
```

**PROCEDURE (c: Change) Set (IN name: ARRAY OF CHAR; IN id: CasHash.Id)**

```
Пред: name из 1..63 знаков — 22
Пред: набор не записан (c.id пуст) — 23
Пост: Find(name) = id; прежнее значение имени заменено
```

**PROCEDURE (c: Change) Find (IN name: ARRAY OF CHAR; OUT id: CasHash.Id): BOOLEAN**
Пост: `TRUE` — набор задаёт имени `name` значение `id`; `FALSE` — имени нет, `id` пуст.

**PROCEDURE Put (s: CasStore.Store; c: Change; OUT id: CasHash.Id; OUT res: INTEGER)**

```
Пред: s # NIL — 20
Пред: c # NIL — 21
Пост: res = ok: id = c.id — Id блока набора; ссылки не тронуты
```

**PROCEDURE Get (s: CasStore.Store; IN id: CasHash.Id; OUT c: Change; OUT res: INTEGER)**

```
Пред: s # NIL — 20
Пост: res = ok: c.id = id, поля — как при Put
Пост: res = corrupt — блок не набор; absent, failed — от хранилища; c = NIL
```

**PROCEDURE Commit (s: CasStore.Store; IN ref: ARRAY OF CHAR; c: Change; OUT id: CasHash.Id; OUT res: INTEGER)**

```
Пред: s # NIL — 20
Пред: c # NIL — 21
Пред: CasStore.ValidRef(ref) — 22
Пост: res = ok: ref = id = c.id
Пост: res = conflict: ref # c.parent, ссылка не изменилась, блок набора записан
Пост: иначе — ошибка хранилища, ссылка не изменилась
```

**PROCEDURE Rebase (s: CasStore.Store; c: Change; IN head: CasHash.Id; OUT new: Change; OUT clash: Name; OUT res: INTEGER)**

```
Пред: s # NIL — 20
Пред: c # NIL — 21
Пост: res = ok: new.parent = head, записи, автор, время и пояснение — как у c; new не записан
Пост: res = conflict: набор после c.parent до head включительно дал имени clash другое значение; new = NIL
Пост: res = unrelated: c.parent нет в цепочке head; new = NIL
Пост: absent, corrupt, failed — ошибка чтения цепочки; new = NIL
```

**PROCEDURE Log (s: CasStore.Store; IN head: CasHash.Id; OUT first: Change; OUT res: INTEGER)**

```
Пред: s # NIL — 20
Пост: res = ok: first — набор head, older — его родитель, и так до набора без родителя; head пуст — first = NIL
Пост: иначе — ошибка чтения, first = NIL
```
