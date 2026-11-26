MODULE RaftSm;
(** Автомат состояний над журналом Raft и сеансы клиентов.

	Каждый узел применяет зафиксированные записи журнала к своей копии
	автомата по порядку индексов; одинаковые команды в одном порядке дают
	одинаковое состояние на всех узлах.

	Клиент, не дождавшись ответа (лидер упал, письмо потерялось),
	повторяет команду — возможно, у другого лидера. Первая попытка могла
	уже попасть в журнал, и без защиты команда применилась бы дважды.
	Защита — сеанс (§6.3 диссертации Raft): команда несёт номер клиента и
	номер операции, Sessions помнит для каждого клиента последний
	применённый номер и его результат и на повтор отдаёт результат, не
	трогая автомат. Клиент шлёт операции по одной (следующую — после
	ответа на предыдущую), поэтому достаточно одного номера на клиента.

	Сеанс помнит и срок записи, в которой клиент последний раз работал
	(§6.3: срок последней активности — по журналу). Перед применением
	очередной записи сеансы, молчавшие дольше expiry сроков, вытесняются:
	так таблица сеансов не растёт вечно. Вытеснение — компромисс: клиент,
	вернувшийся после очень долгой паузы, может повторить операцию,
	которую кластер уже забыл, и она применится второй раз. По умолчанию
	expiry = 0 — не вытеснять; владелец узла задаёт срок (RaftNode.Config).

	Снимок узла — снимок сеансов вместе со снимком автомата: без таблицы
	сеансов повтор, пришедший после установки снимка, применился бы
	заново. Снимок хранит и срок последней операции клиента. *)

	IMPORT Core := RaftCore;

	CONST
		head = 8;	(* номер клиента и номер операции, по 4 байта *)
		snapVersion = 100000002;	(* формат снимка с сроком сеанса; меньше — старый *)

	TYPE
		Bytes* = Core.Bytes;

		StateMachine* = POINTER TO ABSTRACT RECORD END;

		Session = POINTER TO RECORD
			client, seq, term: INTEGER;
			res: Bytes;
			next: Session
		END;

		Sessions* = POINTER TO RECORD
			sm-: StateMachine;
			list: Session;
			dups-: INTEGER;	(** повторов, отвеченных из памяти сеанса *)
			expiry-: INTEGER	(** сроков без работы до вытеснения; 0 — не вытеснять *)
		END;

	PROCEDURE (sm: StateMachine) Apply* (index: INTEGER; IN cmd: ARRAY OF BYTE; beg, len: INTEGER): Bytes,
		NEW, ABSTRACT;
	(** Применить команду cmd[beg .. beg+len-1] записи index. Пост: результат (может быть NIL). *)

	PROCEDURE (sm: StateMachine) Snapshot* (): Bytes, NEW, ABSTRACT;
	(** Пост: состояние целиком; Restore(Snapshot()) его восстанавливает. *)

	PROCEDURE (sm: StateMachine) Restore* (b: Bytes), NEW, ABSTRACT;

	PROCEDURE (sm: StateMachine) Query* (IN q: ARRAY OF BYTE; len: INTEGER): Bytes, NEW, EXTENSIBLE;
	(** Чтение без изменения: ответ на запрос q[0 .. len-1]. Его зовёт водитель
		узла (RaftNode) после подтверждения ReadIndex. По умолчанию — снимок. *)
	BEGIN
		RETURN sm.Snapshot()
	END Query;

	PROCEDURE PutInt (VAR b: ARRAY OF BYTE; i, x: INTEGER);
	BEGIN
		b[i] := SHORT(SHORT(x MOD 256)); b[i + 1] := SHORT(SHORT(x DIV 256 MOD 256));
		b[i + 2] := SHORT(SHORT(x DIV 10000H MOD 256)); b[i + 3] := SHORT(SHORT(ASH(x, -24) MOD 256))
	END PutInt;

	PROCEDURE Int (IN b: ARRAY OF BYTE; i: INTEGER): INTEGER;
	BEGIN
		RETURN b[i] MOD 256 + b[i + 1] MOD 256 * 100H + b[i + 2] MOD 256 * 10000H + ASH(LONG(b[i + 3]), 24)
	END Int;

	PROCEDURE Encode* (client, seq: INTEGER; IN body: ARRAY OF BYTE; len: INTEGER): Bytes;
	(** Пред: client > 0 — 20; seq > 0 — 21. Пост: команда журнала с сеансом. *)
		VAR b: Bytes; k: INTEGER;
	BEGIN
		ASSERT(client > 0, 20); ASSERT(seq > 0, 21);
		NEW(b, head + len); PutInt(b, 0, client); PutInt(b, 4, seq);
		FOR k := 0 TO len - 1 DO b[head + k] := body[k] END;
		RETURN b
	END Encode;

	PROCEDURE Decode* (b: Bytes; OUT client, seq: INTEGER);
	(** Пост: client = 0 — пустая запись или команда без сеанса. *)
	BEGIN
		IF (b = NIL) OR (LEN(b^) < head) THEN client := 0; seq := 0
		ELSE client := Int(b, 0); seq := Int(b, 4)
		END
	END Decode;

	PROCEDURE New* (sm: StateMachine): Sessions;
	(** Пред: sm # NIL — 20. *)
		VAR s: Sessions;
	BEGIN
		ASSERT(sm # NIL, 20);
		NEW(s); s.sm := sm; RETURN s
	END New;

	PROCEDURE (s: Sessions) SetExpiry* (terms: INTEGER), NEW;
	(** Пред: terms >= 0. Пост: сеанс, молчавший больше terms сроков,
		вытесняется при применении следующей записи; 0 — не вытеснять. *)
	BEGIN
		ASSERT(terms >= 0, 20);
		s.expiry := terms
	END SetExpiry;

	PROCEDURE (s: Sessions) Count* (): INTEGER, NEW;
	(** Пост: число живых сеансов (для проверок). *)
		VAR p: Session; k: INTEGER;
	BEGIN
		k := 0; p := s.list; WHILE p # NIL DO INC(k); p := p.next END;
		RETURN k
	END Count;

	PROCEDURE (s: Sessions) Has* (client: INTEGER): BOOLEAN, NEW;
	(** Пост: сеанс клиента есть в таблице (для проверок). *)
		VAR p: Session;
	BEGIN
		p := s.list; WHILE (p # NIL) & (p.client # client) DO p := p.next END;
		RETURN p # NIL
	END Has;

	PROCEDURE Expire (s: Sessions; term: INTEGER);
	(* вытеснить сеансы, последняя запись которых старше expiry сроков *)
		VAR p, prev, nx: Session; old: BOOLEAN;
	BEGIN
		IF (s.expiry <= 0) OR (Core.mutNoSessionExpire IN Core.mutations) THEN RETURN END;
		prev := NIL; p := s.list;
		WHILE p # NIL DO
			nx := p.next;
			old := p.term + s.expiry < term;
			IF Core.mutExpireCurrent IN Core.mutations THEN old := p.term <= term END;	(* порча *)
			IF old THEN
				IF prev = NIL THEN s.list := nx ELSE prev.next := nx END
			ELSE prev := p
			END;
			p := nx
		END
	END Expire;

	PROCEDURE (s: Sessions) Apply* (index, term: INTEGER; data: Bytes; OUT res: Bytes; OUT dup: BOOLEAN), NEW;
	(** Применить запись журнала index срока term. Пост: dup — повтор уже
		применённой операции, res — её прежний результат; пустая запись и
		запись состава не меняют ничего. Перед применением вытесняются
		сеансы, молчавшие дольше expiry сроков. *)
		VAR c, q: INTEGER; p: Session;
	BEGIN
		res := NIL; dup := FALSE;
		Expire(s, term);
		Decode(data, c, q);
		IF c # 0 THEN
			p := s.list; WHILE (p # NIL) & (p.client # c) DO p := p.next END;
			IF p = NIL THEN
				NEW(p); p.client := c; p.next := s.list; s.list := p;
				IF Core.mutSessionTermStale IN Core.mutations THEN p.term := 0 ELSE p.term := term END	(* порча *)
			END;
			IF (q <= p.seq) & ~(Core.mutNoDedup IN Core.mutations) THEN
				dup := TRUE; INC(s.dups);
				IF q = p.seq THEN res := p.res END	(* старше последней — клиент ответа уже не ждёт *)
			ELSE
				res := s.sm.Apply(index, data, head, LEN(data^) - head);
				p.seq := q; p.res := res
			END;
			IF ~(Core.mutSessionTermStale IN Core.mutations) THEN p.term := term END	(* срок последней работы *)
		END
	END Apply;

	PROCEDURE (s: Sessions) Snapshot* (): Bytes, NEW;
	(** Пост: сеансы (с сроком последней работы) и снимок автомата одним
		массивом байтов. *)
		VAR p: Session; n, size, k, i, len: INTEGER; b, inner: Bytes;
	BEGIN
		inner := s.sm.Snapshot(); n := 0; size := 12;	(* версия, n, длина снимка автомата *)
		p := s.list;
		WHILE p # NIL DO
			INC(n); INC(size, 16); IF p.res # NIL THEN INC(size, LEN(p.res^)) END; p := p.next
		END;
		IF inner # NIL THEN INC(size, LEN(inner^)) END;
		NEW(b, size); PutInt(b, 0, snapVersion); PutInt(b, 4, n); k := 8; p := s.list;
		WHILE p # NIL DO
			PutInt(b, k, p.client); PutInt(b, k + 4, p.seq); PutInt(b, k + 8, p.term);
			IF p.res = NIL THEN len := -1 ELSE len := LEN(p.res^) END;
			PutInt(b, k + 12, len); INC(k, 16);
			FOR i := 0 TO len - 1 DO b[k + i] := p.res[i] END;
			IF len > 0 THEN INC(k, len) END;
			p := p.next
		END;
		IF inner = NIL THEN len := -1 ELSE len := LEN(inner^) END;
		PutInt(b, k, len); INC(k, 4);
		FOR i := 0 TO len - 1 DO b[k + i] := inner[i] END;
		RETURN b
	END Snapshot;

	PROCEDURE (s: Sessions) Restore* (b: Bytes), NEW;
	(** Пред: b — результат Snapshot или NIL (пустое состояние); снимок
		старого формата (без срока) тоже принимается. *)
		VAR p: Session; n, k, i, len: INTEGER; inner: Bytes; old: BOOLEAN;
	BEGIN
		s.list := NIL;
		IF b = NIL THEN s.sm.Restore(NIL); RETURN END;
		old := Int(b, 0) # snapVersion;
		IF old THEN n := Int(b, 0); k := 4
		ELSE n := Int(b, 4); k := 8
		END;
		WHILE n > 0 DO
			NEW(p); p.client := Int(b, k); p.seq := Int(b, k + 4);
			IF old THEN p.term := 0; len := Int(b, k + 8); INC(k, 12)
			ELSE p.term := Int(b, k + 8); len := Int(b, k + 12); INC(k, 16)
			END;
			IF len >= 0 THEN NEW(p.res, len); FOR i := 0 TO len - 1 DO p.res[i] := b[k + i] END; INC(k, len) END;
			p.next := s.list; s.list := p; DEC(n)
		END;
		len := Int(b, k); INC(k, 4); inner := NIL;
		IF len >= 0 THEN NEW(inner, len); FOR i := 0 TO len - 1 DO inner[i] := b[k + i] END END;
		s.sm.Restore(inner)
	END Restore;

END RaftSm.
