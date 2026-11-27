MODULE ActorEvent;
(** Поток событий: подписки в пределах системы.

	Поток — актор; его ящик принимает письма для публикации, а также
	служебные Subscribe и Unsubscribe. Подписчик — адрес и
	необязательный предикат Filter: письмо уходит подписчику, если
	предиката нет или Match вернул TRUE. Publisher и подписчик видят
	одно и то же письмо; отправитель, с которым письмо пришло потоку,
	сохраняется.

	Поток наблюдает подписчиков (Watch): остановленный подписчик
	снимается из списка по Terminated сам.

	Мёртвые письма. ActorEvent.SetDefault(s) ставит поток приёмником
	мёртвых писем ActorCore: каждое письмо, не дошедшее до адресата,
	публикуется в s как DeadLetter с исходным письмом, адресатом и
	отправителем. Мёртвое письмо о самом DeadLetter не публикуется, а
	вложенное мёртвое письмо во время вызова приёмника не учитывается:
	иначе остановленный поток замкнул бы круг.

	Публикация не выделяет памяти: Publish — это Tell потоку, а сам
	поток только ставит письмо в ящики подписчиков. Выделяются лишь
	Subscribe, Unsubscribe и DeadLetter (мёртвый путь). *)

	IMPORT ActorCore;

	TYPE
		Msg = ActorCore.Msg;
		Ref = ActorCore.Ref;

		(** Предикат подписки. *)
		Filter* = POINTER TO ABSTRACT RECORD END;

		(** Событие мёртвого письма: m не дошло до to (to = NIL — адреса
			нет), отправитель — sender. *)
		DeadLetter* = POINTER TO RECORD (Msg)
			m-: Msg;
			to-, sender-: Ref
		END;

		Subscriber = POINTER TO RECORD r: Ref; f: Filter; next: Subscriber END;

		Stream = POINTER TO RECORD (ActorCore.Actor) sub: Subscriber END;

		SubMsg = POINTER TO RECORD (Msg) r: Ref; f: Filter END;
		UnsubMsg = POINTER TO RECORD (Msg) r: Ref END;

		Hook = POINTER TO RECORD (ActorCore.DeadHook) stream: Ref END;

	VAR
		def: Ref;
		hook: Hook;

	PROCEDURE (f: Filter) Match* (m: Msg): BOOLEAN, NEW, ABSTRACT;
	(** Пост: TRUE — письмо m доставить подписчику. *)

	(* --- подписки --------------------------------------------------------- *)

	PROCEDURE Find (a: Stream; r: Ref): Subscriber;
		VAR s: Subscriber;
	BEGIN
		s := a.sub; WHILE (s # NIL) & (s.r # r) DO s := s.next END;
		RETURN s
	END Find;

	PROCEDURE Add (a: Stream; r: Ref; f: Filter);
		VAR s: Subscriber;
	BEGIN
		s := Find(a, r);
		IF s = NIL THEN
			NEW(s); s.r := r; s.f := f; s.next := a.sub; a.sub := s
		ELSE s.f := f
		END
	END Add;

	PROCEDURE Remove (a: Stream; r: Ref);
		VAR s, p: Subscriber;
	BEGIN
		p := NIL; s := a.sub;
		WHILE (s # NIL) & (s.r # r) DO p := s; s := s.next END;
		IF s # NIL THEN
			IF p = NIL THEN a.sub := s.next ELSE p.next := s.next END
		END
	END Remove;

	PROCEDURE Send (a: Stream; m: Msg; sender: Ref);
		VAR s: Subscriber;
	BEGIN
		s := a.sub;
		WHILE s # NIL DO
			IF (s.f = NIL) OR s.f.Match(m) THEN s.r.Post(m, sender) END;
			s := s.next
		END
	END Send;

	PROCEDURE (a: Stream) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: SubMsg DO
			Add(a, m.r, m.f); c.Watch(m.r)
		| m: UnsubMsg DO
			Remove(a, m.r); c.Unwatch(m.r)
		| m: ActorCore.Terminated DO
			Remove(a, m.who)
		ELSE Send(a, m, c.Sender())
		END
	END Receive;

	PROCEDURE NewStream (): ActorCore.Actor;
		VAR a: Stream;
	BEGIN
		NEW(a); RETURN a
	END NewStream;

	(* --- приёмник мёртвых писем ------------------------------------------- *)

	PROCEDURE (h: Hook) Dead (m: Msg; sender, to: Ref);
		VAR d: DeadLetter;
	BEGIN
		IF m IS DeadLetter THEN RETURN END;	(* мёртвое событие не плодит событий *)
		NEW(d); d.m := m; d.to := to; d.sender := sender;
		h.stream.Post(d, NIL)
	END Dead;

	(* --- интерфейс -------------------------------------------------------- *)

	PROCEDURE New* (): Ref;
	(** Пост: новый поток-корень без подписчиков; приёмником мёртвых писем
		он не становится (см. SetDefault). *)
	BEGIN
		RETURN ActorCore.Spawn(NIL, ActorCore.NewProps(NewStream), "")
	END New;

	PROCEDURE Subscribe* (s, r: Ref; f: Filter);
	(** Пред: s # NIL (трап 20), r # NIL (21).
		Пост: письмо потоку: r подписан с предикатом f (NIL — все письма);
		повторная подписка того же r заменяет предикат. *)
		VAR m: SubMsg;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(r # NIL, 21);
		NEW(m); m.r := r; m.f := f; ActorCore.Tell(s, m)
	END Subscribe;

	PROCEDURE Unsubscribe* (s, r: Ref);
	(** Пред: s # NIL (трап 20), r # NIL (21).
		Пост: письмо потоку: r больше не подписан. *)
		VAR m: UnsubMsg;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(r # NIL, 21);
		NEW(m); m.r := r; ActorCore.Tell(s, m)
	END Unsubscribe;

	PROCEDURE Publish* (s: Ref; m: Msg);
	(** Пред: s # NIL (трап 20), m # NIL (21).
		Пост: письмо m ушло потоку; подписчики получат его в порядке ящика
		потока, с отправителем письма потоку. Поток остановлен — m мёртвое. *)
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(m # NIL, 21);
		ActorCore.Tell(s, m)
	END Publish;

	PROCEDURE SetDefault* (s: Ref);
	(** Пост: поток s — приёмник мёртвых писем ActorCore; s = NIL —
		приёмника нет. *)
	BEGIN
		def := s;
		IF s = NIL THEN ActorCore.SetDeadHook(NIL)
		ELSE NEW(hook); hook.stream := s; ActorCore.SetDeadHook(hook)
		END
	END SetDefault;

	PROCEDURE Default* (): Ref;
	(** Пост: поток, поставленный приёмником мёртвых писем; NIL — нет. *)
	BEGIN
		RETURN def
	END Default;

END ActorEvent.
