# Обзор на примерах: ObxCluster

Счета в кластере: восемь счетов — виртуальные акторы на общем журнале, перевод — сага, узел
теряется посреди работы. Пример относится к руководству
[Actor/Docu/ru/Cluster.md](../../../Actor/Docu/ru/Cluster.md).

Короткий прогон `Do` держит оба узла в одном процессе, сеть и журнал — в памяти. Узлы A и B
хранят счета, клиент адресует их по номеру, а узел A теряется, когда четвёртый перевод находится
между списанием и зачислением. Те же акторы работают в пяти процессах в сценарии `obxcluster`
из `tools/test-cluster.sh`: там журнал — кластер Raft, а состав приходит из службы состава.

```
MODULE ObxCluster;
(** Счета в кластере: виртуальные акторы на журнале Raft, переводы сагой.

	Счёт — виртуальный актор вида "acct" (ActorGrain): адрес — номер
	счёта, состояние — поток "acct.<номер>" общего журнала. Перевод —
	сага из трёх шагов: списать у источника, зачислить получателю, при
	отказе зачисления вернуть списание. Каждый шаг — команда счёту с
	номером операции id*3 + шаг; счёт помнит номера и на повтор отвечает
	прежним итогом, поэтому перевод не применяется дважды.

	Координатор перевода — тоже виртуальный актор ("xfer", ключ — номер
	перевода). Он не держит адресов счетов: шаг саги он отдаёт клиенту
	письмом Cmd, клиент исполняет команду и отвечает Done. Журнал
	координатора хранит только состояние шага, поэтому падение узла не
	теряет перевод: воплощение поднимется на другом узле и продолжит с
	записанного шага.

	Клиент — актор узла: он рассылает Begin и Tick координаторам,
	исполняет их команды и собирает итог. Tick — часы тайм-аута: если
	ответа на шаг нет retryMs, координатор повторяет шаг, а поднятое
	заново воплощение снова присылает текущий шаг. Сценарий может терять
	первый ответ шага (SetDropStep): тогда шаг исполняется повторно, и
	счёт обязан ответить прежним итогом из списка номеров.

	Когда все переводы закончены, клиент выжидает тишину, чтобы счета
	пассивировались, и повторяет первые шаги: ответ после восстановления
	из снимка должен совпасть с записанным. Затем он собирает остатки и
	сверяет их с итогами переводов (conserved), поэтому потеря остатка
	или номеров при Restore видна.

	Короткий прогон Do — два узла в памяти: счета и переводы идут через
	ActorGrain на общем журнале, один узел теряется посреди работы.
	Кластер в процессах — Actor_devCluster, сценарий obxcluster. *)

	IMPORT Tasks, Services, Console, Strings, Stores,
		ActorCore, ActorJournal, ActorPersist, ActorNet, ActorNetMem, ActorSim, ActorGrain;

	CONST
		accounts* = 8;	(** счетов в примере *)
		initial* = 1000;	(** начальный остаток каждого счёта *)
		limit* = 1300;	(** предел остатка: обычное зачисление сверх него отклоняется *)
		maxTransfers* = 64;	(** предел числа переводов клиента *)
		maxOps = 128;	(** номеров операций, которые помнит счёт *)
		(* состояния координатора *)
		begun* = 0; withdrawn* = 1; done* = 2; refunding* = 3; refunded* = 4; failed* = 5;
		notStarted = -1;
		steps = 3;	(** шагов перевода: списание, зачисление, возврат *)
		quietDrives = 3;	(** проходов клиента без работы до проверки повтором *)

	TYPE
		Name = ActorCore.Name;
		Msg = ActorCore.Msg;

		(* --- письма между узлами --------------------------------------------- *)
		Move* = POINTER TO RECORD (ActorNet.Msg) op*: LONGINT; sum*: INTEGER END;
		Reply* = POINTER TO RECORD (ActorNet.Msg) op*: LONGINT; ok*: BOOLEAN END;
		Begin* = POINTER TO RECORD (ActorNet.Msg) id*: LONGINT; from*, to*, sum*: INTEGER END;
		Tick* = POINTER TO RECORD (ActorNet.Msg) END;
		Done* = POINTER TO RECORD (ActorNet.Msg) id*: LONGINT; op*: LONGINT; ok*: BOOLEAN END;
		Cmd* = POINTER TO RECORD (ActorNet.Msg) id*: LONGINT; state*: INTEGER; op*: LONGINT; acc*, sum*: INTEGER; retry*: BOOLEAN END;
		Finished* = POINTER TO RECORD (ActorNet.Msg) id*: LONGINT; state*: INTEGER END;
		Stats* = POINTER TO RECORD (ActorNet.Msg) acc*: INTEGER END;
		StatsReply* = POINTER TO RECORD (ActorNet.Msg) acc*, balance*, n*, dups*: INTEGER END;

		(* --- местные письма клиенту ------------------------------------------ *)
		Drive* = POINTER TO RECORD (Msg) END;
		Report* = POINTER TO RECORD (Msg)
			done*, failed*, refunded*, total*, sum*, expect*, applied*, dups*: INTEGER;
			retries*, rejectedRetries*, coordRetries*: INTEGER;
			verified*, verifyBad*: INTEGER;
			losses*: INTEGER;	(** сколько первых ответов шага потеряно *)
			bal*: ARRAY accounts OF INTEGER;
			conserved*, good*: BOOLEAN
		END;

		(* --- события --------------------------------------------------------- *)
		Moved* = POINTER TO RECORD (ActorPersist.Event) op*: LONGINT; sum*: INTEGER; ok*: BOOLEAN END;
		Step* = POINTER TO RECORD (ActorPersist.Event)
			id*: LONGINT; from*, to*, sum*, state*: INTEGER
		END;

		(** Счёт: остаток и номера применённых операций. *)
		Account* = POINTER TO RECORD (ActorGrain.Actor)
			balance*: INTEGER;
			ops*: ARRAY maxOps OF LONGINT;
			oks*: ARRAY maxOps OF BOOLEAN;
			n*: INTEGER;
			dups*: INTEGER	(** сколько раз событие с прежним номером всё же пришло *)
		END;

		(** Координатор перевода: шаг саги и его состояние. *)
		Coord* = POINTER TO RECORD (ActorGrain.Actor)
			started*: BOOLEAN; id*: LONGINT; from*, to*, sum*, state*: INTEGER;
			lastMs: LONGINT	(* время последней отдачи шага; тайм-аут повтора *)
		END;

		(** Клиент узла: рассылка Begin и Tick, исполнение команд, итог. *)
		Client* = POINTER TO RECORD (ActorCore.Actor)
			n*, head*: LONGINT; finished*: INTEGER;
			sequential*, verbose*: BOOLEAN; pace*: INTEGER;
			sink*: ActorCore.Ref;
			state*: ARRAY maxTransfers OF INTEGER;
			begun*: ARRAY maxTransfers OF BOOLEAN;
			statsGot*: ARRAY accounts OF BOOLEAN;
			statBal*, statN*, statDups*: ARRAY accounts OF INTEGER;
			drives*: INTEGER;
			reporting*, reported*: BOOLEAN;
			(* повтор шага: первый ответ шага dropStep теряется, ответ на
			   повтор считается; тишина перед проверкой и её итог *)
			dropStep, quiet*: INTEGER;
			verifying*: BOOLEAN;
			dropped, repeating: ARRAY maxTransfers OF SET;
			verifyPending, verifyExpected: ARRAY maxTransfers OF BOOLEAN;
			retries*, rejectedRetries*, coordRetries*: INTEGER;
			verified*, verifyBad*, losses*: INTEGER;
			acct*: ARRAY accounts OF ActorCore.Ref;
			coord*: ARRAY maxTransfers OF ActorCore.Ref
		END;

		Printer = POINTER TO RECORD (ActorCore.Actor) END;
		Driver = POINTER TO RECORD (Tasks.Job) client: ActorCore.Ref; n: INTEGER; drive: Drive END;

	VAR
		cl: ActorGrain.Cluster;
		lastClient: Client;
		pendingN: LONGINT; pendingPace: INTEGER;
		pendingSeq, pendingVerbose: BOOLEAN; pendingSink: ActorCore.Ref;
		demoDone: BOOLEAN;	crashed: BOOLEAN;
		net: ActorNetMem.Net;
		retryMs: LONGINT;	(* тишина без ответа до повтора шага; 0 — повтор на каждый Tick *)
		dropStep: INTEGER;	(* шаг, чей первый ответ теряется; -1 — не терять *)

	(* --- номера --------------------------------------------------------------- *)

	PROCEDURE Key (i: INTEGER; OUT s: ARRAY OF CHAR);
	BEGIN
		Strings.IntToString(i, s)
	END Key;

	PROCEDURE WInt (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WInt;

	PROCEDURE Plan* (id: LONGINT; OUT from, to, sum: INTEGER);
	(** Пост: счёт-источник, счёт-получатель и сумма перевода id. *)
	BEGIN
		from := SHORT(id MOD accounts);
		to := SHORT((id * 3 + 1) MOD accounts);
		IF to = from THEN to := SHORT((to + 1) MOD accounts) END;
		sum := 100 + SHORT((id * 137) MOD 1400)
	END Plan;

	PROCEDURE SetCluster* (c: ActorGrain.Cluster);
	(** Пред: c # NIL — 20. Пост: кластер, в котором клиент адресует счета. *)
	BEGIN
		ASSERT(c # NIL, 20);
		cl := c
	END SetCluster;

	PROCEDURE SetRetry* (ms: LONGINT);
	(** Пост: координатор повторяет шаг, если ответа нет ms мс; 0 — повтор
		на каждый Tick. *)
	BEGIN
		retryMs := ms
	END SetRetry;

	PROCEDURE SetDropStep* (step: INTEGER);
	(** Пост: первый ответ шага step каждого перевода теряется, чтобы шаг
		исполнился повторно; -1 — не терять. *)
	BEGIN
		ASSERT((step >= -1) & (step < steps), 20);
		dropStep := step
	END SetDropStep;

	PROCEDURE Progress* (): INTEGER;
	(** Пост: сколько переводов клиента узла уже закончено. *)
	BEGIN
		IF lastClient = NIL THEN RETURN 0 ELSE RETURN lastClient.finished END
	END Progress;

	PROCEDURE Active* (): LONGINT;
	(** Пост: перевод, который последовательный клиент ведёт сейчас; -1 — нет. *)
	BEGIN
		IF (lastClient = NIL) OR ~lastClient.sequential THEN RETURN -1 END;
		RETURN lastClient.head
	END Active;

	PROCEDURE Status* (OUT drives, n, finished: INTEGER; OUT reporting, reported: BOOLEAN);
	(** Пост: счётчики клиента узла: тиков, переводов, законченных, фазы итога. *)
	BEGIN
		drives := 0; n := 0; finished := 0; reporting := FALSE; reported := FALSE;
		IF lastClient # NIL THEN
			drives := lastClient.drives; n := SHORT(lastClient.n); finished := lastClient.finished;
			reporting := lastClient.reporting; reported := lastClient.reported
		END
	END Status;

	PROCEDURE ActiveState* (): INTEGER;
	(** Пост: состояние текущего перевода последовательного клиента; notStarted — нет. *)
	BEGIN
		IF (lastClient = NIL) OR ~lastClient.sequential OR (lastClient.head >= lastClient.n) THEN RETURN notStarted END;
		RETURN lastClient.state[lastClient.head]
	END ActiveState;

	PROCEDURE Final* (state: INTEGER): BOOLEAN;
	BEGIN
		RETURN (state = done) OR (state = refunded) OR (state = failed)
	END Final;

	(* --- письма --------------------------------------------------------------- *)

	PROCEDURE (m: Move) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteInt(m.sum)
	END Write;

	PROCEDURE (m: Move) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadInt(m.sum)
	END Read;

	PROCEDURE (m: Reply) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteBool(m.ok)
	END Write;

	PROCEDURE (m: Reply) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadBool(m.ok)
	END Read;

	PROCEDURE (m: Begin) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.id); wr.WriteInt(m.from); wr.WriteInt(m.to); wr.WriteInt(m.sum)
	END Write;

	PROCEDURE (m: Begin) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.id); rd.ReadInt(m.from); rd.ReadInt(m.to); rd.ReadInt(m.sum)
	END Read;

	PROCEDURE (m: Tick) Write- (VAR wr: Stores.Writer);
	END Write;

	PROCEDURE (m: Tick) Read- (VAR rd: Stores.Reader);
	END Read;

	PROCEDURE (m: Done) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.id); wr.WriteLong(m.op); wr.WriteBool(m.ok)
	END Write;

	PROCEDURE (m: Done) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.id); rd.ReadLong(m.op); rd.ReadBool(m.ok)
	END Read;

	PROCEDURE (m: Cmd) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.id); wr.WriteInt(m.state); wr.WriteLong(m.op);
		wr.WriteInt(m.acc); wr.WriteInt(m.sum); wr.WriteBool(m.retry)
	END Write;

	PROCEDURE (m: Cmd) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.id); rd.ReadInt(m.state); rd.ReadLong(m.op);
		rd.ReadInt(m.acc); rd.ReadInt(m.sum); rd.ReadBool(m.retry)
	END Read;

	PROCEDURE (m: Finished) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.id); wr.WriteInt(m.state)
	END Write;

	PROCEDURE (m: Finished) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.id); rd.ReadInt(m.state)
	END Read;

	PROCEDURE (m: Stats) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.acc)
	END Write;

	PROCEDURE (m: Stats) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.acc)
	END Read;

	PROCEDURE (m: StatsReply) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.acc); wr.WriteInt(m.balance); wr.WriteInt(m.n); wr.WriteInt(m.dups)
	END Write;

	PROCEDURE (m: StatsReply) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.acc); rd.ReadInt(m.balance); rd.ReadInt(m.n); rd.ReadInt(m.dups)
	END Read;

	(* --- события -------------------------------------------------------------- *)

	PROCEDURE (e: Moved) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteLong(e.op); wr.WriteInt(e.sum); wr.WriteBool(e.ok)
	END Externalize;

	PROCEDURE (e: Moved) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadLong(e.op); rd.ReadInt(e.sum); rd.ReadBool(e.ok)
	END Internalize;

	PROCEDURE (e: Step) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteLong(e.id);
		wr.WriteInt(e.from); wr.WriteInt(e.to); wr.WriteInt(e.sum); wr.WriteInt(e.state)
	END Externalize;

	PROCEDURE (e: Step) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadLong(e.id); rd.ReadInt(e.from); rd.ReadInt(e.to); rd.ReadInt(e.sum); rd.ReadInt(e.state)
	END Internalize;

	(* --- счёт ---------------------------------------------------------------- *)

	PROCEDURE Seen (a: Account; op: LONGINT): INTEGER;
	(* Пост: место операции op в списке или a.n, если её нет. *)
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < a.n) & (a.ops[i] # op) DO INC(i) END;
		RETURN i
	END Seen;

	PROCEDURE Fits* (balance: INTEGER; op: LONGINT; sum: INTEGER): BOOLEAN;
	(** Пост: проходит ли операция шага op MOD 3 по остатку balance:
		списание — не ниже нуля, зачисление — не выше предела, возврат —
		всегда. Границы входят в допуск: 0 после списания и ровно предел
		после зачисления проходят. *)
		VAR step: INTEGER;
	BEGIN
		step := SHORT(op MOD steps);
		IF step = 0 THEN RETURN balance + sum >= 0
		ELSIF step = 1 THEN RETURN balance + sum <= limit
		ELSE RETURN TRUE
		END
	END Fits;

	PROCEDURE (a: Account) Apply* (e: ActorPersist.Event);
		VAR i: INTEGER;
	BEGIN
		WITH e: Moved DO
			i := Seen(a, e.op);
			IF i < a.n THEN
				INC(a.dups);	(* повторное событие: считается порчей *)
				IF e.ok THEN INC(a.balance, e.sum) END
			ELSIF a.n < LEN(a.ops) THEN
				a.ops[a.n] := e.op; a.oks[a.n] := e.ok; INC(a.n);
				IF e.ok THEN INC(a.balance, e.sum) END
			ELSE
				INC(a.dups)
			END
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Handle* (c: ActorCore.Context; m: Msg);
		VAR e: Moved; r: Reply; s: StatsReply; i: INTEGER;
	BEGIN
		WITH m: Move DO
			i := Seen(a, m.op);
			IF i = a.n THEN
				NEW(e); e.op := m.op; e.sum := m.sum; e.ok := Fits(a.balance, m.op, m.sum);
				a.Persist(c, e, m.op)
			ELSE
				NEW(r); r.op := m.op; r.ok := a.oks[i]; c.Respond(r)
			END
		| m: Stats DO
			NEW(s); s.acc := m.acc; s.balance := a.balance; s.n := a.n; s.dups := a.dups; c.Respond(s)
		ELSE
		END
	END Handle;

	PROCEDURE (a: Account) Persisted* (c: ActorCore.Context; e: ActorPersist.Event);
		VAR r: Reply;
	BEGIN
		WITH e: Moved DO NEW(r); r.op := e.op; r.ok := e.ok; c.Respond(r) ELSE END
	END Persisted;

	PROCEDURE (a: Account) Externalize- (VAR wr: Stores.Writer);
		VAR i: INTEGER;
	BEGIN
		wr.WriteVersion(0); wr.WriteInt(a.balance); wr.WriteInt(a.n); wr.WriteInt(a.dups);
		FOR i := 0 TO maxOps - 1 DO wr.WriteLong(a.ops[i]); wr.WriteBool(a.oks[i]) END
	END Externalize;

	PROCEDURE (a: Account) Internalize- (VAR rd: Stores.Reader);
		VAR v, i: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(a.balance); rd.ReadInt(a.n); rd.ReadInt(a.dups);
		FOR i := 0 TO maxOps - 1 DO rd.ReadLong(a.ops[i]); rd.ReadBool(a.oks[i]) END
	END Internalize;

	PROCEDURE (a: Account) Restore* (s: Stores.Store);
		VAR i: INTEGER;
	BEGIN
		WITH s: Account DO
			a.balance := s.balance; a.n := s.n; a.dups := s.dups;
			FOR i := 0 TO maxOps - 1 DO a.ops[i] := s.ops[i]; a.oks[i] := s.oks[i] END
		ELSE
		END
	END Restore;

	PROCEDURE NewAccount* (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.balance := initial; a.n := 0; a.dups := 0; RETURN a
	END NewAccount;

	(* --- координатор ---------------------------------------------------------- *)

	PROCEDURE (k: Coord) Apply* (e: ActorPersist.Event);
	BEGIN
		WITH e: Step DO
			k.started := TRUE; k.id := e.id; k.from := e.from; k.to := e.to;
			k.sum := e.sum; k.state := e.state
		ELSE
		END
	END Apply;

	PROCEDURE (k: Coord) Save (c: ActorCore.Context; s: INTEGER), NEW;
		VAR e: Step;
	BEGIN
		NEW(e); e.id := k.id; e.from := k.from; e.to := k.to; e.sum := k.sum; e.state := s;
		k.Persist(c, e, k.id * 3 + s)
	END Save;

	PROCEDURE (k: Coord) Push (c: ActorCore.Context; retry: BOOLEAN), NEW;
	(* Отдать отправителю текущий шаг или итог; retry — шаг повторяется. *)
		VAR s: ActorCore.Ref; cmd: Cmd; f: Finished;
	BEGIN
		s := c.Sender();
		IF (s = NIL) OR ~k.started THEN RETURN END;
		k.lastMs := Services.Ticks();
		IF Final(k.state) THEN
			NEW(f); f.id := k.id; f.state := k.state; c.Tell(s, f)
		ELSE
			NEW(cmd); cmd.id := k.id; cmd.state := k.state; cmd.retry := retry;
			CASE k.state OF
			| begun: cmd.op := k.id * 3; cmd.acc := k.from; cmd.sum := -k.sum
			| withdrawn: cmd.op := k.id * 3 + 1; cmd.acc := k.to; cmd.sum := k.sum
			| refunding: cmd.op := k.id * 3 + 2; cmd.acc := k.from; cmd.sum := k.sum
			END;
			c.Tell(s, cmd)
		END
	END Push;

	PROCEDURE (k: Coord) Handle* (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: Begin DO
			IF ~k.started THEN
				k.started := TRUE; k.id := m.id; k.from := m.from; k.to := m.to; k.sum := m.sum;
				k.Save(c, begun)
			ELSE k.Push(c, TRUE)
			END
		| m: Tick DO
			(* тайм-аут ответа: молчим, пока шаг молод, и повторяем позже *)
			IF (retryMs <= 0) OR (Services.Ticks() - k.lastMs >= retryMs) THEN k.Push(c, TRUE) END
		| m: Done DO
			IF (k.state = begun) & (m.id = k.id) & (m.op = k.id * 3) THEN
				IF m.ok THEN k.Save(c, withdrawn) ELSE k.Save(c, failed) END
			ELSIF (k.state = withdrawn) & (m.id = k.id) & (m.op = k.id * 3 + 1) THEN
				IF m.ok THEN k.Save(c, done) ELSE k.Save(c, refunding) END
			ELSIF (k.state = refunding) & (m.id = k.id) & (m.op = k.id * 3 + 2) & m.ok THEN
				k.Save(c, refunded)
			END
		ELSE
		END
	END Handle;

	PROCEDURE (k: Coord) Persisted* (c: ActorCore.Context; e: ActorPersist.Event);
	BEGIN
		IF e IS Step THEN k.Push(c, FALSE) END
	END Persisted;

	PROCEDURE (k: Coord) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(0); wr.WriteBool(k.started); wr.WriteLong(k.id);
		wr.WriteInt(k.from); wr.WriteInt(k.to); wr.WriteInt(k.sum); wr.WriteInt(k.state)
	END Externalize;

	PROCEDURE (k: Coord) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadBool(k.started); rd.ReadLong(k.id);
		rd.ReadInt(k.from); rd.ReadInt(k.to); rd.ReadInt(k.sum); rd.ReadInt(k.state)
	END Internalize;

	PROCEDURE (k: Coord) Restore* (s: Stores.Store);
	BEGIN
		WITH s: Coord DO
			k.started := s.started; k.id := s.id; k.from := s.from; k.to := s.to;
			k.sum := s.sum; k.state := s.state
		ELSE
		END
	END Restore;

	PROCEDURE NewCoord* (): ActorCore.Actor;
		VAR k: Coord;
	BEGIN
		NEW(k); k.started := FALSE; k.state := begun; RETURN k
	END NewCoord;

	(* --- клиент --------------------------------------------------------------- *)

	PROCEDURE (c: Client) Ensure, NEW;
		VAR i: INTEGER; s: ARRAY 16 OF CHAR;
	BEGIN
		IF c.acct[0] = NIL THEN
			FOR i := 0 TO accounts - 1 DO Key(i, s); c.acct[i] := ActorGrain.Ref(cl, "acct", s) END
		END;
		IF c.coord[0] = NIL THEN
			FOR i := 0 TO maxTransfers - 1 DO Key(i, s); c.coord[i] := ActorGrain.Ref(cl, "xfer", s) END
		END
	END Ensure;

	PROCEDURE (c: Client) Note (id: INTEGER; st: INTEGER), NEW;
	(* Конечное состояние не меняется: запоздавшее письмо прежнего шага
		не возвращает перевод в работу. *)
	BEGIN
		IF Final(c.state[id]) THEN RETURN END;
		IF c.state[id] # st THEN
			IF Final(st) THEN
				INC(c.finished);
				IF c.verbose THEN
					Console.WriteStr("transfer "); WInt(id);
					Console.WriteStr(": ");
					CASE st OF
					| done: Console.WriteStr("done")
					| failed: Console.WriteStr("failed")
					| refunded: Console.WriteStr("refunded")
					END;
					Console.WriteLn
				END
			END;
			c.state[id] := st
		END
	END Note;

	PROCEDURE (c: Client) AllFinal (): BOOLEAN, NEW;
	BEGIN
		RETURN c.finished >= c.n
	END AllFinal;

	PROCEDURE (c: Client) SendStats (ctx: ActorCore.Context; i: INTEGER), NEW;
		VAR s: Stats;
	BEGIN
		NEW(s); s.acc := i; ctx.Tell(c.acct[i], s)
	END SendStats;

	PROCEDURE (c: Client) StartStats (ctx: ActorCore.Context), NEW;
		VAR i: INTEGER;
	BEGIN
		c.reporting := TRUE;
		FOR i := 0 TO accounts - 1 DO c.statsGot[i] := FALSE; c.SendStats(ctx, i) END
	END StartStats;

	PROCEDURE (c: Client) SendReport (ctx: ActorCore.Context), NEW;
		VAR i, from, to, sum: INTEGER; r: Report; expected: ARRAY accounts OF INTEGER;
	BEGIN
		NEW(r);
		r.total := SHORT(c.n);
		FOR i := 0 TO SHORT(c.n) - 1 DO
			IF c.state[i] = done THEN INC(r.done)
			ELSIF c.state[i] = failed THEN INC(r.failed)
			ELSIF c.state[i] = refunded THEN INC(r.refunded)
			END
		END;
		r.expect := 2 * r.done + r.failed + 3 * r.refunded;
		(* Ожидаемый остаток: начальный плюс итог законченных переводов.
			Отклонённый и возвращённый переводы денег не двигают. *)
		FOR i := 0 TO accounts - 1 DO expected[i] := initial END;
		FOR i := 0 TO SHORT(c.n) - 1 DO
			IF c.state[i] = done THEN
				Plan(i, from, to, sum);
				DEC(expected[from], sum); INC(expected[to], sum)
			END
		END;
		r.retries := c.retries; r.rejectedRetries := c.rejectedRetries;
		r.coordRetries := c.coordRetries;
		r.verified := c.verified; r.verifyBad := c.verifyBad; r.losses := c.losses;
		r.conserved := TRUE;
		FOR i := 0 TO accounts - 1 DO
			r.bal[i] := c.statBal[i];
			IF c.statBal[i] # expected[i] THEN r.conserved := FALSE END;
			INC(r.sum, c.statBal[i]); INC(r.applied, c.statN[i]); INC(r.dups, c.statDups[i])
		END;
		r.good := (r.done + r.failed + r.refunded = r.total)
			& (r.sum = accounts * initial) & (r.dups = 0) & (r.applied = r.expect)
			& r.conserved & (r.verified = r.total) & (r.verifyBad = 0);
		ctx.Tell(c.sink, r); c.reported := TRUE
	END SendReport;

	PROCEDURE (c: Client) SendVerify (ctx: ActorCore.Context; id: INTEGER), NEW;
	(* Повторить первый шаг перевода: счёт после пассивации должен ответить
		прежним итогом из снимка и не двинуть деньги. *)
		VAR from, to, sum: INTEGER; mv: Move;
	BEGIN
		Plan(id, from, to, sum);
		NEW(mv); mv.op := id * 3; mv.sum := -sum;
		c.verifyExpected[id] := (c.state[id] = done) OR (c.state[id] = refunded);
		c.verifyPending[id] := TRUE;
		ctx.Tell(c.acct[from], mv)
	END SendVerify;

	PROCEDURE (c: Client) StartVerify (ctx: ActorCore.Context), NEW;
		VAR i: INTEGER;
	BEGIN
		c.verifying := TRUE;
		FOR i := 0 TO SHORT(c.n) - 1 DO c.SendVerify(ctx, i) END
	END StartVerify;

	PROCEDURE (c: Client) Collect (ctx: ActorCore.Context), NEW;
		VAR i: INTEGER; all: BOOLEAN;
	BEGIN
		all := TRUE;
		FOR i := 0 TO accounts - 1 DO
			IF ~c.statsGot[i] THEN c.SendStats(ctx, i); all := FALSE END
		END;
		IF all THEN c.SendReport(ctx) END
	END Collect;

	PROCEDURE (c: Client) Launch (ctx: ActorCore.Context; id: INTEGER), NEW;
		VAR b: Begin; from, to, sum: INTEGER;
	BEGIN
		Plan(id, from, to, sum);
		NEW(b); b.id := id; b.from := from; b.to := to; b.sum := sum;
		c.begun[id] := TRUE; ctx.Tell(c.coord[id], b)
	END Launch;

	PROCEDURE (c: Client) DriveOne (ctx: ActorCore.Context; id: INTEGER), NEW;
		VAR t: Tick;
	BEGIN
		IF ~c.begun[id] THEN c.Launch(ctx, id)
		ELSIF c.state[id] = notStarted THEN	(* Begin пропал *)
			IF c.drives MOD 5 = 0 THEN c.Launch(ctx, id) END
		ELSIF c.drives MOD 5 = 0 THEN	(* Tick — часы тайм-аута координатора *)
			NEW(t); ctx.Tell(c.coord[id], t)
		END
	END DriveOne;

	PROCEDURE (c: Client) Drive (ctx: ActorCore.Context), NEW;
		VAR i, k: INTEGER;
	BEGIN
		c.Ensure; INC(c.drives);
		IF c.reporting THEN
			IF ~c.reported THEN c.Collect(ctx) END
		ELSIF c.verifying THEN
			(* проверка после пассивации: повторяем неотвеченные шаги *)
			FOR i := 0 TO SHORT(c.n) - 1 DO
				IF c.verifyPending[i] THEN c.SendVerify(ctx, i) END
			END;
			IF c.verified + c.verifyBad >= c.n THEN c.StartStats(ctx) END
		ELSE
			IF c.sequential THEN
				WHILE (c.head < c.n) & Final(c.state[c.head]) DO INC(c.head) END;
				IF c.head < c.n THEN c.DriveOne(ctx, SHORT(c.head)) END
			ELSE
				k := c.pace;
				FOR i := 0 TO SHORT(c.n) - 1 DO
					IF ~Final(c.state[i]) THEN
						IF ~c.begun[i] & (c.pace > 0) THEN
							IF k > 0 THEN DEC(k); c.DriveOne(ctx, i) END
						ELSE c.DriveOne(ctx, i)
						END
					END
				END
			END;
			IF c.AllFinal() THEN	(* дать счетам заснуть, затем проверить повтор *)
				INC(c.quiet);
				IF c.quiet >= quietDrives THEN c.StartVerify(ctx) END
			END
		END
	END Drive;

	PROCEDURE (c: Client) Receive* (ctx: ActorCore.Context; m: Msg);
		VAR id, step: INTEGER; d: Done; mv: Move;
	BEGIN
		c.Ensure;
		WITH m: Drive DO c.Drive(ctx)
		| m: Cmd DO
			id := SHORT(m.id);
			IF (id >= 0) & (id < c.n) THEN
				c.Note(id, m.state);
				IF ~Final(m.state) & (m.acc >= 0) & (m.acc < accounts) THEN
					IF m.retry THEN
						INC(c.coordRetries);
						INCL(c.repeating[id], SHORT(m.op MOD steps))
					END;
					NEW(mv); mv.op := m.op; mv.sum := m.sum;
					ctx.Tell(c.acct[m.acc], mv)
				END
			END
		| m: Finished DO
			id := SHORT(m.id);
			IF (id >= 0) & (id < c.n) THEN c.Note(id, m.state) END
		| m: Reply DO
			id := SHORT(m.op DIV steps); step := SHORT(m.op MOD steps);
			IF (id >= 0) & (id < c.n) THEN
				IF c.verifying & c.verifyPending[id] & (step = 0) THEN
					(* ответ на проверочный повтор: сверяем с итогом перевода *)
					c.verifyPending[id] := FALSE;
					INC(c.retries);	(* повтор шага после пассивации *)
					IF ~m.ok THEN INC(c.rejectedRetries) END;
					IF m.ok = c.verifyExpected[id] THEN INC(c.verified) ELSE INC(c.verifyBad) END
				ELSIF ~c.verifying & (c.dropStep >= 0) & (step = c.dropStep)
						& ~(step IN c.dropped[id]) THEN
					INCL(c.dropped[id], step);	(* первый ответ потерян: ждём повтора *)
					INC(c.losses)
				ELSE
					IF step IN c.repeating[id] THEN
						EXCL(c.repeating[id], step);
						INC(c.retries);
						IF ~m.ok THEN INC(c.rejectedRetries) END
					END;
					NEW(d); d.id := id; d.op := m.op; d.ok := m.ok;
					ctx.Tell(c.coord[id], d)
				END
			END
		| m: StatsReply DO
			IF (m.acc >= 0) & (m.acc < accounts) THEN
				c.statsGot[m.acc] := TRUE;
				c.statBal[m.acc] := m.balance; c.statN[m.acc] := m.n; c.statDups[m.acc] := m.dups
			END
		ELSE
		END
	END Receive;

	PROCEDURE NewClient* (): ActorCore.Actor;
		VAR c: Client; i: INTEGER;
	BEGIN
		NEW(c); c.n := pendingN; c.pace := pendingPace;
		c.sequential := pendingSeq; c.verbose := pendingVerbose; c.sink := pendingSink;
		c.head := 0; c.finished := 0; c.drives := 0; c.reporting := FALSE; c.reported := FALSE;
		c.verifying := FALSE; c.quiet := 0; c.dropStep := dropStep;
		c.retries := 0; c.rejectedRetries := 0; c.coordRetries := 0;
		c.verified := 0; c.verifyBad := 0; c.losses := 0;
		FOR i := 0 TO maxTransfers - 1 DO
			c.state[i] := notStarted; c.begun[i] := FALSE;
			c.dropped[i] := {}; c.repeating[i] := {};
			c.verifyPending[i] := FALSE; c.verifyExpected[i] := FALSE
		END;
		FOR i := 0 TO accounts - 1 DO c.statsGot[i] := FALSE END;
		lastClient := c;
		RETURN c
	END NewClient;

	PROCEDURE SpawnClient* (parent: ActorCore.Context; IN name: ARRAY OF CHAR; n: LONGINT;
		pace: INTEGER; sequential, verbose: BOOLEAN; sink: ActorCore.Ref): ActorCore.Ref;
	(** Пост: клиент с n переводами; pace > 0 — не больше pace новых Begin за
		Drive, sequential — вести переводы по одному, verbose — печатать итог
		каждого в журнал, sink — адрес получателя отчёта. *)
	BEGIN
		ASSERT((n >= 0) & (n <= maxTransfers), 20); ASSERT(sink # NIL, 21);
		pendingN := n; pendingPace := pace; pendingSeq := sequential;
		pendingVerbose := verbose; pendingSink := sink;
		RETURN ActorCore.Spawn(parent, ActorCore.NewProps(NewClient), name)
	END SpawnClient;

	(* --- короткий прогон ------------------------------------------------------ *)

	PROCEDURE (p: Printer) Receive (c: ActorCore.Context; m: Msg);
		VAR i: INTEGER;
	BEGIN
		WITH m: Report DO
			Console.WriteStr("transfers: done="); WInt(m.done);
			Console.WriteStr(" failed="); WInt(m.failed);
			Console.WriteStr(" refunded="); WInt(m.refunded);
			Console.WriteLn;
			Console.WriteStr("accounts:");
			FOR i := 0 TO accounts - 1 DO
				Console.WriteStr(" "); WInt(m.bal[i])
			END;
			Console.WriteStr(" sum="); WInt(m.sum); Console.WriteLn;
			Console.WriteStr("events="); WInt(m.applied);
			Console.WriteStr(" dup="); WInt(m.dups);
			IF m.good THEN Console.WriteStr(" ok") ELSE Console.WriteStr(" bad") END;
			Console.WriteLn;
			demoDone := TRUE
		ELSE
		END
	END Receive;

	PROCEDURE NewPrinter* (): ActorCore.Actor;
		VAR p: Printer;
	BEGIN
		NEW(p); RETURN p
	END NewPrinter;

	PROCEDURE Settle;
	BEGIN
		REPEAT IF ActorSim.Drain(100000) THEN END UNTIL ~ActorNetMem.Pump(net) & ActorSim.Idle()
	END Settle;

	PROCEDURE (t: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR from, to, sum: INTEGER; owner: ActorCore.Name; s: ARRAY 32 OF CHAR;
	BEGIN
		WHILE ~demoDone DO
			IF ActorSim.Step() THEN END;
			IF ActorNetMem.Pump(net) THEN END;
			IF ~crashed & (Progress() >= 4) & (ActiveState() = withdrawn) THEN
				Plan(Active(), from, to, sum);
				Key(to, s);
				ActorGrain.Owner(cl, "acct", s, owner);
				Console.WriteStr("node "); Console.WriteStr(owner); Console.WriteStr(" crashes");
				Console.WriteLn;
				ActorNetMem.Crash(net, owner); crashed := TRUE; Settle
			END;
			IF t.n MOD 4 = 0 THEN
				NEW(t.drive); ActorCore.Tell(t.client, t.drive)
			END;
			INC(t.n);
			IF ActorSim.Idle() & (ActorNetMem.Pending(net) = 0) THEN Tasks.Sleep(1) END
		END
	END Body;

	PROCEDURE Do*;
	(** Короткий прогон: два узла, 16 переводов, падение узла посреди работы. *)
		VAR a, b: ActorNet.Node; ca, cb: ActorGrain.Cluster; j: ActorJournal.Journal;
			cap: ARRAY 256 OF CHAR; d: Driver; printer, client: ActorCore.Ref;
	BEGIN
		ActorSim.Install; net := ActorNetMem.New(1);
		j := ActorJournal.NewMem();
		a := ActorNet.New("A", net, 11); ActorNetMem.Add(net, a);
		b := ActorNet.New("B", net, 22); ActorNetMem.Add(net, b);
		ca := ActorGrain.New(a, j); ActorGrain.Register(ca, "acct", NewAccount);
		ActorGrain.Register(ca, "xfer", NewCoord);
		cb := ActorGrain.New(b, j); ActorGrain.Register(cb, "acct", NewAccount);
		ActorGrain.Register(cb, "xfer", NewCoord);
		ActorGrain.HostCap(ca, cap); ActorGrain.Member(cb, cap);
		ActorGrain.HostCap(cb, cap); ActorGrain.Member(ca, cap);
		cl := cb; crashed := FALSE; demoDone := FALSE;
		printer := ActorCore.Spawn(NIL, ActorCore.NewProps(NewPrinter), "printer");
		client := SpawnClient(NIL, "client", 16, 0, TRUE, TRUE, printer);
		Console.WriteStr("ObxCluster: "); WInt(accounts);
		Console.WriteStr(" accounts, 16 transfers"); Console.WriteLn;
		NEW(d); d.client := client; d.n := 0; Tasks.Start(d);
		WHILE ~demoDone DO Services.actionHook.Step END
	END Do;

BEGIN
	dropStep := -1	(* терять ответы умеет только сценарий *)
END ObxCluster.
```

