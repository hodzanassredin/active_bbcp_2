MODULE DemoReaderWriter;
(** Читатели и писатели.

	Перенесено из книги: P. Reali, Active Oberon Language Report,
	ETH Zurich, 27.10.2004, приложение B.1 "Readers and Writers".
	Источник: https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf
	Оригинал:
		PROCEDURE EnterReader*; BEGIN {EXCLUSIVE} AWAIT(n >= 0); INC(n) END;
		PROCEDURE EnterWriter*; BEGIN {EXCLUSIVE} AWAIT(n = 0); DEC(n) END;
	n > 0 — читатели внутри, n < 0 — писатель. Комментарии-инварианты
	оригинала сохранены. Здесь входы и выходы — операция Op над
	монитором-объектом RW; вход ждёт флага, выход меняет n.

	Команда DemoReaderWriter.Run: 3 читателя по 5 входов и 2 писателя по
	3 входа. Итог детерминирован: 15 чтений, 6 записей, 0 нарушений
	взаимного исключения. *)

	IMPORT Tasks, DemoSupport;

	CONST
		readers = 3;
		writers = 2;
		readsPer = 5;
		writesPer = 3;

		(* вид операции *)
		enterReader = 0; exitReader = 1; enterWriter = 2; exitWriter = 3;
		readersMayEnter = 0;	(* флаг Ready: n >= 0 *)
		writerMayEnter = 1;	(* флаг Ready: n = 0 *)

	TYPE
		RwOp = RECORD (Tasks.Op) kind: INTEGER END;

		RW* = POINTER TO RECORD (Tasks.Object)
			n: LONGINT	(* n = 0 пусто; n < 0 писателей; n > 0 читателей *)
		END;

		Reader = POINTER TO RECORD (Tasks.Job) rw: RW END;
		Writer = POINTER TO RECORD (Tasks.Job) rw: RW; id: INTEGER END;

	VAR
		Data*: INTEGER;		(* защищаемые данные *)
		Reads*: INTEGER;
		Writes*: INTEGER;
		Bad*: INTEGER;		(* видели чужой вид деятельности внутри *)
		Failed*: BOOLEAN;

		readersInside, writersInside: INTEGER;

	PROCEDURE (rw: RW) Ready* (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF rw.n >= 0 THEN INCL(s, readersMayEnter) END;
		IF rw.n = 0 THEN INCL(s, writerMayEnter) END;
		RETURN s
	END Ready;

	PROCEDURE (rw: RW) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: RwOp DO
			CASE op.kind OF
			| enterReader:
				WHILE rw.n < 0 DO
					IF ~rw.Await(ctx, {readersMayEnter}) THEN RETURN END
				END;
				INC(rw.n)
			| exitReader:
				DEC(rw.n)
			| enterWriter:
				WHILE rw.n # 0 DO
					IF ~rw.Await(ctx, {writerMayEnter}) THEN RETURN END
				END;
				DEC(rw.n)
			| exitWriter:
				INC(rw.n)
			END
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE Enter (rw: RW; kind: INTEGER; VAR ctx: Tasks.Ctx);
		VAR op: RwOp;
	BEGIN
		op.kind := kind; rw.Do(ctx, op)
	END Enter;

	PROCEDURE (r: Reader) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO readsPer DO
			Enter(r.rw, enterReader, ctx);
			INC(readersInside);
			IF writersInside > 0 THEN INC(Bad) END;
			Tasks.Sleep(1);
			DEC(readersInside);
			Enter(r.rw, exitReader, ctx);
			INC(Reads)
		END
	END Body;

	PROCEDURE (w: Writer) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO writesPer DO
			Enter(w.rw, enterWriter, ctx);
			INC(writersInside);
			IF readersInside > 0 THEN INC(Bad) END;
			Tasks.Sleep(1);
			Data := w.id * 100 + i;
			DEC(writersInside);
			Enter(w.rw, exitWriter, ctx);
			INC(Writes)
		END
	END Body;

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
