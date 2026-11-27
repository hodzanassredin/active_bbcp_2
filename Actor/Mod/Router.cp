MODULE ActorRouter;
(** Маршрутизаторы: письмо — одному маршруту или всем.

	Маршрутизатор — актор. Его ящик принимает письма и пересылает их
	маршрутам с прежним отправителем, сам он писем не обрабатывает.
	Набор маршрутов задаётся двумя способами: пул — маршрутизатор сам
	рождает n детей фабрикой, и группа — адреса заданы снаружи. Смена
	набора приходит письмом (SetGroup, SetCount); уменьшение пула
	останавливает лишних детей.

	Способ выбора маршрута:
	- RoundRobin — по кругу;
	- Random — по генератору от зерна: то же зерно и те же вызовы дают
	  ту же последовательность;
	- Broadcast — письмо всем маршрутам;
	- ConsistentHash — по ключу письма: вес пары «имя маршрута, ключ»
	  считает ActorHash, выбирается наибольший вес (тот же рандеву-хеш,
	  что у ActorGrain). Письмо без ключа идёт по пустому ключу.

	Пул под надзором: стратегия маршрутизатора (Props.strategy) решает
	судьбу отказавшего ребёнка. Перезапуск оставляет адрес ребёнка тем
	же, поэтому маршрут не меняется. Остановленный маршрут (директива
	Stop или StopActor) снимается из набора: маршрутизатор наблюдает
	детей и по Terminated убирает адрес. В пустом наборе письмо
	пропадает.

	Выделений на письмо нет: массив маршрутов создан при рождении
	маршрутизатора, пересылка только ставит письмо в ящик. *)

	IMPORT ActorCore, ActorHash;

	CONST
		(** способ выбора маршрута *)
		roundRobin* = 0;
		random* = 1;
		broadcast* = 2;
		consistentHash* = 3;
		maxRoutes* = 64;
		hashKind = "router";	(* вид в рандеву-хеше; ключ — имя маршрута *)

	TYPE
		Ref = ActorCore.Ref;
		Msg = ActorCore.Msg;

		(** Письмо с ключом для ConsistentHash: наследник заполняет key. *)
		Keyed* = POINTER TO EXTENSIBLE RECORD (Msg)
			key*: ActorCore.Name
		END;

		GroupMsg = POINTER TO RECORD (Msg)
			refs: POINTER TO ARRAY OF Ref
		END;

		CountMsg = POINTER TO RECORD (Msg) n: INTEGER END;
		KindMsg = POINTER TO RECORD (Msg) kind: INTEGER END;

		Router = POINTER TO RECORD (ActorCore.Actor)
			routes: POINTER TO ARRAY OF Ref;
			n, idx, kind, count: INTEGER;
			seed: LONGINT;
			factory: ActorCore.Factory;
			pool: BOOLEAN
		END;

	VAR
		(* настройка, из которой фабрика рождает маршрутизатор: Spawn зовёт
		   фабрику сразу, поэтому глобалы живут только внутри NewPool и NewGroup *)
		newRefs: POINTER TO ARRAY OF Ref;
		newN, newKind, newCount: INTEGER;
		newSeed: LONGINT;
		newFactory: ActorCore.Factory;
		newPool: BOOLEAN;

	(* --- рандеву-хеш ------------------------------------------------------- *)

	PROCEDURE HashOwner (r: Router; IN key: ActorCore.Name): INTEGER;
	(* Пост: индекс маршрута с наибольшим весом для key; при равном весе —
		меньшее имя; -1 — маршрутов нет. *)
		VAR i, best: INTEGER; w, bw: LONGINT;
	BEGIN
		best := -1; bw := -1;
		FOR i := 0 TO r.n - 1 DO
			w := ActorHash.Weight(r.routes[i].name, hashKind, key);
			IF (w > bw) OR (w = bw) & (r.routes[i].name < r.routes[best].name) THEN best := i; bw := w END
		END;
		RETURN best
	END HashOwner;

	PROCEDURE Pick (r: Router; m: Msg): INTEGER;
	(* Пост: индекс маршрута по правилу r.kind; -1 — маршрутов нет. *)
		VAR key: ActorCore.Name;
	BEGIN
		IF r.n = 0 THEN RETURN -1 END;
		IF r.kind = consistentHash THEN
			IF m IS Keyed THEN key := m(Keyed).key ELSE key := "" END;
			RETURN HashOwner(r, key)
		ELSIF r.kind = random THEN
			r.seed := r.seed * 6364136223846793005 + 1442695040888963407;	(* LCG Кнута, как в ActorNetMem *)
			RETURN SHORT(ASH(r.seed, -33) MOD r.n)
		ELSE
			INC(r.idx);
			IF r.idx >= r.n THEN r.idx := 0 END;
			RETURN r.idx
		END
	END Pick;

	(* --- пересылка --------------------------------------------------------- *)

	PROCEDURE Forward (r: Router; m: Msg; s: Ref);
		VAR i, k: INTEGER;
	BEGIN
		IF r.n = 0 THEN RETURN END;
		IF r.kind = broadcast THEN
			FOR i := 0 TO r.n - 1 DO r.routes[i].Post(m, s) END
		ELSE
			k := Pick(r, m);
			IF k >= 0 THEN r.routes[k].Post(m, s) END
		END
	END Forward;

	PROCEDURE RemoveRoute (r: Router; who: Ref);
		VAR i, j: INTEGER;
	BEGIN
		i := 0; WHILE (i < r.n) & (r.routes[i] # who) DO INC(i) END;
		IF i < r.n THEN
			FOR j := i TO r.n - 2 DO r.routes[j] := r.routes[j + 1] END;
			DEC(r.n); r.routes[r.n] := NIL;
			IF r.idx >= r.n THEN r.idx := -1 END
		END
	END RemoveRoute;

	PROCEDURE WatchAll (c: ActorCore.Context; r: Router);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO r.n - 1 DO c.Watch(r.routes[i]) END
	END WatchAll;

	PROCEDURE UnwatchAll (c: ActorCore.Context; r: Router);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO r.n - 1 DO c.Unwatch(r.routes[i]) END
	END UnwatchAll;

	PROCEDURE SetRoutes (c: ActorCore.Context; r: Router;
		refs: POINTER TO ARRAY OF Ref; n: INTEGER);
	(* Пост: набор маршрутов — refs[0 .. n-1]; наблюдение прежних снято,
		новые наблюдаются; счёт круга сброшен. *)
		VAR i: INTEGER;
	BEGIN
		UnwatchAll(c, r);
		r.n := MIN(n, maxRoutes); r.idx := -1;
		FOR i := 0 TO r.n - 1 DO r.routes[i] := refs[i] END;
		FOR i := r.n TO maxRoutes - 1 DO r.routes[i] := NIL END;
		WatchAll(c, r)
	END SetRoutes;

	PROCEDURE Resize (c: ActorCore.Context; r: Router; n: INTEGER);
	(* Пост: в пуле n детей; лишние остановлены, недостающие рождены и
		наблюдаются. *)
		VAR i: INTEGER; x: Ref;
	BEGIN
		IF n < 0 THEN n := 0 END;
		IF n > maxRoutes THEN n := maxRoutes END;
		FOR i := n TO r.n - 1 DO c.Unwatch(r.routes[i]); ActorCore.StopActor(r.routes[i]); r.routes[i] := NIL END;
		FOR i := r.n TO n - 1 DO
			x := ActorCore.Spawn(c, ActorCore.NewProps(r.factory), "");
			IF x # NIL THEN r.routes[i] := x; c.Watch(x) END
		END;
		r.n := n; r.idx := -1
	END Resize;

	PROCEDURE (a: Router) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: GroupMsg DO SetRoutes(c, a, m.refs, LEN(m.refs^))
		| m: CountMsg DO IF a.pool & (a.factory # NIL) THEN Resize(c, a, m.n) END
		| m: KindMsg DO
			IF (m.kind >= roundRobin) & (m.kind <= consistentHash) THEN a.kind := m.kind; a.idx := -1 END
		| m: ActorCore.Started DO
			IF a.pool & (a.factory # NIL) THEN Resize(c, a, a.count) ELSE WatchAll(c, a) END
		| m: ActorCore.Terminated DO RemoveRoute(a, m.who)
		ELSE Forward(a, m, c.Sender())
		END
	END Receive;

	PROCEDURE NewRouter (): ActorCore.Actor;
		VAR a: Router; i: INTEGER;
	BEGIN
		NEW(a); a.kind := newKind; a.seed := newSeed; a.pool := newPool;
		a.factory := newFactory; a.count := newCount; a.idx := -1;
		NEW(a.routes, maxRoutes);
		IF newRefs # NIL THEN
			a.n := MIN(newN, maxRoutes);
			FOR i := 0 TO a.n - 1 DO a.routes[i] := newRefs[i] END
		END;
		RETURN a
	END NewRouter;

	(* --- интерфейс --------------------------------------------------------- *)

	PROCEDURE NewPool* (factory: ActorCore.Factory; n: INTEGER; kind: INTEGER;
		seed: LONGINT; strategy: ActorCore.Strategy): Ref;
	(** Пред: factory # NIL (трап 20); 0 < n <= maxRoutes (21); kind —
		roundRobin, random, broadcast или consistentHash (22).
		Пост: маршрутизатор-корень с пулом из n детей; отказ ребёнка решает
		strategy. *)
		VAR p: ActorCore.Props; r: Ref;
	BEGIN
		ASSERT(factory # NIL, 20); ASSERT((n > 0) & (n <= maxRoutes), 21);
		ASSERT((kind >= roundRobin) & (kind <= consistentHash), 22);
		newFactory := factory; newCount := n; newKind := kind; newSeed := seed;
		newPool := TRUE; newRefs := NIL; newN := 0;
		p := ActorCore.NewProps(NewRouter); p.strategy := strategy;
		r := ActorCore.Spawn(NIL, p, "");
		newFactory := NIL; newPool := FALSE;
		RETURN r
	END NewPool;

	PROCEDURE NewGroup* (IN refs: ARRAY OF Ref; kind: INTEGER; seed: LONGINT): Ref;
	(** Пред: LEN(refs) > 0 и не больше maxRoutes (20, 21); адреса не NIL
		(22); kind — допустимый способ (23).
		Пост: маршрутизатор-корень с заданными маршрутами. *)
		VAR p: ActorCore.Props; r: Ref; i: INTEGER;
	BEGIN
		ASSERT((LEN(refs) > 0) & (LEN(refs) <= maxRoutes), 20);
		FOR i := 0 TO LEN(refs) - 1 DO ASSERT(refs[i] # NIL, 22) END;
		ASSERT((kind >= roundRobin) & (kind <= consistentHash), 23);
		NEW(newRefs, LEN(refs));
		FOR i := 0 TO LEN(refs) - 1 DO newRefs[i] := refs[i] END;
		newN := LEN(refs); newKind := kind; newSeed := seed;
		newPool := FALSE; newFactory := NIL; newCount := 0;
		p := ActorCore.NewProps(NewRouter);
		r := ActorCore.Spawn(NIL, p, "");
		newRefs := NIL; newN := 0;
		RETURN r
	END NewGroup;

	PROCEDURE SetGroup* (r: Ref; IN refs: ARRAY OF Ref);
	(** Пред: r # NIL (20); LEN(refs) > 0 и не больше maxRoutes (21);
		адреса не NIL (22).
		Пост: письмо маршрутизатору: набор маршрутов — refs; способ выбора
		не меняется. *)
		VAR m: GroupMsg; i: INTEGER;
	BEGIN
		ASSERT(r # NIL, 20); ASSERT((LEN(refs) > 0) & (LEN(refs) <= maxRoutes), 21);
		FOR i := 0 TO LEN(refs) - 1 DO ASSERT(refs[i] # NIL, 22) END;
		NEW(m); NEW(m.refs, LEN(refs));
		FOR i := 0 TO LEN(refs) - 1 DO m.refs[i] := refs[i] END;
		ActorCore.Tell(r, m)
	END SetGroup;

	PROCEDURE SetCount* (r: Ref; n: INTEGER);
	(** Пред: r # NIL (20). Пост: письмо маршрутизатору-пулу: число
		маршрутов n; у группы письмо ничего не меняет. *)
		VAR m: CountMsg;
	BEGIN
		ASSERT(r # NIL, 20);
		NEW(m); m.n := n; ActorCore.Tell(r, m)
	END SetCount;

	PROCEDURE SetKind* (r: Ref; kind: INTEGER);
	(** Пред: r # NIL (20). Пост: письмо маршрутизатору: способ выбора
		kind; недопустимый способ ничего не меняет. *)
		VAR m: KindMsg;
	BEGIN
		ASSERT(r # NIL, 20);
		NEW(m); m.kind := kind; ActorCore.Tell(r, m)
	END SetKind;

END ActorRouter.
