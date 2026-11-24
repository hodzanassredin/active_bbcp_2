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

	Снимок узла — снимок сеансов вместе со снимком автомата: без таблицы
	сеансов повтор, пришедший после установки снимка, применился бы
	заново.

	Сеансы живут вечно: вытеснения старых клиентов нет (граница этапа). *)

	IMPORT Core := RaftCore;

	CONST
		head = 8;	(* номер клиента и номер операции, по 4 байта *)

	TYPE
		Bytes* = Core.Bytes;

		StateMachine* = POINTER TO ABSTRACT RECORD END;

		Session = POINTER TO RECORD
			client, seq: INTEGER;
			res: Bytes;
			next: Session
		END;

		Sessions* = POINTER TO RECORD
			sm-: StateMachine;
			list: Session;
			dups-: INTEGER	(** повторов, отвеченных из памяти сеанса *)
		END;

	PROCEDURE (sm: StateMachine) Apply* (index: INTEGER; IN cmd: ARRAY OF BYTE; beg, len: INTEGER): Bytes,
		NEW, ABSTRACT;
	(** Применить команду cmd[beg .. beg+len-1] записи index. Пост: результат (может быть NIL). *)

	PROCEDURE (sm: StateMachine) Snapshot* (): Bytes, NEW, ABSTRACT;
	(** Пост: состояние целиком; Restore(Snapshot()) его восстанавливает. *)

	PROCEDURE (sm: StateMachine) Restore* (b: Bytes), NEW, ABSTRACT;

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

	PROCEDURE (s: Sessions) Apply* (index: INTEGER; data: Bytes; OUT res: Bytes; OUT dup: BOOLEAN), NEW;
	(** Применить запись журнала index. Пост: dup — повтор уже применённой
		операции, res — её прежний результат; пустая запись не меняет ничего. *)
		VAR c, q: INTEGER; p: Session;
	BEGIN
		res := NIL; dup := FALSE;
		Decode(data, c, q);
		IF c # 0 THEN
			p := s.list; WHILE (p # NIL) & (p.client # c) DO p := p.next END;
			IF p = NIL THEN NEW(p); p.client := c; p.next := s.list; s.list := p END;
			IF (q <= p.seq) & ~(Core.mutNoDedup IN Core.mutations) THEN
				dup := TRUE; INC(s.dups);
				IF q = p.seq THEN res := p.res END	(* старше последней — клиент ответа уже не ждёт *)
			ELSE
				res := s.sm.Apply(index, data, head, LEN(data^) - head);
				p.seq := q; p.res := res
			END
		END
	END Apply;

	PROCEDURE (s: Sessions) Snapshot* (): Bytes, NEW;
	(** Пост: сеансы и снимок автомата одним массивом байтов. *)
		VAR p: Session; n, size, k, i, len: INTEGER; b, inner: Bytes;
	BEGIN
		inner := s.sm.Snapshot(); n := 0; size := 8;
		p := s.list;
		WHILE p # NIL DO
			INC(n); INC(size, 12); IF p.res # NIL THEN INC(size, LEN(p.res^)) END; p := p.next
		END;
		IF inner # NIL THEN INC(size, LEN(inner^)) END;
		NEW(b, size); PutInt(b, 0, n); k := 4; p := s.list;
		WHILE p # NIL DO
			PutInt(b, k, p.client); PutInt(b, k + 4, p.seq);
			IF p.res = NIL THEN len := -1 ELSE len := LEN(p.res^) END;
			PutInt(b, k + 8, len); INC(k, 12);
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
	(** Пред: b — результат Snapshot или NIL (пустое состояние). *)
		VAR p: Session; n, k, i, len: INTEGER; inner: Bytes;
	BEGIN
		s.list := NIL;
		IF b = NIL THEN s.sm.Restore(NIL); RETURN END;
		n := Int(b, 0); k := 4;
		WHILE n > 0 DO
			NEW(p); p.client := Int(b, k); p.seq := Int(b, k + 4); len := Int(b, k + 8); INC(k, 12);
			IF len >= 0 THEN NEW(p.res, len); FOR i := 0 TO len - 1 DO p.res[i] := b[k + i] END; INC(k, len) END;
			p.next := s.list; s.list := p; DEC(n)
		END;
		len := Int(b, k); INC(k, 4); inner := NIL;
		IF len >= 0 THEN NEW(inner, len); FOR i := 0 TO len - 1 DO inner[i] := b[k + i] END END;
		s.sm.Restore(inner)
	END Restore;

END RaftSm.
