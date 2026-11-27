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
	нет: при разделении сети возможны два воплощения. Воплощение —
	persist-актор с fresh: перед каждым письмом оно сверяет seq с концом
	потока (Last) и догоняет его, поэтому две копии пишут по очереди, а
	не мимо друг друга; запись со старым номером получает conflict
	(ActorPersist.conflictTrap). *)

	IMPORT ActorHash, ActorCore, ActorJournal, ActorPersist, ActorNet, Stores;

	CONST
		maxMembers* = 32;
		capLen* = 256;	(** размер пропуска узла в записи службы состава *)
		notNetMsg* = 31;	(** трап: адресу ключа послано письмо не ActorNet.Msg *)
		(** переключатели мутаций — только для тестов *)
		mutNoApply* = 0;	(** ApplyMembers не меняет список членов *)
		mutKeepAlive* = 1;	(** все записи списка считаются живыми *)
		mutNoRejoin* = 2;	(** вернувшийся член не оживает *)
		mutLinearFind* = 3;	(** поиск воплощения перебором списка, не хеш-таблицей *)
		(* начальное число корзин таблицы воплощений; растёт вдвое *)
		hashMin = 256;

	TYPE
		Name = ActorCore.Name;

		(** Воплощение виртуального актора. kind и key заданы до Started. *)
		Actor* = POINTER TO ABSTRACT RECORD (ActorPersist.Actor)
			kind-, key-: Name
		END;

		Kind = POINTER TO RECORD name: Name; f: ActorCore.Factory; next: Kind END;

		Mem = RECORD
			name: Name;
			host: ActorCore.Ref;
			alive: BOOLEAN;
			id: INTEGER;	(** номер узла в службе состава; -1 — не из службы *)
			key: ActorJournal.Bytes	(** открытый ключ узла (проверка — этап 5) *)
		END;

		(** Запись службы состава для ApplyMembers. *)
		MemberInfo* = RECORD
			id*: INTEGER;
			cap*: ARRAY capLen OF CHAR;
			key*: ActorJournal.Bytes;
			alive*: BOOLEAN	(** аренда не истекла и узел не вышел *)
		END;

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
			next: Act;	(* список всех воплощений *)
			hnext: Act	(* цепочка корзины таблицы имя -> воплощение *)
		END;

		Host = POINTER TO RECORD (ActorCore.Actor)
			cl: Cluster; acts: Act;
			table: POINTER TO ARRAY OF Act;	(* имя -> воплощение, растёт вдвое *)
			nActs: INTEGER
		END;

		GRef = POINTER TO RECORD (ActorCore.Ref) cl: Cluster; kind, key: Name END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)
		newCl: Cluster;	(* для фабрик: кластер, вид и ключ создаваемого *)
		newKind: Kind; newKey: Name;
		tick: TickMsg; passivate: Passivate;

	(* --- размещение -------------------------------------------------------- *)

	PROCEDURE OwnerIndex (cl: Cluster; IN kind, key: ARRAY OF CHAR): INTEGER;
		VAR i, best: INTEGER; w, bw: LONGINT;
	BEGIN
		best := -1; bw := -1;
		FOR i := 0 TO cl.n - 1 DO
			IF cl.m[i].alive THEN
				w := ActorHash.Weight(cl.m[i].name, kind, key);
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
		a.fresh := TRUE;	(* ответ не старше подтверждённой записи: Last через журнал *)
		RETURN a
	END NewGrain;

	(* --- хозяин ------------------------------------------------------------- *)

	PROCEDURE Hash (IN name: ARRAY OF CHAR; nb: INTEGER): INTEGER;
		VAR x, i: INTEGER;
	BEGIN
		x := 0; i := 0;
		WHILE name[i] # 0X DO x := (x * 31 + ORD(name[i])) MOD nb; INC(i) END;
		RETURN x
	END Hash;

	PROCEDURE Bucket (h: Host; IN name: ARRAY OF CHAR): INTEGER;
	BEGIN
		RETURN Hash(name, LEN(h.table^))
	END Bucket;

	PROCEDURE Grow (h: Host);
	(* Пост: таблица вдвое больше, цепочки перестроены. *)
		VAR nt: POINTER TO ARRAY OF Act; i, b, nb: INTEGER; x, nx: Act;
	BEGIN
		nb := 2 * LEN(h.table^); NEW(nt, nb);
		FOR i := 0 TO LEN(h.table^) - 1 DO
			x := h.table[i];
			WHILE x # NIL DO
				nx := x.hnext;
				b := Hash(x.ref.name, nb);
				x.hnext := nt[b]; nt[b] := x;
				x := nx
			END
		END;
		h.table := nt
	END Grow;

	PROCEDURE (h: Host) Find (IN name: ARRAY OF CHAR): Act, NEW;
		VAR x: Act; b: INTEGER;
	BEGIN
		IF mutLinearFind IN mutations THEN	(* для замера: перебор списка *)
			x := h.acts; WHILE (x # NIL) & (x.ref.name # name) DO x := x.next END;
			RETURN x
		END;
		b := Bucket(h, name); x := h.table[b];
		WHILE (x # NIL) & (x.ref.name # name) DO x := x.hnext END;
		RETURN x
	END Find;

	PROCEDURE (h: Host) Deliver (c: ActorCore.Context; e: Envelope; sender: ActorCore.Ref), NEW;
		VAR k: Kind; x: Act; name, full: Name; r: ActorCore.Ref; hd: Held; b: INTEGER;
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
			NEW(x); x.ref := r; x.next := h.acts; h.acts := x;
			b := Bucket(h, full); x.hnext := h.table[b]; h.table[b] := x; INC(h.nActs);
			IF h.nActs > 2 * LEN(h.table^) THEN Grow(h) END
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
		VAR x, p, y, py: Act; i, b: INTEGER; hd: Held;
	BEGIN
		p := NIL; x := h.acts;
		WHILE (x # NIL) & (x.ref # t.who) DO p := x; x := x.next END;
		IF x # NIL THEN
			IF p = NIL THEN h.acts := x.next ELSE p.next := x.next END;
			b := Bucket(h, x.ref.name);	(* снять из корзины *)
			py := NIL; y := h.table[b];
			WHILE (y # NIL) & (y # x) DO py := y; y := y.hnext END;
			IF y # NIL THEN
				IF py = NIL THEN h.table[b] := y.hnext ELSE py.hnext := y.hnext END;
				DEC(h.nActs)
			END;
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
		NEW(h); h.cl := newCl; NEW(h.table, hashMin); h.nActs := 0; RETURN h
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
		cl.m[0].name := n.name$; cl.m[0].host := cl.host; cl.m[0].alive := TRUE; cl.m[0].id := -1; cl.n := 1;
		RETURN cl
	END New;

	PROCEDURE HostRef* (cl: Cluster): ActorCore.Ref;
	(** Пост: адрес хозяина узла cl (корневого актора); клиенты службы
		состава вешают на него свои акторы. *)
	BEGIN
		RETURN cl.host
	END HostRef;

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
		cl.m[cl.n].name := r.node$; cl.m[cl.n].host := r; cl.m[cl.n].alive := TRUE; cl.m[cl.n].id := -1;
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

	PROCEDURE ApplyMembers* (cl: Cluster; IN list: ARRAY OF MemberInfo; n: INTEGER);
	(** Состав из службы: привести список членов cl к list[0 .. n-1].
		Пост: новый член добавлен (Member), вернувшийся снова живой
		(Rejoin), пропавший из списка — мёртвый (Leave); ключи и номера
		записаны в записи членов. Сами ключи не проверяются: это этап 5.
		Размещение ключей считается по живым членам при каждом обращении,
		поэтому меняется вместе с составом. *)
		VAR i, j: INTEGER; r: ActorCore.Ref; seen: ARRAY maxMembers OF BOOLEAN; found: BOOLEAN;
	BEGIN
		IF mutNoApply IN mutations THEN RETURN END;
		FOR i := 0 TO maxMembers - 1 DO seen[i] := FALSE END;
		FOR i := 0 TO n - 1 DO
			IF (i < LEN(list)) & (list[i].cap # "") THEN
				r := ActorNet.Import(cl.node, list[i].cap);
				IF r # NIL THEN
					found := FALSE;
					IF r.node = cl.node.name THEN	(* сам узел: запись 0 уже есть *)
						seen[0] := TRUE; found := TRUE;
						cl.m[0].id := list[i].id; cl.m[0].key := list[i].key
					ELSIF ~list[i].alive & ~(mutKeepAlive IN mutations) THEN	(* вышел или аренда истекла: считается пропавшим *)
					ELSE
						j := 1;
						WHILE (j < cl.n) & (cl.m[j].name # r.node) DO INC(j) END;
						IF j < cl.n THEN
							seen[j] := TRUE; found := TRUE;
							cl.m[j].id := list[i].id; cl.m[j].key := list[i].key;
							IF ~cl.m[j].alive & ~(mutNoRejoin IN mutations) THEN Rejoin(cl, cl.m[j].name) END
						END
					END;
					IF ~found & (list[i].alive OR (mutKeepAlive IN mutations)) & (cl.n < maxMembers) THEN
						j := cl.n;
						Member(cl, list[i].cap);	(* добавляет запись j и наблюдает хозяина *)
						cl.m[j].id := list[i].id; cl.m[j].key := list[i].key;
						seen[j] := TRUE
					END
				END
			END
		END;
		FOR j := 1 TO cl.n - 1 DO
			IF ~seen[j] & cl.m[j].alive THEN Leave(cl, cl.m[j].name) END
		END
	END ApplyMembers;

	PROCEDURE Key* (cl: Cluster; IN name: ARRAY OF CHAR): ActorJournal.Bytes;
	(** Пост: открытый ключ члена name из последнего ApplyMembers; NIL — нет. *)
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < cl.n) & (cl.m[i].name # name) DO INC(i) END;
		IF i = cl.n THEN RETURN NIL ELSE RETURN cl.m[i].key END
	END Key;

	PROCEDURE Members* (cl: Cluster): INTEGER;
	(** Пост: число членов в списке cl, включая сам узел. *)
	BEGIN
		RETURN cl.n
	END Members;

	PROCEDURE AliveCount* (cl: Cluster): INTEGER;
	(** Пост: число живых членов, включая сам узел. *)
		VAR i, k: INTEGER;
	BEGIN
		k := 1;
		FOR i := 1 TO cl.n - 1 DO IF cl.m[i].alive THEN INC(k) END END;
		RETURN k
	END AliveCount;

	PROCEDURE MemberName* (cl: Cluster; i: INTEGER; OUT name: Name);
	(** Пост: имя члена i (0 — сам узел); "" — за пределами списка. *)
	BEGIN
		IF (i < 0) OR (i >= cl.n) THEN name := "" ELSE name := cl.m[i].name END
	END MemberName;

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
