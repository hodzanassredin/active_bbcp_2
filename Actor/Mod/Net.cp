MODULE ActorNet;
(** Акторы между узлами: удалённый адрес, кадр, наблюдение, потеря узла.

	Узел — имя корневого актора; акторы узла — его потомки ("B/acct").
	Узлы в одном процессе делят ActorCore, но письма между ними идут
	только через транспорт: кадр кодируется и раскодируется.

	Удалённый адрес (Remote) — узел, имя актора и метка mac = первые
	16 байт хеша (секрет узла-владельца + имя). Метку выдаёт только
	владелец: когда адрес уходит в кадре (отправитель, поле письма) или
	через Export. Кадр с неверной меткой отклоняется (refused). Метка
	вычисляется, а не хранится: она переживает перезапуск узла с тем же
	секретом.

	Кадр: вид, узел-отправитель, воплощение и номер кадра, адрес
	назначения, отправитель письма, тело (письмо через Stores). Получатель
	отбрасывает уже виденный номер (окно window номеров на воплощение
	узла): дубль сети не исполняется дважды. Потерянный кадр не
	повторяется — «не более одного раза».

	Наблюдение: Watch удалённого адреса шлёт кадр watch; узел-владелец
	ставит своего наблюдателя-посредника (Proxy) и при остановке актора
	шлёт кадр term. Потеря узла (Lost от транспорта) даёт Terminated всем
	наблюдателям адресов этого узла. *)

	IMPORT S := SYSTEM, Kernel, Stores, CasHash, ActorCore, ActorCodec, ActorJournal;

	CONST
		kMsg = 0; kWatch = 1; kUnwatch = 2; kTerm = 3; kStop = 4;
		window = 256;	(* окно номеров кадров против дублей *)
		macLen = 16;
		notNetMsg* = 30;	(** трап: удалённому адресу послано письмо не ActorNet.Msg *)

	TYPE
		Name = ActorCore.Name;
		Bytes = ActorJournal.Bytes;
		Mac = ARRAY macLen OF BYTE;

		(** Письмо, которое может уйти на другой узел. Поля пишет Write,
			читает Read (в том же порядке); адреса — WriteRef и ReadRef. *)
		Msg* = POINTER TO ABSTRACT RECORD (ActorCore.Msg) END;

		Node* = POINTER TO LIMITED RECORD
			name-: Name;
			up-: BOOLEAN;
			incarnation-: LONGINT;	(** растёт при каждом Up *)
			sent-, received-, duplicates-, refused-: LONGINT;	(** счётчики кадров *)
			t: Transport;
			secret: ARRAY 8 OF BYTE;
			seq: LONGINT;
			peers: Peer;
			remotes: Remote;
			proxies: Proxy;
			next: Node
		END;

		(** Транспорт кадров между узлами. *)
		Transport* = POINTER TO ABSTRACT RECORD END;

		Peer = POINTER TO RECORD
			name: Name; inc, max: LONGINT;
			seen: ARRAY window OF BOOLEAN;	(* seen[seq MOD window] для seq в (max - window, max] *)
			next: Peer
		END;

		Watcher = POINTER TO RECORD r: ActorCore.Ref; next: Watcher END;

		Remote = POINTER TO RECORD (ActorCore.Ref)
			owner: Node; mac: Mac;
			watchers: Watcher;
			next: Remote
		END;

		(* наблюдатель на узле-владельце за своим актором по просьбе узла peer *)
		Proxy = POINTER TO RECORD (ActorCore.Ref)
			owner: Node; peer: Name; target: ActorCore.Ref; mac: Mac;
			next: Proxy
		END;

		(* тело кадра: письмо с именем типа *)
		Box = POINTER TO RECORD (Stores.Store) m: Msg END;

		(* запись и чтение заголовка *)
		Buf = RECORD b: Bytes; n: INTEGER END;

	VAR
		cur: Node;	(* узел, чей кадр кодируется или раскодируется сейчас *)

	(* --- письмо -------------------------------------------------------------- *)

	PROCEDURE (m: Msg) Write- (VAR wr: Stores.Writer), NEW, EMPTY;
	(** Записать поля письма. *)

	PROCEDURE (m: Msg) Read- (VAR rd: Stores.Reader), NEW, EMPTY;
	(** Прочитать поля в порядке Write. *)

	(* --- байты --------------------------------------------------------------- *)

	PROCEDURE Put (VAR f: Buf; x: LONGINT; k: INTEGER);
		VAR i: INTEGER; nb: Bytes;
	BEGIN
		IF f.b = NIL THEN NEW(f.b, 256) END;
		IF f.n + k > LEN(f.b^) THEN
			NEW(nb, 2 * (f.n + k)); FOR i := 0 TO f.n - 1 DO nb[i] := f.b[i] END; f.b := nb
		END;
		FOR i := 0 TO k - 1 DO f.b[f.n] := SHORT(SHORT(SHORT(x MOD 256))); x := x DIV 256; INC(f.n) END
	END Put;

	PROCEDURE PutBytes (VAR f: Buf; IN x: ARRAY OF BYTE; len: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO len - 1 DO Put(f, x[i], 1) END
	END PutBytes;

	PROCEDURE PutName (VAR f: Buf; IN s: ARRAY OF CHAR);
		VAR i, l: INTEGER;
	BEGIN
		l := LEN(s$); Put(f, l, 2);
		FOR i := 0 TO l - 1 DO Put(f, ORD(s[i]), 2) END
	END PutName;

	PROCEDURE Get (IN b: ARRAY OF BYTE; VAR n: INTEGER; len, k: INTEGER; VAR ok: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: LONGINT;
	BEGIN
		IF ~ok OR (n + k > len) THEN ok := FALSE; RETURN 0 END;
		x := 0;
		FOR i := k - 1 TO 0 BY -1 DO x := x * 256 + b[n + i] MOD 256 END;
		IF (k < 8) & (x >= ASH(LONG(1), 8 * k - 1)) THEN x := x - ASH(LONG(1), 8 * k) END;
		INC(n, k);
		RETURN x
	END Get;

	PROCEDURE GetName (IN b: ARRAY OF BYTE; VAR n: INTEGER; len: INTEGER; VAR ok: BOOLEAN; OUT s: Name);
		VAR i, l: INTEGER;
	BEGIN
		l := SHORT(Get(b, n, len, 2, ok));
		IF ok & ((l < 0) OR (l >= ActorCore.nameLen)) THEN ok := FALSE END;
		i := 0;
		WHILE ok & (i < l) DO s[i] := CHR(Get(b, n, len, 2, ok) MOD 65536); INC(i) END;
		IF ok THEN s[l] := 0X ELSE s := "" END
	END GetName;

	PROCEDURE GetMac (IN b: ARRAY OF BYTE; VAR n: INTEGER; len: INTEGER; VAR ok: BOOLEAN; OUT m: Mac);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO macLen - 1 DO m[i] := SHORT(SHORT(SHORT(Get(b, n, len, 1, ok)))) END
	END GetMac;

	(* --- метка -------------------------------------------------------------- *)

	PROCEDURE (n: Node) Mac (IN name: ARRAY OF CHAR; OUT m: Mac), NEW;
	(* Пост: m — метка имени name от секрета n. *)
		VAR f: Buf; id: CasHash.Id; i: INTEGER;
	BEGIN
		f.n := 0; PutBytes(f, n.secret, LEN(n.secret)); PutName(f, name);
		CasHash.Of(f.b^, 0, f.n, id);
		FOR i := 0 TO macLen - 1 DO m[i] := id[i] END
	END Mac;

	PROCEDURE SameMac (IN a, b: Mac): BOOLEAN;
		VAR i: INTEGER; d: INTEGER;
	BEGIN
		d := 0; FOR i := 0 TO macLen - 1 DO IF a[i] # b[i] THEN INC(d) END END;
		RETURN d = 0
	END SameMac;

	(* --- удалённые адреса ------------------------------------------------------ *)

	PROCEDURE (n: Node) Intern (IN node, name: ARRAY OF CHAR; IN mac: Mac): Remote, NEW;
	(* Пост: единственный Remote узла n для (node, name); метка обновлена. *)
		VAR r: Remote;
	BEGIN
		r := n.remotes;
		WHILE (r # NIL) & ((r.node # node) OR (r.name # name)) DO r := r.next END;
		IF r = NIL THEN
			NEW(r); ActorCore.InitRef(r, node, name); r.owner := n; r.next := n.remotes; n.remotes := r
		END;
		r.mac := mac;
		RETURN r
	END Intern;

	PROCEDURE^ (n: Node) Send (kind: INTEGER; IN to, name: ARRAY OF CHAR; IN mac: Mac;
		sender: ActorCore.Ref; m: Msg; reason: INTEGER), NEW;

	PROCEDURE (r: Remote) Post (m: ActorCore.Msg; sender: ActorCore.Ref);
	BEGIN
		ASSERT(m # NIL, 20);
		IF m IS Msg THEN r.owner.Send(kMsg, r.node, r.name, r.mac, sender, m(Msg), 0)
		ELSIF m IS ActorCore.Terminated THEN	(* Ask через узел: конец адреса ответа не пересылается *)
		ELSE HALT(notNetMsg)
		END
	END Post;

	PROCEDURE Find (w: Watcher; r: ActorCore.Ref): BOOLEAN;
	BEGIN
		WHILE (w # NIL) & (w.r # r) DO w := w.next END;
		RETURN w # NIL
	END Find;

	PROCEDURE (r: Remote) AddWatcher (w: ActorCore.Ref);
		VAR x: Watcher;
	BEGIN
		ASSERT(w # NIL, 20);
		IF ~Find(r.watchers, w) THEN
			NEW(x); x.r := w; x.next := r.watchers;
			IF r.watchers = NIL THEN r.owner.Send(kWatch, r.node, r.name, r.mac, NIL, NIL, 0) END;
			r.watchers := x
		END
	END AddWatcher;

	PROCEDURE (r: Remote) RemoveWatcher (w: ActorCore.Ref);
		VAR p, x: Watcher;
	BEGIN
		ASSERT(w # NIL, 20);
		p := NIL; x := r.watchers;
		WHILE (x # NIL) & (x.r # w) DO p := x; x := x.next END;
		IF x # NIL THEN
			IF p = NIL THEN r.watchers := x.next ELSE p.next := x.next END;
			IF r.watchers = NIL THEN r.owner.Send(kUnwatch, r.node, r.name, r.mac, NIL, NIL, 0) END
		END
	END RemoveWatcher;

	PROCEDURE (r: Remote) Stop;
	BEGIN
		r.owner.Send(kStop, r.node, r.name, r.mac, NIL, NIL, 0)
	END Stop;

	PROCEDURE (r: Remote) Ended (reason: INTEGER), NEW;
	(* Пост: каждый наблюдатель r получил Terminated с who = r и reason; наблюдателей нет. *)
		VAR w: Watcher;
	BEGIN
		w := r.watchers; r.watchers := NIL;
		WHILE w # NIL DO ActorCore.Terminate(w.r, r, reason); w := w.next END
	END Ended;

	(* --- посредник наблюдения ------------------------------------------------- *)

	PROCEDURE (p: Proxy) Post (m: ActorCore.Msg; sender: ActorCore.Ref);
	BEGIN
		IF (m IS ActorCore.Terminated) & (m(ActorCore.Terminated).who = p.target) THEN
			p.owner.Send(kTerm, p.peer, p.target.name, p.mac, NIL, NIL, m(ActorCore.Terminated).reason)
		END
	END Post;

	PROCEDURE (p: Proxy) AddWatcher (w: ActorCore.Ref);
	END AddWatcher;

	PROCEDURE (p: Proxy) RemoveWatcher (w: ActorCore.Ref);
	END RemoveWatcher;

	PROCEDURE (p: Proxy) Stop;
	END Stop;

	(* --- тело кадра ---------------------------------------------------------- *)

	PROCEDURE WriteMsg* (VAR wr: Stores.Writer; m: Msg);
	(** Письмо m (может быть NIL) — полем другого письма или тела кадра:
		имя модуля и типа, затем m.Write. Удобно последним полем. *)
		VAR t: Kernel.Type; mod, typ: Kernel.Name;
	BEGIN
		IF m = NIL THEN wr.WriteBool(FALSE)
		ELSE
			wr.WriteBool(TRUE);
			t := Kernel.TypeOf(m^); Kernel.GetModName(t.mod, mod); Kernel.GetTypeName(t, typ);
			wr.WriteString(mod); wr.WriteString(typ);
			m.Write(wr)
		END
	END WriteMsg;

	PROCEDURE ReadMsg* (VAR rd: Stores.Reader; OUT m: Msg);
	(** Письмо, записанное WriteMsg. Тип ищется только среди загруженных
		модулей; не найден — m = NIL и rd.cancelled (дальше читать нельзя). *)
		VAR mod, typ: Kernel.Name; md: Kernel.Module; t: Kernel.Type; p: S.PTR; a: ANYPTR; has: BOOLEAN;
	BEGIN
		m := NIL; rd.ReadBool(has);
		IF has & ~rd.cancelled THEN
			rd.ReadString(mod); rd.ReadString(typ);
			md := Kernel.ThisLoadedMod(mod); t := NIL;	(* кадр не загружает модулей *)
			IF md # NIL THEN t := Kernel.ThisType(md, typ) END;
			a := NIL;
			IF t # NIL THEN Kernel.NewObj(p, t); a := S.VAL(ANYPTR, p) END;
			IF (a # NIL) & (a IS Msg) THEN m := a(Msg); m.Read(rd)
			ELSE rd.TurnIntoAlien(Stores.alienComponent)
			END
		END
	END ReadMsg;

	PROCEDURE (b: Box) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		b.Externalize^(wr); wr.WriteVersion(0); WriteMsg(wr, b.m)
	END Externalize;

	PROCEDURE (b: Box) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		b.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		ReadMsg(rd, b.m)	(* тип не найден: b.m = NIL, кадр отклонит Deliver *)
	END Internalize;

	(* --- адрес в письме ------------------------------------------------------- *)

	PROCEDURE WriteRef* (VAR wr: Stores.Writer; r: ActorCore.Ref);
	(** Из Msg.Write: записать адрес r. Локальный адрес уходит пропуском
		узла, который кодирует кадр; удалённый — как есть. *)
		VAR mac: Mac; i: INTEGER;
	BEGIN
		ASSERT(cur # NIL, 20);
		IF r = NIL THEN wr.WriteBool(FALSE)
		ELSE
			wr.WriteBool(TRUE);
			IF r IS Remote THEN wr.WriteString(r.node); mac := r(Remote).mac
			ELSE wr.WriteString(cur.name); cur.Mac(r.name, mac)
			END;
			wr.WriteString(r.name);
			FOR i := 0 TO macLen - 1 DO wr.WriteByte(mac[i]) END
		END
	END WriteRef;

	PROCEDURE^ (n: Node) Resolve (IN node, name: ARRAY OF CHAR; IN mac: Mac): ActorCore.Ref, NEW;

	PROCEDURE ReadRef* (VAR rd: Stores.Reader; OUT r: ActorCore.Ref);
	(** Из Msg.Read: прочитать адрес, записанный WriteRef. Свой адрес с
		неверной меткой читается как NIL. *)
		VAR has: BOOLEAN; node, name: Name; mac: Mac; i: INTEGER;
	BEGIN
		ASSERT(cur # NIL, 20);
		r := NIL; rd.ReadBool(has);
		IF has THEN
			rd.ReadString(node); rd.ReadString(name);
			FOR i := 0 TO macLen - 1 DO rd.ReadByte(mac[i]) END;
			IF ~rd.cancelled THEN r := cur.Resolve(node, name, mac) END
		END
	END ReadRef;

	(* --- узел ------------------------------------------------------------------ *)

	PROCEDURE (t: Transport) Send* (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER), NEW, ABSTRACT;
	(** Отправить кадр frame[0 .. len-1] узлу to. Не ждёт; кадр может
		пропасть. Доставка — Deliver на узле to. *)

	PROCEDURE (n: Node) Resolve (IN node, name: ARRAY OF CHAR; IN mac: Mac): ActorCore.Ref, NEW;
	(* Пост: свой адрес с верной меткой — локальный актор (NIL, если его нет);
	   свой с неверной — NIL и refused; чужой — Remote узла n. *)
		VAR m: Mac;
	BEGIN
		IF node = n.name THEN
			n.Mac(name, m);
			IF ~SameMac(m, mac) THEN INC(n.refused); RETURN NIL END;
			RETURN ActorCore.dir.Lookup(ActorCore.node, name)
		ELSE RETURN n.Intern(node, name, mac)
		END
	END Resolve;

	PROCEDURE (n: Node) Send (kind: INTEGER; IN to, name: ARRAY OF CHAR; IN mac: Mac;
		sender: ActorCore.Ref; m: Msg; reason: INTEGER), NEW;
		VAR f: Buf; i, len: INTEGER; b: Box; data: Bytes; smac: Mac; save: Node;
	BEGIN
		IF ~n.up THEN RETURN END;
		INC(n.seq); f.n := 0;
		Put(f, kind, 1); PutName(f, n.name); Put(f, n.incarnation, 8); Put(f, n.seq, 8);
		PutName(f, name); PutBytes(f, mac, macLen); Put(f, reason, 1);
		save := cur; cur := n;
		IF sender = NIL THEN Put(f, 0, 1)
		ELSE
			Put(f, 1, 1);
			IF sender IS Remote THEN PutName(f, sender.node); smac := sender(Remote).mac
			ELSE PutName(f, n.name); n.Mac(sender.name, smac)
			END;
			PutName(f, sender.name); PutBytes(f, smac, macLen)
		END;
		IF m # NIL THEN
			NEW(b); b.m := m; ActorCodec.dir.Encode(b, data, len);
			Put(f, len, 4); PutBytes(f, data, len)
		ELSE Put(f, 0, 4)
		END;
		cur := save;
		INC(n.sent);
		n.t.Send(n.name, to, f.b^, f.n)
	END Send;

	PROCEDURE (n: Node) Fresh (IN from: ARRAY OF CHAR; inc, seq: LONGINT): BOOLEAN, NEW;
	(* Пост: кадр (from, inc, seq) не виден раньше; теперь виден. *)
		VAR p: Peer; i: LONGINT;
	BEGIN
		p := n.peers;
		WHILE (p # NIL) & (p.name # from) DO p := p.next END;
		IF p = NIL THEN NEW(p); p.name := from$; p.inc := -1; p.next := n.peers; n.peers := p END;
		IF inc > p.inc THEN	(* новое воплощение отправителя: окно заново *)
			p.inc := inc; p.max := 0; FOR i := 0 TO window - 1 DO p.seen[i] := FALSE END
		ELSIF inc < p.inc THEN RETURN FALSE
		END;
		IF seq > p.max THEN
			i := p.max + 1;
			WHILE (i < seq) & (i <= p.max + window) DO p.seen[i MOD window] := FALSE; INC(i) END;
			p.max := seq; p.seen[seq MOD window] := TRUE; RETURN TRUE
		ELSIF (seq <= p.max - window) OR p.seen[seq MOD window] THEN RETURN FALSE
		ELSE p.seen[seq MOD window] := TRUE; RETURN TRUE
		END
	END Fresh;

	PROCEDURE (n: Node) Watch (IN peer: ARRAY OF CHAR; target: ActorCore.Ref; IN mac: Mac), NEW;
		VAR p: Proxy;
	BEGIN
		p := n.proxies;
		WHILE (p # NIL) & ((p.peer # peer) OR (p.target # target)) DO p := p.next END;
		IF p = NIL THEN
			NEW(p); ActorCore.InitRef(p, n.name, "$watch"); p.owner := n; p.peer := peer$; p.target := target;
			p.mac := mac; p.next := n.proxies; n.proxies := p
		END;
		target.AddWatcher(p)
	END Watch;

	PROCEDURE (n: Node) Unwatch (IN peer: ARRAY OF CHAR; target: ActorCore.Ref), NEW;
		VAR p, q: Proxy;
	BEGIN
		q := NIL; p := n.proxies;
		WHILE (p # NIL) & ((p.peer # peer) OR (p.target # target)) DO q := p; p := p.next END;
		IF p # NIL THEN
			target.RemoveWatcher(p);
			IF q = NIL THEN n.proxies := p.next ELSE q.next := p.next END
		END
	END Unwatch;

	PROCEDURE Deliver* (n: Node; IN frame: ARRAY OF BYTE; len: INTEGER);
	(** Для транспорта: кадр пришёл узлу n. Повтор, чужая метка и битый
		кадр отбрасываются и считаются; иначе письмо в ящике адресата. *)
		VAR pos, kind, blen, reason: INTEGER; ok, has: BOOLEAN; from, name, snode, sname: Name;
			inc, seq: LONGINT; mac, smac: Mac; to, sender: ActorCore.Ref; s: Stores.Store;
			body: Bytes; i, res: INTEGER; save: Node; r: Remote; mine: Mac;
	BEGIN
		ASSERT(n # NIL, 20);
		IF ~n.up THEN RETURN END;
		pos := 0; ok := TRUE;
		kind := SHORT(Get(frame, pos, len, 1, ok));
		GetName(frame, pos, len, ok, from);
		inc := Get(frame, pos, len, 8, ok); seq := Get(frame, pos, len, 8, ok);
		GetName(frame, pos, len, ok, name); GetMac(frame, pos, len, ok, mac);
		reason := SHORT(Get(frame, pos, len, 1, ok));
		IF (reason < ActorCore.byStop) OR (reason > ActorCore.byNodeLoss) THEN ok := FALSE END;
		has := Get(frame, pos, len, 1, ok) = 1;
		IF has THEN GetName(frame, pos, len, ok, snode); GetName(frame, pos, len, ok, sname); GetMac(frame, pos, len, ok, smac) END;
		blen := SHORT(Get(frame, pos, len, 4, ok));
		IF ~ok OR (blen < 0) OR (pos + blen > len) THEN INC(n.refused); RETURN END;
		(* метка проверяется до окна повторов: окно двигает только держатель пропуска *)
		IF kind = kTerm THEN	(* адрес на узле from кончился; метка — та, что выдал владелец *)
			r := n.remotes;
			WHILE (r # NIL) & ((r.node # from) OR (r.name # name)) DO r := r.next END;
			IF (r = NIL) OR ~SameMac(r.mac, mac) THEN INC(n.refused); RETURN END;
			IF ~n.Fresh(from, inc, seq) THEN INC(n.duplicates); RETURN END;
			INC(n.received); r.Ended(reason);
			RETURN
		END;
		n.Mac(name, mine);
		IF ~SameMac(mine, mac) THEN INC(n.refused); RETURN END;
		IF ~n.Fresh(from, inc, seq) THEN INC(n.duplicates); RETURN END;
		INC(n.received);
		to := ActorCore.dir.Lookup(ActorCore.node, name);
		IF kind = kWatch THEN
			IF to # NIL THEN n.Watch(from, to, mac) ELSE n.Send(kTerm, from, name, mac, NIL, NIL, ActorCore.byStop) END
		ELSIF kind = kUnwatch THEN
			IF to # NIL THEN n.Unwatch(from, to) END
		ELSIF kind = kStop THEN
			IF to # NIL THEN ActorCore.StopActor(to) END
		ELSIF to # NIL THEN
			save := cur; cur := n;
			sender := NIL;
			IF has THEN sender := n.Resolve(snode, sname, smac) END;
			IF blen > 0 THEN
				NEW(body, blen); FOR i := 0 TO blen - 1 DO body[i] := frame[pos + i] END;
				ActorCodec.dir.Decode(body, blen, s, res)
			ELSE res := ActorCodec.unknown
			END;
			cur := save;
			IF (res = ActorCodec.ok) & (s IS Box) & (s(Box).m # NIL) THEN to.Post(s(Box).m, sender)
			ELSE INC(n.refused)
			END
		END
	END Deliver;

	PROCEDURE Lost* (n: Node; IN node: ARRAY OF CHAR);
	(** Для транспорта: связь узла n с узлом node потеряна. Пост: каждый
		наблюдатель адреса на node получил Terminated; посредники
		наблюдения по просьбам node сняты. *)
		VAR r: Remote; p: Proxy;
	BEGIN
		ASSERT(n # NIL, 20);
		r := n.remotes;
		WHILE r # NIL DO
			IF r.node = node THEN r.Ended(ActorCore.byNodeLoss) END;
			r := r.next
		END;
		p := n.proxies;
		WHILE p # NIL DO
			IF p.peer = node THEN n.Unwatch(node, p.target) END;
			p := p.next
		END
	END Lost;

	PROCEDURE New* (IN name: ARRAY OF CHAR; t: Transport; secret: LONGINT): Node;
	(** Узел name на транспорте t. Пред: name # "" и без "/" — 20; t # NIL — 21.
		Пост: узел поднят (up), воплощение 1. Метки адресов узла зависят от
		secret: узел, перезапущенный с тем же secret, принимает прежние
		адреса. Корневой актор узла создаёт пользователь: Spawn(NIL, …, name). *)
		VAR n: Node; i: INTEGER;
	BEGIN
		ASSERT((name # "") & (LEN(name$) < ActorCore.nameLen), 20); ASSERT(t # NIL, 21);
		i := 0; WHILE name[i] # 0X DO ASSERT(name[i] # "/", 20); INC(i) END;
		NEW(n); n.name := name$; n.t := t; n.up := TRUE; n.incarnation := 1;
		FOR i := 0 TO 7 DO n.secret[i] := SHORT(SHORT(SHORT(secret MOD 256))); secret := secret DIV 256 END;
		RETURN n
	END New;

	PROCEDURE Down* (n: Node);
	(** Падение узла: кадры от него и к нему пропадают, память узла о
		кадрах и наблюдениях стёрта, корневой актор остановлен. Журналы в
		хранилище не затронуты. *)
		VAR root: ActorCore.Ref;
	BEGIN
		n.up := FALSE; n.peers := NIL; n.proxies := NIL; n.remotes := NIL;
		root := ActorCore.dir.Lookup(ActorCore.node, n.name);
		IF root # NIL THEN ActorCore.StopActor(root) END
	END Down;

	PROCEDURE Up* (n: Node);
	(** Пред: узел упал (Down), его корневой актор остановлен — 20.
		Пост: узел поднят с новым воплощением; корневой актор создаёт
		пользователь заново. *)
	BEGIN
		ASSERT(~n.up & (ActorCore.dir.Lookup(ActorCore.node, n.name) = NIL), 20);
		n.up := TRUE; INC(n.incarnation); n.seq := 0
	END Up;

	PROCEDURE Export* (n: Node; r: ActorCore.Ref; OUT cap: ARRAY OF CHAR);
	(** Пропуск к локальному адресу r для передачи другому узлу вне писем.
		Пред: r — локальный — 20. Пост: cap — "узел|имя|метка". *)
		VAR mac: Mac; i: INTEGER; hex: ARRAY 2 * macLen + 1 OF CHAR; d: ARRAY 17 OF CHAR;
	BEGIN
		ASSERT(~(r IS Remote), 20);
		n.Mac(r.name, mac); d := "0123456789abcdef";
		FOR i := 0 TO macLen - 1 DO hex[2 * i] := d[mac[i] MOD 256 DIV 16]; hex[2 * i + 1] := d[mac[i] MOD 16] END;
		hex[2 * macLen] := 0X;
		cap := n.name + "|" + r.name + "|" + hex
	END Export;

	PROCEDURE Import* (n: Node; IN cap: ARRAY OF CHAR): ActorCore.Ref;
	(** Адрес по пропуску Export для писем с узла n. Пост: NIL — пропуск
		не разобран. Верность метки проверит узел-владелец. *)
		VAR i, k, j: INTEGER; node, name: Name; mac: Mac; c: CHAR; v: INTEGER; x: ARRAY macLen OF INTEGER;
	BEGIN
		i := 0; k := 0;
		WHILE (i < LEN(cap)) & (cap[i] # 0X) & (cap[i] # "|") DO
			IF k >= ActorCore.nameLen - 1 THEN RETURN NIL END;
			node[k] := cap[i]; INC(i); INC(k)
		END;
		node[k] := 0X; IF (i >= LEN(cap)) OR (cap[i] # "|") OR (k = 0) THEN RETURN NIL END; INC(i); k := 0;
		WHILE (i < LEN(cap)) & (cap[i] # 0X) & (cap[i] # "|") DO
			IF k >= ActorCore.nameLen - 1 THEN RETURN NIL END;
			name[k] := cap[i]; INC(i); INC(k)
		END;
		name[k] := 0X; IF (i >= LEN(cap)) OR (cap[i] # "|") OR (k = 0) THEN RETURN NIL END; INC(i);
		IF i + 2 * macLen > LEN(cap) THEN RETURN NIL END;
		IF (i + 2 * macLen < LEN(cap)) & (cap[i + 2 * macLen] # 0X) THEN RETURN NIL END;	(* хвост после метки *)
		FOR j := 0 TO 2 * macLen - 1 DO
			c := cap[i + j];
			IF (c >= "0") & (c <= "9") THEN v := ORD(c) - ORD("0")
			ELSIF (c >= "a") & (c <= "f") THEN v := ORD(c) - ORD("a") + 10
			ELSE RETURN NIL
			END;
			IF ODD(j) THEN x[j DIV 2] := x[j DIV 2] + v ELSE x[j DIV 2] := 16 * v END
		END;
		FOR j := 0 TO macLen - 1 DO mac[j] := SHORT(SHORT(x[j] - 256 * (x[j] DIV 128))) END;
		RETURN n.Intern(node, name, mac)
	END Import;

END ActorNet.
