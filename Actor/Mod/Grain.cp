MODULE ActorGrain;
(** Виртуальные акторы: адрес — вид и ключ, а не узел и имя.

	Кластер узла знает свой узел ActorNet, статический список членов и
	общий журнал. Владелец ключа — живой член с наибольшим весом
	хеш(член, вид, ключ) (рандеву-хеш): при уходе члена переезжают только
	его ключи. Письмо адресу ключа идёт хозяину владельца — корневому
	актору узла — конвертом; хозяин находит или поднимает воплощение
	(ребёнка "вид.ключ") и передаёт письмо с исходным отправителем.

	Воплощение — ActorGrain.Actor, персистентный актор с потоком журнала
	"вид.ключ": на другом узле поднимается то же состояние. После idle
	тиков без писем хозяин пассивирует его: снимок и остановка.

	Член, чей хозяин пропал (Terminated с byNodeLoss), считается мёртвым,
	его ключи размещаются среди живых; возврат — Rejoin. Согласования
	нет: при разделении сети возможны два воплощения, писать может одно —
	второе получает conflict журнала (ActorPersist.conflictTrap). *)

	IMPORT CasHash, ActorCore, ActorJournal, ActorPersist, ActorNet, Stores;

	CONST
		maxMembers* = 32;
		notNetMsg* = 31;	(** трап: адресу ключа послано письмо не ActorNet.Msg *)

	TYPE
		Name = ActorCore.Name;

		(** Воплощение виртуального актора. kind и key заданы до Started. *)
		Actor* = POINTER TO ABSTRACT RECORD (ActorPersist.Actor)
			kind-, key-: Name
		END;

		Kind = POINTER TO RECORD name: Name; f: ActorCore.Factory; next: Kind END;

		Mem = RECORD name: Name; host: ActorCore.Ref; alive: BOOLEAN END;

		Cluster* = POINTER TO LIMITED RECORD
			node-: ActorNet.Node;
			journal-: ActorJournal.Journal;
			idle*: INTEGER;	(** тиков простоя до пассивации; 0 — не пассивировать *)
			activations-, passivations-: LONGINT;
			m: ARRAY maxMembers OF Mem; n: INTEGER;
			kinds: Kind;
			host: ActorCore.Ref
		END;

		Envelope = POINTER TO RECORD (ActorNet.Msg) kind, key: Name; m: ActorNet.Msg END;

		TickMsg = POINTER TO RECORD (ActorCore.Msg) END;
		Passivate = POINTER TO RECORD (ActorCore.Msg) END;
		WatchHost = POINTER TO RECORD (ActorCore.Msg) i: INTEGER END;

		Held = POINTER TO RECORD e: Envelope; sender: ActorCore.Ref; next: Held END;

		Act = POINTER TO RECORD
			ref: ActorCore.Ref; age: INTEGER;
			asked: BOOLEAN;	(* попросили пассивироваться: письма — в held до Terminated *)
			held, last: Held;
			next: Act
		END;

		Host = POINTER TO RECORD (ActorCore.Actor) cl: Cluster; acts: Act END;

		GRef = POINTER TO RECORD (ActorCore.Ref) cl: Cluster; kind, key: Name END;

	VAR
		newCl: Cluster;	(* для фабрик: кластер, вид и ключ создаваемого *)
		newKind: Kind; newKey: Name;
		tick: TickMsg; passivate: Passivate;

	(* --- размещение -------------------------------------------------------- *)

	PROCEDURE Weight (IN member, kind, key: ARRAY OF CHAR): LONGINT;
		VAR b: ARRAY 3 * ActorCore.nameLen + 2 OF BYTE; n: INTEGER; id: CasHash.Id; w: LONGINT; i: INTEGER;

		PROCEDURE Add (IN s: ARRAY OF CHAR);
			VAR i: INTEGER;
		BEGIN
			i := 0; WHILE s[i] # 0X DO b[n] := SHORT(SHORT(ORD(s[i]) MOD 256)); INC(n); INC(i) END;
			b[n] := 0; INC(n)
		END Add;

	BEGIN
		n := 0; Add(member); Add(kind); Add(key);
		CasHash.Of(b, 0, n, id);
		w := 0; FOR i := 0 TO 6 DO w := w * 256 + id[i] MOD 256 END;
		RETURN w
	END Weight;

	PROCEDURE OwnerIndex (cl: Cluster; IN kind, key: ARRAY OF CHAR): INTEGER;
		VAR i, best: INTEGER; w, bw: LONGINT;
	BEGIN
		best := -1; bw := -1;
		FOR i := 0 TO cl.n - 1 DO
			IF cl.m[i].alive THEN
				w := Weight(cl.m[i].name, kind, key);
				IF (w > bw) OR (w = bw) & (cl.m[i].name < cl.m[best].name) THEN best := i; bw := w END
			END
		END;
		RETURN best
	END OwnerIndex;

	PROCEDURE Owner* (cl: Cluster; IN kind, key: ARRAY OF CHAR; OUT node: ARRAY OF CHAR);
	(** Пост: имя узла-владельца ключа среди живых членов, по мнению cl. *)
	BEGIN
		node := cl.m[OwnerIndex(cl, kind, key)].name$
	END Owner;

	(* --- воплощение ---------------------------------------------------------- *)

	PROCEDURE (a: Actor) Receive* (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF (m IS Passivate) & ~a.Busy() THEN	(* во время записи пассивация ждёт ответа журнала вместе с прочими письмами *)
			IF a.seq > 0 THEN a.SaveSnapshot(c) END;
			c.Stop
		ELSE a.Receive^(c, m)
		END
	END Receive;

	PROCEDURE NewGrain (): ActorCore.Actor;
		VAR x: ActorCore.Actor; a: Actor;
	BEGIN
		x := newKind.f();
		IF (x = NIL) OR ~(x IS Actor) THEN RETURN NIL END;
		a := x(Actor);
		a.kind := newKind.name$; a.key := newKey$;
		a.stream := newKind.name + "." + newKey;
		a.journal := newCl.journal;
		RETURN a
	END NewGrain;

	(* --- хозяин ------------------------------------------------------------- *)

	PROCEDURE (h: Host) Find (IN name: ARRAY OF CHAR): Act, NEW;
		VAR x: Act;
	BEGIN
		x := h.acts; WHILE (x # NIL) & (x.ref.name # name) DO x := x.next END;
		RETURN x
	END Find;

	PROCEDURE (h: Host) Deliver (c: ActorCore.Context; e: Envelope; sender: ActorCore.Ref), NEW;
		VAR k: Kind; x: Act; name, full: Name; r: ActorCore.Ref; hd: Held;
	BEGIN
		k := h.cl.kinds; WHILE (k # NIL) & (k.name # e.kind) DO k := k.next END;
		IF (k = NIL) OR (e.m = NIL) THEN RETURN END;	(* неизвестный вид: письмо пропадает *)
		name := e.kind + "." + e.key; full := c.name + "/" + name;
		x := h.Find(full);
		IF x = NIL THEN
			newCl := h.cl; newKind := k; newKey := e.key$;
			r := ActorCore.Spawn(c, ActorCore.NewProps(NewGrain), name);
			newCl := NIL; newKind := NIL;
			IF r = NIL THEN RETURN END;
			c.Watch(r); INC(h.cl.activations);
			NEW(x); x.ref := r; x.next := h.acts; h.acts := x
		END;
		IF x.asked THEN	(* воплощение уходит: письмо ждёт следующего *)
			NEW(hd); hd.e := e; hd.sender := sender;
			IF x.last = NIL THEN x.held := hd ELSE x.last.next := hd END;
			x.last := hd
		ELSE
			x.age := 0; x.ref.Post(e.m, sender)
		END
	END Deliver;

	PROCEDURE (h: Host) Ended (c: ActorCore.Context; t: ActorCore.Terminated), NEW;
		VAR x, p: Act; i: INTEGER; hd: Held;
	BEGIN
		p := NIL; x := h.acts;
		WHILE (x # NIL) & (x.ref # t.who) DO p := x; x := x.next END;
		IF x # NIL THEN
			IF p = NIL THEN h.acts := x.next ELSE p.next := x.next END;
			hd := x.held;	(* письма, ждавшие конца пассивации, — новому воплощению по порядку *)
			WHILE hd # NIL DO h.Deliver(c, hd.e, hd.sender); hd := hd.next END
		ELSIF t.reason = ActorCore.byNodeLoss THEN
			FOR i := 0 TO h.cl.n - 1 DO
				IF h.cl.m[i].host = t.who THEN h.cl.m[i].alive := FALSE END
			END
		END
	END Ended;

	PROCEDURE (h: Host) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR x: Act; i: INTEGER;
	BEGIN
		WITH m: Envelope DO h.Deliver(c, m, c.Sender())
		| m: ActorCore.Terminated DO h.Ended(c, m)
		| m: WatchHost DO c.Watch(h.cl.m[m.i].host)
		| m: TickMsg DO
			IF h.cl.idle > 0 THEN
				x := h.acts;
				WHILE x # NIL DO
					INC(x.age);
					IF (x.age >= h.cl.idle) & ~x.asked THEN
						x.asked := TRUE; INC(h.cl.passivations); x.ref.Post(passivate, NIL)
					END;
					x := x.next
				END
			END
		ELSE
		END
	END Receive;

	PROCEDURE NewHost (): ActorCore.Actor;
		VAR h: Host;
	BEGIN
		NEW(h); h.cl := newCl; RETURN h
	END NewHost;

	(* --- адрес ключа --------------------------------------------------------- *)

	PROCEDURE (r: GRef) Post (m: ActorCore.Msg; sender: ActorCore.Ref);
		VAR e: Envelope; i: INTEGER;
	BEGIN
		ASSERT(m # NIL, 20);
		IF ~(m IS ActorNet.Msg) THEN
			IF m IS ActorCore.Terminated THEN RETURN END;	(* Ask: конец адреса ответа *)
			HALT(notNetMsg)
		END;
		NEW(e); e.kind := r.kind$; e.key := r.key$; e.m := m(ActorNet.Msg);
		i := OwnerIndex(r.cl, r.kind, r.key);
		r.cl.m[i].host.Post(e, sender)
	END Post;

	PROCEDURE (r: GRef) AddWatcher (w: ActorCore.Ref);
	END AddWatcher;	(* конец воплощения — не событие для клиента *)

	PROCEDURE (r: GRef) RemoveWatcher (w: ActorCore.Ref);
	END RemoveWatcher;

	PROCEDURE (r: GRef) Stop;
	END Stop;

	(* --- конверт ------------------------------------------------------------- *)

	PROCEDURE (e: Envelope) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteString(e.kind); wr.WriteString(e.key); ActorNet.WriteMsg(wr, e.m)
	END Write;

	PROCEDURE (e: Envelope) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadString(e.kind); rd.ReadString(e.key); ActorNet.ReadMsg(rd, e.m)
	END Read;

	(* --- интерфейс ----------------------------------------------------------- *)

	PROCEDURE New* (n: ActorNet.Node; j: ActorJournal.Journal): Cluster;
	(** Кластер узла n с общим журналом j; член — сам узел.
		Пред: n # NIL — 20; j # NIL — 21; корневого актора с именем узла нет — 22.
		Пост: хозяин (корневой актор n.name) создан. *)
		VAR cl: Cluster;
	BEGIN
		ASSERT(n # NIL, 20); ASSERT(j # NIL, 21);
		ASSERT(ActorCore.dir.Lookup(ActorCore.node, n.name) = NIL, 22);
		NEW(cl); cl.node := n; cl.journal := j; cl.idle := 0;
		newCl := cl; cl.host := ActorCore.Spawn(NIL, ActorCore.NewProps(NewHost), n.name); newCl := NIL;
		cl.m[0].name := n.name$; cl.m[0].host := cl.host; cl.m[0].alive := TRUE; cl.n := 1;
		RETURN cl
	END New;

	PROCEDURE HostCap* (cl: Cluster; OUT cap: ARRAY OF CHAR);
	(** Пост: пропуск хозяина узла для Member других узлов. *)
	BEGIN
		ActorNet.Export(cl.node, cl.host, cap)
	END HostCap;

	PROCEDURE Member* (cl: Cluster; IN cap: ARRAY OF CHAR);
	(** Член по пропуску его хозяина (HostCap того узла). Пред: пропуск
		разобран — 20; мест меньше maxMembers — 21. Пост: член живой; хозяин
		cl наблюдает его хозяина. *)
		VAR r: ActorCore.Ref; w: WatchHost;
	BEGIN
		r := ActorNet.Import(cl.node, cap); ASSERT(r # NIL, 20); ASSERT(cl.n < maxMembers, 21);
		cl.m[cl.n].name := r.node$; cl.m[cl.n].host := r; cl.m[cl.n].alive := TRUE;
		NEW(w); w.i := cl.n; INC(cl.n);
		cl.host.Post(w, NIL)
	END Member;

	PROCEDURE Alive* (cl: Cluster; IN name: ARRAY OF CHAR): BOOLEAN;
	(** Пост: член name живой, по мнению cl. *)
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < cl.n) & (cl.m[i].name # name) DO INC(i) END;
		RETURN (i < cl.n) & cl.m[i].alive
	END Alive;

	PROCEDURE Rejoin* (cl: Cluster; IN name: ARRAY OF CHAR);
	(** Член name вернулся: снова живой, его хозяин снова наблюдается.
		Пред: член есть — 20. *)
		VAR i: INTEGER; w: WatchHost;
	BEGIN
		i := 0; WHILE (i < cl.n) & (cl.m[i].name # name) DO INC(i) END;
		ASSERT(i < cl.n, 20);
		IF ~cl.m[i].alive THEN
			cl.m[i].alive := TRUE;
			IF i > 0 THEN NEW(w); w.i := i; cl.host.Post(w, NIL) END
		END
	END Rejoin;

	PROCEDURE Leave* (cl: Cluster; IN name: ARRAY OF CHAR);
	(** Член name считается мёртвым (решение снаружи, например разделение
		сети). Пред: член есть и это не сам узел — 20. *)
		VAR i: INTEGER;
	BEGIN
		i := 1; WHILE (i < cl.n) & (cl.m[i].name # name) DO INC(i) END;
		ASSERT(i < cl.n, 20);
		cl.m[i].alive := FALSE
	END Leave;

	PROCEDURE Register* (cl: Cluster; IN kind: ARRAY OF CHAR; f: ActorCore.Factory);
	(** Вид kind: воплощения создаёт f (наследник Actor). Пред: kind # "",
		без "." и "/" — 20; f # NIL — 21. *)
		VAR k: Kind; i: INTEGER;
	BEGIN
		ASSERT(kind # "", 20); ASSERT(f # NIL, 21);
		i := 0; WHILE kind[i] # 0X DO ASSERT((kind[i] # ".") & (kind[i] # "/"), 20); INC(i) END;
		NEW(k); k.name := kind$; k.f := f; k.next := cl.kinds; cl.kinds := k
	END Register;

	PROCEDURE Ref* (cl: Cluster; IN kind, key: ARRAY OF CHAR): ActorCore.Ref;
	(** Адрес ключа key вида kind. Пред: key # "", без "/" — 20. Пост: Tell и
		Ask к нему идут воплощению у текущего владельца; Watch не действует. *)
		VAR r: GRef; i: INTEGER;
	BEGIN
		ASSERT(key # "", 20);
		i := 0; WHILE key[i] # 0X DO ASSERT(key[i] # "/", 20); INC(i) END;
		NEW(r); r.cl := cl; r.kind := kind$; r.key := key$;
		ActorCore.InitRef(r, "grain", kind + "." + key);
		RETURN r
	END Ref;

	PROCEDURE Tick* (cl: Cluster);
	(** Тик простоя: воплощения узла cl без писем idle тиков подряд пассивируются. *)
	BEGIN
		cl.host.Post(tick, NIL)
	END Tick;

BEGIN
	NEW(tick); NEW(passivate)
END ActorGrain.