Сборка и запуск:

   ObxCluster.Do

```
echo 'ObxCluster.Do' | tools/run64.sh
```

В журнале появится:

```
ObxCluster: 8 accounts, 16 transfers
transfer 0: done
transfer 1: done
transfer 2: refunded
transfer 3: refunded
node A crashes
transfer 4: refunded
transfer 5: refunded
transfer 6: refunded
transfer 7: failed
transfer 8: failed
transfer 9: failed
transfer 10: failed
transfer 11: done
transfer 12: refunded
transfer 13: refunded
transfer 14: refunded
transfer 15: refunded
transfers: done=3 failed=4 refunded=9
accounts: 900 863 1207 793 1237 1000 1000 1000 sum=8000
events=37 dup=0 ok
```

Что произошло:

1. Каждый счёт — актор `ActorGrain` вида `"acct"`; его ключ — номер счёта. Поток журнала —
   `acct.<номер>`, поэтому состояние поднимается на любом узле.
2. Перевод — сага: координатор вида `"xfer"` записывает шаг в свой журнал, отдаёт шаг клиенту
   письмом `Cmd`, клиент шлёт команду `Move` счёту и отвечает `Done`.
3. Номер операции — `перевод * 3 + шаг`. Счёт помнит номера, поэтому повтор операции возвращает
   записанный итог и не двигает деньги второй раз.
