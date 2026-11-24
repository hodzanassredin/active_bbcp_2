MODULE ActorPersist;
(** Персистентный актор: состояние — журнал событий и снимки.

	Обработчик команды не меняет состояние сам: он записывает событие
	(Persist), и только затем состояние меняет Apply. Apply — чистое
	изменение состояния без писем и записей; его же зовёт восстановление.
	Поэтому состояние после перезапуска равно состоянию до него: снимок
	(если есть) плюс события после снимка, применённые тем же Apply.

	Восстановление идёт в обработке Started — и после создания, и после
	перезапуска супервизором (ActorCore.FinishRestart зовёт фабрику, затем
	Started). Хранилище синхронное, точек переключения в восстановлении
	нет; письма, пришедшие тем временем, ждут в ящике. Конец восстановления
	обработчик узнаёт письмом ReplayComplete.

	Отказы. Persist, получивший conflict (поток дописал кто-то другой),
	трапует conflictTrap; ошибка среды — storeTrap; кадр, который кодек не
	читает, — decodeTrap. Событие при этом не применено. Трап — отказ
	актора, его судьбу решает супервизор: Restart перечитывает журнал. *)

	IMPORT Stores, ActorCore, ActorJournal, ActorCodec;

	CONST
		conflictTrap* = 70;	(** ожидаемый номер не совпал: поток дописан другим *)
		storeTrap* = 71;	(** ошибка хранилища *)
		decodeTrap* = 72;	(** кадр не читается: тип не найден, версия вне допустимых *)

	TYPE
		(** Событие. Поля пишет Externalize наследника; meta — из журнала. *)
		Event* = POINTER TO ABSTRACT RECORD (Stores.Store)
			meta-: ActorJournal.Meta
		END;

		ReplayComplete* = POINTER TO RECORD (ActorCore.Msg) END;

		(** Персистентный актор. journal = NIL — ActorJournal.dir. *)
		Actor* = POINTER TO ABSTRACT RECORD (ActorCore.Actor)
			seq-: LONGINT;	(** номер последнего применённого события *)
			snapEvery*: INTEGER;	(** снимок каждые snapEvery событий; 0 — только по SaveSnapshot *)
			journal*: ActorJournal.Journal
		END;

		(** Посетитель событий потока (читатель-представление, CQRS). *)
		Visitor* = POINTER TO ABSTRACT RECORD END;

		Counter = POINTER TO RECORD (Visitor) v: Visitor; last: LONGINT END;

		Replayer = POINTER TO RECORD (ActorJournal.Sink)
			a: Actor; v: Visitor; res: INTEGER
		END;

	VAR
		replayComplete: ReplayComplete;

	PROCEDURE (a: Actor) Apply* (e: Event), NEW, ABSTRACT;
	(** Пост: состояние a изменено событием e. Без писем, записей и трапов. *)

	PROCEDURE (a: Actor) Handle* (c: ActorCore.Context; m: ActorCore.Msg), NEW, ABSTRACT;
	(** Обработчик команд; первым получает ReplayComplete, Started не получает. *)

	PROCEDURE (a: Actor) Snapshot* (): Stores.Store, NEW, EXTENSIBLE;
	(** Пост: объект, чей кадр — снимок состояния. По умолчанию — сам актор. *)
	BEGIN
		RETURN a
	END Snapshot;

	PROCEDURE (a: Actor) Restore* (s: Stores.Store), NEW, EMPTY;
	(** Пост: состояние a взято из s — объекта, прочитанного из снимка. *)

	PROCEDURE (v: Visitor) Visit* (e: Event), NEW, ABSTRACT;
	(** Пост: событие e (с метаданными в e.meta) учтено. *)

	PROCEDURE (a: Actor) J (): ActorJournal.Journal, NEW;
	BEGIN
		IF a.journal # NIL THEN RETURN a.journal ELSE RETURN ActorJournal.dir END
	END J;

	PROCEDURE (r: Replayer) Event (IN m: ActorJournal.Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
		VAR s: Stores.Store; res: INTEGER;
	BEGIN
		ActorCodec.dir.Decode(body, len, s, res);
		IF (res # ActorCodec.ok) OR ~(s IS Event) THEN r.res := ActorCodec.unknown; RETURN FALSE END;
		s(Event).meta := m;
		IF r.a # NIL THEN r.a.Apply(s(Event)); r.a.seq := m.seq ELSE r.v.Visit(s(Event)) END;
		RETURN TRUE
	END Event;

	PROCEDURE (a: Actor) Recover (c: ActorCore.Context), NEW;
	(* Пост: состояние = снимок + события после него; seq — последний номер. *)
		VAR j: ActorJournal.Journal; data: ActorJournal.Bytes; len, res: INTEGER; s: Stores.Store;
			r: Replayer;
	BEGIN
		j := a.J(); a.seq := 0;
		j.LoadSnapshot(c.name, a.seq, data, len, res);
		IF res = ActorJournal.ok THEN
			ActorCodec.dir.Decode(data, len, s, res);
			IF res # ActorCodec.ok THEN HALT(decodeTrap) END;
			a.Restore(s)
		ELSIF res # ActorJournal.absent THEN HALT(storeTrap)
		END;
		NEW(r); r.a := a; r.res := ActorCodec.ok;
		j.Read(c.name, a.seq + 1, r, res);
		IF res # ActorJournal.ok THEN HALT(storeTrap) END;
		IF r.res # ActorCodec.ok THEN HALT(decodeTrap) END
	END Recover;

	PROCEDURE (a: Actor) Receive* (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF m IS ActorCore.Started THEN a.Recover(c); a.Handle(c, replayComplete)
		ELSE a.Handle(c, m)
		END
	END Receive;

	PROCEDURE (a: Actor) SaveSnapshot* (c: ActorCore.Context), NEW;
	(** Пред: вызов из обработчика a (не проверяется); a.seq > 0 — 20.
		Пост: снимок состояния после события seq сохранён; ошибка — трап storeTrap. *)
		VAR data: ActorJournal.Bytes; len, res: INTEGER;
	BEGIN
		ASSERT(a.seq > 0, 20);
		ActorCodec.dir.Encode(a.Snapshot(), data, len);
		a.J().SaveSnapshot(c.name, a.seq, data, len, res);
		IF res # ActorJournal.ok THEN HALT(storeTrap) END
	END SaveSnapshot;

	PROCEDURE (a: Actor) Persist* (c: ActorCore.Context; e: Event; cause: LONGINT), NEW;
	(** Записать событие и применить его.
		Пред: вызов из обработчика a (не проверяется); e # NIL — 20.
		Пост: e записано в поток c.name с номером seq + 1, автором c.name,
		причиной cause и временем записи; затем a.Apply(e); seq увеличен;
		снимок, если seq делится на snapEvery. Поток дописан другим — трап
		conflictTrap, ошибка хранилища — storeTrap; в обоих случаях e не
		применено. Точек переключения нет. *)
		VAR data: ActorJournal.Bytes; len, res: INTEGER; m: ActorJournal.Meta;
	BEGIN
		ASSERT(e # NIL, 20);
		m.seq := a.seq + 1; m.time := ActorJournal.Now(); m.cause := cause; m.author := c.name$;
		ActorCodec.dir.Encode(e, data, len);
		a.J().Append(c.name, a.seq, m, data, len, res);
		IF res = ActorJournal.conflict THEN HALT(conflictTrap)
		ELSIF res # ActorJournal.ok THEN HALT(storeTrap)
		END;
		e.meta := m; a.Apply(e); a.seq := m.seq;
		IF (a.snapEvery > 0) & (a.seq MOD a.snapEvery = 0) THEN a.SaveSnapshot(c) END
	END Persist;

	PROCEDURE (c: Counter) Visit (e: Event);
	BEGIN
		c.v.Visit(e); c.last := e.meta.seq
	END Visit;

	PROCEDURE Read* (j: ActorJournal.Journal; IN stream: ARRAY OF CHAR; from: LONGINT; v: Visitor;
		OUT last: LONGINT; OUT res: INTEGER);
	(** События потока для читателя (CQRS): каждое событие с номером >= from
		разобрано и отдано v.Visit по порядку.
		Пред: j # NIL — 20; v # NIL — 21.
		Пост: res = ActorJournal.ok — last — номер последнего отданного события
		(from - 1, если новых нет); иначе res — ошибка журнала или
		ActorJournal.corrupt (кадр не читается), last — номер последнего
		удачного. *)
		VAR r: Replayer; c: Counter;
	BEGIN
		ASSERT(j # NIL, 20); ASSERT(v # NIL, 21);
		NEW(c); c.v := v; c.last := from - 1;
		NEW(r); r.v := c; r.res := ActorCodec.ok;
		j.Read(stream, from, r, res);
		last := c.last;
		IF (res = ActorJournal.ok) & (r.res # ActorCodec.ok) THEN res := ActorJournal.corrupt END
	END Read;

BEGIN
	NEW(replayComplete)
END ActorPersist.
