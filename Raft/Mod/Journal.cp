MODULE RaftJournal;
(** Журнал стойких акторов (ActorJournal.Journal) поверх Raft.

	Журнал — автомат состояний Raft (Machine): команды «дописать событие»
	и «сохранить снимок» идут в журнал Raft и применяются на каждом узле
	к своей копии журнала в памяти (ActorJournal.NewMem). Оптимистическая
	проверка номера (expected) выполняется при применении, поэтому она
	одна на кластер: два воплощения актора на разных узлах не допишут
	поток мимо друг друга — второе получит conflict.

	Client — сторона актора: реализация ActorJournal.Journal, которую
	ставят в ActorPersist.Actor.journal. Запросы Client исполняет его
	агент — актор на узле net ("узел/$journal.id"): Submit, SubmitSnapshot
	и Replay отдают запрос агенту и возвращаются сразу; агент шлёт
	RaftNode.Propose (запись) или RaftNode.Query (чтение, ReadIndex) узлу,
	который считает лидером, и отвечает запросу (Request.Done), когда
	лидер ответил: запись — после фиксации большинством и применения на
	лидере, чтение — после подтверждения лидерства. Не-лидер называет
	лидера, агент переходит к нему; нет ответа за timeoutTicks тиков — к
	следующему узлу; за giveUpTicks — ответ failed («неизвестно»: команда
	могла пройти). Запросы одного Client агент исполняет по одному: сеанс
	Raft — одна операция клиента за раз.

	Часы агента: tickMs > 0 (по умолчанию 10) — свои часы главного цикла
	(Services), тайм-ауты в миллисекундах timeoutMs и giveUpMs; tickMs = 0
	— тики шлёт владелец (Tick), так работает детерминированный прогон
	под ActorSim, где одна задача-водитель обслуживает и стойкие акторы, и
	узлы Raft. Ставить tickMs до первого запроса.

	Append, SaveSnapshot, Last, Read и LoadSnapshot — те же запросы с
	ожиданием ответа (AsyncChan): только из задачи и не под ActorSim.

	Ограничения.
	- res = failed у записи значит «неизвестно». ActorPersist на failed
	  трапует, и перезапуск актора перечитывает журнал — состояние снова
	  верное.
	- Номер клиента id обязан быть новым для каждого воплощения Client:
	  сеанс помнит последний номер операции, и старый id с номерами
	  сначала получил бы ответы из памяти сеанса.
	- Снимок автомата — все принятые команды подряд; сеансы не
	  вытесняются (граница этапа). *)

	IMPORT Services, Strings, Tasks, AsyncChan, AsyncSelect, ActorCore, ActorNet, ActorJournal,
		Core := RaftCore, RaftSm, RaftNode;

	CONST
		opAppend = 1; opSnap = 2;	(* команды *)
		qLast = 1; qRead = 2; qLoad = 3; qReplay = 4;	(* чтения *)
		(** переключатели мутаций — только для тестов *)
		mutNoWait* = 0;	(** команда отправлена, ответ «записано» — не дожидаясь фиксации *)
		mutNoExpected* = 1;	(** автомат дописывает событие, не сверяя expected с последним номером *)

	TYPE
		Bytes = Core.Bytes;

		Buf = RECORD b: Bytes; n: INTEGER END;
		Rd = RECORD b: Bytes; n, len: INTEGER; ok: BOOLEAN END;

		Cmd = POINTER TO RECORD b: Bytes; next: Cmd END;

		(** Автомат журнала: копия журнала на узле. *)
		Machine* = POINTER TO RECORD (RaftSm.StateMachine)
			mem-: ActorJournal.Journal;
			cmds, tail: Cmd; ncmds: INTEGER
		END;

		Collect = POINTER TO RECORD (ActorJournal.Sink) out: Buf; count: INTEGER END;

		Clock = POINTER TO RECORD (Services.Action) to: ActorCore.Ref; ms: INTEGER; dead: BOOLEAN END;

		(** Сторона актора. *)
		Client* = POINTER TO RECORD (ActorJournal.Journal)
			tickMs*: INTEGER;	(** > 0 — свои часы, мс на тик (по умолчанию 10); 0 — тики шлёт владелец *)
			timeoutMs*, giveUpMs*: INTEGER;	(** при своих часах: ожидание одного узла и предел операции, мс; 1000 и 20000 *)
			timeoutTicks*, giveUpTicks*: INTEGER;	(** при tickMs = 0: то же в тиках; 100 и 2000 *)
			lastIndex-: INTEGER;	(** индекс записи Raft последней удачной команды *)
			retries-: INTEGER;	(** переходов к другому узлу и повторов *)
			seq-: INTEGER;	(** номер последней операции сеанса *)
			id-: INTEGER;	(** номер клиента сеанса *)
			net: ActorNet.Node;
			n: INTEGER;
			peers: ARRAY Core.maxNodes OF ActorCore.Ref;
			agent: ActorCore.Ref;
			clock: Clock
		END;

		(* операция клиента: команда (запись) или чтение *)
		Op = POINTER TO RECORD (ActorCore.Msg)
			cmd: BOOLEAN; what: INTEGER;	(* opAppend, opSnap или qReplay; 0 — сырые данные для Wait *)
			body: Bytes;
			r: ActorJournal.Request;
			next: Op
		END;

		TickMsg = POINTER TO RECORD (ActorCore.Msg) END;

		Agent = POINTER TO RECORD (ActorCore.Actor)
			cl: Client;
			head, tail, cur: Op;
			now, target, tag, sentAt, startAt, waitUntil, tmo, giveUp: INTEGER
		END;

		(* ожидание ответа для блокирующих методов *)
		Wait = POINTER TO RECORD (ActorJournal.Request)
			box: AsyncChan.Mailbox; got: BOOLEAN; data: Bytes; res: INTEGER
		END;

		Token = POINTER TO RECORD (AsyncSelect.Msg) END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)
		pendingCl: Client;	(* для фабрики агента *)

	(* --- байты ------------------------------------------------------------- *)

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
		NEW(r, f.n); FOR i := 0 TO f.n - 1 DO r[i] := f.b[i] END; RETURN r
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

	PROCEDURE PutMeta (VAR f: Buf; IN m: ActorJournal.Meta);
	BEGIN
		Put(f, m.seq, 8); Put(f, m.time, 8); Put(f, m.cause, 8); PutStr(f, m.author)
	END PutMeta;

	PROCEDURE GetMeta (VAR r: Rd; OUT m: ActorJournal.Meta);
	BEGIN
		m.seq := Get(r, 8); m.time := Get(r, 8); m.cause := Get(r, 8); GetStr(r, m.author)
	END GetMeta;

	(* --- автомат ------------------------------------------------------------ *)

	PROCEDURE (m: Machine) Do (b: Bytes; beg, len: INTEGER): INTEGER, NEW;
	(* применить команду b[beg .. beg+len-1]; результат — код ActorJournal *)
		VAR r: Rd; op, res, blen: INTEGER; st: ActorJournal.Name; exp, seq: LONGINT; meta: ActorJournal.Meta;
			body: Bytes;
	BEGIN
		Open(r, b, beg, len);
		op := SHORT(Get(r, 1)); GetStr(r, st);
		IF ~r.ok OR (st = "") THEN RETURN ActorJournal.corrupt END;
		IF op = opAppend THEN
			exp := Get(r, 8); GetMeta(r, meta); GetBytes(r, body, blen);
			IF ~r.ok OR (meta.seq # exp + 1) THEN RETURN ActorJournal.corrupt END;
			IF mutNoExpected IN mutations THEN exp := m.mem.Last(st); meta.seq := exp + 1 END;
			m.mem.Append(st, exp, meta, body, blen, res)
		ELSIF op = opSnap THEN
			seq := Get(r, 8); GetBytes(r, body, blen);
			IF ~r.ok THEN RETURN ActorJournal.corrupt END;
			IF (seq < 1) OR (seq > m.mem.Last(st)) THEN RETURN ActorJournal.conflict END;
			m.mem.SaveSnapshot(st, seq, body, blen, res)
		ELSE RETURN ActorJournal.corrupt
		END;
		RETURN res
	END Do;

	PROCEDURE (m: Machine) Keep (IN cmd: ARRAY OF BYTE; beg, len: INTEGER), NEW;
		VAR c: Cmd; i: INTEGER;
	BEGIN
		NEW(c); NEW(c.b, MAX(len, 1)); FOR i := 0 TO len - 1 DO c.b[i] := cmd[beg + i] END;
		IF m.tail = NIL THEN m.cmds := c ELSE m.tail.next := c END;
		m.tail := c; INC(m.ncmds)
	END Keep;

	PROCEDURE (m: Machine) Apply* (index: INTEGER; IN cmd: ARRAY OF BYTE; beg, len: INTEGER): Bytes;
		VAR b, r: Bytes; i, res: INTEGER;
	BEGIN
		NEW(b, MAX(len, 1)); FOR i := 0 TO len - 1 DO b[i] := cmd[beg + i] END;
		res := m.Do(b, 0, len);
		IF res = ActorJournal.ok THEN m.Keep(cmd, beg, len) END;
		NEW(r, 1); r[0] := SHORT(SHORT(res)); RETURN r
	END Apply;

	PROCEDURE (m: Machine) Snapshot* (): Bytes;
		VAR f: Buf; c: Cmd;
	BEGIN
		f.b := NIL; f.n := 0; Put(f, m.ncmds, 4);
		c := m.cmds; WHILE c # NIL DO PutBytes(f, c.b, LEN(c.b^)); c := c.next END;
		RETURN Done(f)
	END Snapshot;

	PROCEDURE (m: Machine) Restore* (b: Bytes);
		VAR r: Rd; k, cnt, len: INTEGER; x: Bytes;
	BEGIN
		m.mem := ActorJournal.NewMem(); m.cmds := NIL; m.tail := NIL; m.ncmds := 0;
		IF b # NIL THEN
			Open(r, b, 0, LEN(b^)); cnt := SHORT(Get(r, 4)); k := 0;
			WHILE r.ok & (k < cnt) DO
				GetBytes(r, x, len);
				IF r.ok & (m.Do(x, 0, len) = ActorJournal.ok) THEN m.Keep(x, 0, len) END;
				INC(k)
			END
		END
	END Restore;

	PROCEDURE (s: Collect) Event (IN m: ActorJournal.Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
	BEGIN
		PutMeta(s.out, m); PutBytes(s.out, body, len); INC(s.count); RETURN TRUE
	END Event;

	PROCEDURE (m: Machine) Query* (IN q: ARRAY OF BYTE; len: INTEGER): Bytes;
		VAR r: Rd; op, res, dlen, i: INTEGER; st: ActorJournal.Name; f: Buf; from, seq: LONGINT; s: Collect;
			b, data: Bytes;
	BEGIN
		NEW(b, MAX(len, 1)); FOR i := 0 TO len - 1 DO b[i] := q[i] END;
		Open(r, b, 0, len); op := SHORT(Get(r, 1)); GetStr(r, st); f.b := NIL; f.n := 0;
		IF ~r.ok THEN Put(f, ActorJournal.corrupt, 1)
		ELSIF op = qLast THEN Put(f, ActorJournal.ok, 1); Put(f, m.mem.Last(st), 8)
		ELSIF op = qRead THEN
			from := Get(r, 8); NEW(s); s.out.n := 0; Put(s.out, 0, 4);
			m.mem.Read(st, from, s, res);
			i := s.out.n; s.out.n := 0; Put(s.out, s.count, 4); s.out.n := i;
			Put(f, res, 1); PutBytes(f, s.out.b, s.out.n)
		ELSIF op = qLoad THEN
			m.mem.LoadSnapshot(st, seq, data, dlen, res);
			Put(f, res, 1); Put(f, seq, 8);
			IF data = NIL THEN Put(f, 0, 4) ELSE PutBytes(f, data, dlen) END
		ELSIF op = qReplay THEN	(* снимок, если есть, и события после него *)
			m.mem.LoadSnapshot(st, seq, data, dlen, res);
			IF res = ActorJournal.absent THEN seq := 0; data := NIL; res := ActorJournal.ok END;
			IF res # ActorJournal.ok THEN Put(f, res, 1)
			ELSE
				NEW(s); s.out.n := 0; Put(s.out, 0, 4);
				m.mem.Read(st, seq + 1, s, res);
				i := s.out.n; s.out.n := 0; Put(s.out, s.count, 4); s.out.n := i;
				Put(f, res, 1); Put(f, seq, 8);
				IF data = NIL THEN Put(f, 0, 1) ELSE Put(f, 1, 1); PutBytes(f, data, dlen) END;
				PutBytes(f, s.out.b, s.out.n)
			END
		ELSE Put(f, ActorJournal.corrupt, 1)
		END;
		RETURN Done(f)
	END Query;

	PROCEDURE NewMachine* (): RaftSm.StateMachine;
	(** Пост: пустой автомат журнала — для RaftNode.Config.newSm. *)
		VAR m: Machine;
	BEGIN
		NEW(m); m.mem := ActorJournal.NewMem(); RETURN m
	END NewMachine;

	(* --- сторона актора: агент --------------------------------------------------- *)

	PROCEDURE (k: Clock) Do;
		VAR t: TickMsg;
	BEGIN
		IF ~k.dead THEN
			NEW(t); ActorCore.Tell(k.to, t);
			Services.DoLater(k, Services.Ticks() + k.ms)
		END
	END Do;

	PROCEDURE (w: Wait) Done (res: INTEGER);
		VAR t: Token;
	BEGIN
		w.res := res; NEW(t); w.box.Send(t)
	END Done;

	PROCEDURE Answer (op: Op; got: BOOLEAN; data: Bytes);
	(* итог операции — её запросу *)
		VAR rd, e: Rd; res, len, blen, cnt, k: INTEGER; seq: LONGINT; has: BOOLEAN; list, snap, body: Bytes;
			m: ActorJournal.Meta; w: Wait;
	BEGIN
		IF op.r IS Wait THEN w := op.r(Wait); w.got := got; w.data := data; w.Done(ActorJournal.ok)
		ELSIF op.cmd THEN
			IF got & (data # NIL) & (LEN(data^) >= 1) THEN op.r.Done(data[0]) ELSE op.r.Done(ActorJournal.failed) END
		ELSIF ~got OR (data = NIL) THEN op.r.Done(ActorJournal.failed)
		ELSE	(* qReplay *)
			Open(rd, data, 0, LEN(data^)); res := SHORT(Get(rd, 1));
			IF ~rd.ok THEN op.r.Done(ActorJournal.corrupt); RETURN END;
			IF res # ActorJournal.ok THEN op.r.Done(res); RETURN END;
			seq := Get(rd, 8); has := Get(rd, 1) = 1; snap := NIL; len := 0;
			IF has THEN GetBytes(rd, snap, len) END;
			GetBytes(rd, list, blen);
			IF ~rd.ok THEN op.r.Done(ActorJournal.corrupt); RETURN END;
			IF has THEN op.r.Snapshot(seq, snap^, len) END;
			Open(e, list, 0, blen); cnt := SHORT(Get(e, 4)); k := 0;
			WHILE e.ok & (k < cnt) DO
				GetMeta(e, m); GetBytes(e, body, blen);
				IF e.ok & ~op.r.Event(m, body^, blen) THEN k := cnt END;
				INC(k)
			END;
			IF e.ok THEN op.r.Done(ActorJournal.ok) ELSE op.r.Done(ActorJournal.corrupt) END
		END
	END Answer;

	PROCEDURE (g: Agent) Send (c: ActorCore.Context), NEW;
		VAR p: RaftNode.Propose; q: RaftNode.Query;
	BEGIN
		IF g.cur.cmd THEN
			NEW(p); p.client := g.cl.id; p.seq := g.cl.seq; p.body := g.cur.body; c.Tell(g.cl.peers[g.target], p)
		ELSE NEW(q); q.tag := -g.tag; q.body := g.cur.body; c.Tell(g.cl.peers[g.target], q)	(* метка чтения < 0: не спутать с номером команды *)
		END;
		g.sentAt := g.now; g.waitUntil := -1
	END Send;

	PROCEDURE (g: Agent) Start (c: ActorCore.Context), NEW;
	(* следующая операция очереди, если нет текущей *)
		VAR op: Op; cl: Client; ok: Bytes;
	BEGIN
		cl := g.cl;
		WHILE (g.cur = NIL) & (g.head # NIL) DO
			op := g.head; g.head := op.next; IF g.head = NIL THEN g.tail := NIL END; op.next := NIL;
			g.cur := op; g.startAt := g.now;
			IF cl.tickMs > 0 THEN g.tmo := MAX(1, cl.timeoutMs DIV cl.tickMs); g.giveUp := MAX(1, cl.giveUpMs DIV cl.tickMs)
			ELSE g.tmo := MAX(1, cl.timeoutTicks); g.giveUp := MAX(1, cl.giveUpTicks)
			END;
			IF op.cmd THEN INC(cl.seq) ELSE INC(g.tag) END;
			g.Send(c);
			IF op.cmd & (mutNoWait IN mutations) THEN
				g.cur := NIL; NEW(ok, 1); ok[0] := ActorJournal.ok; Answer(op, TRUE, ok)
			END
		END
	END Start;

	PROCEDURE (g: Agent) Finish (c: ActorCore.Context; got: BOOLEAN; data: Bytes), NEW;
		VAR op: Op;
	BEGIN
		op := g.cur; g.cur := NIL; Answer(op, got, data); g.Start(c)
	END Finish;

	PROCEDURE (g: Agent) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR r: RaftNode.Reply; op: Op;
	BEGIN
		IF m IS Op THEN
			op := m(Op);
			IF g.tail = NIL THEN g.head := op ELSE g.tail.next := op END;
			g.tail := op; g.Start(c)
		ELSIF m IS TickMsg THEN
			INC(g.now);
			IF g.cur # NIL THEN
				IF g.now - g.startAt > g.giveUp THEN g.Finish(c, FALSE, NIL)
				ELSIF g.waitUntil >= 0 THEN IF g.now >= g.waitUntil THEN g.Send(c) END
				ELSIF g.now - g.sentAt >= g.tmo THEN g.target := (g.target + 1) MOD g.cl.n; INC(g.cl.retries); g.Send(c)
				END
			END
		ELSIF (m IS RaftNode.Reply) & (g.cur # NIL) THEN
			r := m(RaftNode.Reply);
			IF g.cur.cmd & (r.tag = g.cl.seq) OR ~g.cur.cmd & (r.tag = -g.tag) THEN
				IF r.res = RaftNode.ok THEN
					IF g.cur.cmd THEN g.cl.lastIndex := r.index END;
					g.Finish(c, TRUE, r.data)
				ELSIF (r.res = RaftNode.notLeader) & (r.leader >= 0) & (r.leader < g.cl.n) & (r.leader # g.target) THEN
					g.target := r.leader; INC(g.cl.retries); g.Send(c)
				ELSIF r.res = RaftNode.notLeader THEN	(* лидера нет или он ещё не готов: подождать выборов *)
					g.target := (g.target + 1) MOD g.cl.n; INC(g.cl.retries); g.waitUntil := g.now + g.tmo DIV 10 + 1
				ELSE INC(g.cl.retries); g.Send(c)	(* lost — повтор у того же узла *)
				END
			END
		END
	END Receive;

	PROCEDURE NewAgent (): ActorCore.Actor;
		VAR g: Agent;
	BEGIN
		NEW(g); g.cl := pendingCl; RETURN g
	END NewAgent;

	(* --- сторона актора: Client ------------------------------------------------- *)

	PROCEDURE (c: Client) Post (cmd: BOOLEAN; what: INTEGER; VAR f: Buf; r: ActorJournal.Request), NEW;
		VAR op: Op;
	BEGIN
		IF (c.tickMs > 0) & (c.clock = NIL) THEN
			NEW(c.clock); c.clock.to := c.agent; c.clock.ms := c.tickMs;
			Services.DoLater(c.clock, Services.Ticks() + c.tickMs)
		END;
		NEW(op); op.cmd := cmd; op.what := what; op.body := Done(f); op.r := r;
		ActorCore.Tell(c.agent, op)
	END Post;

	PROCEDURE (c: Client) Call (cmd: BOOLEAN; VAR f: Buf; OUT data: Bytes): BOOLEAN, NEW;
	(* Пост: TRUE — ответ ok лидера, data — его данные. *)
		VAR w: Wait; tok: AsyncSelect.Msg;
	BEGIN
		ASSERT(Tasks.InTask(), 20);
		NEW(w); w.box := AsyncChan.NewMailbox();
		c.Post(cmd, 0, f, w);
		w.box.Recv(tok);
		data := w.data; RETURN w.got
	END Call;

	PROCEDURE (c: Client) Command (VAR f: Buf; OUT res: INTEGER), NEW;
		VAR data: Bytes;
	BEGIN
		IF c.Call(TRUE, f, data) & (data # NIL) & (LEN(data^) >= 1) THEN res := data[0]
		ELSE res := ActorJournal.failed
		END
	END Command;

	PROCEDURE (c: Client) Ask (VAR f: Buf; OUT r: Rd): BOOLEAN, NEW;
		VAR data: Bytes;
	BEGIN
		IF c.Call(FALSE, f, data) & (data # NIL) THEN Open(r, data, 0, LEN(data^)); RETURN TRUE END;
		RETURN FALSE
	END Ask;

	PROCEDURE AppendCmd (VAR f: Buf; IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: ActorJournal.Meta;
		IN body: ARRAY OF BYTE; len: INTEGER);
	BEGIN
		ASSERT(stream # "", 20); ASSERT(m.seq = expected + 1, 21); ASSERT((len >= 0) & (len <= LEN(body)), 22);
		f.b := NIL; f.n := 0; Put(f, opAppend, 1); PutStr(f, stream); Put(f, expected, 8); PutMeta(f, m); PutBytes(f, body, len)
	END AppendCmd;

	PROCEDURE SnapCmd (VAR f: Buf; IN stream: ARRAY OF CHAR; seq: LONGINT; IN data: ARRAY OF BYTE; len: INTEGER);
	BEGIN
		ASSERT(seq > 0, 20); ASSERT((len >= 0) & (len <= LEN(data)), 21);
		f.b := NIL; f.n := 0; Put(f, opSnap, 1); PutStr(f, stream); Put(f, seq, 8); PutBytes(f, data, len)
	END SnapCmd;

	PROCEDURE (c: Client) Submit* (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: ActorJournal.Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; r: ActorJournal.Request);
		VAR f: Buf;
	BEGIN
		ASSERT(r # NIL, 23);
		AppendCmd(f, stream, expected, m, body, len); c.Post(TRUE, opAppend, f, r)
	END Submit;

	PROCEDURE (c: Client) SubmitSnapshot* (IN stream: ARRAY OF CHAR; seq: LONGINT;
		IN data: ARRAY OF BYTE; len: INTEGER; r: ActorJournal.Request);
		VAR f: Buf;
	BEGIN
		ASSERT(r # NIL, 23);
		SnapCmd(f, stream, seq, data, len); c.Post(TRUE, opSnap, f, r)
	END SubmitSnapshot;

	PROCEDURE (c: Client) Replay* (IN stream: ARRAY OF CHAR; r: ActorJournal.Request);
		VAR f: Buf;
	BEGIN
		ASSERT(r # NIL, 20);
		f.b := NIL; f.n := 0; Put(f, qReplay, 1); PutStr(f, stream); c.Post(FALSE, qReplay, f, r)
	END Replay;

	PROCEDURE (c: Client) Last* (IN stream: ARRAY OF CHAR): LONGINT;
		VAR f: Buf; r: Rd; res: INTEGER; x: LONGINT;
	BEGIN
		f.b := NIL; f.n := 0; Put(f, qLast, 1); PutStr(f, stream);
		IF c.Ask(f, r) THEN res := SHORT(Get(r, 1)); x := Get(r, 8); IF r.ok & (res = ActorJournal.ok) THEN RETURN x END END;
		RETURN -1	(* узнать не удалось *)
	END Last;

	PROCEDURE (c: Client) Append* (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: ActorJournal.Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR f: Buf;
	BEGIN
		AppendCmd(f, stream, expected, m, body, len); c.Command(f, res)
	END Append;

	PROCEDURE (c: Client) Read* (IN stream: ARRAY OF CHAR; from: LONGINT; s: ActorJournal.Sink; OUT res: INTEGER);
		VAR f: Buf; r, e: Rd; cnt, k, len: INTEGER; list: Bytes; m: ActorJournal.Meta; body: Bytes;
	BEGIN
		ASSERT(s # NIL, 20);
		f.b := NIL; f.n := 0; Put(f, qRead, 1); PutStr(f, stream); Put(f, from, 8);
		IF ~c.Ask(f, r) THEN res := ActorJournal.failed; RETURN END;
		res := SHORT(Get(r, 1)); GetBytes(r, list, len);
		IF ~r.ok THEN res := ActorJournal.corrupt; RETURN END;
		IF res # ActorJournal.ok THEN RETURN END;
		Open(e, list, 0, len); cnt := SHORT(Get(e, 4)); k := 0;
		WHILE e.ok & (k < cnt) DO
			GetMeta(e, m); GetBytes(e, body, len);
			IF e.ok & ~s.Event(m, body, len) THEN k := cnt END;
			INC(k)
		END;
		IF ~e.ok THEN res := ActorJournal.corrupt END
	END Read;

	PROCEDURE (c: Client) SaveSnapshot* (IN stream: ARRAY OF CHAR; seq: LONGINT;
		IN data: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR f: Buf;
	BEGIN
		SnapCmd(f, stream, seq, data, len); c.Command(f, res)
	END SaveSnapshot;

	PROCEDURE (c: Client) LoadSnapshot* (IN stream: ARRAY OF CHAR; OUT seq: LONGINT;
		OUT data: Bytes; OUT len: INTEGER; OUT res: INTEGER);
		VAR f: Buf; r: Rd;
	BEGIN
		seq := 0; data := NIL; len := 0;
		f.b := NIL; f.n := 0; Put(f, qLoad, 1); PutStr(f, stream);
		IF ~c.Ask(f, r) THEN res := ActorJournal.failed; RETURN END;
		res := SHORT(Get(r, 1)); seq := Get(r, 8); GetBytes(r, data, len);
		IF ~r.ok THEN res := ActorJournal.corrupt; seq := 0; data := NIL; len := 0
		ELSIF res # ActorJournal.ok THEN seq := 0; data := NIL; len := 0
		END
	END LoadSnapshot;

	PROCEDURE (c: Client) Close*;
	(** Пост: агент остановлен, свои часы погашены; запросы без ответа ответа не получат. *)
	BEGIN
		IF c.clock # NIL THEN c.clock.dead := TRUE END;
		ActorCore.StopActor(c.agent)
	END Close;

	PROCEDURE Tick* (c: Client);
	(** Тик часов агента от владельца (tickMs = 0). Пред: c # NIL — 20. *)
		VAR t: TickMsg;
	BEGIN
		ASSERT(c # NIL, 20);
		NEW(t); ActorCore.Tell(c.agent, t)
	END Tick;

	PROCEDURE New* (net: ActorNet.Node; IN caps: RaftNode.Caps; n, id: INTEGER): Client;
	(** Журнал на узле net для кластера из n узлов-акторов RaftNode с
		пропусками caps[0 .. n-1] (ActorNet.Export). id — номер клиента
		сеанса, новый для каждого воплощения.
		Пред: net # NIL — 20; 0 < n <= RaftCore.maxNodes — 21; id > 0 — 22; caps[0 .. n-1] — пропуски — 23;
		исполнитель акторов установлен, имя агента "net.name/$journal<id>" свободно — трапы ActorCore.Spawn.
		Пост: агент создан; свои часы (tickMs = 10) пойдут с первым запросом. *)
		VAR c: Client; i: INTEGER; name: ActorCore.Name; d: ARRAY 16 OF CHAR;
	BEGIN
		ASSERT(net # NIL, 20); ASSERT((n > 0) & (n <= Core.maxNodes), 21); ASSERT(id > 0, 22);
		NEW(c); c.net := net; c.n := n; c.id := id;
		c.tickMs := 10; c.timeoutMs := 1000; c.giveUpMs := 20000; c.timeoutTicks := 100; c.giveUpTicks := 2000;
		FOR i := 0 TO n - 1 DO c.peers[i] := ActorNet.Import(net, caps[i]); ASSERT(c.peers[i] # NIL, 23) END;
		Strings.IntToString(id, d); name := net.name$ + "/$journal" + d$;
		pendingCl := c; c.agent := ActorCore.Spawn(NIL, ActorCore.NewProps(NewAgent), name); pendingCl := NIL;
		RETURN c
	END New;

END RaftJournal.
