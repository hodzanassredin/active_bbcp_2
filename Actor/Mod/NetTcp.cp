MODULE ActorNetTcp;
(** Транспорт узлов на TCP: CommTCP и AsyncIO, как у компутонов.

	Net слушает свой адрес; адреса других узлов задаёт Peer. Кадр к узлу
	уходит по исходящему соединению (одно на узел, открывается при первом
	кадре) в очередь задачи-писателя: Send не ждёт сети и годится и из
	обработчика актора, и вне задач. На проводе — длина (4 байта) и кадр
	ActorNet; первое сообщение соединения — имя узла-отправителя.

	Входящие соединения читает задача-читатель на каждое: кадры идут в
	ActorNet.Deliver, обрыв даёт ActorNet.Lost(имя отправителя). Обрыв
	исходящего соединения тоже даёт Lost; следующий кадр к узлу откроет
	соединение заново. Доставка — «не более одного раза»: кадры в очереди
	оборванного соединения пропадают.

	Ожидание готовности сокета — AsyncIO: главный цикл должен опрашивать
	дескрипторы (в консоли это делает цикл ввода, в тестах — AsyncIO.Poll).
	Только Linux (AsyncIO ждёт дескриптор через Poller). *)

	IMPORT Tasks, CommStreams, CommTCP, AsyncIO, ActorCore, ActorJournal, ActorNet;

	CONST
		maxFrame = 16 * 1024 * 1024;	(* больший кадр — обрыв соединения *)
		idleMs = 20;	(* писатель без кадров проверяет обрыв так часто *)

	TYPE
		Name = ActorCore.Name;
		Bytes = ActorJournal.Bytes;

		Item = POINTER TO RECORD b: Bytes; len: INTEGER; next: Item END;

		Link = POINTER TO RECORD
			name: Name; adr: ARRAY 64 OF CHAR;
			out: Writer;
			next: Link
		END;

		Net* = POINTER TO LIMITED RECORD (ActorNet.Transport)
			node-: ActorNet.Node;
			sentFrames-, readFrames-: LONGINT;
			l: CommStreams.Listener;
			peers: Link;
			acceptor: Acceptor;
			readers: Reader;
			closed: BOOLEAN
		END;

		Writer = POINTER TO RECORD (Tasks.Task)
			net: Net; peer: Link; s: CommStreams.Stream;
			head, tail: Item; parked, dead: BOOLEAN
		END;

		Reader = POINTER TO RECORD (Tasks.Task)
			net: Net; s: CommStreams.Stream; next: Reader
		END;

		Acceptor = POINTER TO RECORD (Tasks.Task) net: Net END;

	(* --- провод -------------------------------------------------------------- *)

	PROCEDURE WriteN (s: CommStreams.Stream; IN x: ARRAY OF BYTE; n: INTEGER): BOOLEAN;
		VAR beg, k: INTEGER;
	BEGIN
		beg := 0;
		WHILE beg < n DO
			AsyncIO.Write(s, x, beg, n - beg, k);
			IF k <= 0 THEN RETURN FALSE END;
			INC(beg, k)
		END;
		RETURN TRUE
	END WriteN;

	PROCEDURE ReadN (s: CommStreams.Stream; VAR x: ARRAY OF BYTE; n: INTEGER): BOOLEAN;
		VAR beg, k: INTEGER;
	BEGIN
		beg := 0;
		WHILE beg < n DO
			AsyncIO.Read(s, x, beg, n - beg, k);
			IF k <= 0 THEN RETURN FALSE END;
			INC(beg, k)
		END;
		RETURN TRUE
	END ReadN;

	PROCEDURE PutLen (VAR h: ARRAY OF BYTE; n: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO 3 DO h[i] := SHORT(SHORT(n MOD 256)); n := n DIV 256 END
	END PutLen;

	PROCEDURE GetLen (IN h: ARRAY OF BYTE): INTEGER;
		VAR i, n: INTEGER;
	BEGIN
		n := 0; FOR i := 3 TO 0 BY -1 DO n := n * 256 + h[i] MOD 256 END;
		RETURN n
	END GetLen;

	PROCEDURE SendMsg (s: CommStreams.Stream; IN b: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
		VAR h: ARRAY 4 OF BYTE;
	BEGIN
		PutLen(h, len);
		RETURN WriteN(s, h, 4) & WriteN(s, b, len)
	END SendMsg;

	PROCEDURE RecvMsg (s: CommStreams.Stream; OUT b: Bytes; OUT len: INTEGER): BOOLEAN;
		VAR h: ARRAY 4 OF BYTE;
	BEGIN
		b := NIL; len := 0;
		IF ~ReadN(s, h, 4) THEN RETURN FALSE END;
		len := GetLen(h);
		IF (len < 0) OR (len > maxFrame) THEN RETURN FALSE END;
		NEW(b, MAX(len, 1));
		RETURN ReadN(s, b, len)
	END RecvMsg;

	PROCEDURE NameBytes (IN s: ARRAY OF CHAR; OUT b: Bytes; OUT len: INTEGER);
		VAR i: INTEGER;
	BEGIN
		len := LEN(s$); NEW(b, MAX(len, 1));
		FOR i := 0 TO len - 1 DO b[i] := SHORT(SHORT(ORD(s[i]) MOD 128)) END
	END NameBytes;

	(* --- писатель ------------------------------------------------------------ *)

	PROCEDURE (w: Writer) Run;
		VAR it: Item; res, k: INTEGER; b: Bytes; len: INTEGER; ok: BOOLEAN; probe: ARRAY 1 OF BYTE;
	BEGIN
		CommTCP.NewStream("", w.peer.adr, w.s, res);
		ok := (res = CommStreams.done) & (w.s # NIL);
		IF ok THEN NameBytes(w.net.node.name, b, len); ok := SendMsg(w.s, b, len) END;
		WHILE ok & ~w.net.closed DO
			IF w.head = NIL THEN	(* ждём кадр; раз в idleMs проверяем, не закрыт ли сокет той стороной *)
				w.s.ReadBytes(probe, 0, 1, k);
				IF ~w.s.IsConnected() THEN ok := FALSE
				ELSE w.parked := TRUE; Tasks.Sleep(idleMs); w.parked := FALSE
				END
			ELSE
				it := w.head; w.head := it.next; IF w.head = NIL THEN w.tail := NIL END;
				ok := SendMsg(w.s, it.b, it.len);
				IF ok THEN INC(w.net.sentFrames) END
			END
		END;
		w.dead := TRUE; w.head := NIL; w.tail := NIL;
		IF w.peer.out = w THEN w.peer.out := NIL END;
		IF w.s # NIL THEN w.s.Close END;
		IF ~w.net.closed & (w.net.node # NIL) THEN ActorNet.Lost(w.net.node, w.peer.name) END
	END Run;

	PROCEDURE (net: Net) Send* (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER);
		VAR p: Link; it: Item; i: INTEGER; w: Writer;
	BEGIN
		IF net.closed THEN RETURN END;
		p := net.peers; WHILE (p # NIL) & (p.name # to) DO p := p.next END;
		IF p = NIL THEN RETURN END;	(* адрес узла не задан: кадр пропал *)
		IF p.out = NIL THEN
			NEW(w); w.net := net; w.peer := p; p.out := w; Tasks.Start(w)
		END;
		w := p.out;
		NEW(it); NEW(it.b, MAX(len, 1)); it.len := len;
		FOR i := 0 TO len - 1 DO it.b[i] := frame[i] END;
		IF w.tail = NIL THEN w.head := it ELSE w.tail.next := it END;
		w.tail := it;
		IF w.parked THEN w.parked := FALSE; Tasks.Unpark(w) END
	END Send;

	(* --- читатель и приём ------------------------------------------------------- *)

	PROCEDURE (r: Reader) Run;
		VAR b: Bytes; len, i: INTEGER; from: Name;
	BEGIN
		IF RecvMsg(r.s, b, len) & (len < ActorCore.nameLen) THEN
			FOR i := 0 TO len - 1 DO from[i] := CHR(b[i] MOD 256) END; from[len] := 0X;
			WHILE ~r.net.closed & RecvMsg(r.s, b, len) DO
				INC(r.net.readFrames);
				IF r.net.node # NIL THEN ActorNet.Deliver(r.net.node, b, len) END
			END;
			IF ~r.net.closed & (r.net.node # NIL) THEN ActorNet.Lost(r.net.node, from) END
		END;
		r.s.Close
	END Run;

	PROCEDURE (a: Acceptor) Run;
		VAR s: CommStreams.Stream; r: Reader;
	BEGIN
		WHILE ~a.net.closed DO
			IF AsyncIO.Accept(a.net.l, s, 100) & ~a.net.closed THEN
				NEW(r); r.net := a.net; r.s := s; r.next := a.net.readers; a.net.readers := r;
				Tasks.Start(r)
			END
		END
	END Run;

	(* --- сеть ------------------------------------------------------------------ *)

	PROCEDURE New* (IN listen: ARRAY OF CHAR; OUT net: Net; OUT res: INTEGER);
	(** Сеть, слушающая адрес listen ("127.0.0.1:0" — свободный порт).
		Пост: res = CommStreams.done — net слушает, адрес — Address;
		иначе net = NIL. *)
	BEGIN
		NEW(net); CommTCP.NewListener(listen, net.l, res);
		IF (res # CommStreams.done) OR (net.l = NIL) THEN net := NIL
		ELSE NEW(net.acceptor); net.acceptor.net := net; Tasks.Start(net.acceptor)
		END
	END New;

	PROCEDURE Attach* (net: Net; n: ActorNet.Node);
	(** Пред: узел n создан на транспорте net — не проверяется; узла ещё нет — 20.
		Пост: принятые кадры идут узлу n. *)
	BEGIN
		ASSERT(net.node = NIL, 20);
		net.node := n
	END Attach;

	PROCEDURE Address* (net: Net; OUT adr: ARRAY OF CHAR);
	(** Пост: адрес, который слушает net ("хост:порт"). *)
		VAR a: CommStreams.Adr;
	BEGIN
		a := net.l.LocalAdr(); adr := a^$
	END Address;

	PROCEDURE Peer* (net: Net; IN name, adr: ARRAY OF CHAR);
	(** Кадры узлу name идут на адрес adr. Повтор для того же имени меняет адрес. *)
		VAR p: Link;
	BEGIN
		p := net.peers; WHILE (p # NIL) & (p.name # name) DO p := p.next END;
		IF p = NIL THEN NEW(p); p.name := name$; p.next := net.peers; net.peers := p END;
		p.adr := adr$
	END Peer;

	PROCEDURE Close* (net: Net);
	(** Сеть закрыта: слушатель и соединения закрыты, задачи завершаются;
		другие узлы увидят обрыв (Lost). *)
		VAR p: Link; r: Reader;
	BEGIN
		IF net.closed THEN RETURN END;
		net.closed := TRUE;
		net.l.Close;
		p := net.peers;
		WHILE p # NIL DO
			IF p.out # NIL THEN
				IF p.out.s # NIL THEN p.out.s.Close END;
				IF p.out.parked THEN p.out.parked := FALSE; Tasks.Unpark(p.out) END
			END;
			p := p.next
		END;
		r := net.readers; WHILE r # NIL DO r.s.Close; r := r.next END
	END Close;

END ActorNetTcp.
