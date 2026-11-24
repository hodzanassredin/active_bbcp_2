# Обзор на примерах: DemoReaderWriter

Читатели и писатели.

Многие задачи читают общие данные, некоторые их пишут. Читать вместе можно, писать — только в
одиночку. Пример переносит решение из P. Reali, *Active Oberon Language Report* (приложение B.1
«Readers and Writers»;
<https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf>).

```
MODULE DemoReaderWriter;
(** Читатели и писатели.

	Перенесено из книги: P. Reali, Active Oberon Language Report,
	ETH Zurich, 27.10.2004, приложение B.1 "Readers and Writers".
	Источник: https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf
	Оригинал:
		PROCEDURE EnterReader*; BEGIN {EXCLUSIVE} AWAIT(n >= 0); INC(n) END;
		PROCEDURE EnterWriter*; BEGIN {EXCLUSIVE} AWAIT(n = 0); DEC(n) END;
	n > 0 — читатели внутри, n < 0 — писатель. Комментарии-инварианты
	оригинала сохранены.

	Команда DemoReaderWriter.Run: 3 читателя по 5 входов и 2 писателя по
	3 входа. Итог детерминирован: 15 чтений, 6 записей, 0 нарушений
	взаимного исключения. *)

	IMPORT ACTIVE, Tasks, DemoSupport;

	CONST
		readers = 3;
		writers = 2;
		readsPer = 5;
		writesPer = 3;

	TYPE
		RW* = POINTER TO RECORD
			n: LONGINT	(* n = 0 пусто; n < 0 писателей; n > 0 читателей *)
		END;

		Reader = POINTER TO RECORD (Tasks.Task) rw: RW END;
		Writer = POINTER TO RECORD (Tasks.Task) rw: RW; id: INTEGER END;

	VAR
		Data*: INTEGER;		(* защищаемые данные *)
		Reads*: INTEGER;
		Writes*: INTEGER;
		Bad*: INTEGER;		(* видели чужой вид деятельности внутри *)
		Failed*: BOOLEAN;

		readersInside, writersInside: INTEGER;

	PROCEDURE [exclusive] (rw: RW) EnterReader*, NEW;
	BEGIN
		ACTIVE.AWAIT(rw.n >= 0);
		INC(rw.n)
	END EnterReader;

	PROCEDURE [exclusive] (rw: RW) ExitReader*, NEW;
	BEGIN
		DEC(rw.n)
	END ExitReader;

	PROCEDURE [exclusive] (rw: RW) EnterWriter*, NEW;
	BEGIN
		ACTIVE.AWAIT(rw.n = 0);
		DEC(rw.n)
	END EnterWriter;

	PROCEDURE [exclusive] (rw: RW) ExitWriter*, NEW;
	BEGIN
		INC(rw.n)
	END ExitWriter;

	PROCEDURE (r: Reader) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO readsPer DO
			r.rw.EnterReader;
			INC(readersInside);
			IF writersInside > 0 THEN INC(Bad) END;
			Tasks.Sleep(1);
			DEC(readersInside);
			r.rw.ExitReader;
			INC(Reads)
		END
	END Run;

	PROCEDURE (w: Writer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO writesPer DO
			w.rw.EnterWriter;
			INC(writersInside);
			IF readersInside > 0 THEN INC(Bad) END;
			Tasks.Sleep(1);
			Data := w.id * 100 + i;
			DEC(writersInside);
			w.rw.ExitWriter;
			INC(Writes)
		END
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: 3 читателя и 2 писателя. *)
		VAR rw: RW; r: ARRAY readers OF Reader; w: ARRAY writers OF Writer;
			i: INTEGER; ok: BOOLEAN;
	BEGIN
		Data := 0; Reads := 0; Writes := 0; Bad := 0; Failed := FALSE;
		readersInside := 0; writersInside := 0;
		NEW(rw);
		FOR i := 0 TO readers - 1 DO
			NEW(r[i]); r[i].rw := rw; Tasks.Start(r[i])
		END;
		FOR i := 0 TO writers - 1 DO
			NEW(w[i]); w[i].rw := rw; w[i].id := i + 1; Tasks.Start(w[i])
		END;
		ok := DemoSupport.Pump(15000);
		FOR i := 0 TO readers - 1 DO
			IF Tasks.Failed(r[i]) THEN Failed := TRUE END
		END;
		FOR i := 0 TO writers - 1 DO
			IF Tasks.Failed(w[i]) THEN Failed := TRUE END
		END;
		Failed := Failed OR ~ok;
		DemoSupport.LogInt("DemoReaderWriter: reads=", Reads);
		DemoSupport.LogInt(" writes=", Writes);
		DemoSupport.LogInt(" bad=", Bad);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoReaderWriter.
```

Модуль собирает `tools/build64.sh` (исходник — `Demo/Mod/ReaderWriter.cp`; подсистема `Demo` — только для
amd64). В окне BlackBox команду исполняет коммандер:

```
   DemoReaderWriter.Run
```

В консоли:

```sh
echo 'DemoReaderWriter.Run' | tools/run64.sh
```

В журнале появится:

```
DemoReaderWriter: reads=15 writes=6 bad=0
```

## Что произошло

Состояние монитора — одно число `n`: 0 — внутри никого, `n > 0` — внутри `n` читателей,
`n < 0` — писатель. `EnterReader` ждёт `n >= 0` и увеличивает `n`, `EnterWriter` ждёт `n = 0` и
уменьшает его. Сами чтение и запись идут вне монитора, поэтому читатели действительно работают
вместе.

Пример добавляет проверку вокруг критической секции: читатель, увидевший внутри писателя, и
писатель, увидевший читателя, увеличивают `Bad`. Три читателя делают по пять проходов, два
писателя — по три; итог — 15 чтений, 6 записей и `bad=0`.

## Тест

`Demo_devExamples.TestProcReadersWriters` проверяет 15 чтений, 6 записей, отсутствие пересечения и трапов.

В этом примере мы увидели монитор, который охраняет не данные, а право доступа к ним, и
проверку взаимного исключения счётчиками.
