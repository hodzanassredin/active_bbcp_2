MODULE RaftNode;
(** Узел Raft как актор: водитель автомата RaftCore над ActorNet.

	Актор держит RaftCore.Node, автомат состояний (RaftSm.Sessions) и
	стойкое хранилище (RaftStorage). Входы автомата — письма актора:
	Tick (тик часов), письмо Raft соседа (тип модуля, кадр ActorNet),
	Propose (команда клиента) и Query (чтение). После каждого входа
	водитель выполняет Ready: сохранить (RaftStorage.Save), потом
	отправить письма соседям, потом применить записи к автомату и
	ответить клиентам, потом Advance.

	Соседи — узлы-акторы на других узлах ActorNet; их адреса — пропуски
	ActorNet.Export в Config.peers, номер соседа — индекс в массиве.
	Пропуск можно дописать после Spawn: адрес берётся при первой отправке.

	Клиент шлёт Propose с номером клиента и операции (сеанс RaftSm) и
	получает Reply, когда команда применена на этом узле: res = ok и
	результат автомата. Не-лидер отвечает res = notLeader и подсказкой
	leader (номер узла или RaftCore.none). Если под индексом команды
	оказалась чужая запись (лидер сменился, хвост стёрт), ответ — lost:
	клиент повторяет ту же операцию с тем же номером, сеанс не даст
	применить её дважды. Потерянный ответ клиент тоже лечит повтором.

	Query — линеаризуемое чтение ReadIndex: ответ — sm.Query(тело) после
	того, как большинство подтвердило лидерство и автомат догнал
	зафиксированное на момент запроса.

	Тики: Config.tickMs > 0 — часы главного цикла (Services) шлют Tick
	сами; 0 — тики шлёт владелец (детерминированный прогон в ActorSim).

	Ошибка хранилища: узел перестаёт отвечать и останавливает себя
	(broken); поднимают его новым Spawn — состояние берётся из хранилища.
	Отказ обработчика (трап) по умолчанию — тоже остановка.

	Состав кластера задаётся при создании и не меняется (граница этапа). *)

	IMPORT Services, Stores, ActorCore, ActorNet, Core := RaftCore, RaftSm, RaftStorage;

	CONST
		(** Reply.res *)
		ok* = 0;
		notLeader* = 1;	(** узел не лидер; leader — подсказка *)
		lost* = 2;	(** запись под индексом команды чужая: повторить *)
		(** переключатели мутаций водителя — только для тестов *)
		mutSendFirst* = 0;	(** отправить письма до сохранения *)
		mutEarlyReply* = 1;	(** ответить клиенту сразу, до фиксации *)
		capLen* = 256;
		maxEnts = 65536;	(* предел числа записей в кадре: память под них берётся до чтения *)
		maxBytes = 64 * 1024 * 1024;	(* предел длины байтов в кадре *)

	TYPE
		Bytes* = Core.Bytes;
		Config* = POINTER TO ConfigDesc;
		Caps* = ARRAY Core.maxNodes OF ARRAY capLen OF CHAR;	(** пропуски узлов-акторов по номерам *)

		Tick* = POINTER TO RECORD (ActorCore.Msg) END;

		(** Команда клиента. *)
		Propose* = POINTER TO RECORD (ActorNet.Msg)
			client*, seq*: INTEGER;	(** сеанс: client > 0, seq > 0 *)
			body*: Bytes
		END;

		(** Чтение. *)
		Query* = POINTER TO RECORD (ActorNet.Msg)
			tag*: INTEGER;	(** метка клиента; вернётся в Reply.tag *)
			body*: Bytes
		END;

		Reply* = POINTER TO RECORD (ActorNet.Msg)
			res*: INTEGER;
			tag*: INTEGER;	(** Propose.seq или Query.tag: к какому запросу ответ *)
			leader*: INTEGER;	(** при notLeader — кто лидер, по мнению узла; RaftCore.none — неизвестно *)
			index*: INTEGER;	(** при ok на Propose — индекс записи *)
			data*: Bytes	(** результат автомата или ответ Query *)
		END;

		(* письмо Raft между узлами *)
		Wire = POINTER TO RECORD (ActorNet.Msg) m: Core.Msg END;

		Waiter = POINTER TO RECORD
			read: BOOLEAN;
			index: INTEGER;	(* Propose — индекс записи; Query — подтверждённый индекс или -1 *)
			client, seq, ctx, tag: INTEGER;
			body: Bytes;
			to: ActorCore.Ref;
			term: INTEGER;	(* чтение: срок лидера, принявшего его *)
			next: Waiter
		END;

		Clock = POINTER TO RECORD (Services.Action) to: ActorCore.Ref; ms: INTEGER; dead: BOOLEAN END;

		Node* = POINTER TO RECORD (ActorCore.Actor)
			cfg-: Config;
			nd-: Core.Node;	(** NIL до Started *)
			sm-: RaftSm.Sessions;
			broken-: BOOLEAN;
			peer: ARRAY Core.maxNodes OF ActorCore.Ref;
			waits: Waiter;
			ctx: INTEGER;
			clock: Clock;
			tick: Tick
		END;

		(** Устройство узла. *)
		ConfigDesc* = RECORD
			id*, n*: INTEGER;	(** номер узла и размер кластера *)
			net*: ActorNet.Node;	(** узел ActorNet, на котором живёт актор *)
			peers*: Caps;	(** пропуски узлов-акторов; свой — не нужен *)
			storage*: RaftStorage.Storage;	(** переживает перезапуск узла *)
			newSm*: PROCEDURE (): RaftSm.StateMachine;	(** пустой автомат *)
			electionTicks*: INTEGER;	(** по умолчанию 10 *)
			tickMs*: INTEGER;	(** > 0 — свои часы, мс на тик *)
			seed*: LONGINT;
			compactEvery*: INTEGER;	(** снимок после стольких применённых записей; 0 — без снимков *)
			actor-: Node;	(** последнее воплощение актора узла *)
			starts: INTEGER
		END;

		(** Наблюдатель для тестов: применённые записи. *)
		Tracer* = POINTER TO ABSTRACT RECORD END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)
		tracer*: Tracer;	(** NIL — без наблюдения *)
		pending: Config;	(* устройство для фабрики Spawn *)

	PROCEDURE (t: Tracer) Applied* (id, index: INTEGER; IN e: Core.Entry), NEW, ABSTRACT;

	(* --- кадр письма Raft ----------------------------------------------------- *)

	PROCEDURE WriteBytes (VAR wr: Stores.Writer; b: Bytes);
		VAR k: INTEGER;
	BEGIN
		IF b = NIL THEN wr.WriteInt(-1)
		ELSE wr.WriteInt(LEN(b^)); FOR k := 0 TO LEN(b^) - 1 DO wr.WriteByte(b[k]) END
		END
	END WriteBytes;

	PROCEDURE ReadBytes (VAR rd: Stores.Reader; OUT b: Bytes);
		VAR k, len: INTEGER;
	BEGIN
		b := NIL; rd.ReadInt(len);
		IF rd.cancelled OR (len < 0) THEN RETURN END;
		IF len > maxBytes THEN rd.TurnIntoAlien(Stores.alienComponent); RETURN END;
		NEW(b, len); k := 0;
		WHILE (k < len) & ~rd.cancelled DO rd.ReadByte(b[k]); INC(k) END
	END ReadBytes;

	PROCEDURE (w: Wire) Write- (VAR wr: Stores.Writer);
		VAR m: Core.Msg; k: INTEGER;
	BEGIN
		m := w.m;
		wr.WriteInt(m.kind); wr.WriteInt(m.from); wr.WriteInt(m.to); wr.WriteInt(m.term);
		wr.WriteInt(m.index); wr.WriteInt(m.logTerm); wr.WriteInt(m.commit);
		wr.WriteBool(m.ok); wr.WriteInt(m.match); wr.WriteInt(m.ctx);
		wr.WriteInt(m.n);
		FOR k := 0 TO m.n - 1 DO wr.WriteInt(m.ents[k].term); WriteBytes(wr, m.ents[k].data) END;
		WriteBytes(wr, m.snap)
	END Write;

	PROCEDURE (w: Wire) Read- (VAR rd: Stores.Reader);
		VAR m: Core.Msg; k: INTEGER;
	BEGIN
		NEW(m); w.m := m;
		rd.ReadInt(m.kind); rd.ReadInt(m.from); rd.ReadInt(m.to); rd.ReadInt(m.term);
		rd.ReadInt(m.index); rd.ReadInt(m.logTerm); rd.ReadInt(m.commit);
		rd.ReadBool(m.ok); rd.ReadInt(m.match); rd.ReadInt(m.ctx);
		rd.ReadInt(m.n);
		IF rd.cancelled OR (m.n < 0) OR (m.n > maxEnts) THEN m.n := 0; rd.TurnIntoAlien(Stores.alienComponent); RETURN END;
		IF m.n > 0 THEN NEW(m.ents, m.n) END;
		k := 0;
		WHILE (k < m.n) & ~rd.cancelled DO rd.ReadInt(m.ents[k].term); ReadBytes(rd, m.ents[k].data); INC(k) END;
		ReadBytes(rd, m.snap)
	END Read;

	PROCEDURE (m: Propose) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.client); wr.WriteInt(m.seq); WriteBytes(wr, m.body)
	END Write;

	PROCEDURE (m: Propose) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.client); rd.ReadInt(m.seq); ReadBytes(rd, m.body)
	END Read;

	PROCEDURE (m: Query) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.tag); WriteBytes(wr, m.body)
	END Write;

	PROCEDURE (m: Query) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.tag); ReadBytes(rd, m.body)
	END Read;

	PROCEDURE (m: Reply) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.res); wr.WriteInt(m.tag); wr.WriteInt(m.leader); wr.WriteInt(m.index); WriteBytes(wr, m.data)
	END Write;

	PROCEDURE (m: Reply) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.res); rd.ReadInt(m.tag); rd.ReadInt(m.leader); rd.ReadInt(m.index); ReadBytes(rd, m.data)
	END Read;

	(* --- часы ----------------------------------------------------------------- *)

	PROCEDURE (k: Clock) Do;
		VAR t: Tick;
	BEGIN
		IF ~k.dead THEN
			NEW(t); ActorCore.Tell(k.to, t);
			Services.DoLater(k, Services.Ticks() + k.ms)
		END
	END Do;

	(* --- ответы и ожидания ------------------------------------------------------ *)

	PROCEDURE Answer (c: ActorCore.Context; to: ActorCore.Ref; tag, res, leader, index: INTEGER; data: Bytes);
		VAR r: Reply;
	BEGIN
		IF to # NIL THEN
			NEW(r); r.res := res; r.tag := tag; r.leader := leader; r.index := index; r.data := data;
			c.Tell(to, r)
		END
	END Answer;

	PROCEDURE (a: Node) Wait (w: Waiter), NEW;
	BEGIN
		w.next := a.waits; a.waits := w
	END Wait;

	PROCEDURE (a: Node) Applied (c: ActorCore.Context; k: INTEGER; data, out: Bytes), NEW;
	(* запись k применена: ответить ждущим её команды *)
		VAR w, p, nx: Waiter; cl, sq: INTEGER;
	BEGIN
		RaftSm.Decode(data, cl, sq);
		p := NIL; w := a.waits;
		WHILE w # NIL DO
			nx := w.next;
			IF ~w.read & (w.index = k) THEN
				IF (cl = w.client) & (sq = w.seq) THEN Answer(c, w.to, w.tag, ok, a.cfg.id, k, out)
				ELSE Answer(c, w.to, w.tag, lost, a.nd.leader, k, NIL)
				END;
				IF p = NIL THEN a.waits := nx ELSE p.next := nx END
			ELSE p := w
			END;
			w := nx
		END
	END Applied;

	PROCEDURE (a: Node) Settle (c: ActorCore.Context; snapIndex: INTEGER), NEW;
	(* Ответить чтениям, которые можно исполнить; отказать чтениям, которые
		не подтвердятся (узел больше не лидер), и командам, чьи записи
		поглотил принятый снимок (snapIndex). *)
		VAR w, p, nx: Waiter; done: BOOLEAN; empty: ARRAY 1 OF BYTE; d: Bytes;
	BEGIN
		p := NIL; w := a.waits;
		WHILE w # NIL DO
			nx := w.next; done := FALSE;
			IF w.read THEN
				IF (w.index >= 0) & (a.nd.applied >= w.index) THEN
					IF w.body = NIL THEN d := a.sm.sm.Query(empty, 0) ELSE d := a.sm.sm.Query(w.body, LEN(w.body^)) END;
					Answer(c, w.to, w.tag, ok, a.cfg.id, w.index, d); done := TRUE
				ELSIF (w.index < 0) & ((a.nd.role # Core.leader) OR (a.nd.term # w.term)) THEN	(* лидерство срока w.term утрачено: ядро это чтение забыло *)
					Answer(c, w.to, w.tag, notLeader, a.nd.leader, 0, NIL); done := TRUE
				END
			ELSIF w.index <= snapIndex THEN Answer(c, w.to, w.tag, lost, a.nd.leader, w.index, NIL); done := TRUE
			END;
			IF done THEN IF p = NIL THEN a.waits := nx ELSE p.next := nx END
			ELSE p := w
			END;
			w := nx
		END
	END Settle;

	(* --- Ready ------------------------------------------------------------------ *)

	PROCEDURE (a: Node) Peer (p: INTEGER): ActorCore.Ref, NEW;
	BEGIN
		IF (a.peer[p] = NIL) & (a.cfg.peers[p] # "") THEN a.peer[p] := ActorNet.Import(a.cfg.net, a.cfg.peers[p]) END;
		RETURN a.peer[p]
	END Peer;

	PROCEDURE (a: Node) Send (c: ActorCore.Context; m: Core.Msg), NEW;
		VAR nx: Core.Msg; w: Wire; r: ActorCore.Ref;
	BEGIN
		WHILE m # NIL DO
			nx := m.next; m.next := NIL;
			IF (m.to >= 0) & (m.to < a.cfg.n) & (m.to # a.cfg.id) THEN
				r := a.Peer(m.to);
				IF r # NIL THEN NEW(w); w.m := m; c.Tell(r, w) END
			END;
			m := nx
		END
	END Send;

	PROCEDURE (a: Node) Break (c: ActorCore.Context), NEW;
	BEGIN
		a.broken := TRUE;
		IF a.clock # NIL THEN a.clock.dead := TRUE END;
		ActorCore.StopActor(c)
	END Break;

	PROCEDURE (a: Node) Pump (c: ActorCore.Context), NEW;
	(* Выполнить Ready, пока они есть: сохранить, отправить, применить, Advance. *)
		VAR r: Core.Ready; nd: Core.Node; k, j, res, snapIndex: INTEGER; first, dup: BOOLEAN; e: Core.Entry;
			out, snap: Bytes; w: Waiter;
	BEGIN
		nd := a.nd; snapIndex := -1;
		WHILE ~a.broken & Core.HasReady(nd) DO
			Core.GetReady(nd, r);
			first := r.syncFirst & ~(mutSendFirst IN mutations);
			IF ~first THEN a.Send(c, r.msgs) END;
			a.cfg.storage.Save(nd, r, res);
			IF res # RaftStorage.ok THEN a.Break(c); RETURN END;
			IF first THEN a.Send(c, r.msgs) END;
			IF r.snap # NIL THEN a.sm.Restore(r.snap); snapIndex := r.snapIndex END;
			FOR k := r.applyLo TO r.applyHi DO
				Core.EntryAt(nd, k, e);
				a.sm.Apply(k, e.data, out, dup);
				IF tracer # NIL THEN tracer.Applied(a.cfg.id, k, e) END;
				a.Applied(c, k, e.data, out)
			END;
			FOR j := 0 TO r.nreads - 1 DO
				w := a.waits;
				WHILE (w # NIL) & ~(w.read & (w.ctx = r.reads[j].ctx)) DO w := w.next END;
				IF w # NIL THEN w.index := r.reads[j].index END
			END;
			Core.Advance(nd, r);
			IF (a.cfg.compactEvery > 0) & (nd.applied = nd.commit) & (nd.applied - nd.snapIndex >= a.cfg.compactEvery) THEN
				snap := a.sm.Snapshot(); Core.Compact(nd, nd.applied, snap);
				a.cfg.storage.Compact(nd.snapIndex, nd.snapTerm, snap, res);
				IF res # RaftStorage.ok THEN a.Break(c); RETURN END
			END
		END;
		IF ~a.broken THEN a.Settle(c, snapIndex) END
	END Pump;

	(* --- входы ------------------------------------------------------------------ *)

	PROCEDURE (a: Node) Start (c: ActorCore.Context), NEW;
		VAR cfg: Config; t, v, si, st, last, res: INTEGER; snap: Bytes; log: Core.Entries;
	BEGIN
		cfg := a.cfg; INC(cfg.starts);
		cfg.storage.Load(t, v, si, st, snap, log, last, res);
		IF res # RaftStorage.ok THEN a.Break(c); RETURN END;
		a.nd := Core.New(cfg.id, cfg.n, cfg.seed * 7919 + cfg.starts * 31, cfg.electionTicks, t, v, si, st, snap, log, last);
		a.sm := RaftSm.New(cfg.newSm()); a.sm.Restore(snap);
		IF cfg.tickMs > 0 THEN
			NEW(a.clock); a.clock.to := c; a.clock.ms := cfg.tickMs;
			Services.DoLater(a.clock, Services.Ticks() + cfg.tickMs)
		END
	END Start;

	PROCEDURE (a: Node) OnPropose (c: ActorCore.Context; m: Propose), NEW;
		VAR w: Waiter; idx: INTEGER; empty: ARRAY 1 OF BYTE; data: Bytes;
	BEGIN
		IF a.nd.role # Core.leader THEN Answer(c, c.Sender(), m.seq, notLeader, a.nd.leader, 0, NIL)
		ELSIF (m.client <= 0) OR (m.seq <= 0) THEN Answer(c, c.Sender(), m.seq, lost, a.cfg.id, 0, NIL)
		ELSE
			IF m.body = NIL THEN data := RaftSm.Encode(m.client, m.seq, empty, 0)
			ELSE data := RaftSm.Encode(m.client, m.seq, m.body, LEN(m.body^))
			END;
			idx := Core.Propose(a.nd, data);
			IF mutEarlyReply IN mutations THEN Answer(c, c.Sender(), m.seq, ok, a.cfg.id, idx, NIL)
			ELSIF c.Sender() # NIL THEN
				NEW(w); w.index := idx; w.client := m.client; w.seq := m.seq; w.tag := m.seq; w.to := c.Sender(); a.Wait(w)
			END;
			a.Pump(c)
		END
	END OnPropose;

	PROCEDURE (a: Node) OnQuery (c: ActorCore.Context; m: Query), NEW;
		VAR w: Waiter;
	BEGIN
		IF a.nd.role # Core.leader THEN Answer(c, c.Sender(), m.tag, notLeader, a.nd.leader, 0, NIL)
		ELSE
			INC(a.ctx);
			IF Core.ReadIndex(a.nd, a.ctx) THEN
				NEW(w); w.read := TRUE; w.index := -1; w.ctx := a.ctx; w.term := a.nd.term; w.tag := m.tag; w.body := m.body; w.to := c.Sender(); a.Wait(w);
				a.Pump(c)
			ELSE Answer(c, c.Sender(), m.tag, notLeader, a.nd.leader, 0, NIL)
			END
		END
	END OnQuery;

	PROCEDURE (a: Node) Receive* (c: ActorCore.Context; m: ActorCore.Msg);
		VAR x: Core.Msg;
	BEGIN
		IF m IS ActorCore.Started THEN a.Start(c)
		ELSIF (m IS ActorCore.Stopping) OR (m IS ActorCore.Stopped) THEN
			IF a.clock # NIL THEN a.clock.dead := TRUE END
		ELSIF (a.nd = NIL) OR a.broken THEN	(* не поднят или сломан: молчит *)
		ELSIF m IS Tick THEN Core.Tick(a.nd); a.Pump(c)
		ELSIF m IS Wire THEN
			x := m(Wire).m;
			IF (x # NIL) & (x.to = a.cfg.id) & (x.from >= 0) & (x.from < a.cfg.n) & (x.from # a.cfg.id)
				& (x.kind >= Core.voteReq) & (x.kind <= Core.snapReq) THEN
				Core.Step(a.nd, x); a.Pump(c)
			END
		ELSIF m IS Propose THEN a.OnPropose(c, m(Propose))
		ELSIF m IS Query THEN a.OnQuery(c, m(Query))
		END
	END Receive;

	(* --- создание ----------------------------------------------------------- *)

	PROCEDURE NewNode (): ActorCore.Actor;
		VAR a: Node;
	BEGIN
		NEW(a); a.cfg := pending; pending.actor := a; RETURN a
	END NewNode;

	PROCEDURE NewConfig* (id, n: INTEGER; net: ActorNet.Node; storage: RaftStorage.Storage;
		newSm: PROCEDURE (): RaftSm.StateMachine): Config;
	(** Пред: 0 <= id < n <= RaftCore.maxNodes — 20; net, storage, newSm # NIL — 21.
		Пост: устройство с electionTicks = 10, без своих часов и снимков; пропуски пусты. *)
		VAR cfg: Config;
	BEGIN
		ASSERT((id >= 0) & (id < n) & (n <= Core.maxNodes), 20);
		ASSERT((net # NIL) & (storage # NIL) & (newSm # NIL), 21);
		NEW(cfg); cfg.id := id; cfg.n := n; cfg.net := net; cfg.storage := storage; cfg.newSm := newSm;
		cfg.electionTicks := 10; cfg.seed := 1;
		RETURN cfg
	END NewConfig;

	PROCEDURE Start* (cfg: Config): ActorCore.Ref;
	(** Узел-актор — корневой актор узла cfg.net (его имя — имя узла), со своей
		задачей исполнителя (Props.dedicated). Падение узла ActorNet
		останавливает его; новый Start после ActorNet.Up поднимает узел из
		хранилища. Пред: cfg # NIL — 20.
		Пост: адрес актора; первым он получит Started и прочитает хранилище. *)
		VAR p: ActorCore.Props;
	BEGIN
		ASSERT(cfg # NIL, 20);
		pending := cfg;
		p := ActorCore.NewProps(NewNode); p.dedicated := TRUE;
		RETURN ActorCore.Spawn(NIL, p, cfg.net.name)
	END Start;

	PROCEDURE Status* (cfg: Config; OUT role, term, leader, last, lastTerm, commit, applied: INTEGER;
		OUT broken: BOOLEAN);
	(** Наблюдение за узлом: роль (RaftCore.follower/candidate/leader; none —
		узел ещё не прочитал хранилище), срок, подсказка о лидере, последняя
		запись журнала и её срок, зафиксированный и применённый индексы,
		признак отказа хранилища. Пред: cfg # NIL — 20. Пост: значения на
		момент вызова; чтение состояния узла не меняет. *)
	BEGIN
		ASSERT(cfg # NIL, 20);
		role := Core.none; term := 0; leader := Core.none;
		last := 0; lastTerm := 0; commit := 0; applied := 0; broken := FALSE;
		IF cfg.actor # NIL THEN
			broken := cfg.actor.broken;
			IF cfg.actor.nd # NIL THEN
				role := cfg.actor.nd.role; term := cfg.actor.nd.term; leader := cfg.actor.nd.leader;
				last := cfg.actor.nd.last; commit := cfg.actor.nd.commit; applied := cfg.actor.nd.applied;
				IF last > cfg.actor.nd.snapIndex THEN lastTerm := Core.TermAt(cfg.actor.nd, last)
				ELSIF last = cfg.actor.nd.snapIndex THEN lastTerm := cfg.actor.nd.snapTerm
				END
			END
		END
	END Status;

END RaftNode.
