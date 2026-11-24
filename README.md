# Active BlackBox

Fork of [BlackBox Cross-Platform](https://github.com/bbcb/bbcp) (Component Pascal) that adds tasks with their
own stacks, Active Oberon style monitors and an `Async` library of channels. Based on the 64-bit GTK4 snapshot
DIA261123 by Ivan Denisov. Documentation is mostly in Russian.

Форк [BlackBox Cross-Platform](https://github.com/bbcb/bbcp) с задачами на собственных стеках, мониторами в
духе Active Oberon и библиотекой `Async`. Основа — 64-битный снимок DIA261123 Ивана Денисова (GTK4, исходники
в `.cp`, документация в `.md`).

## Что добавлено

- **Задачи** (`Tasks`) — кооперативные, каждая на своём стеке; планировщик встроен в главный цикл BlackBox.
  Сборщик мусора обходит стеки всех задач.
- **Мониторы** — расширение языка без новых ключевых слов: системный флаг `[exclusive]` у процедуры и
  псевдомодуль `ACTIVE` с процедурой `AWAIT(условие)`. Реализация — обычный модуль `Monitors`.
- **Точки уступки** — ключ компиляции `^` вставляет уступку в конец тела каждого цикла.
- **`Async`** — канал-встреча, ограниченный буфер, почтовый ящик, однократная переменная, выбор из
  нескольких ожиданий (`Select`), ввод-вывод без блокировки, рабочие нити, компутоны.
- **`Unit`** — каркас модульных тестов; тесты лежат в своих подсистемах как `<Подсистема>Tests<Тема>`.
- **`Demo`** — примеры из Active Oberon и Go и модель муравейника (`DemoAnts`) на 1000 задач.

Пример монитора:

```
MODULE DemoBuffer;
	IMPORT ACTIVE;

	TYPE Buffer* = POINTER TO RECORD n: INTEGER; ... END;

	PROCEDURE [exclusive] (b: Buffer) Put* (x: INTEGER), NEW;
	BEGIN
		ACTIVE.AWAIT(b.n < LEN(b.data));
		...
	END Put;
END DemoBuffer.
```

## Сборка и запуск

Нужен Linux x86-64; для окна — GTK4. Загрузочный компилятор `dev0Linux` лежит в корне репозитория.

```sh
tools/build64.sh                                  # собрать всё в .work/BB64LIN
echo 'DemoAnts.Run' | tools/run64.sh              # пример в консоли
cd .work/BB64LIN && LD_LIBRARY_PATH=. ./blackbox --BB_RUN=DemoAnts.Open   # окно
tools/test.sh                                     # тесты
```

32-битная сборка — `tools/build32.sh` и `tools/test32.sh`; Windows под wine — `tools/test-win64.sh`.

## Документация

| Файл | О чём |
|---|---|
| `Docu/ru/Async.md`, `Docu/Async.md` | библиотека и расширение языка |
| `Unit/Docu/ru/Testing.md` | соглашение о тестах |
| `Demo/Docu/Sys-Map.md` | список примеров |
| `docs/stand.md` | сборка, сценарии, состояние, откат ядра |
| `docs/tasks.md`, `docs/async-library.md` | устройство задач и библиотеки |
| `docs/kernel-plan.md` | что ещё нужно исправить в ядре |

## Состояние

Экспериментальная ветка. Задачи кооперативные и исполняются в одной нити ОС; рабочие нити не должны
обращаться к куче. Известные ограничения и план правок — в `docs/kernel-plan.md`.

## Лицензия

BSD 2-Clause, как у BlackBox Cross-Platform — см. `LICENSE.txt`.
