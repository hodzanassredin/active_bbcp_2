MODULE RaftMembership;
(** Служба состава: список участников кластера в журнале Raft.

	Автомат состояний (Machine) хранит записи участников: номер, адрес
	(пропуск ActorNet.Export хозяина), открытый ключ, срок аренды и время
	вступления. Вступление, выход и продление аренды — команды журнала
	(записи Raft), поэтому список одинаков на всех узлах службы и
	переживает перезапуск в снимке. Живость — аренда: участник продлевает
	её командой Renew, а List считает живым того, у кого
	leaseUntil > now; now передаёт спрашивающий. Время в командах и
	запросах — число самого отправителя (тики водителя или мс), поэтому
	автомат детерминирован: одинаковые байты дают одинаковое состояние.

	Ключ сейчас только хранится: проверка подписи и сертификаты — этап 5,
	шифрование здесь не делается.

	Клиент (Client) — актор-агент на узле ActorNet: он шлёт Propose или
	Query узлу Raft, которого считает лидером, переходит к лидеру по
	подсказке, повторяет при потере ответа и отдаёт итог запросу (Result)
	в его ящик. Запросы одного клиента исполняются по одному: сеанс Raft
	помнит последний номер операции, поэтому повтор не применится дважды.
	Часы: tickMs > 0 — свои (Services), tickMs = 0 — тики шлёт владелец
	(RaftMembership.Tick), так идут детерминированные прогоны под
	ActorSim.

	Переключатели мутаций — только для тестов; в рабочем пути пусто. *)

	IMPORT Services, Strings, ActorCore, ActorNet,
		Core := RaftCore, RaftSm, RaftNode;

	CONST
		maxMembers* = 32;
		addrLen* = 256;
		opJoin = 1; opLeave = 2; opRenew = 3;	(* команды журнала *)
		qList = 1;	(* чтение *)
		(** итог *)
		ok* = 0;
		notFound* = 1;
		full* = 2;
		corrupt* = 3;
		failed* = 4;
		(** переключатели мутаций — только для тестов *)
		mutNoLeave* = 0;	(** выход не меняет запись: участник остаётся *)
		mutNoExpire* = 1;	(** живость не смотрит на аренду *)
		mutNoJoin* = 2;	(** вступление не меняет состав *)
		mutNoRenew* = 3;	(** продление не двигает срок аренды *)

	TYPE
		Bytes = Core.Bytes;
		Addr* = ARRAY addrLen OF CHAR;

		(** Участник службы. alive в записи нет: живость считает List по
			аренде и переданному now. *)
		Member* = RECORD
			id*: INTEGER;
			addr*: Addr;
			key*: Bytes;
			active*: BOOLEAN;	(** не вышел из состава *)
			alive*: BOOLEAN;	(** в ответе List: active и аренда не истекла *)
			joined*: LONGINT;	(** время вступления *)
			leaseUntil*: LONGINT	(** аренда: жив, пока leaseUntil > now *)
		END;

		(** Прочитанный список участников. *)
		Members* = POINTER TO RECORD
			m*: ARRAY maxMembers OF Member;
			n*: INTEGER
		END;

		(** Автомат состояний службы. *)
		Machine* = POINTER TO RECORD (RaftSm.StateMachine)
			m: ARRAY maxMembers OF Member;
			n: INTEGER
		END;

		Buf = RECORD b: Bytes; n: INTEGER END;
		Rd = RECORD b: Bytes; n, len: INTEGER; ok: BOOLEAN END;

		(* сторона клиента *)
		Op = POINTER TO RECORD (ActorCore.Msg)
			write: BOOLEAN;
			kind, tag: INTEGER;	(** kind — метка запроса; tag — метка на проводе *)
			body: Bytes;
			sink: ActorCore.Ref;
			next: Op
		END;

		(** Итог операции клиента. kind — метка, данная при посылке. *)
		Result* = POINTER TO RECORD (ActorCore.Msg)
			kind*, tag*, res*: INTEGER;
			data*: Bytes
		END;

		TickMsg = POINTER TO RECORD (ActorCore.Msg) END;

		Clock = POINTER TO RECORD (Services.Action) to: ActorCore.Ref; ms: INTEGER; dead: BOOLEAN END;

		Agent = POINTER TO RECORD (ActorCore.Actor)
			cl: Client;
			head, tail, cur: Op;
			now, target, sentAt, startAt, waitUntil, tag: INTEGER;
			sent, replies: INTEGER
		END;

		(** Клиент службы состава. id — номер клиента сеанса Raft, новый
			для каждого воплощения. *)
		Client* = POINTER TO RECORD
			tickMs*: INTEGER;	(** > 0 — свои часы, мс на тик; 0 — тики шлёт владелец *)
			timeoutMs*, giveUpMs*: INTEGER;
			timeoutTicks*, giveUpTicks*: INTEGER;
			net: ActorNet.Node;
			n: INTEGER;
			peers: ARRAY Core.maxNodes OF ActorCore.Ref;
			agent: ActorCore.Ref;
			id: INTEGER;
			seq: INTEGER;
			sent*, replies*, nowAt*, targetAt*, curStart*: INTEGER;
			clock: Clock
		END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)
		pendingCl: Client;	(* для фабрики агента *)

	(* --- байты -------------------------------------------------------------- *)

	PROCEDURE Put (VAR f: Buf; x: LONGINT; k: INTEGER);
		VAR i: INTEGER; nb: Bytes;
	BEGIN
		IF f.b = NIL THEN NEW(f.b, 64) END;
		IF f.n + k > LEN(f.b^) THEN
			NEW(nb, 2 * (f.n + k)); FOR i := 0 TO f.n - 1 DO nb[i] := f.b[i] END; f.b := nb
		END;
		FOR i := 0 TO k - 1 DO f.b[f.n] := SHORT(SHORT(SHORT(x MOD 256))); x := x DIV 256; INC(f.n) END
	END Put;

	PROCEDURE PutStr (VAR f: Buf; IN s: ARRAY OF CHAR);
		VAR i, l: INTEGER;
	BEGIN
		l := LEN(s$); Put(f, l, 2); FOR i := 0 TO l - 1 DO Put(f, ORD(s[i]), 2) END
	END PutStr;

	PROCEDURE PutBytes (VAR f: Buf; IN x: ARRAY OF BYTE; len: INTEGER);
		VAR i: INTEGER;
	BEGIN
		Put(f, len, 4); FOR i := 0 TO len - 1 DO Put(f, x[i], 1) END
	END PutBytes;

	PROCEDURE Done (VAR f: Buf): Bytes;
		VAR r: Bytes; i: INTEGER;
	BEGIN
		NEW(r, MAX(f.n, 1)); FOR i := 0 TO f.n - 1 DO r[i] := f.b[i] END; RETURN r
	END Done;

	PROCEDURE Open (VAR r: Rd; b: Bytes; beg, len: INTEGER);
	BEGIN
		r.b := b; r.n := beg; r.len := beg + len; r.ok := b # NIL
	END Open;

	PROCEDURE Get (VAR r: Rd; k: INTEGER): LONGINT;
		VAR i: INTEGER; x: LONGINT;
	BEGIN
		IF ~r.ok OR (r.n + k > r.len) THEN r.ok := FALSE; RETURN 0 END;
		x := 0; FOR i := k - 1 TO 0 BY -1 DO x := x * 256 + r.b[r.n + i] MOD 256 END;
		IF (k < 8) & (x >= ASH(LONG(1), 8 * k - 1)) THEN x := x - ASH(LONG(1), 8 * k) END;
		INC(r.n, k); RETURN x
	END Get;

	PROCEDURE GetStr (VAR r: Rd; OUT s: ARRAY OF CHAR);
		VAR i, l: INTEGER;
	BEGIN
		l := SHORT(Get(r, 2)); IF (l < 0) OR (l >= LEN(s)) THEN r.ok := FALSE; l := 0 END;
		i := 0; WHILE r.ok & (i < l) DO s[i] := CHR(Get(r, 2) MOD 65536); INC(i) END;
		s[i] := 0X
	END GetStr;

	PROCEDURE GetBytes (VAR r: Rd; OUT x: Bytes; OUT len: INTEGER);
		VAR i: INTEGER;
	BEGIN
		len := SHORT(Get(r, 4)); x := NIL;
		IF ~r.ok OR (len < 0) OR (r.n + len > r.len) THEN r.ok := FALSE; len := 0; RETURN END;
		NEW(x, MAX(len, 1)); FOR i := 0 TO len - 1 DO x[i] := r.b[r.n + i] END; INC(r.n, len)
	END GetBytes;

	PROCEDURE CopyKey (IN key: ARRAY OF BYTE; len: INTEGER): Bytes;
		VAR b: Bytes; i: INTEGER;
	BEGIN
		NEW(b, MAX(len, 1)); FOR i := 0 TO len - 1 DO b[i] := key[i] END; RETURN b
	END CopyKey;

	(* --- команды и запросы --------------------------------------------------- *)

	PROCEDURE JoinCmd* (id: INTEGER; IN addr: ARRAY OF CHAR; IN key: ARRAY OF BYTE; keyLen: INTEGER;
		now, lease: LONGINT): Bytes;
	(** Пред: id >= 0 — 20; 0 <= keyLen <= LEN(key) — 21; lease >= 0 — 22.
		Пост: тело команды вступления. *)
		VAR f: Buf;
	BEGIN
		ASSERT(id >= 0, 20); ASSERT((keyLen >= 0) & (keyLen <= LEN(key)), 21); ASSERT(lease >= 0, 22);
		f.b := NIL; f.n := 0; Put(f, opJoin, 1); Put(f, id, 4); Put(f, now, 8); Put(f, lease, 8);
		PutStr(f, addr); PutBytes(f, key, keyLen); RETURN Done(f)
	END JoinCmd;

	PROCEDURE LeaveCmd* (id: INTEGER): Bytes;
	(** Пред: id >= 0 — 20. Пост: тело команды выхода. *)
		VAR f: Buf;
	BEGIN
		ASSERT(id >= 0, 20);
		f.b := NIL; f.n := 0; Put(f, opLeave, 1); Put(f, id, 4); RETURN Done(f)
	END LeaveCmd;

	PROCEDURE RenewCmd* (id: INTEGER; now, lease: LONGINT): Bytes;
	(** Пред: id >= 0 — 20; lease >= 0 — 21. Пост: тело команды продления. *)
		VAR f: Buf;
	BEGIN
		ASSERT(id >= 0, 20); ASSERT(lease >= 0, 21);
		f.b := NIL; f.n := 0; Put(f, opRenew, 1); Put(f, id, 4); Put(f, now, 8); Put(f, lease, 8); RETURN Done(f)
	END RenewCmd;

	PROCEDURE ListQuery* (now: LONGINT): Bytes;
	(** Пост: тело запроса списка на время now. *)
		VAR f: Buf;
	BEGIN
		f.b := NIL; f.n := 0; Put(f, qList, 1); Put(f, now, 8); RETURN Done(f)
	END ListQuery;

	(* --- автомат -------------------------------------------------------------- *)

	PROCEDURE Find (m: Machine; id: INTEGER): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < m.n) & (m.m[i].id # id) DO INC(i) END;
		IF i = m.n THEN RETURN -1 ELSE RETURN i END
	END Find;

	PROCEDURE ByteRes (res: INTEGER): Bytes;
		VAR b: Bytes;
	BEGIN
		NEW(b, 1); b[0] := SHORT(SHORT(res)); RETURN b
	END ByteRes;

	PROCEDURE CopyAddr (IN s: ARRAY OF CHAR; OUT dst: Addr);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(dst) - 1) & (i < LEN(s)) & (s[i] # 0X) DO dst[i] := s[i]; INC(i) END;
		dst[i] := 0X
	END CopyAddr;

	PROCEDURE (m: Machine) Apply* (index: INTEGER; IN cmd: ARRAY OF BYTE; beg, len: INTEGER): Bytes;
	(* Применить команду: вступление, выход или продление аренды. *)
		VAR r: Rd; op, res, i, id, klen: INTEGER; addr: Addr; key: Bytes; now, lease: LONGINT; b: Bytes;
	BEGIN
		IF len < 1 THEN RETURN ByteRes(corrupt) END;
		(* тело команды — в cmd[beg .. beg+len-1]; копия, потому что запись
			журнала может быть освобождена после применения *)
		NEW(b, MAX(len, 1)); FOR i := 0 TO len - 1 DO b[i] := cmd[beg + i] END;
		Open(r, b, 0, len); op := SHORT(Get(r, 1));
		IF op = opJoin THEN
			IF len < 1 + 4 + 8 + 8 + 2 THEN RETURN ByteRes(corrupt) END;
			id := SHORT(Get(r, 4)); now := Get(r, 8); lease := Get(r, 8);
			GetStr(r, addr); GetBytes(r, key, klen);
			IF ~r.ok OR (id < 0) THEN RETURN ByteRes(corrupt) END;
			IF mutNoJoin IN mutations THEN RETURN ByteRes(ok) END;
			i := Find(m, id);
			IF i >= 0 THEN
				m.m[i].active := TRUE; CopyAddr(addr, m.m[i].addr); m.m[i].key := key;
				m.m[i].leaseUntil := now + lease; res := ok
			ELSIF m.n >= maxMembers THEN res := full
			ELSE
				i := m.n; m.m[i].id := id; CopyAddr(addr, m.m[i].addr); m.m[i].key := key;
				m.m[i].active := TRUE; m.m[i].joined := now; m.m[i].leaseUntil := now + lease;
				INC(m.n); res := ok
			END;
			RETURN ByteRes(res)
		ELSIF op = opLeave THEN
			IF len < 1 + 4 THEN RETURN ByteRes(corrupt) END;
			id := SHORT(Get(r, 4));
			IF ~r.ok THEN RETURN ByteRes(corrupt) END;
			IF mutNoLeave IN mutations THEN RETURN ByteRes(ok) END;
			i := Find(m, id);
			IF i < 0 THEN res := notFound
			ELSE m.m[i].active := FALSE; m.m[i].leaseUntil := 0; res := ok
			END;
			RETURN ByteRes(res)
		ELSIF op = opRenew THEN
			IF len < 1 + 4 + 8 + 8 THEN RETURN ByteRes(corrupt) END;
			id := SHORT(Get(r, 4)); now := Get(r, 8); lease := Get(r, 8);
			IF ~r.ok THEN RETURN ByteRes(corrupt) END;
			i := Find(m, id);
			IF (i < 0) OR ~m.m[i].active THEN res := notFound
			ELSIF mutNoRenew IN mutations THEN res := ok
			ELSE m.m[i].leaseUntil := now + lease; res := ok
			END;
			RETURN ByteRes(res)
		END;
		RETURN ByteRes(corrupt)
	END Apply;

	PROCEDURE (m: Machine) Snapshot* (): Bytes;
		VAR f: Buf; i: INTEGER;
	BEGIN
		f.b := NIL; f.n := 0; Put(f, 1, 4); Put(f, m.n, 4);
		FOR i := 0 TO m.n - 1 DO
			Put(f, m.m[i].id, 4); Put(f, m.m[i].joined, 8); Put(f, m.m[i].leaseUntil, 8);
			IF m.m[i].active THEN Put(f, 1, 1) ELSE Put(f, 0, 1) END;
			PutStr(f, m.m[i].addr);
			IF m.m[i].key = NIL THEN Put(f, 0, 4) ELSE PutBytes(f, m.m[i].key^, LEN(m.m[i].key^)) END
		END;
		RETURN Done(f)
	END Snapshot;

	PROCEDURE (m: Machine) Restore* (b: Bytes);
		VAR r: Rd; i, n, ver, klen: INTEGER; key: Bytes; addr: Addr; act: INTEGER;
	BEGIN
		m.n := 0;
		IF b = NIL THEN RETURN END;
		Open(r, b, 0, LEN(b^)); ver := SHORT(Get(r, 4)); n := SHORT(Get(r, 4));
		IF ~r.ok OR (ver # 1) OR (n < 0) OR (n > maxMembers) THEN m.n := 0; RETURN END;
		i := 0;
		WHILE r.ok & (i < n) DO
			m.m[i].id := SHORT(Get(r, 4)); m.m[i].joined := Get(r, 8); m.m[i].leaseUntil := Get(r, 8);
			act := SHORT(Get(r, 1)); GetStr(r, addr); GetBytes(r, key, klen);
			IF r.ok THEN
				m.m[i].active := act # 0; CopyAddr(addr, m.m[i].addr); m.m[i].key := key; INC(i)
			END
		END;
		IF r.ok THEN m.n := n ELSE m.n := 0 END
	END Restore;

	PROCEDURE (m: Machine) Query* (IN q: ARRAY OF BYTE; len: INTEGER): Bytes;
	(* Список участников на время now: жив, если active и аренда не истекла. *)
		VAR r: Rd; i, op, cnt: INTEGER; now: LONGINT; f: Buf; alive: BOOLEAN; b: Bytes;
	BEGIN
		NEW(b, MAX(len, 1)); FOR i := 0 TO len - 1 DO b[i] := q[i] END;
		Open(r, b, 0, len); op := SHORT(Get(r, 1)); now := Get(r, 8);
		IF ~r.ok OR (op # qList) THEN RETURN ByteRes(corrupt) END;
		f.b := NIL; f.n := 0; cnt := m.n; Put(f, cnt, 4);
		FOR i := 0 TO m.n - 1 DO
			IF mutNoExpire IN mutations THEN alive := m.m[i].active
			ELSE alive := m.m[i].active & (m.m[i].leaseUntil > now)
			END;
			Put(f, m.m[i].id, 4);
			IF alive THEN Put(f, 1, 1) ELSE Put(f, 0, 1) END;
			Put(f, m.m[i].joined, 8); Put(f, m.m[i].leaseUntil, 8);
			PutStr(f, m.m[i].addr);
			IF m.m[i].key = NIL THEN Put(f, 0, 4) ELSE PutBytes(f, m.m[i].key^, LEN(m.m[i].key^)) END
		END;
		RETURN Done(f)
	END Query;

	PROCEDURE DecodeList* (data: Bytes; OUT members: Members): INTEGER;
	(** Пост: members — разобранный ответ List; res = ok или corrupt. *)
		VAR r: Rd; i, n, alen, klen: INTEGER; addr: Addr; key: Bytes; alive: INTEGER;
	BEGIN
		NEW(members); members.n := 0;
		IF data = NIL THEN RETURN corrupt END;
		Open(r, data, 0, LEN(data^)); n := SHORT(Get(r, 4));
		IF ~r.ok OR (n < 0) OR (n > maxMembers) THEN RETURN corrupt END;
		i := 0;
		WHILE r.ok & (i < n) DO
			members.m[i].id := SHORT(Get(r, 4)); alive := SHORT(Get(r, 1));
			members.m[i].joined := Get(r, 8); members.m[i].leaseUntil := Get(r, 8);
			GetStr(r, addr); GetBytes(r, key, klen);
			IF r.ok THEN
				members.m[i].alive := alive # 0; CopyAddr(addr, members.m[i].addr);
				members.m[i].key := key; INC(i)
			END
		END;
		IF ~r.ok THEN members.n := 0; RETURN corrupt END;
		members.n := n; RETURN ok
	END DecodeList;

	PROCEDURE NewMachine* (): RaftSm.StateMachine;
	(** Пост: пустая служба состава — для RaftNode.Config.newSm. *)
		VAR m: Machine;
	BEGIN
		NEW(m); RETURN m
	END NewMachine;

	(* --- клиент: агент -------------------------------------------------------- *)

	PROCEDURE (k: Clock) Do;
		VAR t: TickMsg;
	BEGIN
		IF ~k.dead THEN
			NEW(t); ActorCore.Tell(k.to, t);
			Services.DoLater(k, Services.Ticks() + k.ms)
		END
	END Do;

	PROCEDURE (g: Agent) Send (c: ActorCore.Context), NEW;
		VAR p: RaftNode.Propose; q: RaftNode.Query;
	BEGIN
		IF g.cur.write THEN
			NEW(p); p.client := g.cl.id; p.seq := g.cur.tag; p.body := g.cur.body; c.Tell(g.cl.peers[g.target], p)
		ELSE NEW(q); q.tag := -g.cur.tag; q.body := g.cur.body; c.Tell(g.cl.peers[g.target], q)	(* метка чтения < 0: не спутать с номером команды *)
		END;
		INC(g.cl.sent); g.cl.targetAt := g.target;
		g.sentAt := g.now; g.waitUntil := -1
	END Send;

	PROCEDURE (g: Agent) Start (c: ActorCore.Context), NEW;
		VAR op: Op;
	BEGIN
		WHILE (g.cur = NIL) & (g.head # NIL) DO
			op := g.head; g.head := op.next; IF g.head = NIL THEN g.tail := NIL END; op.next := NIL;
			g.cur := op; g.startAt := g.now; g.cl.curStart := g.startAt;
			IF g.cur.write THEN INC(g.cl.seq); g.cur.tag := g.cl.seq
			ELSE INC(g.tag); g.cur.tag := g.tag
			END;
			g.Send(c)
		END
	END Start;

	PROCEDURE (g: Agent) Finish (c: ActorCore.Context; res: INTEGER; data: Bytes), NEW;
		VAR op: Op; r: Result;
	BEGIN
		op := g.cur; g.cur := NIL;
		IF op.write & (data # NIL) & (LEN(data^) >= 1) THEN res := data[0] END;	(* итог автомата *)
		IF op.sink # NIL THEN
			NEW(r); r.kind := op.kind; r.tag := op.tag; r.res := res; r.data := data; c.Tell(op.sink, r)
		END;
		g.Start(c)
	END Finish;

	PROCEDURE (g: Agent) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR r: RaftNode.Reply; op: Op;
	BEGIN
		IF m IS Op THEN
			op := m(Op);
			IF g.tail = NIL THEN g.head := op ELSE g.tail.next := op END;
			g.tail := op; g.Start(c)
		ELSIF m IS TickMsg THEN
			INC(g.now); g.cl.nowAt := g.now;
			IF g.cur # NIL THEN
				IF g.now - g.startAt > g.cl.giveUpTicks THEN g.Finish(c, failed, NIL)
				ELSIF g.waitUntil >= 0 THEN IF g.now >= g.waitUntil THEN g.Send(c) END
				ELSIF g.now - g.sentAt >= g.cl.timeoutTicks THEN
					g.target := (g.target + 1) MOD g.cl.n; g.Send(c)
				END
			END
		ELSIF (m IS RaftNode.Reply) & (g.cur # NIL) THEN
			r := m(RaftNode.Reply); INC(g.cl.replies);
			IF (g.cur.write & (r.tag = g.cur.tag)) OR (~g.cur.write & (r.tag = -g.cur.tag)) THEN
				IF r.res = RaftNode.ok THEN g.Finish(c, ok, r.data)
				ELSIF (r.res = RaftNode.notLeader) & (r.leader >= 0) & (r.leader < g.cl.n) & (r.leader # g.target) THEN
					g.target := r.leader; g.Send(c)
				ELSIF r.res = RaftNode.notLeader THEN
					g.target := (g.target + 1) MOD g.cl.n; g.waitUntil := g.now + g.cl.timeoutTicks DIV 10 + 1
				ELSE g.Send(c)	(* lost — повтор у того же узла *)
				END
			END
		END
	END Receive;

	PROCEDURE NewAgent (): ActorCore.Actor;
		VAR g: Agent;
	BEGIN
		NEW(g); g.cl := pendingCl; RETURN g
	END NewAgent;

	(* --- клиент: сторона владельца -------------------------------------------- *)

	PROCEDURE Submit* (c: Client; write: BOOLEAN; kind: INTEGER; body: Bytes; sink: ActorCore.Ref);
	(** Пред: c # NIL — 20; body # NIL — 21; sink # NIL — 22. Пост: операция
		встала в очередь агента; итог придёт sink письмом Result. *)
		VAR op: Op;
	BEGIN
		ASSERT(c # NIL, 20); ASSERT(body # NIL, 21); ASSERT(sink # NIL, 22);
		IF (c.tickMs > 0) & (c.clock = NIL) THEN
			NEW(c.clock); c.clock.to := c.agent; c.clock.ms := c.tickMs;
			Services.DoLater(c.clock, Services.Ticks() + c.tickMs)
		END;
		NEW(op); op.write := write; op.kind := kind; op.body := body; op.sink := sink;
		ActorCore.Tell(c.agent, op)
	END Submit;

	PROCEDURE Tick* (c: Client);
	(** Тик часов агента от владельца (tickMs = 0). *)
		VAR t: TickMsg;
	BEGIN
		ASSERT(c # NIL, 20);
		NEW(t); ActorCore.Tell(c.agent, t)
	END Tick;

	PROCEDURE Trace* (c: Client; OUT sent, replies, now, target, curStart: INTEGER);
	(** Наблюдение за агентом (для тестов): отправлено, ответов, тик агента,
		цель и тик начала текущей операции. *)
	BEGIN
		sent := 0; replies := 0; now := 0; target := 0; curStart := 0;
		IF c # NIL THEN
			sent := c.sent; replies := c.replies; now := c.nowAt; target := c.targetAt; curStart := c.curStart
		END
	END Trace;

	PROCEDURE Stats* (c: Client; OUT sent, replies, seq: INTEGER);
	(** Наблюдение за агентом (для тестов): отправлено, получено ответов, номер операции. *)
	BEGIN
		sent := 0; replies := 0; seq := 0;
		IF c # NIL THEN sent := c.sent; replies := c.replies; seq := c.seq END
	END Stats;

	PROCEDURE Close* (c: Client);
	(** Пост: часы погашены, агент остановлен; запросы без ответа ответа не получат. *)
	BEGIN
		IF c.clock # NIL THEN c.clock.dead := TRUE END;
		ActorCore.StopActor(c.agent)
	END Close;

	PROCEDURE New* (net: ActorNet.Node; IN caps: RaftNode.Caps; n, id: INTEGER): Client;
	(** Журнал службы состава на узле net для кластера из n узлов RaftNode
		с пропусками caps[0 .. n-1]. id — номер клиента сеанса, новый для
		каждого воплощения.
		Пред: net # NIL — 20; 0 < n <= RaftCore.maxNodes — 21; id > 0 — 22;
		caps[0 .. n-1] — пропуски — 23. Пост: агент создан, свои часы
		(tickMs = 10) пойдут с первым запросом. *)
		VAR c: Client; i: INTEGER; name: ActorCore.Name; d: ARRAY 16 OF CHAR;
	BEGIN
		ASSERT(net # NIL, 20); ASSERT((n > 0) & (n <= Core.maxNodes), 21); ASSERT(id > 0, 22);
		NEW(c); c.net := net; c.n := n; c.id := id;
		c.tickMs := 10; c.timeoutMs := 1000; c.giveUpMs := 20000; c.timeoutTicks := 100; c.giveUpTicks := 2000;
		FOR i := 0 TO n - 1 DO c.peers[i] := ActorNet.Import(net, caps[i]); ASSERT(c.peers[i] # NIL, 23) END;
		Strings.IntToString(id, d); name := net.name$ + "/$membership" + d$;
		pendingCl := c; c.agent := ActorCore.Spawn(NIL, ActorCore.NewProps(NewAgent), name); pendingCl := NIL;
		RETURN c
	END New;

	PROCEDURE SetPeer* (c: Client; id: INTEGER; IN cap: ARRAY OF CHAR);
	(** Дописать пропуск узла id (служба выросла). Пред: 0 <= id < n — 20;
		cap разбирается — 21. Пост: агент шлёт письма и этому узлу. *)
		VAR r: ActorCore.Ref;
	BEGIN
		ASSERT((id >= 0) & (id < c.n), 20);
		r := ActorNet.Import(c.net, cap); ASSERT(r # NIL, 21);
		c.peers[id] := r
	END SetPeer;

END RaftMembership.
