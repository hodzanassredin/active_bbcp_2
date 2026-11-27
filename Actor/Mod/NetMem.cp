MODULE ActorNetMem;
(** Сеть узлов в памяти: транспорт для тестов и примеров с планом сбоев.

	Кадр, посланный Send, встаёт в очередь сети. Pump делает один шаг
	сетевого времени (now) и доставляет один кадр, срок которого настал.
	План сбоев задаёт доли в тысячных: потерю, дубль, а также задержку
	(до delay шагов) и перестановку (кадр для доставки выбирается
	жребием среди готовых). Жребий — генератор от зерна seed: тот же seed
	и те же действия дают ту же последовательность доставок.

	Сеть без плана (NewAuto) доставляет кадры сама — действием Services в
	главном цикле, по порядку отправки, без потерь. *)

	IMPORT Int64, Services, ActorCore, ActorJournal, ActorNet;

	TYPE
		Name = ActorCore.Name;

		Plan* = RECORD
			loss*, dup*: INTEGER;	(** доли в тысячных *)
			delay*: INTEGER;	(** наибольшая задержка, шагов *)
			reorder*: BOOLEAN	(** выбирать готовый кадр жребием *)
		END;

		Member = POINTER TO RECORD n: ActorNet.Node; next: Member END;

		Frame = POINTER TO RECORD
			from, to: Name; b: ActorJournal.Bytes; len: INTEGER; due: LONGINT;
			next: Frame
		END;

		Net* = POINTER TO LIMITED RECORD (ActorNet.Transport)
			plan*: Plan;
			now-: LONGINT;	(** шагов сети *)
			lost-, duplicated-, delivered-: LONGINT;
			x: LONGINT;	(* состояние генератора *)
			members: Member;
			head, tail: Frame;
			auto: BOOLEAN;
			pump: Pumper
		END;

		Pumper = POINTER TO RECORD (Services.Action) net: Net; armed: BOOLEAN END;

	PROCEDURE Rand* (net: Net; k: INTEGER): INTEGER;
	(** Пред: k > 0 — 20. Пост: число жребия из [0, k), от зерна сети. *)
		VAR x: LONGINT;
	BEGIN
		ASSERT(k > 0, 20);
		x := Int64.Mul(net.x, 6364136223846793005); INC(x, 1442695040888963407);	(* LCG Кнута, 64 бита *)
		net.x := x;
		RETURN SHORT(ASH(x, -33) MOD k)	(* старшие биты: у младших короткий период *)
	END Rand;

	PROCEDURE Find (net: Net; IN name: ARRAY OF CHAR): ActorNet.Node;
		VAR m: Member;
	BEGIN
		m := net.members;
		WHILE (m # NIL) & (m.n.name # name) DO m := m.next END;
		IF m = NIL THEN RETURN NIL ELSE RETURN m.n END
	END Find;

	PROCEDURE Enqueue (net: Net; IN from, to: ARRAY OF CHAR; IN b: ARRAY OF BYTE; len: INTEGER);
		VAR f: Frame; i: INTEGER;
	BEGIN
		NEW(f); f.from := from$; f.to := to$; f.len := len; NEW(f.b, MAX(len, 1));
		FOR i := 0 TO len - 1 DO f.b[i] := b[i] END;
		f.due := net.now;
		IF net.plan.delay > 0 THEN f.due := net.now + Rand(net, net.plan.delay + 1) END;
		IF net.tail = NIL THEN net.head := f ELSE net.tail.next := f END;
		net.tail := f
	END Enqueue;

	PROCEDURE (net: Net) Send* (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER);
	BEGIN
		IF (net.plan.loss > 0) & (Rand(net, 1000) < net.plan.loss) THEN INC(net.lost); RETURN END;
		Enqueue(net, from, to, frame, len);
		IF (net.plan.dup > 0) & (Rand(net, 1000) < net.plan.dup) THEN
			INC(net.duplicated); Enqueue(net, from, to, frame, len)
		END;
		IF net.auto & ~net.pump.armed THEN net.pump.armed := TRUE; Services.DoLater(net.pump, Services.now) END
	END Send;

	PROCEDURE Pending* (net: Net): INTEGER;
	(** Пост: кадров в пути. *)
		VAR f: Frame; k: INTEGER;
	BEGIN
		k := 0; f := net.head; WHILE f # NIL DO INC(k); f := f.next END;
		RETURN k
	END Pending;

	PROCEDURE Pump* (net: Net): BOOLEAN;
	(** Шаг сети: now растёт на 1, доставлен не более чем один готовый кадр
		(первый по порядку или, с reorder, выбранный жребием).
		Пост: TRUE — кадры в пути ещё есть. *)
		VAR f, p, pick, pickPrev: Frame; k, ready: INTEGER; n: ActorNet.Node;
	BEGIN
		INC(net.now);
		ready := 0; f := net.head;
		WHILE f # NIL DO IF f.due <= net.now THEN INC(ready) END; f := f.next END;
		IF ready > 0 THEN
			IF net.plan.reorder THEN k := Rand(net, ready) ELSE k := 0 END;
			p := NIL; f := net.head; pick := NIL;
			WHILE pick = NIL DO
				IF f.due <= net.now THEN
					IF k = 0 THEN pick := f; pickPrev := p ELSE DEC(k) END
				END;
				IF pick = NIL THEN p := f; f := f.next END
			END;
			IF pickPrev = NIL THEN net.head := pick.next ELSE pickPrev.next := pick.next END;
			IF net.tail = pick THEN net.tail := pickPrev END;
			n := Find(net, pick.to);
			IF n # NIL THEN INC(net.delivered); ActorNet.Deliver(n, pick.b, pick.len) END
		END;
		RETURN net.head # NIL
	END Pump;

	PROCEDURE (p: Pumper) Do;
	BEGIN
		p.armed := FALSE;
		WHILE Pump(p.net) DO END
	END Do;

	PROCEDURE Add* (net: Net; n: ActorNet.Node);
	(** Пред: узла с тем же именем в сети нет — 20. Пост: кадры к n.name доходят до n. *)
		VAR m: Member;
	BEGIN
		ASSERT(Find(net, n.name) = NIL, 20);
		NEW(m); m.n := n; m.next := net.members; net.members := m
	END Add;

	PROCEDURE Drop* (net: Net);
	(** Все кадры в пути пропадают (потеря ответа в тестах). *)
		VAR k: INTEGER;
	BEGIN
		k := Pending(net); INC(net.lost, k); net.head := NIL; net.tail := NIL
	END Drop;

	PROCEDURE Crash* (net: Net; IN name: ARRAY OF CHAR);
	(** Падение узла name: ActorNet.Down, кадры к нему в пути пропадают,
		остальные узлы получают Lost(name). *)
		VAR n: ActorNet.Node; m: Member; f, p: Frame;
	BEGIN
		n := Find(net, name); ASSERT(n # NIL, 20);
		ActorNet.Down(n);
		p := NIL; f := net.head;
		WHILE f # NIL DO
			IF f.to = name THEN
				INC(net.lost);
				IF p = NIL THEN net.head := f.next ELSE p.next := f.next END;
				IF net.tail = f THEN net.tail := p END
			ELSE p := f
			END;
			f := f.next
		END;
		m := net.members;
		WHILE m # NIL DO
			IF m.n # n THEN ActorNet.Lost(m.n, name) END;
			m := m.next
		END
	END Crash;

	PROCEDURE New* (seed: LONGINT): Net;
	(** Сеть с планом сбоев (по умолчанию пустым), доставка — Pump. *)
		VAR net: Net;
	BEGIN
		NEW(net); net.x := Int64.Mul(seed, 2654435761); INC(net.x, 88172645463325252);
		IF net.x = 0 THEN net.x := 1 END;
		NEW(net.pump); net.pump.net := net;
		RETURN net
	END New;

	PROCEDURE NewAuto* (): Net;
	(** Сеть без сбоев: кадры доставляет главный цикл по порядку отправки. *)
		VAR net: Net;
	BEGIN
		net := New(1); net.auto := TRUE;
		RETURN net
	END NewAuto;

END ActorNetMem.
