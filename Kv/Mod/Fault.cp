MODULE KvFault;
(** Хранилище в памяти со сбоями — для тестов Kv.

	Хранилище держит два слоя страниц: носитель (то, что пережило Sync) и
	кеш устройства (записанное после последнего Sync). Чтение видит кеш
	поверх носителя. PowerOff выключает питание: кеш пропадает целиком или,
	при ненулевом зерне, каждая несинхронизированная страница доходит до
	носителя или нет по жребию — так переставляет записи настоящий диск.

	Сбой задают заранее: Arm(k, keep) — событие с номером k (счёт с нуля;
	событие — каждая Write и каждая Sync) не исполняется, и устройство
	умирает: дальнейшие Write и Sync возвращают ioError. При keep >= 0
	прерванная запись страницы успевает записать первые keep байтов, а
	остальные остаются прежними.
	Случайность — только от зерна, поэтому каждый провал воспроизводится. *)

	IMPORT KvPages, KvStore;

	CONST
		size = KvPages.size;

	TYPE
		Page = POINTER TO ARRAY size OF BYTE;
		Pages = POINTER TO ARRAY OF Page;

		Store* = POINTER TO RECORD (KvStore.Store)
			disk, cache: Pages;	(* cache[i] # NIL — запись после Sync *)
			n: INTEGER;	(* число страниц, видимых чтению *)
			dn: INTEGER;	(* число страниц на носителе *)
			events-: INTEGER;	(** событий (Write и Sync) с последнего Arm *)
			armAt: INTEGER;	(* номер события сбоя или -1 *)
			keep: INTEGER;	(* байтов прерванной записи, которые доходят; -1 — ни одного *)
			dead-: BOOLEAN;	(** сбой произошёл, устройство не отвечает *)
			writes-, syncs-: INTEGER	(** счётчики за всю жизнь *)
		END;

	VAR
		seed: INTEGER;

	PROCEDURE Rand (n: INTEGER): INTEGER;
	BEGIN
		seed := SHORT((LONG(seed) * 1103515245 + 12345) MOD 40000000H);
		RETURN seed DIV 65536 MOD n
	END Rand;

	PROCEDURE Grow (VAR p: Pages; n: INTEGER);
		VAR q: Pages; i: INTEGER;
	BEGIN
		IF (p = NIL) OR (n > LEN(p)) THEN
			NEW(q, MAX(n, 2 * MAX(16, n)));
			IF p # NIL THEN FOR i := 0 TO LEN(p) - 1 DO q[i] := p[i] END END;
			p := q
		END
	END Grow;

	PROCEDURE New* (): Store;
	(** Пост: пустое исправное хранилище без заданного сбоя. *)
		VAR s: Store;
	BEGIN
		NEW(s); Grow(s.disk, 16); Grow(s.cache, 16); s.armAt := -1; s.keep := -1;
		RETURN s
	END New;

	PROCEDURE (s: Store) Arm* (k, keep: INTEGER), NEW;
	(** Пред: keep < size — 20.
		Пост: счёт событий начат заново; событие k (k < 0 — никакое) станет
		сбоем; запись страницы, прерванная им, оставит keep первых байтов. *)
	BEGIN
		ASSERT(keep < size, 20);
		s.events := 0; s.armAt := k; s.keep := keep
	END Arm;

	PROCEDURE Event (s: Store): BOOLEAN;
		(* TRUE — событие надо исполнить *)
	BEGIN
		IF s.dead THEN RETURN FALSE END;
		IF s.events = s.armAt THEN s.dead := TRUE END;
		INC(s.events);
		RETURN ~s.dead
	END Event;

	PROCEDURE (s: Store) Read* (id: INTEGER; VAR buf: ARRAY OF BYTE; beg: INTEGER; OUT res: INTEGER);
		VAR p: Page; i: INTEGER;
	BEGIN
		ASSERT(id >= 0, 20); ASSERT(LEN(buf) >= beg + size, 21);
		IF id >= s.n THEN res := KvStore.ioError
		ELSE
			p := s.cache[id];
			IF p = NIL THEN p := s.disk[id] END;
			IF p = NIL THEN FOR i := 0 TO size - 1 DO buf[beg + i] := 0 END
			ELSE FOR i := 0 TO size - 1 DO buf[beg + i] := p[i] END
			END;
			res := KvStore.ok
		END
	END Read;

	PROCEDURE (s: Store) Write* (id: INTEGER; IN buf: ARRAY OF BYTE; beg: INTEGER; OUT res: INTEGER);
		VAR p, old: Page; i: INTEGER; torn: BOOLEAN;
	BEGIN
		ASSERT(id >= 0, 20); ASSERT(LEN(buf) >= beg + size, 21);
		torn := (s.keep >= 0) & ~s.dead & (s.events = s.armAt);
		IF Event(s) OR torn THEN
			Grow(s.disk, id + 1); Grow(s.cache, id + 1);
			IF id >= s.n THEN s.n := id + 1 END;
			NEW(p); old := s.cache[id];
			IF old = NIL THEN old := s.disk[id] END;
			FOR i := 0 TO size - 1 DO p[i] := buf[beg + i] END;
			IF torn & (old # NIL) THEN
				FOR i := s.keep TO size - 1 DO p[i] := old[i] END
			ELSIF torn THEN
				FOR i := s.keep TO size - 1 DO p[i] := 0 END
			END;
			s.cache[id] := p; INC(s.writes)
		END;
		IF s.dead THEN res := KvStore.ioError ELSE res := KvStore.ok END
	END Write;

	PROCEDURE (s: Store) Sync* (OUT res: INTEGER);
		VAR i: INTEGER;
	BEGIN
		IF Event(s) THEN
			FOR i := 0 TO s.n - 1 DO
				IF s.cache[i] # NIL THEN s.disk[i] := s.cache[i]; s.cache[i] := NIL END
			END;
			s.dn := s.n; INC(s.syncs); res := KvStore.ok
		ELSE res := KvStore.ioError
		END
	END Sync;

	PROCEDURE (s: Store) Pages* (): INTEGER;
	BEGIN
		RETURN s.n
	END Pages;

	PROCEDURE (s: Store) Close*;
	BEGIN
	END Close;

	PROCEDURE (s: Store) PowerOff* (rnd: INTEGER), NEW;
	(** Выключение и включение питания. rnd = 0 — всё несинхронизированное
		пропало; rnd # 0 — каждая несинхронизированная страница дошла до
		носителя с вероятностью 1/2 по жребию от зерна rnd.
		Пост: устройство исправно, сбой не задан, чтение видит носитель. *)
		VAR i: INTEGER;
	BEGIN
		seed := rnd;
		FOR i := 0 TO s.n - 1 DO
			IF s.cache[i] # NIL THEN
				IF (rnd # 0) & (Rand(2) = 0) THEN
					s.disk[i] := s.cache[i]; s.dn := MAX(s.dn, i + 1)
				END;
				s.cache[i] := NIL
			END
		END;
		s.n := s.dn; s.dead := FALSE; s.armAt := -1; s.keep := -1; s.events := 0
	END PowerOff;

	PROCEDURE (s: Store) Clone* (): Store, NEW;
	(** Пред: несинхронизированных записей нет — 20.
		Пост: независимая копия носителя (страницы общие: они не меняются). *)
		VAR c: Store; i: INTEGER;
	BEGIN
		c := New(); Grow(c.disk, s.n); Grow(c.cache, s.n);
		FOR i := 0 TO s.n - 1 DO
			ASSERT(s.cache[i] = NIL, 20); c.disk[i] := s.disk[i]
		END;
		c.n := s.n; c.dn := s.dn;
		RETURN c
	END Clone;

	PROCEDURE (s: Store) Damage* (id, pos: INTEGER), NEW;
	(** Пред: страница id есть на носителе — 20. Пост: байт pos страницы
		испорчен (инвертирован) прямо на носителе. *)
		VAR p: Page; i: INTEGER;
	BEGIN
		ASSERT((id < s.dn) & (s.disk[id] # NIL), 20);
		NEW(p); FOR i := 0 TO size - 1 DO p[i] := s.disk[id][i] END;
		KvPages.PutByte(p^, pos, 255 - KvPages.Byte(p^, pos));
		s.disk[id] := p
	END Damage;

END KvFault.
