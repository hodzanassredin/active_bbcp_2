MODULE ActorPersist;
(** Персистентный актор: состояние — журнал событий и снимки.

	Обработчик команды не меняет состояние сам: он записывает событие
	(Persist), и только затем состояние меняет Apply. Apply — чистое
	изменение состояния без писем и записей; его же зовёт восстановление.
	Поэтому состояние после перезапуска равно состоянию до него: снимок
	(если есть) плюс события после снимка, применённые тем же Apply.

	Запись идёт через ActorJournal.Submit: журнал отвечает сразу (память,
	файл, Kv) или позже (журнал поверх Raft — после фиксации большинством).
	Ответил сразу — Persist применяет событие и зовёт Persisted до
	возврата, как синхронная запись. Ответит позже — Persist возвращается
	сразу, событие применяется, когда придёт ответ, и тогда же зовётся
	Persisted; до ответа актор не берёт новых писем: каждое пришедшее
	откладывается (ActorCore.Context.Stash) и после ответа возвращается в
	начало ящика. Отвечать на команду поэтому надо в Persisted, где
	c.Sender() — отправитель команды и состояние уже включает событие;
	код после Persist видит новое состояние только при ответе сразу.

	Восстановление идёт в обработке Started — и после создания, и после
	перезапуска супервизором (ActorCore.FinishRestart зовёт фабрику, затем
	Started): ActorJournal.Replay отдаёт снимок и события после него.
	Письма, пришедшие до конца восстановления, откладываются так же.
	Конец восстановления обработчик узнаёт письмом ReplayComplete.

	Отказы. Запись, получившая conflict (поток дописал кто-то другой),
	трапует conflictTrap; ошибка среды или неизвестный итог — storeTrap;
	кадр, который кодек не читает, — decodeTrap. Событие при этом не
	применено. Трап — отказ актора, его судьбу решает супервизор: Restart
	перечитывает журнал; отложенные письма перезапуск возвращает в ящик.

	Ответ журнала приходит письмом в ящик актора. Ящик с пределом
	(Props.capacity > 0) может его не принять — тогда актор ждёт вечно;
	журналу с ответом позже нужен ящик без предела.

	Свежесть. Актор с fresh = TRUE перед каждым пользовательским письмом
	спрашивает у журнала последний номер потока (SubmitLast) и, если
	поток ушёл вперёд, догоняет его чтением (Replay). Письмо ждёт ответа
	в отложенных. Так ответ, в том числе повтор команды, не может быть
	старше подтверждённой записи: у журнала поверх Raft Last идёт через
	ReadIndex, и отрезанная от большинства копия не отвечает вовсе
	(storeTrap). Цена — один запрос к журналу на письмо. *)

	IMPORT Stores, ActorCore, ActorJournal, ActorCodec;

	CONST
		conflictTrap* = 70;	(** ожидаемый номер не совпал: поток дописан другим *)
		storeTrap* = 71;	(** ошибка хранилища *)
		decodeTrap* = 72;	(** кадр не читается: тип не найден, версия вне допустимых *)
		(** переключатели мутаций — только для тестов *)
		mutNoStash* = 0;	(** письмо, пришедшее во время записи, обрабатывается, не дожидаясь ответа журнала *)
		mutNoFresh* = 1;	(** fresh: письмо обрабатывается без проверки свежести состояния *)
		mutKeepOnConflict* = 2;	(** конфликт журнала не останавливает актора: событие применяется и подтверждается *)
		mutNoCatchUp* = 3;	(** Last видит отставший поток, но догона нет: ответ по старому состоянию *)

		kEvent = 0; kSnap = 1; kReplay = 2; kLast = 3; kRefresh = 4;	(* вид запроса *)

	TYPE
		(** Событие. Поля пишет Externalize наследника; meta — из журнала. *)
		Event* = POINTER TO ABSTRACT RECORD (Stores.Store)
			meta-: ActorJournal.Meta
		END;

		ReplayComplete* = POINTER TO RECORD (ActorCore.Msg) END;

		Req = POINTER TO ReqDesc;

		(** Персистентный актор. journal = NIL — ActorJournal.dir. *)
		Actor* = POINTER TO ABSTRACT RECORD (ActorCore.Actor)
			seq-: LONGINT;	(** номер последнего применённого события *)
			fresh*: BOOLEAN;	(** перед каждым письмом сверять seq с последним номером потока (Last) и догонять: ответ не может быть старше подтверждённой записи *)
			checked-: BOOLEAN;	(** отложенное письмо уже прошло проверку свежести *)
			snapEvery*: INTEGER;	(** снимок каждые snapEvery событий; 0 — только по SaveSnapshot *)
			journal*: ActorJournal.Journal;
			stream*: ActorCore.Name;	(** поток журнала; "" — имя ящика c.name *)
			req: Req;	(* запрос у журнала без ответа; NIL — нет *)
			head, tail: Req;	(* запросы, ждущие за ним *)
			nev: INTEGER;	(* событий в req и в очереди *)
			recovering: BOOLEAN;
			spare: Req	(* отработавший запрос для повторного использования *)
		END;

		(* событие, принятое Replay до ответа *)
		Rec = POINTER TO RECORD m: ActorJournal.Meta; b: ActorJournal.Bytes; len: INTEGER; next: Rec END;

		ReqDesc = RECORD (ActorJournal.Request)
			a: Actor;
			self, sender: ActorCore.Ref;	(* кому и от чьего имени придёт ответ *)
			kind: INTEGER;
			lastSeq: LONGINT;	(* ответ SubmitLast *)
			e: Event; m: ActorJournal.Meta;
			data: ActorJournal.Bytes; len: INTEGER;
			calling, done, bad, hasSnap: BOOLEAN;	(* calling — идёт вызов журнала *)
			res: INTEGER;
			snapSeq: LONGINT; snap: ActorJournal.Bytes; snapLen: INTEGER;
			first, last: Rec;
			next: Req
		END;

		(* ответ журнала, пришедший позже *)
		Written = POINTER TO RECORD (ActorCore.Urgent) r: Req END;	(* мимо предела ящика *)

		(** Посетитель событий потока (читатель-представление, CQRS). *)
		Visitor* = POINTER TO ABSTRACT RECORD END;

		Counter = POINTER TO RECORD (Visitor) v: Visitor; last: LONGINT END;

		Replayer = POINTER TO RECORD (ActorJournal.Sink)
			v: Visitor; res: INTEGER
		END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)
		replayComplete: ReplayComplete;

	PROCEDURE (a: Actor) Apply* (e: Event), NEW, ABSTRACT;
	(** Пост: состояние a изменено событием e. Без писем, записей и трапов. *)

	PROCEDURE (a: Actor) Handle* (c: ActorCore.Context; m: ActorCore.Msg), NEW, ABSTRACT;
	(** Обработчик команд; первым получает ReplayComplete, Started не получает. *)

	PROCEDURE (a: Actor) Persisted* (c: ActorCore.Context; e: Event), NEW, EMPTY;
	(** После записи: e записано, применено, a.seq = e.meta.seq. c.Sender() —
		отправитель письма, обработчик которого записал e. Здесь отвечают на
		команду; можно звать Persist. *)

	PROCEDURE (a: Actor) Snapshot* (): Stores.Store, NEW, EXTENSIBLE;
	(** Пост: объект, чей кадр — снимок состояния. По умолчанию — сам актор. *)
	BEGIN
		RETURN a
	END Snapshot;

	PROCEDURE (a: Actor) Restore* (s: Stores.Store), NEW, EMPTY;
	(** Пост: состояние a взято из s — объекта, прочитанного из снимка. *)

	PROCEDURE (v: Visitor) Visit* (e: Event), NEW, ABSTRACT;
	(** Пост: событие e (с метаданными в e.meta) учтено. *)

	PROCEDURE (a: Actor) Busy* (): BOOLEAN, NEW;
	(** Пост: идёт восстановление или запись без ответа журнала — новые письма откладываются. *)
	BEGIN
		RETURN a.recovering OR (a.req # NIL)
	END Busy;

	PROCEDURE (a: Actor) S (c: ActorCore.Context; OUT s: ActorCore.Name), NEW;
	BEGIN
		IF a.stream # "" THEN s := a.stream$ ELSE s := c.name$ END
	END S;

	PROCEDURE (a: Actor) J (): ActorJournal.Journal, NEW;
	BEGIN
		IF a.journal # NIL THEN RETURN a.journal ELSE RETURN ActorJournal.dir END
	END J;

	PROCEDURE Copy (IN data: ARRAY OF BYTE; len: INTEGER): ActorJournal.Bytes;
		VAR b: ActorJournal.Bytes; i: INTEGER;
	BEGIN
		NEW(b, MAX(len, 1)); FOR i := 0 TO len - 1 DO b[i] := data[i] END;
		RETURN b
	END Copy;

	(* --- запрос к журналу ------------------------------------------------------ *)

	PROCEDURE (r: Req) Restore (seq: LONGINT; IN data: ARRAY OF BYTE; len: INTEGER), NEW;
		VAR s: Stores.Store; res: INTEGER;
	BEGIN
		ActorCodec.dir.Decode(data, len, s, res);
		IF res # ActorCodec.ok THEN r.bad := TRUE
		ELSE r.a.seq := seq; r.a.Restore(s)	(* Restore видит номер снимка, как до ADR 0026 *)
		END
	END Restore;

	PROCEDURE (r: Req) Replayed (IN m: ActorJournal.Meta; IN body: ARRAY OF BYTE; len: INTEGER), NEW;
		VAR s: Stores.Store; res: INTEGER;
	BEGIN
		ActorCodec.dir.Decode(body, len, s, res);
		IF (res # ActorCodec.ok) OR ~(s IS Event) THEN r.bad := TRUE
		ELSE s(Event).meta := m; r.a.Apply(s(Event)); r.a.seq := m.seq
		END
	END Replayed;

	PROCEDURE (r: Req) Event (IN m: ActorJournal.Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
	(* во время вызова журнала — применить сразу; позже — запомнить: актор применит по ответу *)
		VAR x: Rec;
	BEGIN
		IF r.bad THEN RETURN FALSE END;
		IF r.calling THEN r.Replayed(m, body, len)
		ELSE
			NEW(x); x.m := m; x.b := Copy(body, len); x.len := len;
			IF r.last = NIL THEN r.first := x ELSE r.last.next := x END;
			r.last := x
		END;
		RETURN ~r.bad
	END Event;

	PROCEDURE (r: Req) Snapshot (seq: LONGINT; IN data: ARRAY OF BYTE; len: INTEGER);
	BEGIN
		IF r.calling THEN r.Restore(seq, data, len)
		ELSE r.hasSnap := TRUE; r.snapSeq := seq; r.snap := Copy(data, len); r.snapLen := len
		END
	END Snapshot;

	PROCEDURE (r: Req) Done (res: INTEGER);
		VAR w: Written;
	BEGIN
		r.res := res; r.done := TRUE;
		IF ~r.calling THEN NEW(w); w.r := r; r.self.Post(w, r.sender) END
	END Done;

	PROCEDURE (r: Req) Last (seq: LONGINT);
	BEGIN
		r.lastSeq := seq
	END Last;

	PROCEDURE (a: Actor) NewReq (c: ActorCore.Context; kind: INTEGER): Req, NEW;
		VAR r: Req;
	BEGIN
		IF a.spare # NIL THEN r := a.spare; a.spare := NIL ELSE NEW(r) END;
		r.a := a; r.self := c; r.sender := c.Sender(); r.kind := kind;
		r.lastSeq := 0; r.e := NIL; r.data := NIL; r.len := 0;
		r.calling := FALSE; r.done := FALSE; r.bad := FALSE; r.hasSnap := FALSE; r.res := ActorJournal.ok;
		r.snap := NIL; r.first := NIL; r.last := NIL; r.next := NIL;
		RETURN r
	END NewReq;

	(* --- восстановление ----------------------------------------------------- *)

	PROCEDURE (a: Actor) Recovered (c: ActorCore.Context; r: Req), NEW;
	(* Ответ Replay: применить запомненное, сообщить обработчику ReplayComplete. *)
		VAR x: Rec;
	BEGIN
		a.req := NIL;
		IF r.hasSnap THEN r.Restore(r.snapSeq, r.snap^, r.snapLen) END;
		x := r.first;
		WHILE (x # NIL) & ~r.bad DO r.Replayed(x.m, x.b^, x.len); x := x.next END;
		IF r.res # ActorJournal.ok THEN HALT(storeTrap) END;
		IF r.bad THEN HALT(decodeTrap) END;
		a.recovering := FALSE;
		a.Handle(c, replayComplete)
	END Recovered;

	PROCEDURE (a: Actor) Recover (c: ActorCore.Context), NEW;
	(* Пост: Replay начат; ответил сразу — состояние = снимок + события после него. *)
		VAR r: Req; st: ActorCore.Name;
	BEGIN
		a.seq := 0; a.recovering := TRUE; a.head := NIL; a.tail := NIL; a.nev := 0;
		r := a.NewReq(c, kReplay); r.sender := NIL; a.req := r;
		a.S(c, st);
		r.calling := TRUE; a.J().Replay(st, r); r.calling := FALSE;
		IF r.done THEN a.Recovered(c, r) END
	END Recover;

	(* --- свежесть --------------------------------------------------------------- *)

	PROCEDURE^ (a: Actor) Refreshed (c: ActorCore.Context; r: Req), NEW;

	PROCEDURE (a: Actor) Checked (c: ActorCore.Context; r: Req), NEW;
	(* Ответ Last: если поток ушёл вперёд, догнать его чтением через журнал. *)
		VAR r2: Req; st: ActorCore.Name;
	BEGIN
		a.req := NIL;
		IF r.res # ActorJournal.ok THEN HALT(storeTrap) END;
		IF (r.lastSeq > a.seq) & ~(mutNoCatchUp IN mutations) THEN
			r2 := a.NewReq(c, kRefresh); a.req := r2;
			a.S(c, st); r2.calling := TRUE; a.J().Replay(st, r2); r2.calling := FALSE;
			IF r2.done THEN a.Refreshed(c, r2) END
		ELSE a.checked := TRUE
		END
	END Checked;

	PROCEDURE (a: Actor) Refreshed (c: ActorCore.Context; r: Req), NEW;
	(* Ответ Replay для догона: применить недостающие события, письмо — дальше. *)
		VAR x: Rec;
	BEGIN
		a.req := NIL;
		IF ~r.bad & r.hasSnap & (r.snapSeq > a.seq) THEN r.Restore(r.snapSeq, r.snap^, r.snapLen) END;
		x := r.first;
		WHILE (x # NIL) & ~r.bad DO
			IF x.m.seq > a.seq THEN r.Replayed(x.m, x.b^, x.len) END;
			x := x.next
		END;
		IF r.res # ActorJournal.ok THEN HALT(storeTrap) END;
		IF r.bad THEN HALT(decodeTrap) END;
		a.checked := TRUE
	END Refreshed;

	PROCEDURE (a: Actor) Check (c: ActorCore.Context), NEW;
	(* Начать проверку свежести; отложенное письмо вернётся после ответа. *)
		VAR r: Req; st: ActorCore.Name;
	BEGIN
		r := a.NewReq(c, kLast); a.req := r;
		a.S(c, st); r.calling := TRUE; a.J().SubmitLast(st, r); r.calling := FALSE;
		IF r.done THEN a.Checked(c, r) END
	END Check;

	(* --- запись ---------------------------------------------------------------- *)

	PROCEDURE^ (a: Actor) SaveSnapshot* (c: ActorCore.Context), NEW;

	PROCEDURE (a: Actor) Complete (c: ActorCore.Context; r: Req), NEW;
	(* Ответ журнала на запись r: применить событие и позвать Persisted; отказ — трап. *)
		VAR e: Event;
	BEGIN
		a.req := NIL;
		IF r.kind = kSnap THEN
			IF r.res # ActorJournal.ok THEN HALT(storeTrap) END
		ELSE
			DEC(a.nev);
			IF r.res = ActorJournal.conflict THEN
				IF mutKeepOnConflict IN mutations THEN r.res := ActorJournal.ok ELSE HALT(conflictTrap) END
			ELSIF r.res # ActorJournal.ok THEN HALT(storeTrap)
			END;
			e := r.e; e.meta := r.m; a.Apply(e); a.seq := r.m.seq;
			r.e := NIL; r.data := NIL; a.spare := r;
			a.Persisted(c, e);
			IF (a.snapEvery > 0) & (a.seq MOD a.snapEvery = 0) THEN a.SaveSnapshot(c) END
		END
	END Complete;

	PROCEDURE (a: Actor) Next (c: ActorCore.Context), NEW;
	(* Отдать журналу ждущие запросы по одному; ответивших сразу — завершить. *)
		VAR r: Req; st: ActorCore.Name; j: ActorJournal.Journal;
	BEGIN
		j := a.J();
		WHILE (a.req = NIL) & (a.head # NIL) DO
			r := a.head; a.head := r.next; IF a.head = NIL THEN a.tail := NIL END; r.next := NIL;
			a.req := r; a.S(c, st);
			r.calling := TRUE;
			IF r.kind = kSnap THEN j.SubmitSnapshot(st, r.m.seq, r.data^, r.len, r)
			ELSE j.Submit(st, r.m.seq - 1, r.m, r.data^, r.len, r)
			END;
			r.calling := FALSE;
			IF r.done THEN a.Complete(c, r) END
		END
	END Next;

	PROCEDURE (a: Actor) Enqueue (c: ActorCore.Context; r: Req), NEW;
	BEGIN
		IF a.tail = NIL THEN a.head := r ELSE a.tail.next := r END;
		a.tail := r;
		a.Next(c)
	END Enqueue;

	PROCEDURE (a: Actor) SaveSnapshot* (c: ActorCore.Context), NEW;
	(** Пред: вызов из обработчика a (не проверяется); a.seq > 0 — 20.
		Пост: снимок состояния после события seq отдан журналу после
		ждущих записей; отказ журнала — трап storeTrap (по его ответу). *)
		VAR r: Req;
	BEGIN
		ASSERT(a.seq > 0, 20);
		r := a.NewReq(c, kSnap); r.m.seq := a.seq;
		ActorCodec.dir.Encode(a.Snapshot(), r.data, r.len);
		a.Enqueue(c, r)
	END SaveSnapshot;

	PROCEDURE (a: Actor) Persist* (c: ActorCore.Context; e: Event; cause: LONGINT), NEW;
	(** Записать событие и применить его.
		Пред: вызов из обработчика a (не проверяется); e # NIL — 20.
		Пост: e отдано журналу в поток (stream или c.name) с номером
		seq + 1 + число событий, ещё ждущих ответа, автором — отправителем
		команды (c.Sender(); без отправителя — сам c.name), причиной cause и
		временем записи. По ответу журнала: a.Apply(e), seq увеличен,
		a.Persisted(c, e), снимок, если seq делится на snapEvery. Журнал
		ответил сразу — всё это до возврата из Persist. Поток дописан другим
		— трап conflictTrap, ошибка хранилища или неизвестный итог —
		storeTrap; в обоих случаях e не применено. *)
		VAR r: Req; from: ActorCore.Ref;
	BEGIN
		ASSERT(e # NIL, 20);
		r := a.NewReq(c, kEvent); r.e := e;
		r.m.seq := a.seq + 1 + a.nev; r.m.time := ActorJournal.Now(); r.m.cause := cause;
		from := c.Sender();
		IF from # NIL THEN r.m.author := from.name$ ELSE r.m.author := c.name$ END;
		ActorCodec.dir.Encode(e, r.data, r.len);
		INC(a.nev);
		a.Enqueue(c, r)
	END Persist;

	PROCEDURE (a: Actor) Receive* (c: ActorCore.Context; m: ActorCore.Msg), EXTENSIBLE;
		VAR r: Req;
	BEGIN
		IF m IS ActorCore.Started THEN a.Recover(c)
		ELSIF m IS Written THEN
			r := m(Written).r;
			IF (r = a.req) & r.done THEN	(* иначе — ответ прежнему воплощению *)
				IF r.kind = kReplay THEN a.Recovered(c, r)
				ELSIF r.kind = kLast THEN a.Checked(c, r)
				ELSIF r.kind = kRefresh THEN a.Refreshed(c, r)
				ELSE a.Complete(c, r)
				END;
				a.Next(c)
			END
		ELSIF (m IS ActorCore.Stopping) OR (m IS ActorCore.Stopped) OR (m IS ActorCore.Restarting) THEN
			a.Handle(c, m); RETURN
		ELSIF a.recovering OR a.Busy() & ~(mutNoStash IN mutations) THEN c.Stash; RETURN
		ELSIF a.fresh & ~a.checked & ~(mutNoFresh IN mutations) THEN	(* свежесть: Last, затем при необходимости догон *)
			c.Stash; a.Check(c)
		ELSE a.checked := FALSE; a.Handle(c, m)
		END;
		IF ~a.Busy() THEN c.Unstash END
	END Receive;

	(* --- читатель ------------------------------------------------------------- *)

	PROCEDURE (r: Replayer) Event (IN m: ActorJournal.Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
		VAR s: Stores.Store; res: INTEGER;
	BEGIN
		ActorCodec.dir.Decode(body, len, s, res);
		IF (res # ActorCodec.ok) OR ~(s IS Event) THEN r.res := ActorCodec.unknown; RETURN FALSE END;
		s(Event).meta := m; r.v.Visit(s(Event));
		RETURN TRUE
	END Event;

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
