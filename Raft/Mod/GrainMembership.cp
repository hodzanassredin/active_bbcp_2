MODULE RaftGrainMembership;
(** Участник службы состава: ActorGrain берёт членов из RaftMembership.

	Актор узла вступает в службу (Join) со своим пропуском хозяина и
	открытым ключом, продлевает аренду (Renew) и время от времени читает
	список (List) через ReadIndex узла Raft. Каждый ответ List применяется
	к ActorGrain.ApplyMembers: новые члены добавляются, вернувшиеся снова
	живые, пропавшие из списка (выход или истёкшая аренда) — мёртвые.
	Размещение ключей считается по живым членам, поэтому смена состава
	сразу меняет владельцев.

	Ключ только хранится и передаётся в ActorGrain; проверка подписи и
	сертификаты — этап 5.

	Часы: tickMs > 0 — свои (Services), tickMs = 0 — тики шлёт владелец
	(RaftGrainMembership.Tick), так идут детерминированные прогоны. *)

	IMPORT SYSTEM, Kernel, Services, ActorCore, ActorGrain, RaftMembership, Core := RaftCore;

	CONST
		joinTag = 1; renewTag = 2; listTag = 3; leaveTag = 4;	(* метки запросов (Result.kind) *)
		(** переключатели мутаций — только для тестов *)
		mutIgnoreList* = 0;	(** ответ List не применяется *)
		mutAllAlive* = 1;	(** все записи списка считаются живыми *)
		mutNoJoin* = 2;	(** вступление не начинается *)
		mutNoRenew* = 3;	(** аренда не продлевается *)

	TYPE
		Bytes = Core.Bytes;

		TickMsg = POINTER TO RECORD (ActorCore.Msg) END;

		Clock = POINTER TO RECORD (Services.Action) to: ActorCore.Ref; ms: INTEGER; dead: BOOLEAN END;

		Node* = POINTER TO RECORD (ActorCore.Actor)
			cfg-: Config;
			now, lists, applied, members, errors: INTEGER;
			last: RaftMembership.Members;	(* последний применённый список *)
			joined, left: BOOLEAN;
			joinPending, renewPending, listPending: BOOLEAN;
			clock: Clock
		END;

		(** Устройство участника. *)
		Config* = POINTER TO RECORD
			ref-: ActorCore.Ref;	(** адрес актора после Start *)
			id*: INTEGER;	(** номер узла в службе *)
			cl*: ActorGrain.Cluster;	(** кластер виртуальных акторов узла *)
			ms*: RaftMembership.Client;	(** клиент службы состава *)
			key*: Bytes;	(** открытый ключ (проверка — этап 5) *)
			keyLen*: INTEGER;
			lease*: INTEGER;	(** длина аренды в тиках *)
			renewEvery*, refreshEvery*: INTEGER;	(** тиков между Renew и List *)
			tickMs*: INTEGER;	(** > 0 — свои часы; 0 — тики шлёт владелец *)
			actor-: Node
		END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)
		pending: Config;	(* для фабрики Spawn *)
		time*: LONGINT;	(** общее время аренды: его ставит водитель, одинаковое для всех узлов *)
		applying: RaftMembership.Members;
		applyNode: Node;
		applyDone: BOOLEAN;

	PROCEDURE^ CopyCap (IN src: ARRAY OF CHAR; VAR dst: ARRAY OF CHAR);

	PROCEDURE CopyCap (IN src: ARRAY OF CHAR; VAR dst: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(dst) - 1) & (i < LEN(src)) & (src[i] # 0X) DO dst[i] := src[i]; INC(i) END;
		dst[i] := 0X
	END CopyCap;

	PROCEDURE (k: Clock) Do;
		VAR t: TickMsg;
	BEGIN
		IF ~k.dead THEN
			NEW(t); ActorCore.Tell(k.to, t);
			Services.DoLater(k, Services.Ticks() + k.ms)
		END
	END Do;

	PROCEDURE SendJoin (a: Node);
		VAR cap: ARRAY ActorGrain.capLen OF CHAR; body: Bytes;
	BEGIN
		ActorGrain.HostCap(a.cfg.cl, cap);
		body := RaftMembership.JoinCmd(a.cfg.id, cap, a.cfg.key^, a.cfg.keyLen, time, a.cfg.lease);
		RaftMembership.Submit(a.cfg.ms, TRUE, joinTag, body, a.cfg.ref);
		a.joinPending := TRUE
	END SendJoin;

	PROCEDURE SendRenew (a: Node);
		VAR body: Bytes;
	BEGIN
		body := RaftMembership.RenewCmd(a.cfg.id, time, a.cfg.lease);
		RaftMembership.Submit(a.cfg.ms, TRUE, renewTag, body, a.cfg.ref);
		a.renewPending := TRUE
	END SendRenew;

	PROCEDURE SendList (a: Node);
		VAR body: Bytes;
	BEGIN
		body := RaftMembership.ListQuery(time);
		RaftMembership.Submit(a.cfg.ms, FALSE, listTag, body, a.cfg.ref);
		a.listPending := TRUE
	END SendList;

	PROCEDURE (a: Node) OnTick (c: ActorCore.Context), NEW;
	BEGIN
		INC(a.now);
		IF a.cfg.ms.tickMs = 0 THEN RaftMembership.Tick(a.cfg.ms) END;
		IF ~a.left & ~a.joined & ~a.joinPending & ~(mutNoJoin IN mutations) THEN SendJoin(a)
		ELSIF a.joined & ~a.left THEN
			IF (a.cfg.renewEvery > 0) & (a.now MOD a.cfg.renewEvery = 0) & ~a.renewPending & ~(mutNoRenew IN mutations) THEN SendRenew(a) END;
			IF (a.cfg.refreshEvery > 0) & (a.now MOD a.cfg.refreshEvery = 0) & ~a.listPending THEN SendList(a) END
		END
	END OnTick;

	PROCEDURE^ (a: Node) DoApply (list: RaftMembership.Members), NEW;

	PROCEDURE ApplyBody (x, y, z: SYSTEM.ADDRESS);
	BEGIN
		applyDone := FALSE;
		applyNode.DoApply(applying);
		applyDone := TRUE
	END ApplyBody;

	PROCEDURE (a: Node) Apply (list: RaftMembership.Members), NEW;
	(* Применить список под защитой: битый список не убивает участника. *)
	BEGIN
		applying := list; applyNode := a; applyDone := FALSE;
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(ApplyBody)), 0, 0, 0);
		IF ~applyDone THEN INC(a.errors) END
	END Apply;

	PROCEDURE (a: Node) DoApply (list: RaftMembership.Members), NEW;
		VAR i: INTEGER; infos: ARRAY RaftMembership.maxMembers OF ActorGrain.MemberInfo;
	BEGIN
		IF list = NIL THEN RETURN END;
		FOR i := 0 TO list.n - 1 DO
			CopyCap(list.m[i].addr, infos[i].cap); infos[i].id := list.m[i].id;
			infos[i].key := list.m[i].key; infos[i].alive := list.m[i].alive OR (mutAllAlive IN mutations)
		END;
		ActorGrain.ApplyMembers(a.cfg.cl, infos, list.n);
		a.members := list.n; a.applied := list.n; a.last := list; INC(a.lists)
	END DoApply;

	PROCEDURE (a: Node) OnResult (c: ActorCore.Context; r: RaftMembership.Result), NEW;
		VAR list: RaftMembership.Members; res: INTEGER;
	BEGIN
		IF r.kind = joinTag THEN
			a.joinPending := FALSE;
			IF r.res = RaftMembership.ok THEN a.joined := TRUE; SendList(a) END
		ELSIF r.kind = renewTag THEN
			a.renewPending := FALSE;
			IF r.res # RaftMembership.ok THEN a.joined := FALSE END
		ELSIF r.kind = leaveTag THEN
			IF r.res = RaftMembership.ok THEN a.left := TRUE; a.joined := FALSE END
		ELSIF r.kind = listTag THEN
			a.listPending := FALSE;
			IF (r.res = RaftMembership.ok) & (r.data # NIL) & ~(mutIgnoreList IN mutations) THEN
				res := RaftMembership.DecodeList(r.data, list);
				IF res = RaftMembership.ok THEN a.Apply(list) END
			END
		END
	END OnResult;

	PROCEDURE (a: Node) Start (c: ActorCore.Context), NEW;
	BEGIN
		IF a.cfg.tickMs > 0 THEN
			NEW(a.clock); a.clock.to := a.cfg.ref; a.clock.ms := a.cfg.tickMs;
			Services.DoLater(a.clock, Services.Ticks() + a.cfg.tickMs)
		END;
		IF ~(mutNoJoin IN mutations) THEN SendJoin(a) END
	END Start;

	PROCEDURE (a: Node) Receive* (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF m IS ActorCore.Started THEN a.Start(c)
		ELSIF (m IS ActorCore.Stopping) OR (m IS ActorCore.Stopped) THEN
			IF a.clock # NIL THEN a.clock.dead := TRUE END
		ELSIF m IS TickMsg THEN a.OnTick(c)
		ELSIF m IS RaftMembership.Result THEN a.OnResult(c, m(RaftMembership.Result))
		END
	END Receive;

	PROCEDURE NewNode (): ActorCore.Actor;
		VAR a: Node;
	BEGIN
		NEW(a); a.cfg := pending; pending.actor := a; RETURN a
	END NewNode;

	PROCEDURE NewConfig* (id: INTEGER; cl: ActorGrain.Cluster; ms: RaftMembership.Client;
		IN key: ARRAY OF BYTE; keyLen, lease, renewEvery, refreshEvery, tickMs: INTEGER): Config;
	(** Пред: id >= 0 — 20; cl, ms # NIL — 21; 0 <= keyLen <= LEN(key) — 22;
		lease > 0, renewEvery > 0, refreshEvery > 0 — 23. Пост: устройство
		участника; ключ копируется. *)
		VAR cfg: Config; i: INTEGER;
	BEGIN
		ASSERT(id >= 0, 20); ASSERT((cl # NIL) & (ms # NIL), 21);
		ASSERT((keyLen >= 0) & (keyLen <= LEN(key)), 22);
		ASSERT((lease > 0) & (renewEvery > 0) & (refreshEvery > 0), 23);
		NEW(cfg); cfg.id := id; cfg.cl := cl; cfg.ms := ms;
		NEW(cfg.key, MAX(keyLen, 1)); FOR i := 0 TO keyLen - 1 DO cfg.key[i] := key[i] END; cfg.keyLen := keyLen;
		cfg.lease := lease; cfg.renewEvery := renewEvery; cfg.refreshEvery := refreshEvery; cfg.tickMs := tickMs;
		RETURN cfg
	END NewConfig;

	PROCEDURE Start* (cfg: Config): ActorCore.Ref;
	(** Участник службы — актор-ребёнок хозяина cfg.cl. Пред: cfg # NIL — 20.
		Пост: адрес актора; первым он получит Started и пошлёт Join. *)
		VAR p: ActorCore.Props; host: ActorCore.Ref; parent: ActorCore.Context;
	BEGIN
		ASSERT(cfg # NIL, 20);
		pending := cfg;
		p := ActorCore.NewProps(NewNode);
		host := ActorGrain.HostRef(cfg.cl);
		IF host # NIL THEN parent := host(ActorCore.Context) ELSE parent := NIL END;
		cfg.ref := ActorCore.Spawn(parent, p, "$grainmembership");
		RETURN cfg.ref
	END Start;

	PROCEDURE Leave* (cfg: Config);
	(** Выйти из состава: команда Leave уходит службе; после ответа узел
		больше не вступает и не продлевает аренду. *)
		VAR body: Bytes;
	BEGIN
		ASSERT(cfg # NIL, 20);
		IF cfg.ref # NIL THEN
			body := RaftMembership.LeaveCmd(cfg.id);
			RaftMembership.Submit(cfg.ms, TRUE, leaveTag, body, cfg.ref)
		END
	END Leave;

	PROCEDURE Tick* (cfg: Config);
	(** Тик от владельца (tickMs = 0): Join, Renew, List и тик клиента. *)
		VAR t: TickMsg;
	BEGIN
		ASSERT(cfg # NIL, 20);
		IF cfg.ref # NIL THEN NEW(t); ActorCore.Tell(cfg.ref, t) END
	END Tick;

	PROCEDURE Errors* (cfg: Config): INTEGER;
	(** Пост: сколько раз список состава не удалось применить (трап). *)
	BEGIN
		IF cfg.actor = NIL THEN RETURN 0 ELSE RETURN cfg.actor.errors END
	END Errors;

	PROCEDURE List* (cfg: Config): RaftMembership.Members;
	(** Пост: последний применённый список участников (NIL — ещё не было).
		Запись службы состава: адрес, открытый ключ, живость. *)
	BEGIN
		ASSERT(cfg # NIL, 20);
		IF cfg.actor = NIL THEN RETURN NIL ELSE RETURN cfg.actor.last END
	END List;

	PROCEDURE Status* (cfg: Config; OUT joined: BOOLEAN; OUT now, members, lists: INTEGER);
	(** Наблюдение: вступил ли узел, его тик, размер последнего списка и
		число применённых списков. *)
	BEGIN
		ASSERT(cfg # NIL, 20);
		joined := FALSE; now := 0; members := 0; lists := 0;
		IF cfg.actor # NIL THEN
			joined := cfg.actor.joined; now := cfg.actor.now;
			members := cfg.actor.members; lists := cfg.actor.lists
		END
	END Status;

END RaftGrainMembership.
