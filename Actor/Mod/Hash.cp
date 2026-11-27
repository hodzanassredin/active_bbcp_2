MODULE ActorHash;
(** Рандеву-хеш: вес пары «место, вид, ключ».

	Вес считает CasHash.Of по байтам трёх строк подряд: имя места, вид и
	ключ. Владелец ключа — место с наибольшим весом; при равных весах
	выбирается имя, меньшее по порядку строк. При уходе места
	переезжают только его ключи. Этим же весом пользуются виртуальные
	акторы (ActorGrain) и маршрутизаторы (ActorRouter), поэтому одна и
	та же пара «имя, ключ» попадает в одно место у обоих. *)

	IMPORT CasHash, ActorCore;

	PROCEDURE Weight* (IN member, kind, key: ARRAY OF CHAR): LONGINT;
	(** Пост: вес места member для ключа key вида kind; тот же вход даёт
		тот же вес. *)
		VAR b: ARRAY 3 * ActorCore.nameLen + 2 OF BYTE; n: INTEGER; id: CasHash.Id; w: LONGINT; i: INTEGER;

		PROCEDURE Add (IN s: ARRAY OF CHAR);
			VAR i: INTEGER;
		BEGIN
			i := 0; WHILE s[i] # 0X DO b[n] := SHORT(SHORT(ORD(s[i]) MOD 256)); INC(n); INC(i) END;
			b[n] := 0; INC(n)
		END Add;

	BEGIN
		n := 0; Add(member); Add(kind); Add(key);
		CasHash.Of(b, 0, n, id);
		w := 0; FOR i := 0 TO 6 DO w := w * 256 + id[i] MOD 256 END;
		RETURN w
	END Weight;

END ActorHash.
