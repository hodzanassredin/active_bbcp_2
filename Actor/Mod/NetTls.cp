MODULE ActorNetTls;
(** Транспорт узлов на TLS 1.3 со взаимной проверкой.

	Тот же протокол кадров, что у ActorNetTcp: длина (4 байта) и кадр
	ActorNet; имя узла в первом сообщении не передаётся — оно берётся
	из сертификата соединения. У каждого узла свой сертификат ECDSA
	P-256, выданный корнем кластера; имя узла стоит в SAN. Клиент
	предъявляет клиентский сертификат (сервер требует его), сервер —
	серверный; обе стороны проверяют цепочку до единственного
	доверенного корня. Санкционированность узла — список участников:
	пара (имя SAN, отпечаток SHA-256 сертификата) должна быть в нём, иначе
	рукопожатие (или уже установленное соединение) отвергается.

	Поле «от кого» каждого кадра сверяется с именем из сертификата
	соединения: расхождение — кадр не доставляется, соединение
	закрывается, в журнал пишется строка. Так узел с верным
	сертификатом не может выдать себя за другого.

	Сеть слушает свой адрес; адреса других узлов задаёт Peer. Исходящее
	соединение одно на узел, открывается при первом кадре. Ввод-вывод
	кооперативный: ожидание сокета уступает Tasks (CryptoAosCommStreams.
	onWait), поэтому читатель и писатель — обычные задачи. Только Linux
	(как ActorNetTcp). *)

	IMPORT S := SYSTEM, StdLog, Strings, Services, Tasks, CommStreams, Poller, AsyncIO,
		TCP := CryptoAosTCP, CryptoAosCommStreams, CryptoTLS, CryptoX509, CryptoSHA256,
		ActorCore, ActorJournal, ActorNet;

	CONST
		maxFrame = 16 * 1024 * 1024;	(* больший кадр — обрыв соединения *)
		idleMs = 20;	(* писатель без кадров проверяет обрыв так часто *)
		waitMs = 1000;	(* одноразовое ожидание дескриптора: не дольше *)
		tlsStack = 2 * 1024 * 1024;	(* рукопожатие TLS строит большие кадры на стеке *)
		maxMembers = 64;
		fpLen = 32;
		(** коды Configure *)
		badRoot* = 1; badCertificate* = 2; badKey* = 3;

	TYPE
		Name = ActorCore.Name;
		Bytes = ActorJournal.Bytes;

		(** Отпечаток сертификата: SHA-256 его DER. *)
		Fingerprint* = ARRAY fpLen OF BYTE;

		(** Участник, которому разрешены соединения. *)
		Member* = RECORD
			name*: Name;
			fp*: Fingerprint
		END;

		Item = POINTER TO RECORD b: Bytes; len: INTEGER; next: Item END;

		Link = POINTER TO RECORD
			name: Name; adr: ARRAY 64 OF CHAR;
			allowed: BOOLEAN;
			nextTry: LONGINT;	(* пауза после неудачного рукопожатия *)
			out: Writer;
			next: Link
		END;

		Net* = POINTER TO LIMITED RECORD (ActorNet.Transport)
			node-: ActorNet.Node;
			listen: ARRAY 64 OF CHAR;
			ctx: CryptoTLS.Context;
			l: CryptoTLS.Connection;
			configured, closed: BOOLEAN;
			sentFrames-, readFrames-, refused-, forged-, rejected-, revoked-: LONGINT;
			accepts-, acceptFails-, hsOk-, hsFail-, hsStarted-, accTicks-, badState-: LONGINT;
			peers: Link;
			readers: Reader;
			acceptor: Acceptor;
			members: ARRAY maxMembers OF Member;
			nmembers: INTEGER
		END;

		Writer = POINTER TO RECORD (Tasks.Task)
			net: Net; peer: Link; tls: CryptoTLS.Connection;
			head, tail: Item; parked: BOOLEAN
		END;

		Reader = POINTER TO RECORD (Tasks.Task)
			net: Net; tls: CryptoTLS.Connection; name: Name;
			allowed, closed: BOOLEAN; next: Reader
		END;

		Acceptor = POINTER TO RECORD (Tasks.Task) net: Net END;

	VAR
		(** Журнал проверок: владелец ставит сюда запись в свой файл.
			Транспорт не знает каталога прогона. *)
		audit*: PROCEDURE (IN text: ARRAY OF CHAR);

	(* --- ожидание сокета ------------------------------------------------------- *)

	PROCEDURE Wait (s: CommStreams.Stream; write: BOOLEAN);
	(* CryptoAosCommStreams зовёт это, когда сокет не дал ни байта:
	   задача ждёт готовности дескриптора, а не просыпается каждую
	   миллисекунду. Событие приходит из главного цикла (Poller). *)
		VAR fd: INTEGER;
	BEGIN
		IF Tasks.InTask() & (s # NIL) THEN
			fd := s.Handle();
			IF fd < 0 THEN Tasks.Sleep(1)
			ELSIF write THEN AsyncIO.WaitFdFor(fd, Poller.Out, waitMs)
			ELSE AsyncIO.WaitFdFor(fd, Poller.In, waitMs)
			END
		END
	END Wait;

	(* --- провод --------------------------------------------------------------- *)

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

	PROCEDURE ReadN (s: CryptoTLS.Connection; VAR x: ARRAY OF BYTE; n: INTEGER): BOOLEAN;
		VAR sc: POINTER TO ARRAY OF SHORTCHAR; beg, got, res, i: INTEGER;
	BEGIN
		IF n <= 0 THEN RETURN TRUE END;
		NEW(sc, n);
		beg := 0;
		WHILE beg < n DO
			got := 0;
			s.Receive(sc^, beg, n - beg, 1, got, res);
			IF got <= 0 THEN RETURN FALSE END;
			INC(beg, got)
		END;
		FOR i := 0 TO n - 1 DO x[i] := SHORT(SHORT(ORD(sc[i]) MOD 256)) END;
		RETURN TRUE
	END ReadN;

	PROCEDURE WriteN (s: CryptoTLS.Connection; IN x: ARRAY OF BYTE; n: INTEGER): BOOLEAN;
		VAR sc: POINTER TO ARRAY OF SHORTCHAR; i, res, written: INTEGER;
	BEGIN
		IF n <= 0 THEN RETURN TRUE END;
		NEW(sc, n);
		FOR i := 0 TO n - 1 DO sc[i] := SHORT(CHR(x[i] MOD 256)) END;
		s.Send(sc^, 0, n, TRUE, res);
		written := n;
		IF res # 0 THEN RETURN FALSE END;
		RETURN written = n
	END WriteN;

	PROCEDURE SendMsg (s: CryptoTLS.Connection; IN b: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
	(* Заголовок и кадр — одна запись TLS и одна запись в сокет: меньше
	   системных вызовов и записей, которые ждут подтверждения. *)
		VAR buf: POINTER TO ARRAY OF BYTE; i: INTEGER;
	BEGIN
		NEW(buf, len + 4);
		PutLen(buf^, len);
		FOR i := 0 TO len - 1 DO buf[i + 4] := b[i] END;
		RETURN WriteN(s, buf^, len + 4)
	END SendMsg;

	PROCEDURE RecvMsg (s: CryptoTLS.Connection; OUT b: Bytes; OUT len: INTEGER): BOOLEAN;
		VAR h: ARRAY 4 OF BYTE;
	BEGIN
		b := NIL; len := 0;
		IF ~ReadN(s, h, 4) THEN RETURN FALSE END;
		len := GetLen(h);
		IF (len < 0) OR (len > maxFrame) THEN RETURN FALSE END;
		NEW(b, MAX(len, 1));
		RETURN ReadN(s, b^, len)
	END RecvMsg;

	(* --- сертификаты ----------------------------------------------------------- *)

	PROCEDURE SameFp (IN a, b: Fingerprint): BOOLEAN;
		VAR i, d: INTEGER;
	BEGIN
		d := 0; FOR i := 0 TO fpLen - 1 DO IF a[i] # b[i] THEN INC(d) END END;
		RETURN d = 0
	END SameFp;

	PROCEDURE CertFingerprint (c: CryptoX509.Certificate; OUT fp: Fingerprint);
		VAR h: CryptoSHA256.SHA256; d: ARRAY fpLen OF SHORTCHAR; i: INTEGER;
	BEGIN
		FOR i := 0 TO fpLen - 1 DO fp[i] := 0 END;
		IF c = NIL THEN RETURN END;
		h.Initialize;
		IF (c.raw # NIL) & (c.rawLen > 0) THEN h.OPERATORBRACKETS(c.raw^, 0, c.rawLen) END;
		h.GetHash(d, 0);
		FOR i := 0 TO fpLen - 1 DO fp[i] := SHORT(SHORT(ORD(d[i]) MOD 256)) END
	END CertFingerprint;

	PROCEDURE SameName (IN a: ARRAY OF SHORTCHAR; aOfs, aLen: INTEGER;
		IN b: ARRAY OF CHAR): BOOLEAN;
		VAR i, bLen: INTEGER;
	BEGIN
		bLen := 0; WHILE (bLen < LEN(b)) & (b[bLen] # 0X) DO INC(bLen) END;
		IF aLen # bLen THEN RETURN FALSE END;
		i := 0;
		WHILE i < aLen DO
			IF a[aOfs + i] # SHORT(b[i]) THEN RETURN FALSE END;
			INC(i)
		END;
		RETURN TRUE
	END SameName;

	PROCEDURE CertHasName (c: CryptoX509.Certificate; IN name: ARRAY OF CHAR): BOOLEAN;
	(* Пост: TRUE — в SAN сертификата есть DNS-имя, равное name. CN не
	   используется: имя узла обязано быть в SAN. *)
		VAR i: INTEGER; found: BOOLEAN;
	BEGIN
		IF c = NIL THEN RETURN FALSE END;
		found := FALSE; i := 0;
		WHILE (i < c.sanCount) & ~found DO
			IF ~c.sanIsIP[i] & (c.sanValue[i] # NIL) THEN
				found := SameName(c.sanValue[i]^, 0, c.sanLen[i], name)
			END;
			INC(i)
		END;
		RETURN found
	END CertHasName;

	PROCEDURE FirstName (c: CryptoX509.Certificate; OUT name: Name): BOOLEAN;
	(* Имя соединения — первое DNS-имя SAN сертификата. *)
		VAR i, j: INTEGER; found: BOOLEAN;
	BEGIN
		name := ""; found := FALSE; i := 0;
		WHILE (i < c.sanCount) & ~found DO
			IF ~c.sanIsIP[i] & (c.sanValue[i] # NIL) THEN
				j := 0;
				WHILE (j < c.sanLen[i]) & (j < LEN(name) - 1) DO
					name[j] := CHR(SHORT(SHORT(ORD(c.sanValue[i]^[j]) MOD 65536))); INC(j)
				END;
				name[j] := 0X; found := j > 0
			END;
			INC(i)
		END;
		RETURN found
	END FirstName;

	(* --- список участников ------------------------------------------------------ *)

	PROCEDURE (net: Net) FindMember (IN name: ARRAY OF CHAR): INTEGER, NEW;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < net.nmembers) & (net.members[i].name # name) DO INC(i) END;
		IF i = net.nmembers THEN RETURN -1 ELSE RETURN i END
	END FindMember;

	PROCEDURE (net: Net) Allowed (IN name: ARRAY OF CHAR; IN fp: Fingerprint): BOOLEAN, NEW;
		VAR i: INTEGER;
	BEGIN
		i := net.FindMember(name);
		RETURN (i >= 0) & SameFp(net.members[i].fp, fp)
	END Allowed;

	PROCEDURE (net: Net) AllowedName (IN name: ARRAY OF CHAR): BOOLEAN, NEW;
	BEGIN
		RETURN net.FindMember(name) >= 0
	END AllowedName;

	(* --- сеть ------------------------------------------------------------------ *)

	PROCEDURE Log (IN a, b, c: ARRAY OF CHAR);
	BEGIN
		StdLog.String("ActorNetTls: "); StdLog.String(a);
		IF (LEN(b) > 0) & (b[0] # 0X) THEN StdLog.String(" '"); StdLog.String(b); StdLog.String("'") END;
		IF (LEN(c) > 0) & (c[0] # 0X) THEN StdLog.String(c) END;
		StdLog.Ln
	END Log;

	PROCEDURE (net: Net) CloseLink (name: ARRAY OF CHAR), NEW;
		VAR p: Link;
	BEGIN
		p := net.peers;
		WHILE p # NIL DO
			IF p.name = name THEN
				p.allowed := FALSE;
				IF p.out # NIL THEN
					IF p.out.tls # NIL THEN p.out.tls.SafeClose END
				END
			END;
			p := p.next
		END
	END CloseLink;

	PROCEDURE (net: Net) SetMembers* (IN m: ARRAY OF Member; n: INTEGER), NEW;
	(** Пост: список участников заменён. Исходящие связи с узлами, которых
		в списке нет, закрыты сразу; входящее соединение выбывшего узла
		закрывает его читатель при первом же кадре — кадр не доставляется,
		в журнал пишется строка, наблюдатель получает Terminated. *)
		VAR i: INTEGER; p: Link; r: Reader; known: ARRAY maxMembers OF Name;
			nk: INTEGER; name: Name; found, drop: BOOLEAN;
	BEGIN
		ASSERT((n >= 0) & (n <= maxMembers), 20);
		net.nmembers := n;
		FOR i := 0 TO n - 1 DO net.members[i] := m[i] END;
		(* исходящие связи выбывших закрываем сразу *)
		nk := 0;
		p := net.peers;
		WHILE p # NIL DO
			IF ~net.AllowedName(p.name) THEN
				drop := TRUE; i := 0;
				WHILE i < nk DO IF known[i] = p.name THEN drop := FALSE END; INC(i) END;
				IF drop THEN known[nk] := p.name$; INC(nk) END;
				net.CloseLink(p.name)
			END;
			p := p.next
		END;
		(* входящие соединения не трогаем: Reader сам отвергнет кадр *)
		r := net.readers;
		WHILE r # NIL DO
			IF ~net.AllowedName(r.name) THEN
				drop := TRUE; i := 0;
				WHILE i < nk DO IF known[i] = r.name THEN drop := FALSE END; INC(i) END;
				IF drop THEN known[nk] := r.name$; INC(nk) END
			END;
			r := r.next
		END;
		FOR i := 0 TO nk - 1 DO
			INC(net.revoked);
			Log("membership revoked node", known[i], " — outgoing closed, incoming refused");
			IF net.node # NIL THEN ActorNet.Lost(net.node, known[i]) END
		END
	END SetMembers;

	PROCEDURE (net: Net) Configure* (IN rootFile, certFile, keyFile: ARRAY OF CHAR; OUT res: INTEGER), NEW;
	(** Загрузить доверенный корень и сертификат с ключом узла до Attach.
		Пост: res = 0 — готово; иначе badRoot/badCertificate/badKey. *)
	BEGIN
		ASSERT(net # NIL, 20);
		res := 0;
		net.ctx := CryptoTLS.NewContext();
		IF ~net.ctx.AddTrustedFile(rootFile) THEN res := badRoot; RETURN END;
		IF ~net.ctx.LoadCertificateFile(certFile) THEN res := badCertificate; RETURN END;
		IF ~net.ctx.LoadEcPrivateKeyFile(keyFile) THEN res := badKey; RETURN END;
		IF ~net.ctx.LoadClientCertificateFile(certFile) THEN res := badCertificate; RETURN END;
		IF ~net.ctx.LoadClientEcPrivateKeyFile(keyFile) THEN res := badKey; RETURN END;
		net.ctx.RequireClientCertificate(TRUE);
		net.configured := TRUE
	END Configure;

	PROCEDURE (net: Net) ConfigureNoClient* (IN rootFile, certFile, keyFile: ARRAY OF CHAR;
		OUT res: INTEGER), NEW;
	(** Как Configure, но без клиентского сертификата: нужно для
		отрицательной проверки «клиентский сертификат обязателен». *)
	BEGIN
		ASSERT(net # NIL, 20);
		res := 0;
		net.ctx := CryptoTLS.NewContext();
		IF ~net.ctx.AddTrustedFile(rootFile) THEN res := badRoot; RETURN END;
		IF ~net.ctx.LoadCertificateFile(certFile) THEN res := badCertificate; RETURN END;
		IF ~net.ctx.LoadEcPrivateKeyFile(keyFile) THEN res := badKey; RETURN END;
		net.ctx.RequireClientCertificate(TRUE);
		net.configured := TRUE
	END ConfigureNoClient;

	PROCEDURE New* (IN listen: ARRAY OF CHAR; OUT net: Net; OUT res: INTEGER);
	(** Сеть узла, слушающая адрес listen. Слушатель открывает Attach:
		до него нужен Configure. Пост: res = CommStreams.done — net создан. *)
	BEGIN
		NEW(net); net.listen := listen$; net.nmembers := 0; res := CommStreams.done
	END New;

	PROCEDURE (net: Net) VerifyPeer (cert: CryptoX509.Certificate; IN expected: ARRAY OF CHAR;
		OUT fp: Fingerprint; OUT ok: BOOLEAN), NEW;
	BEGIN
		ok := FALSE; CertFingerprint(cert, fp);
		IF cert = NIL THEN Log("peer without certificate", expected, ""); RETURN END;
		IF ~CertHasName(cert, expected) THEN
			Log("certificate name does not match node", expected, ""); RETURN
		END;
		IF ~net.Allowed(expected, fp) THEN
			Log("node is not a member", expected, ""); RETURN
		END;
		ok := TRUE
	END VerifyPeer;

	PROCEDURE Attach* (net: Net; n: ActorNet.Node);
	(** Пред: net настроен Configure, узла ещё нет — не проверяется.
		Пост: открыт слушатель, принятые кадры идут узлу n. *)
		VAR res: INTEGER;
	BEGIN
		ASSERT((net # NIL) & (n # NIL), 20); ASSERT(net.node = NIL, 21);
		ASSERT(net.configured, 22);
		net.node := n;
		net.l := CryptoTLS.GetConnection();
		net.l.SetContext(net.ctx);
		net.l.Open(net.listen, "", res);
		ASSERT((res = 0) & (net.l.State() = TCP.Listen), 23);
		NEW(net.acceptor); net.acceptor.net := net;
		Tasks.SetStackSize(net.acceptor, tlsStack); Tasks.Start(net.acceptor)
	END Attach;

	PROCEDURE Address* (net: Net; OUT adr: ARRAY OF CHAR);
	(** Пост: адрес, который слушает net (заданный при New). *)
	BEGIN
		adr := net.listen$
	END Address;

	PROCEDURE Peer* (net: Net; IN name, adr: ARRAY OF CHAR);
	(** Кадры узлу name идут на адрес adr; соединение — с проверкой
		сертификата имени name и списка участников. *)
		VAR p: Link;
	BEGIN
		p := net.peers; WHILE (p # NIL) & (p.name # name) DO p := p.next END;
		IF p = NIL THEN NEW(p); p.name := name$; p.next := net.peers; net.peers := p END;
		p.adr := adr$; p.allowed := net.AllowedName(name)
	END Peer;

	PROCEDURE (net: Net) Send* (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER);
		VAR p: Link; it: Item; i, res: INTEGER; w: Writer; tls: CryptoTLS.Connection;
			cert: CryptoX509.Certificate; fp: Fingerprint; ok: BOOLEAN;
	BEGIN
		IF net.closed THEN RETURN END;
		IF ~net.AllowedName(to) THEN INC(net.refused); Log("frame to non-member dropped", to, ""); RETURN END;
		p := net.peers; WHILE (p # NIL) & (p.name # to) DO p := p.next END;
		IF p = NIL THEN INC(net.refused); RETURN END;
		IF ~p.allowed THEN INC(net.refused); RETURN END;
		IF (p.out = NIL) & (Services.Ticks() < p.nextTry) THEN
			(* узел недавно не ответил: новое рукопожатие дорого, ждём *)
			INC(net.refused); RETURN
		END;
		IF p.out = NIL THEN
			NEW(w); w.net := net; w.peer := p; p.out := w;
			Tasks.SetStackSize(w, tlsStack); Tasks.Start(w)
		END;
		w := p.out;
		NEW(it); NEW(it.b, MAX(len, 1)); it.len := len;
		FOR i := 0 TO len - 1 DO it.b[i] := frame[i] END;
		IF w.tail = NIL THEN w.head := it ELSE w.tail.next := it END;
		w.tail := it;
		IF w.parked THEN w.parked := FALSE; Tasks.Unpark(w) END
	END Send;

	(* --- писатель -------------------------------------------------------------- *)

	PROCEDURE ToShort (IN name: ARRAY OF CHAR; OUT sc: ARRAY OF SHORTCHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(sc) - 1) & (i < LEN(name)) & (name[i] # 0X) DO
			sc[i] := SHORT(CHR(ORD(name[i]) MOD 256)); INC(i)
		END;
		sc[i] := 0X
	END ToShort;

	PROCEDURE (w: Writer) Run;
		VAR it: Item; res: INTEGER; ok: BOOLEAN; cert: CryptoX509.Certificate;
			fp: Fingerprint; sc: ARRAY 64 OF SHORTCHAR;
	BEGIN
		ToShort(w.peer.name, sc);
		NEW(w.tls); w.tls.Init(); w.tls.SetContext(w.net.ctx); w.tls.SetServerName(sc);
		INC(w.net.hsStarted);
		w.tls.Open("0.0.0.0:0", w.peer.adr, res);
		ok := (res = 0) & w.tls.ConnAlive() & (w.tls.PeerCertificate() # NIL);
		IF ok THEN
			cert := w.tls.PeerCertificate();
			w.net.VerifyPeer(cert, w.peer.name, fp, ok);
			IF ~ok THEN
				INC(w.net.rejected); INC(w.net.hsFail); Log("server certificate rejected", w.peer.name, "")
			ELSE INC(w.net.hsOk)
			END
		ELSE
			INC(w.net.rejected); INC(w.net.hsFail); Log("handshake failed", w.peer.name, "")
		END;
		IF ok THEN w.peer.nextTry := 0 ELSE w.peer.nextTry := Services.Ticks() + 500 END;
		WHILE ok & ~w.net.closed & w.peer.allowed DO
			IF w.head = NIL THEN
				(* Сон до кадра: Send разбудит сразу; idleMs — только
				   проверка, не оборвано ли соединение. *)
				w.parked := TRUE; Tasks.Sleep(idleMs); w.parked := FALSE;
				IF ~w.peer.allowed OR ~w.tls.ConnAlive() THEN ok := FALSE END
			ELSE
				it := w.head; w.head := it.next; IF w.head = NIL THEN w.tail := NIL END;
				ok := SendMsg(w.tls, it.b^, it.len);
				IF ok THEN INC(w.net.sentFrames) END
			END
		END;
		w.head := NIL; w.tail := NIL;
		IF w.peer.out = w THEN w.peer.out := NIL END;
		IF w.tls # NIL THEN w.tls.SafeClose; w.tls := NIL END;
		IF ~w.net.closed & (w.net.node # NIL) & ~w.peer.allowed THEN
			(* отзыв уже дал Lost в SetMembers; повтор не нужен *)
		ELSIF ~w.net.closed & (w.net.node # NIL) THEN
			ActorNet.Lost(w.net.node, w.peer.name)
		END
	END Run;

	(* --- читатель и приём ------------------------------------------------------- *)

	PROCEDURE (r: Reader) Run;
		VAR b: Bytes; len: INTEGER; from: Name; ok: BOOLEAN;
	BEGIN
		ok := TRUE;
		WHILE ok & ~r.net.closed & ~r.closed & r.allowed DO
			IF ~RecvMsg(r.tls, b, len) THEN ok := FALSE
			ELSIF ~ActorNet.FrameFrom(b^, len, from) THEN
				INC(r.net.forged); Log("malformed frame from", r.name, " — connection closed");
				ok := FALSE
			ELSIF from # r.name THEN
				INC(r.net.forged);
				IF audit # NIL THEN audit("forged-frame: from does not match certificate") END;
				StdLog.String("ActorNetTls: frame from '"); StdLog.String(from$);
				StdLog.String("' on connection authenticated as '"); StdLog.String(r.name$);
				StdLog.String("' — connection closed"); StdLog.Ln;
				ok := FALSE
			ELSIF ~r.net.AllowedName(r.name) THEN
				INC(r.net.revoked);
				IF audit # NIL THEN audit("revoked-frame: node is no longer a member") END;
				Log("frame from revoked node", r.name, " — connection closed");
				ok := FALSE
			ELSE
				INC(r.net.readFrames);
				IF r.net.node # NIL THEN ActorNet.Deliver(r.net.node, b^, len) END
			END
		END;
		r.closed := TRUE;
		IF r.tls # NIL THEN r.tls.SafeClose; r.tls := NIL END;
		IF ~r.net.closed & (r.net.node # NIL) & r.allowed THEN
			ActorNet.Lost(r.net.node, r.name)
		END
	END Run;

	PROCEDURE (a: Acceptor) Run;
		VAR tc: TCP.Connection; tls: CryptoTLS.Connection; r: Reader; res, fd: INTEGER;
			cert: CryptoX509.Certificate; name: Name; fp: Fingerprint; ok: BOOLEAN;
	BEGIN
		WHILE ~a.net.closed DO
			INC(a.net.accTicks);
			tc := NIL; tls := NIL;
			a.net.l.Accept(tc, res);
			IF (res = -2) THEN
				INC(a.net.acceptFails); INC(a.net.hsFail)
			ELSIF (res # 0) & (res # -1) THEN
				INC(a.net.badState)
			END;
			IF (res = 0) & (tc # NIL) & ~a.net.closed THEN
				INC(a.net.accepts);
				tls := tc(CryptoTLS.Connection);
				ok := FALSE; name := "";
				IF tls # NIL THEN cert := tls.PeerCertificate() ELSE cert := NIL END;
				IF cert # NIL THEN
					IF FirstName(cert, name) THEN
						a.net.VerifyPeer(cert, name, fp, ok)
					ELSE Log("peer certificate has no DNS SAN", "", "")
					END
				ELSE Log("peer certificate missing", "", "")
				END;
				IF ok THEN
					INC(a.net.hsOk);
					NEW(r); r.net := a.net; r.tls := tls; r.name := name$;
					r.allowed := TRUE; r.next := a.net.readers; a.net.readers := r;
					Tasks.SetStackSize(r, tlsStack); Tasks.Start(r)
				ELSE
					INC(a.net.rejected); INC(a.net.hsFail); INC(a.net.acceptFails);
					IF tls # NIL THEN tls.SafeClose END
				END
			ELSE
				(* Ждём соединения на слушающем дескрипторе: цикл
				   просыпается по событию, а не каждую миллисекунду. *)
				fd := a.net.l.ListenHandle();
				IF fd >= 0 THEN AsyncIO.WaitFdFor(fd, Poller.In, waitMs)
				ELSE Tasks.Sleep(idleMs)
				END
			END
		END
	END Run;

	PROCEDURE Close* (net: Net);
	(** Сеть закрыта: слушатель и соединения закрыты, задачи завершаются;
		другие узлы увидят обрыв (Lost). *)
		VAR p: Link; r: Reader;
	BEGIN
		IF net.closed THEN RETURN END;
		net.closed := TRUE;
		IF net.l # NIL THEN net.l.SafeClose END;
		p := net.peers;
		WHILE p # NIL DO
			p.allowed := FALSE;
			IF p.out # NIL THEN
				IF p.out.tls # NIL THEN p.out.tls.SafeClose END
			END;
			p := p.next
		END;
		r := net.readers;
		WHILE r # NIL DO
			r.allowed := FALSE; r.closed := TRUE;
			IF r.tls # NIL THEN r.tls.SafeClose END;
			r := r.next
		END
	END Close;

	PROCEDURE PeersText* (net: Net; OUT text: ARRAY OF CHAR);
	(* Диагностика: имя, есть ли писатель, состояние TLS и нижнего TCP. *)
		VAR p: Link; d: ARRAY 16 OF CHAR; i: INTEGER;
	BEGIN
		text := ""; p := net.peers;
		WHILE p # NIL DO
			text := text + p.name + ":";
			IF p.out # NIL THEN text := text + "w" ELSE text := text + "-" END;
			IF (p.out # NIL) & (p.out.tls # NIL) THEN
				Strings.IntToString(p.out.tls.State(), d); text := text + d;
				text := text + ":h";
				Strings.IntToString(p.out.tls.HandshakeState(), d); text := text + d
			ELSE text := text + "?"
			END;
			IF (p.out # NIL) & (p.out.tls # NIL) & p.out.tls.ConnAlive() THEN text := text + ":a"
			ELSE text := text + ":-"
			END;
			text := text + " ";
			p := p.next
		END;
		i := LEN(text$) + 1;
		IF i < LEN(text) THEN text[i - 1] := 0X END
	END PeersText;

	PROCEDURE Detail* (net: Net; OUT accepts, acceptFails, hsOk, hsFail, hsStarted, accTicks: LONGINT);
	BEGIN
		accepts := net.accepts; acceptFails := net.acceptFails;
		hsOk := net.hsOk; hsFail := net.hsFail;
		hsStarted := net.hsStarted; accTicks := net.accTicks;
		hsFail := hsFail + net.badState
	END Detail;

	PROCEDURE Counts* (net: Net; OUT links, readers: INTEGER);
	(** Сколько исходящих и принятых соединений сейчас в сети. *)
		VAR p: Link; r: Reader;
	BEGIN
		links := 0; p := net.peers;
		WHILE p # NIL DO IF p.out # NIL THEN INC(links) END; p := p.next END;
		readers := 0; r := net.readers;
		WHILE r # NIL DO IF ~r.closed THEN INC(readers) END; r := r.next END
	END Counts;

	PROCEDURE Stats* (net: Net; OUT sent, read, refused, forged, rejected, revoked: LONGINT);
	(** Счётчики кадров и отказов. *)
	BEGIN
		sent := net.sentFrames; read := net.readFrames; refused := net.refused;
		forged := net.forged; rejected := net.rejected; revoked := net.revoked
	END Stats;

BEGIN
	CryptoAosCommStreams.onWait := Wait;
	(* Канал узлов шлёт мелкие кадры. Буферы расшифрованных данных
	   уменьшены: сборщик мусора консервативно просматривает их слова. *)
	CryptoTLS.AppBufferSize := 64 * 1024
END ActorNetTls.
