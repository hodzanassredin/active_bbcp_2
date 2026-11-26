MODULE RaftStorage;
(** Стойкое состояние узла Raft: срок, голос, журнал и состав кластера.

	Узел обязан сохранить срок, голос и новые записи до того, как отправит
	письма, которые на них опираются (Ready.syncFirst): голос, забытый после
	падения, даёт второй голос в том же сроке, а запись, подтверждённая
	лидеру и потерянная, — потерю зафиксированного. Save возвращается,
	когда сохранённое переживёт выключение питания.

	Одна Save сохраняет всё, что накопилось в Ready: все записи, которые
	лидер принял после прошлой Save, уходят одной фиксацией (групповая
	фиксация) — один fsync на пачку, а не на запись.

	Снимок автомата заменяет начало журнала: Compact сохраняет снимок
	своего узла, Save с Ready.snap — снимок, принятый от лидера. Вместе со
	снимком хранится состав кластера на его момент (snapVoters,
	snapLearners): записи состава до снимка выброшены, и без него узел
	после перезапуска не знал бы, кто избиратель. Кроме срока, голоса и
	журнала хранится известный commit: он только растёт, а узел, забывший
	его, откатился бы к составу старше своего журнала (RaftCore.SetCommit
	достраивает действующий состав по нему).

	Реализации: Mem (в памяти, для тестов и примеров) и RaftKvStorage (Kv,
	фиксация с fsync). *)

	IMPORT Core := RaftCore;

	CONST
		ok* = 0;
		failed* = 1;	(** ошибка носителя: сохранено ли — неизвестно; узел останавливают *)

	TYPE
		Storage* = POINTER TO ABSTRACT RECORD END;

		Mem = POINTER TO RECORD (Storage)
			term, vote, commit, last: INTEGER;
			snapIndex, snapTerm: INTEGER; snap: Core.Bytes;
			voters, learners: SET;	(* состав на момент снимка *)
			hasConf: BOOLEAN;
			log: Core.Entries	(* log[i] — запись i (снимок записи не убирает: память теста) *)
		END;

	PROCEDURE (s: Storage) Save* (nd: Core.Node; IN r: Core.Ready; OUT res: INTEGER), NEW, ABSTRACT;
	(** Сохранить из r: при r.snap # NIL — снимок и состав на его момент
		(nd.snapVoters, nd.snapLearners); при r.hard — срок и голос; записи
		r.from .. r.last (читаются EntryAt у nd), отбросив сохранённые после
		r.from-1. Известный commit узла тоже сохраняется: он только растёт, и
		после перезапуска узел не должен откатиться к составу старше журнала.
		Пост: res = ok — сохранённое переживёт выключение питания. *)

	PROCEDURE (s: Storage) Compact* (index, term: INTEGER; snap: Core.Bytes;
		voters, learners: SET; OUT res: INTEGER), NEW, ABSTRACT;
	(** Сохранить снимок snap по запись index срока term и состав
		voters/learners на его момент; записи 1 .. index больше не нужны. *)

	PROCEDURE (s: Storage) Load* (OUT term, vote, commit, snapIndex, snapTerm: INTEGER; OUT snap: Core.Bytes;
		OUT log: Core.Entries; OUT last, res: INTEGER; OUT voters, learners: SET; OUT hasConf: BOOLEAN),
		NEW, ABSTRACT;
	(** Пост: res = ok — состояние последних удачных Save и Compact: срок,
		голос, известный commit, снимок по snapIndex и записи snapIndex+1 ..
		last в log[1 .. last-snapIndex]. hasConf — в снимке сохранён состав
		voters/learners; иначе владелец задаёт начальный состав сам. *)

	PROCEDURE (s: Mem) Save* (nd: Core.Node; IN r: Core.Ready; OUT res: INTEGER);
		VAR a: Core.Entries; k: INTEGER; e: Core.Entry;
	BEGIN
		IF r.hard THEN s.term := r.term; s.vote := r.vote END;
		IF nd.commit > s.commit THEN s.commit := nd.commit END;
		IF r.snap # NIL THEN
			s.snap := r.snap; s.snapIndex := r.snapIndex; s.snapTerm := r.snapTerm;
			s.voters := nd.snapVoters; s.learners := nd.snapLearners; s.hasConf := TRUE
		END;
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

	PROCEDURE (s: Mem) Compact* (index, term: INTEGER; snap: Core.Bytes;
		voters, learners: SET; OUT res: INTEGER);
	BEGIN
		IF index > s.snapIndex THEN
			s.snapIndex := index; s.snapTerm := term; s.snap := snap;
			s.voters := voters; s.learners := learners; s.hasConf := TRUE
		END;
		res := ok
	END Compact;

	PROCEDURE (s: Mem) Load* (OUT term, vote, commit, snapIndex, snapTerm: INTEGER; OUT snap: Core.Bytes;
		OUT log: Core.Entries; OUT last, res: INTEGER; OUT voters, learners: SET; OUT hasConf: BOOLEAN);
		VAR k: INTEGER;
	BEGIN
		term := s.term; vote := s.vote; commit := s.commit;
		snapIndex := s.snapIndex; snapTerm := s.snapTerm; snap := s.snap;
		voters := s.voters; learners := s.learners; hasConf := s.hasConf;
		last := s.last; IF last < snapIndex THEN last := snapIndex END;
		IF commit < snapIndex THEN commit := snapIndex END;
		NEW(log, last - snapIndex + 1);
		FOR k := snapIndex + 1 TO s.last DO log[k - snapIndex] := s.log[k] END;
		res := ok
	END Load;

	PROCEDURE NewMem* (): Storage;
	(** Пост: пустое хранилище в памяти: срок 0, голоса нет, журнал пуст,
		состава нет (hasConf = FALSE). *)
		VAR s: Mem;
	BEGIN
		NEW(s); s.vote := Core.none; NEW(s.log, 16); RETURN s
	END NewMem;

END RaftStorage.
