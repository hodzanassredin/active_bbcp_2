MODULE RaftStorage;
(** Стойкое состояние узла Raft: срок, голос и журнал.

	Узел обязан сохранить срок, голос и новые записи до того, как отправит
	письма, которые на них опираются (Ready.syncFirst): голос, забытый после
	падения, даёт второй голос в том же сроке, а запись, подтверждённая
	лидеру и потерянная, — потерю зафиксированного. Save возвращается,
	когда сохранённое переживёт выключение питания.

	Одна Save сохраняет всё, что накопилось в Ready: все записи, которые
	лидер принял после прошлой Save, уходят одной фиксацией (групповая
	фиксация) — один fsync на пачку, а не на запись.

	Снимок автомата заменяет начало журнала: Compact сохраняет снимок
	своего узла, Save с Ready.snap — снимок, принятый от лидера.

	Реализации: Mem (в памяти, для тестов и примеров) и RaftKvStorage (Kv,
	фиксация с fsync). *)

	IMPORT Core := RaftCore;

	CONST
		ok* = 0;
		failed* = 1;	(** ошибка носителя: сохранено ли — неизвестно; узел останавливают *)

	TYPE
		Storage* = POINTER TO ABSTRACT RECORD END;

		Mem = POINTER TO RECORD (Storage)
			term, vote, last: INTEGER;
			snapIndex, snapTerm: INTEGER; snap: Core.Bytes;
			log: Core.Entries	(* log[i] — запись i (снимок записи не убирает: память теста) *)
		END;

	PROCEDURE (s: Storage) Save* (nd: Core.Node; IN r: Core.Ready; OUT res: INTEGER), NEW, ABSTRACT;
	(** Сохранить из r: при r.snap # NIL — снимок (записи по r.snapIndex
		больше не нужны); при r.hard — срок и голос; записи r.from .. r.last
		(читаются EntryAt у nd), отбросив сохранённые после r.from-1.
		Пост: res = ok — сохранённое переживёт выключение питания. *)

	PROCEDURE (s: Storage) Compact* (index, term: INTEGER; snap: Core.Bytes; OUT res: INTEGER), NEW, ABSTRACT;
	(** Сохранить снимок snap по запись index срока term; записи 1 .. index больше не нужны. *)

	PROCEDURE (s: Storage) Load* (OUT term, vote, snapIndex, snapTerm: INTEGER; OUT snap: Core.Bytes;
		OUT log: Core.Entries; OUT last, res: INTEGER), NEW, ABSTRACT;
	(** Пост: res = ok — состояние последних удачных Save и Compact: снимок
		по snapIndex и записи snapIndex+1 .. last в log[1 .. last-snapIndex]. *)

	PROCEDURE (s: Mem) Save* (nd: Core.Node; IN r: Core.Ready; OUT res: INTEGER);
		VAR a: Core.Entries; k: INTEGER; e: Core.Entry;
	BEGIN
		IF r.hard THEN s.term := r.term; s.vote := r.vote END;
		IF r.snap # NIL THEN s.snap := r.snap; s.snapIndex := r.snapIndex; s.snapTerm := r.snapTerm END;
		IF r.from <= r.last + 1 THEN
			IF r.last >= LEN(s.log^) THEN
				NEW(a, 2 * r.last + 16); FOR k := 0 TO LEN(s.log^) - 1 DO a[k] := s.log[k] END;
				s.log := a
			END;
			FOR k := r.from TO r.last DO Core.EntryAt(nd, k, e); s.log[k] := e END;
			FOR k := r.last + 1 TO s.last DO s.log[k].data := NIL END;
			s.last := r.last
		END;
		res := ok
	END Save;

	PROCEDURE (s: Mem) Compact* (index, term: INTEGER; snap: Core.Bytes; OUT res: INTEGER);
	BEGIN
		IF index > s.snapIndex THEN s.snapIndex := index; s.snapTerm := term; s.snap := snap END;
		res := ok
	END Compact;

	PROCEDURE (s: Mem) Load* (OUT term, vote, snapIndex, snapTerm: INTEGER; OUT snap: Core.Bytes;
		OUT log: Core.Entries; OUT last, res: INTEGER);
		VAR k: INTEGER;
	BEGIN
		term := s.term; vote := s.vote; snapIndex := s.snapIndex; snapTerm := s.snapTerm; snap := s.snap;
		last := s.last; IF last < snapIndex THEN last := snapIndex END;
		NEW(log, last - snapIndex + 1);
		FOR k := snapIndex + 1 TO s.last DO log[k - snapIndex] := s.log[k] END;
		res := ok
	END Load;

	PROCEDURE NewMem* (): Storage;
	(** Пост: пустое хранилище в памяти: срок 0, голоса нет, журнал пуст. *)
		VAR s: Mem;
	BEGIN
		NEW(s); s.vote := Core.none; NEW(s.log, 16); RETURN s
	END NewMem;

END RaftStorage.
