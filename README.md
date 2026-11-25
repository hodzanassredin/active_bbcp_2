# BlackBox с задачами

BlackBox Component Builder 2.0 для Linux x86-64 с модулем `Tasks`: кооперативные задачи на
собственных стеках, монитор-объект `Tasks.Object`, пропуск `Tasks.Ctx`, группы и `Spawn`.
Исходники — UTF-8 `.cp`, документация — `.md`, окно — GTK4. Проект продолжает BlackBox
Cross-Platform и сохраняет его каркас, компилятор и консоль.

Язык Component Pascal не расширяется: новых ключевых слов, флагов процедур и псевдомодулей нет.
Задачи и мониторы — обычные модули на Component Pascal; программа, не импортирующая `Tasks`,
остаётся программой на Component Pascal.

```
MODULE Buffer;
	IMPORT Tasks;

	TYPE
		Buffer* = POINTER TO RECORD (Tasks.Object)
			n: INTEGER; data: ARRAY 8 OF INTEGER
		END;
		PutOp* = RECORD (Tasks.Op) x: INTEGER END;
		GetOp* = RECORD (Tasks.Op) x: INTEGER END;

	PROCEDURE (b: Buffer) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE b.n = LEN(b.data) DO IF ~b.Await(ctx, {0}) THEN RETURN END END;
			b.data[b.n] := op.x; INC(b.n)
		| op: GetOp DO
			WHILE b.n = 0 DO IF ~b.Await(ctx, {1}) THEN RETURN END END;
			DEC(b.n); op.x := b.data[b.n]
		END
	END Exclusive;

	PROCEDURE (b: Buffer) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF b.n < LEN(b.data) THEN INCL(s, 0) END;
		IF b.n > 0 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

END Buffer.
```

## Отличия от исходной системы

Это единственный раздел, где перечислены отличия; остальная документация описывает систему как
она есть.

Модуль `Tasks` даёт кооперативные задачи с собственным стеком. Планировщик работает в главном
цикле BlackBox; задачи можно отменять, объединять в группы, а тупик попадает в журнал. Код Component
Pascal исполняет одна нить ОС, поэтому задача отдаёт процессор только в точках переключения. В точке
ожидания задача переключается прямо на следующую готовую, без стека планировщика — до 64 раз
подряд, после чего управление возвращается в `Step`.

Монитор-объект `Tasks.Object` — обычная запись-наследник с методом `Exclusive-`; его зовёт
`Do`, передавая пропуск `Tasks.Ctx` и запрос `Tasks.Op`. Повторный вход и ожидание условия
(`Await`, `AwaitCond`) — в самом объекте; при трапе занятые мониторы отпускаются. Задание
(`Tasks.Job`) исполняет тело за один запуск, задачу (`Tasks.Task`) можно запускать повторно.

Ядро регистрирует стеки задач и сканирует их при сборке мусора; модуль, код которого есть в
каком-либо кадре, не выгружается. Периодическая сборка пропускается, если с прошлой не было
выделений. Ядро собирает мусор, когда с прошлой сборки выделено не меньше выжившего в ней и не
меньше 4 МБ, а не при каждой нехватке свободного блока.

Тесты лежат в подсистеме `Unit` и в `<Подсистема>_dev`. Ядро работает на Linux AMD64 и i386;
Windows x64 поддерживает задачи.

## Сборка и запуск

Нужен Linux x86-64; загрузочный компилятор `dev0Linux` лежит в корне репозитория. Для окна нужен
GTK4; консоль работает без него.

```sh
tools/build64.sh                                  # мир в .work/BB64LIN
echo 'System_devTasksObj.TestMutualExclusion' | tools/run64.sh
tools/test.sh                                     # модульные тесты
tools/test32.sh                                   # i386: сборка и тесты
```

Дополнительно: `tools/test-win64.sh` — Windows x64 под wine. Подробности и условия —
`docs/building.md`.

## Документация

| Файл | О чём |
|---|---|
| `Docu/ru/Tasks.md`, `Docu/Tasks.md` | руководство по модулю `Tasks` |
| `docs/implementation.md` | устройство ядра, `Tasks` и компилятора |
| `docs/building.md` | сборка, запуск, тесты, самосборка, возврат к исходному ядру |
| `docs/measurements.md` | замеры и команды их воспроизведения |
| `docs/gc-many-stacks.md` | сборка мусора при многих спящих задачах: причина квадратичного роста, общий буфер кандидатов, замеры до и после |
| `docs/kernel-plan.md` | план будущих правок ядра |
| `docs/stacks-study.md` | стеки задач: где уходит время, глубина стека, переключение и счётчики процессора |
| `docs/speed-main.md` | скорость задач в основной ветке: поставщики стеков, замеры до и после |
| `docs/monitor-object.md` | монитор-объект и пропуск в `Tasks`: сигнатуры, правила, примеры, замеры, расхождения |
| `docs/adr/README.md` | записи решений: что рассматривали, что отвергли и почему |
| `Unit/Docu/ru/Testing.md`, `Unit/Docu/Testing.md` | соглашение о тестах |
| `Obx/Docu/Sys-Map.md` | карта примеров |
| `AGENTS.md` | правила работы с репозиторием для агентов |

## Лицензия

BSD 2-Clause, как у BlackBox Cross-Platform — см. `LICENSE.txt`.