4. Списание проходит, если остаток не ниже нуля; зачисление — если остаток не выше `limit`; возврат
   проходит всегда. Отклонённое зачисление возвращает списанную сумму.
5. Узел A теряется после первых четырёх переводов. Его воплощения останавливаются, уцелевший
   хозяин считает его мёртвым, и следующее письмо поднимает то же состояние на B из журнала.
6. Итоговая сумма равна начальной, повторных событий в счетах нет (`dup=0`), у каждого перевода один
   из трёх исходов.

Короткий прогон не теряет ответов и не пассивирует счета. Сценарий кластера делает и то и другое.
Координатор повторяет шаг, если `Done` не пришёл за `retryMs`; клиент теряет первый ответ шага 0
(`SetDropStep`), поэтому счёт отвечает на этот шаг из списка номеров операций. Когда все переводы
закончены, клиент ждёт `quietDrives` проходов, пока счета пассивируются, и снова шлёт шаг 0 каждого
перевода; ответ обязан совпасть с записанным итогом. Только после этого он читает остатки и сверяет
каждый с итогами переводов: `done` двигает `sum` от источника к получателю, `failed` и `refunded`
не двигают ничего. В отчёте есть `retries`, `rejectedRetries`, `coordRetries`, `verified`,
`verifyBad`, `losses` и `conserved`; сценарий требует положительного `retries` у каждого узла,
положительного `losses` хотя бы у одного, нулевого `verifyBad` и `conserved=1`.

Пример показывает автомат перевода и сагу на стороне клиента; кластерный прогон добавляет журнал
Raft, службу состава и перезапуски — о них в
[Actor/Docu/ru/Cluster.md](../../../Actor/Docu/ru/Cluster.md).
