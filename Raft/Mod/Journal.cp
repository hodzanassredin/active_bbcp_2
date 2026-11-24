MODULE RaftJournal;
(** Журнал стойких акторов (ActorJournal.Journal) поверх Raft.

	Журнал — автомат состояний Raft (Machine): команды «дописать событие»
	и «сохранить снимок» идут в журнал Raft и применяются на каждом узле
	к своей копии журнала в памяти (ActorJournal.NewMem). Оптимистическая
	проверка номера (expected) выполняется при применении, поэтому она
	одна на кластер: два воплощения актора на разных узлах не допишут
	поток мимо друг друга — второе получит conflict.

	Client — сторона актора: реализация ActorJournal.Journal, которую
	ставят в ActorPersist.Actor.journal. Append и SaveSnapshot шлют
	RaftNode.Propose узлу-лидеру и ждут ответа (ActorCore.Ask: задача
	ждёт в AsyncChan.Select, без опроса); ответ приходит после того,
	как запись зафиксирована большинством и применена на лидере.
	Last, Read и LoadSnapshot — чтение RaftNode.Query (ReadIndex) у лидера:
	видно всё, что зафиксировано до запроса. Не-лидер называет лидера,
	клиент переходит к нему; нет ответа за timeoutMs — к следующему узлу.

	Ограничения.
	- Вызывать методы Client можно только из задачи (обработчик актора в
	  пуле ActorRun или своя задача). Под ActorSim (одна задача-водитель)
	  Append ждал бы сам себя: узлы-акторы обрабатывает та же задача.
	- Узлы-акторы RaftNode должны иметь свои часы (Config.tickMs > 0).
	- Вызовы одного Client идут по одному (замок): сеанс Raft — одна
	  операция клиента за раз.
	- res = failed у Append значит «неизвестно»: за giveUpMs ответа не было,
	  но команда могла быть зафиксирована. ActorPersist на failed трапует,
	  и перезапуск актора перечитывает журнал — состояние снова верное.
	- Номер клиента id обязан быть новым для каждого воплощения Client:
	  сеанс помнит последний номер операции, и старый id с номерами
	  сначала получил бы ответы из памяти сеанса.
	- Снимок автомата — все принятые команды подряд; сеансы не
	  вытесняются (граница этапа). *)

	IMPORT Kernel, Services, Tasks, AsyncChan, AsyncSelect, ActorCore, ActorNet, ActorJournal,
		Core := RaftCore, RaftSm, RaftNode;

	CONST
		opAppend = 1; opSnap = 2;	(* команды *)
		qLast = 1; qRead = 2; qLoad = 3;	(* чтения *)
		(** переключатели мутаций — только для тестов *)
		mutNoWait* = 0;	(** команда отправлена, ответ «записано» — не дожидаясь фиксации *)

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

		(** Сторона актора. *)
		Client* = POINTER TO RECORD (ActorJournal.Journal)
			timeoutMs*: INTEGER;	(** ожидание ответа одного узла, мс; по умолчанию 1000 *)
			giveUpMs*: INTEGER;	(** предел всей операции, мс; по умолчанию 20000 *)
			lastIndex-: INTEGER;	(** индекс записи Raft последней удачной команды *)
			retries-: INTEGER;	(** переходов к другому узлу и повторов *)
			net: ActorNet.Node;
			seq-: INTEGER;	(** номер последней операции сеанса *)
			id-: INTEGER;	(** номер клиента сеанса *)
			n, hint, tag: INTEGER;
			peers: ARRAY Core.maxNodes OF ActorCore.Ref;
			lock: AsyncChan.Mailbox
		END;

		Token = POINTER TO RECORD (AsyncSelect.Msg) END;
		Unlock = POINTER TO RECORD (Kernel.TrapCleaner) c: Client END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)

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
		f.n := 0; Put(f, m.ncmds, 4);
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
		Open(r, b, 0, len); op := SHORT(Get(r, 1)); GetStr(r, st); f.n := 0;
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

	(* --- сторона актора -------------------------------------------------------- *)

	PROCEDURE (u: Unlock) Cleanup;
		VAR t: Token;
	BEGIN
		NEW(t); u.c.lock.Send(t)
	END Cleanup;

	PROCEDURE (c: Client) Call (m: ActorNet.Msg; OUT data: Bytes): BOOLEAN, NEW;
	(* Пост: TRUE — ответ ok лидера, data — его данные. *)
		VAR t0: LONGINT; target, res: INTEGER; reply: ActorCore.Msg; r: RaftNode.Reply; got: BOOLEAN;
			tok: AsyncSelect.Msg; u: Unlock;
	BEGIN
		ASSERT(Tasks.InTask(), 20);
		c.lock.Recv(tok);
		NEW(u); u.c := c; Kernel.PushTrapCleaner(u);
		t0 := Services.Ticks(); target := c.hint; got := FALSE; data := NIL;
		LOOP
			res := ActorCore.Ask(c.peers[target], m, c.timeoutMs, reply);
			IF (res = ActorCore.ok) & (reply IS RaftNode.Reply) THEN
				r := reply(RaftNode.Reply);
				IF r.res = RaftNode.ok THEN data := r.data; c.lastIndex := r.index; got := TRUE; EXIT
				ELSIF (r.res = RaftNode.notLeader) & (r.leader >= 0) & (r.leader < c.n) & (r.leader # target) THEN
					target := r.leader
				ELSIF r.res = RaftNode.notLeader THEN	(* лидера нет или он ещё не готов: подождать выборов *)
					target := (target + 1) MOD c.n; Tasks.Sleep(c.timeoutMs DIV 10 + 1)
				END	(* lost — повтор у того же узла *)
			ELSE target := (target + 1) MOD c.n
			END;
			INC(c.retries);
			IF Services.Ticks() - t0 > c.giveUpMs THEN EXIT END
		END;
		c.hint := target;
		Kernel.PopTrapCleaner(u); u.Cleanup;
		RETURN got
	END Call;

	PROCEDURE (c: Client) Command (VAR f: Buf; OUT res: INTEGER), NEW;
		VAR p: RaftNode.Propose; data: Bytes;
	BEGIN
		INC(c.seq);
		NEW(p); p.client := c.id; p.seq := c.seq; p.body := Done(f);
		IF mutNoWait IN mutations THEN ActorCore.Tell(c.peers[c.hint], p); res := ActorJournal.ok
		ELSIF c.Call(p, data) & (data # NIL) & (LEN(data^) >= 1) THEN res := data[0]
		ELSE res := ActorJournal.failed
		END
	END Command;

	PROCEDURE (c: Client) Ask (VAR f: Buf; OUT r: Rd): BOOLEAN, NEW;
		VAR q: RaftNode.Query; data: Bytes;
	BEGIN
		INC(c.tag); NEW(q); q.tag := c.tag; q.body := Done(f);
		IF c.Call(q, data) & (data # NIL) THEN Open(r, data, 0, LEN(data^)); RETURN TRUE END;
		RETURN FALSE
	END Ask;

	PROCEDURE (c: Client) Last* (IN stream: ARRAY OF CHAR): LONGINT;
		VAR f: Buf; r: Rd; res: INTEGER; x: LONGINT;
	BEGIN
		f.n := 0; Put(f, qLast, 1); PutStr(f, stream);
		IF c.Ask(f, r) THEN res := SHORT(Get(r, 1)); x := Get(r, 8); IF r.ok & (res = ActorJournal.ok) THEN RETURN x END END;
		RETURN -1	(* узнать не удалось *)
	END Last;

	PROCEDURE (c: Client) Append* (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: ActorJournal.Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
		VAR f: Buf;
	BEGIN
		ASSERT(stream # "", 20); ASSERT(m.seq = expected + 1, 21); ASSERT((len >= 0) & (len <= LEN(body)), 22);
		f.n := 0; Put(f, opAppend, 1); PutStr(f, stream); Put(f, expected, 8); PutMeta(f, m); PutBytes(f, body, len);
		c.Command(f, res)
	END Append;

	PROCEDURE (c: Client) Read* (IN stream: ARRAY OF CHAR; from: LONGINT; s: ActorJournal.Sink; OUT res: INTEGER);
		VAR f: Buf; r, e: Rd; cnt, k, len: INTEGER; list: Bytes; m: ActorJournal.Meta; body: Bytes;
	BEGIN
		ASSERT(s # NIL, 20);
		f.n := 0; Put(f, qRead, 1); PutStr(f, stream); Put(f, from, 8);
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
		ASSERT(seq > 0, 20); ASSERT((len >= 0) & (len <= LEN(data)), 21);
		f.n := 0; Put(f, opSnap, 1); PutStr(f, stream); Put(f, seq, 8); PutBytes(f, data, len);
		c.Command(f, res)
	END SaveSnapshot;

	PROCEDURE (c: Client) LoadSnapshot* (IN stream: ARRAY OF CHAR; OUT seq: LONGINT;
		OUT data: Bytes; OUT len: INTEGER; OUT res: INTEGER);
		VAR f: Buf; r: Rd;
	BEGIN
		seq := 0; data := NIL; len := 0;
		f.n := 0; Put(f, qLoad, 1); PutStr(f, stream);
		IF ~c.Ask(f, r) THEN res := ActorJournal.failed; RETURN END;
		res := SHORT(Get(r, 1)); seq := Get(r, 8); GetBytes(r, data, len);
		IF ~r.ok THEN res := ActorJournal.corrupt; seq := 0; data := NIL; len := 0
		ELSIF res # ActorJournal.ok THEN seq := 0; data := NIL; len := 0
		END
	END LoadSnapshot;

	PROCEDURE New* (net: ActorNet.Node; IN caps: RaftNode.Caps; n, id: INTEGER): Client;
	(** Журнал на узле net для кластера из n узлов-акторов RaftNode с
		пропусками caps[0 .. n-1] (ActorNet.Export). id — номер клиента
		сеанса, новый для каждого воплощения.
		Пред: net # NIL — 20; 0 < n <= RaftCore.maxNodes — 21; id > 0 — 22; caps[0 .. n-1] — пропуски — 23. *)
		VAR c: Client; i: INTEGER; t: Token;
	BEGIN
		ASSERT(net # NIL, 20); ASSERT((n > 0) & (n <= Core.maxNodes), 21); ASSERT(id > 0, 22);
		NEW(c); c.net := net; c.n := n; c.id := id; c.timeoutMs := 1000; c.giveUpMs := 20000;
		FOR i := 0 TO n - 1 DO c.peers[i] := ActorNet.Import(net, caps[i]); ASSERT(c.peers[i] # NIL, 23) END;
		c.lock := AsyncChan.NewMailbox(); NEW(t); c.lock.Send(t);
		RETURN c
	END New;

END RaftJournal.
