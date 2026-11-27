MODULE RaftCore;
(** Ядро Raft: детерминированный автомат без ввода-вывода.

	Узел меняется только тремя входами: Step (письмо), Tick (тик часов) и
	Propose (команда клиента на лидере). Всё, что узел хочет сделать в
	мире, он отдаёт через GetReady: стойкое состояние к сохранению (срок,
	голос, хвост журнала начиная с from), письма к отправке и записи к
	применению. Владелец узла сохраняет, отправляет, применяет — в порядке,
	который велит Ready.syncFirst, — и зовёт Advance. Пока Advance не
	позван, новых входов узлу не дают.

	Журнал нумеруется с 1; запись 0 — пустая со сроком 0. Узлы кластера —
	номера 0 .. maxNodes-1. Случайность (тайм-аут выборов) — от зерна узла,
	поэтому прогон с теми же входами повторяется шаг в шаг.

	Состав кластера — множество избирателей voters и учеников learners
	(§4.1–4.3 диссертации Ongaro). Смена состава — запись журнала вида
	entryAddLearner / entryAddVoter / entryRemove. Действующим состав
	становится, когда запись зафиксирована большинством прежнего состава
	(вариант, который Онгаро называет более безопасным: в диссертации
	состав вступает в силу сразу при записи, и падение лидера в узком окне
	оставляет кластер без кворума). Большинство и голоса считаются по
	действующему составу. Меняется один узел за раз и только после
	фиксации прошлой смены. Лидер не начинает смену, пока не зафиксировал
	запись своего срока (исправление ошибки 2015 года).

	Узел голосует по свежести журнала, а не по своему составу: кворум
	считает кандидат по своему действующему составу. Иначе узел, ещё не
	узнавший, что он избиратель (запись состава не дошла), заблокировал бы
	выборы. Голос узла вне состава кандидат не считает.

	PreVote (§9.6): перед выборами узел спрашивает разрешения, не поднимая
	срок; узел, недавно слышавший лидера, разрешения не даёт. Вернувшийся
	из разрыва узел не свергает лидера. CheckQuorum: лидер, не собравший
	ответов большинства за тайм-аут выборов, слагает полномочия. *)

	IMPORT Int64;

	CONST
		(** роль *)
		follower* = 0; candidate* = 1; leader* = 2; preCandidate* = 3;
		(** вид письма *)
		voteReq* = 1; voteResp* = 2; appendReq* = 3; appendResp* = 4;
		snapReq* = 5;	(** InstallSnapshot: index, logTerm — последняя запись снимка; snap — снимок *)
		prevoteReq* = 6; prevoteResp* = 7;
		(** вид записи журнала *)
		entryNormal* = 0; entryAddLearner* = 1; entryAddVoter* = 2; entryRemove* = 3;
		none* = -1;	(** нет голоса, нет лидера *)
		maxNodes* = 9;
		heartbeat = 1;	(* тиков между пульсами лидера *)

		(** переключатели мутаций — только для тестов *)
		mutNoUpToDate* = 0;	(** голос без сверки свежести журнала *)
		mutOldTermCommit* = 1;	(** фиксация записи прошлого срока по счёту реплик *)
		mutSendBeforeSync* = 2;	(** ответ до сохранения срока и голоса *)
		mutNoTruncate* = 3;	(** не откатывать конфликтующий хвост *)
		mutDoubleVote* = 4;	(** два голоса в одном сроке *)
		mutApplyUncommitted* = 5;	(** применять незафиксированное *)
		mutStaleRead* = 6;	(** чтение без подтверждения лидерства большинством *)
		mutNoDedup* = 7;	(** повтор команды клиента применяется заново (RaftSm) *)
		mutNoPreVote* = 8;	(** выборы без предвыборов: срок растёт в разрыве *)
		mutPreVoteIgnoreLeader* = 9;	(** разрешать предвыборы, слыша лидера *)
		mutPreVoteAdoptTerm* = 10;	(** принимать срок предвыборов как свой *)
		mutNoCheckQuorum* = 11;	(** лидер в меньшинстве не слагает полномочия *)
		mutCheckQuorumEver* = 12;	(** считать любой прежний ответ, не глядя на давность *)
		mutCheckQuorumStrict* = 13;	(** требовать ответа всех избирателей, а не большинства *)
		mutNoConfBarrier* = 14;	(** смена состава до фиксации записи своего срока *)
		mutNoLearner* = 15;	(** вводить узел в состав сразу избирателем, без догона *)
		mutNoStepDownRemoved* = 16;	(** удалённый лидер не слагает полномочия *)
		mutRemoveNoEffect* = 17;	(** удаление из состава не меняет состав *)
		mutNoSessionExpire* = 18;	(** сеансы клиентов не вытесняются (RaftSm) *)
		mutExpireCurrent* = 19;	(** вытеснять сеанс текущего срока (RaftSm) *)
		mutSessionTermStale* = 20;	(** не записывать срок последней операции (RaftSm) *)
		mutNoCommitRestore* = 21;	(** забыть commit при перезапуске: откат к составу старше журнала *)
		maxReads = 64;

	TYPE
		Bytes* = POINTER TO ARRAY OF BYTE;

		Entry* = RECORD
			term*: INTEGER;
			data*: Bytes;	(** команда; NIL — пустая запись лидера или запись состава *)
			kind*: INTEGER;	(** entryNormal или вид смены состава *)
			node*: INTEGER	(** номер узла для записи состава *)
		END;

		Entries* = POINTER TO ARRAY OF Entry;

		Msg* = POINTER TO RECORD
			kind*, from*, to*, term*: INTEGER;
			index*, logTerm*: INTEGER;	(** voteReq: последний индекс и срок; appendReq: prevIndex, prevTerm *)
			commit*: INTEGER;	(** appendReq: commitIndex лидера *)
			ents*: Entries; n*: INTEGER;	(** appendReq: записи ents[0 .. n-1] *)
			ok*: BOOLEAN;	(** voteResp, prevoteResp: голос дан; appendResp: принято *)
			match*: INTEGER;	(** appendResp: при ok — последний совпавший индекс, иначе подсказка *)
			ctx*: INTEGER;	(** appendReq: номер пульса лидера; appendResp — его эхо *)
			snap*: Bytes;	(** snapReq: снимок автомата *)
			voters*, learners*: SET;	(** snapReq: состав на момент снимка *)
			next*: Msg
		END;

		(** Подтверждённое чтение: читать можно, когда applied >= index. *)
		Read* = RECORD ctx*, index*: INTEGER END;

		Ready* = RECORD
			hard*: BOOLEAN;	(** срок или голос изменились *)
			term*, vote*: INTEGER;
			from*: INTEGER;	(** сохранить записи from .. last, отбросив хвост после from-1; from > last — нечего *)
			last*: INTEGER;
			syncFirst*: BOOLEAN;	(** TRUE — сохранить до отправки писем *)
			msgs*: Msg;
			applyLo*, applyHi*: INTEGER;	(** применить записи applyLo .. applyHi *)
			reads*: ARRAY maxReads OF Read; nreads*: INTEGER;	(** подтверждённые чтения ReadIndex *)
			snap*: Bytes;	(** не NIL — принят снимок: сохранить его, восстановить из него автомат до применения записей *)
			snapIndex*, snapTerm*: INTEGER
		END;

		Pending = RECORD ctx, index, seq: INTEGER END;

		Node* = POINTER TO RECORD
			id-, n-: INTEGER;
			term-, vote-: INTEGER;
			role-, leader-: INTEGER;
			log: Entries; last-: INTEGER;	(* log[i - snapIndex] — запись i; log[0] — последняя запись снимка *)
			snapIndex-, snapTerm-: INTEGER;	(** записи 1 .. snapIndex заменены снимком *)
			snap: Bytes; snapNew: BOOLEAN;	(* снимок; snapNew — принят от лидера и ещё не отдан в Ready *)
			commit-, applied-: INTEGER;
			stable: INTEGER;	(* записи 1 .. stable сохранены *)
			hardDirty: BOOLEAN;
			elapsed, timeout*, base: INTEGER;
			votes: SET;
			next, match: ARRAY maxNodes OF INTEGER;
			batch: INTEGER;	(* записей в одном AppendEntries *)
			rnd: LONGINT;
			out, outTail: Msg;
			hb: INTEGER;	(* номер последнего пульса лидера *)
			ack: ARRAY maxNodes OF INTEGER;	(* наибольший пульс, на который ответил узел *)
			pend: ARRAY maxReads OF Pending; npend: INTEGER;
			(** состав кластера *)
			voters*, learners*: SET;	(** действующий состав: избиратели и ученики *)
			snapVoters*, snapLearners*: SET;	(** состав на момент снимка *)
			confIndex*: INTEGER;	(** индекс последней записи состава; snapIndex — если её нет *)
			confDirty*: BOOLEAN;	(** последняя запись состава ещё не зафиксирована *)
			(** живучесть *)
			preVote*, checkQuorum*: BOOLEAN;	(** включены PreVote и CheckQuorum *)
			preVotes: SET;	(** голоса предвыборов *)
			ticks*: INTEGER;	(** тиков узла с создания *)
			leaderElapsed*: INTEGER;	(** тиков с последней вести от лидера *)
			lastHeard: ARRAY maxNodes OF INTEGER	(** тик последнего ответа узла лидеру *)
		END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)

	(* --- служебное ------------------------------------------------------- *)

	PROCEDURE Rand (nd: Node): INTEGER;
	BEGIN
		nd.rnd := Int64.Mul(nd.rnd, 6364136223846793005); INC(nd.rnd, 1442695040888963407);	(* LCG Кнута, 64 бита *)
		RETURN SHORT(ASH(nd.rnd, -33) MOD 1000000)
	END Rand;

	PROCEDURE ResetTimeout (nd: Node);
	BEGIN
		nd.elapsed := 0; nd.timeout := nd.base + Rand(nd) MOD nd.base
	END ResetTimeout;

	PROCEDURE CountSet (s, t: SET): INTEGER;
		VAR i, k: INTEGER;
	BEGIN
		k := 0;
		FOR i := 0 TO maxNodes - 1 DO IF (i IN s) & (i IN t) THEN INC(k) END END;
		RETURN k
	END CountSet;

	PROCEDURE Majority (nd: Node): INTEGER;
		VAR c: INTEGER;
	BEGIN
		c := CountSet(nd.voters, nd.voters);
		IF c = 0 THEN RETURN 1 END;	(* состава нет: некому давать большинство *)
		RETURN c DIV 2 + 1
	END Majority;

	PROCEDURE CountVoters* (nd: Node): INTEGER;
	(** Пост: число избирателей в текущем составе. *)
	BEGIN
		RETURN CountSet(nd.voters, nd.voters)
	END CountVoters;

	PROCEDURE IsVoter* (nd: Node; id: INTEGER): BOOLEAN;
	(** Пост: узел id — избиратель текущего состава. *)
	BEGIN
		RETURN (id >= 0) & (id < maxNodes) & (id IN nd.voters)
	END IsVoter;

	PROCEDURE IsLearner* (nd: Node; id: INTEGER): BOOLEAN;
	(** Пост: узел id — ученик текущего состава. *)
	BEGIN
		RETURN (id >= 0) & (id < maxNodes) & (id IN nd.learners)
	END IsLearner;

	PROCEDURE TermAt* (nd: Node; i: INTEGER): INTEGER;
	(** Пред: snapIndex <= i <= last — 20. *)
	BEGIN
		ASSERT((i >= nd.snapIndex) & (i <= nd.last), 20);
		RETURN nd.log[i - nd.snapIndex].term
	END TermAt;

	PROCEDURE EntryAt* (nd: Node; i: INTEGER; OUT e: Entry);
	(** Пред: snapIndex < i <= last — 20. *)
	BEGIN
		ASSERT((i > nd.snapIndex) & (i <= nd.last), 20);
		e := nd.log[i - nd.snapIndex]
	END EntryAt;

	PROCEDURE Put (nd: Node; i: INTEGER; IN e: Entry);
		VAR a: Entries; k: INTEGER;
	BEGIN
		IF i - nd.snapIndex >= LEN(nd.log^) THEN
			NEW(a, 2 * LEN(nd.log^) + 16);
			FOR k := 0 TO LEN(nd.log^) - 1 DO a[k] := nd.log[k] END;
			nd.log := a
		END;
		nd.log[i - nd.snapIndex] := e
	END Put;

	(* --- состав ---------------------------------------------------------- *)

	PROCEDURE ApplyTo (VAR voters, learners: SET; IN e: Entry);
	(* применить запись состава к действующим множествам (только по фиксации) *)
	BEGIN
		IF (e.node < 0) OR (e.node >= maxNodes) THEN RETURN END;
		CASE e.kind OF
		| entryAddLearner:
			INCL(learners, e.node); EXCL(voters, e.node)
		| entryAddVoter:
			INCL(voters, e.node); EXCL(learners, e.node)
		| entryRemove:
			IF ~(mutRemoveNoEffect IN mutations) THEN
				EXCL(voters, e.node); EXCL(learners, e.node)
			END
		END
	END ApplyTo;

	PROCEDURE Rebuild (nd: Node; sv, sl: SET);
	(* действующий состав — снимок sv/sl плюс зафиксированные записи состава;
	   последняя запись состава после commit — ещё не вступившая в силу *)
		VAR i: INTEGER; e: Entry;
	BEGIN
		nd.snapVoters := sv; nd.snapLearners := sl;
		nd.voters := sv; nd.learners := sl; nd.confIndex := nd.snapIndex;
		FOR i := nd.snapIndex + 1 TO nd.last DO
			e := nd.log[i - nd.snapIndex];
			IF e.kind # entryNormal THEN
				IF i <= nd.commit THEN ApplyTo(nd.voters, nd.learners, e) END;
				nd.confIndex := i
			END
		END;
		nd.confDirty := nd.confIndex > nd.commit
	END Rebuild;

	PROCEDURE SetConfig* (nd: Node; voters, learners: SET);
	(** Задать состав на момент снимка: при перезапуске — из хранилища,
		при создании нового узла — начальный состав кластера. Действующий
		состав достраивается по зафиксированным записям состава журнала. *)
	BEGIN
		Rebuild(nd, voters, learners)
	END SetConfig;

	PROCEDURE SetCommit* (nd: Node; commit: INTEGER);
	(** Восстановить известный зафиксированный индекс (владелец хранит его
		вместе с журналом: commit только растёт, и после перезапуска узел
		не должен откатываться к составу старше своего журнала). Зовётся
		после SetConfig; применённое остаётся на снимке, записи до commit
		владелец применит по Ready. Пред: commit — значение, которое узел
		уже знал зафиксированным; большего не выдумывают. *)
	BEGIN
		IF mutNoCommitRestore IN mutations THEN commit := nd.snapIndex END;	(* порча: забыть commit *)
		IF commit < nd.snapIndex THEN commit := nd.snapIndex END;
		IF commit > nd.last THEN commit := nd.last END;
		nd.commit := commit;
		Rebuild(nd, nd.snapVoters, nd.snapLearners)
	END SetCommit;

	PROCEDURE Truncate (nd: Node; last: INTEGER);
	(* отбросить записи после last *)
		VAR k: INTEGER; conf: BOOLEAN;
	BEGIN
		conf := nd.confIndex > last;
		FOR k := last + 1 TO nd.last DO nd.log[k - nd.snapIndex].data := NIL END;
		nd.last := last;
		IF nd.stable > last THEN nd.stable := last END;
		IF conf THEN Rebuild(nd, nd.snapVoters, nd.snapLearners) END
	END Truncate;

	(* --- письма ---------------------------------------------------------- *)

	PROCEDURE SendTerm (nd: Node; m: Msg; term: INTEGER);
	BEGIN
		m.from := nd.id; m.term := term; m.next := NIL;
		IF nd.outTail = NIL THEN nd.out := m ELSE nd.outTail.next := m END;
		nd.outTail := m
	END SendTerm;

	PROCEDURE Send (nd: Node; m: Msg);
	BEGIN
		SendTerm(nd, m, nd.term)
	END Send;

	PROCEDURE SetTerm (nd: Node; term: INTEGER);
	BEGIN
		nd.term := term; nd.vote := none; nd.hardDirty := TRUE
	END SetTerm;

	PROCEDURE BecomeFollower (nd: Node; term, lead: INTEGER);
	BEGIN
		IF term # nd.term THEN SetTerm(nd, term) END;
		nd.role := follower; nd.leader := lead; nd.npend := 0; nd.preVotes := {}; ResetTimeout(nd)
	END BecomeFollower;

	PROCEDURE SendAppend (nd: Node; to: INTEGER);
		VAR m: Msg; prev, k, cnt: INTEGER;
	BEGIN
		prev := nd.next[to] - 1;
		IF prev < nd.snapIndex THEN	(* нужных записей уже нет: послать снимок *)
			NEW(m); m.kind := snapReq; m.to := to; m.index := nd.snapIndex; m.logTerm := nd.snapTerm;
			m.snap := nd.snap; m.commit := nd.commit; m.ctx := nd.hb;
			m.voters := nd.snapVoters; m.learners := nd.snapLearners;
			nd.next[to] := nd.snapIndex + 1;
			Send(nd, m); RETURN
		END;
		cnt := nd.last - prev; IF cnt > nd.batch THEN cnt := nd.batch END;
		NEW(m); m.kind := appendReq; m.to := to;
		m.index := prev; m.logTerm := nd.log[prev - nd.snapIndex].term; m.commit := nd.commit;
		m.n := cnt; m.ctx := nd.hb;
		IF cnt > 0 THEN
			NEW(m.ents, cnt); FOR k := 0 TO cnt - 1 DO m.ents[k] := nd.log[prev + 1 + k - nd.snapIndex] END
		END;
		nd.next[to] := prev + cnt + 1;	(* оптимистично: следующее письмо — дальше; отказ вернёт назад *)
		Send(nd, m)
	END SendAppend;

	PROCEDURE Member (nd: Node; p: INTEGER): BOOLEAN;
	(* узел p получает письма: избиратель или ученик действующего состава *)
	BEGIN
		RETURN (p >= 0) & (p < maxNodes) & ((p IN nd.voters) OR (p IN nd.learners))
	END Member;

	PROCEDURE Broadcast (nd: Node);
		VAR p: INTEGER;
	BEGIN
		INC(nd.hb);
		FOR p := 0 TO maxNodes - 1 DO IF (p # nd.id) & Member(nd, p) THEN SendAppend(nd, p) END END
	END Broadcast;

	PROCEDURE AfterCommit (nd: Node; old: INTEGER);
	(* записи состава, ставшие зафиксированными: ввести в силу по порядку;
	   удалённый лидер слагает полномочия после фиксации своего удаления *)
		VAR i: INTEGER; e: Entry;
	BEGIN
		IF old < nd.snapIndex THEN old := nd.snapIndex END;
		FOR i := old + 1 TO nd.commit DO
			IF i <= nd.last THEN
				EntryAt(nd, i, e);
				IF e.kind # entryNormal THEN ApplyTo(nd.voters, nd.learners, e) END
			END
		END;
		IF nd.confIndex <= nd.commit THEN nd.confDirty := FALSE ELSE nd.confDirty := TRUE END;
		IF (nd.role = leader) & ~(nd.id IN nd.voters) & ~(mutNoStepDownRemoved IN mutations) THEN
			BecomeFollower(nd, nd.term, none)
		END
	END AfterCommit;

	PROCEDURE MaybeCommit (nd: Node);
	(* наибольший N > commit: большинство хранит N и запись N — своего срока *)
		VAR i, p, k, old: INTEGER;
	BEGIN
		i := nd.last;
		WHILE i > nd.commit DO
			IF (nd.log[i - nd.snapIndex].term = nd.term) OR (mutOldTermCommit IN mutations) THEN
				k := 0;
				FOR p := 0 TO maxNodes - 1 DO
					IF p IN nd.voters THEN
						IF p = nd.id THEN IF nd.stable >= i THEN INC(k) END
						ELSIF nd.match[p] >= i THEN INC(k)
						END
					END
				END;
				IF k >= Majority(nd) THEN
					old := nd.commit; nd.commit := i; AfterCommit(nd, old); RETURN
				END
			END;
			DEC(i)
		END
	END MaybeCommit;

	PROCEDURE BecomeLeader (nd: Node);
		VAR p: INTEGER; e: Entry;
	BEGIN
		nd.role := leader; nd.leader := nd.id; nd.elapsed := 0; nd.leaderElapsed := 0;
		FOR p := 0 TO maxNodes - 1 DO
			nd.next[p] := nd.last + 1; nd.match[p] := 0; nd.ack[p] := 0; nd.lastHeard[p] := nd.ticks
		END;
		nd.hb := 0; nd.npend := 0;
		e.term := nd.term; e.data := NIL; e.kind := entryNormal; e.node := none;	(* пустая запись своего срока *)
		INC(nd.last); Put(nd, nd.last, e);
		Broadcast(nd)
	END BecomeLeader;

	PROCEDURE Campaign (nd: Node);
		VAR p: INTEGER; m: Msg;
	BEGIN
		IF ~(nd.id IN nd.voters) THEN RETURN END;	(* не избиратель: выборы не начинает *)
		SetTerm(nd, nd.term + 1); nd.vote := nd.id;
		nd.role := candidate; nd.leader := none; ResetTimeout(nd); nd.preVotes := {};
		nd.votes := {nd.id};
		IF Majority(nd) = 1 THEN BecomeLeader(nd)
		ELSE
			FOR p := 0 TO maxNodes - 1 DO
				IF (p # nd.id) & (p IN nd.voters) THEN
					NEW(m); m.kind := voteReq; m.to := p;
					m.index := nd.last; m.logTerm := nd.log[nd.last - nd.snapIndex].term;
					Send(nd, m)
				END
			END
		END
	END Campaign;

	PROCEDURE StartPreVote (nd: Node);
	(* предвыборы: спросить разрешения, не поднимая срок *)
		VAR p: INTEGER; m: Msg;
	BEGIN
		IF ~(nd.id IN nd.voters) THEN RETURN END;
		nd.role := preCandidate; nd.leader := none; nd.preVotes := {nd.id}; ResetTimeout(nd);
		IF Majority(nd) = 1 THEN Campaign(nd)
		ELSE
			FOR p := 0 TO maxNodes - 1 DO
				IF (p # nd.id) & (p IN nd.voters) THEN
					NEW(m); m.kind := prevoteReq; m.to := p;
					m.index := nd.last; m.logTerm := nd.log[nd.last - nd.snapIndex].term;
					SendTerm(nd, m, nd.term + 1)
				END
			END
		END
	END StartPreVote;

	PROCEDURE CheckQuorum (nd: Node);
	(* лидер слагает полномочия, если большинство избирателей не отвечало
	   дольше тайм-аута выборов (§9.6) *)
		VAR p, k, need: INTEGER; recent: BOOLEAN;
	BEGIN
		k := 0;
		FOR p := 0 TO maxNodes - 1 DO
			IF p IN nd.voters THEN
				recent := (p = nd.id) OR (nd.ticks - nd.lastHeard[p] < nd.timeout);
				IF mutCheckQuorumEver IN mutations THEN recent := (p = nd.id) OR (nd.lastHeard[p] > 0) END;
				IF recent THEN INC(k) END
			END
		END;
		IF mutCheckQuorumStrict IN mutations THEN need := CountSet(nd.voters, nd.voters)
		ELSE need := Majority(nd)
		END;
		IF k < need THEN BecomeFollower(nd, nd.term, none) END
	END CheckQuorum;

	(* --- входы ----------------------------------------------------------- *)

	PROCEDURE Tick* (nd: Node);
	(** Один тик часов узла. *)
	BEGIN
		INC(nd.ticks); INC(nd.leaderElapsed); INC(nd.elapsed);
		IF nd.role = leader THEN
			IF nd.checkQuorum & ~(mutNoCheckQuorum IN mutations) THEN CheckQuorum(nd) END;
			IF (nd.role = leader) & (nd.elapsed >= heartbeat) THEN nd.elapsed := 0; Broadcast(nd) END
		ELSIF nd.elapsed >= nd.timeout THEN
			IF nd.preVote & ~(mutNoPreVote IN mutations) THEN
				IF nd.id IN nd.voters THEN StartPreVote(nd) END
			ELSE
				IF nd.id IN nd.voters THEN Campaign(nd) END
			END
		END
	END Tick;

	PROCEDURE Propose* (nd: Node; data: Bytes): INTEGER;
	(** Пост: результат > 0 — команда записана в журнал лидера под этим
		индексом (ещё не зафиксирована); 0 — узел не лидер (см. leader). *)
		VAR e: Entry;
	BEGIN
		IF nd.role # leader THEN RETURN 0 END;
		e.term := nd.term; e.data := data; e.kind := entryNormal; e.node := none;
		INC(nd.last); Put(nd, nd.last, e);
		Broadcast(nd);
		RETURN nd.last
	END Propose;

	PROCEDURE HandlePreVote (nd: Node; m: Msg);
	(* разрешение на предвыборы: срок не меняется; давший голос не
	   связывает себя, слышавший лидера недавно — отказывает. Состав давшего
	   не важен: кворум считает кандидат по своему составу. *)
		VAR r: Msg; upToDate: BOOLEAN; lt: INTEGER;
	BEGIN
		IF mutPreVoteAdoptTerm IN mutations THEN	(* порча: принять срок предвыборов *)
			IF m.term > nd.term THEN BecomeFollower(nd, m.term, none) END
		END;
		lt := nd.log[nd.last - nd.snapIndex].term;
		upToDate := (m.logTerm > lt) OR (m.logTerm = lt) & (m.index >= nd.last)
			OR (mutNoUpToDate IN mutations);
		NEW(r); r.kind := prevoteResp; r.to := m.from; r.ok := FALSE;
		IF (m.term > nd.term) & upToDate
			& ((nd.role # leader) OR (mutPreVoteIgnoreLeader IN mutations))
			& ((nd.leaderElapsed >= nd.timeout) OR (mutPreVoteIgnoreLeader IN mutations)) THEN
			r.ok := TRUE
		END;
		SendTerm(nd, r, m.term)	(* эхо срока предвыборов *)
	END HandlePreVote;

	PROCEDURE HandlePreVoteResp (nd: Node; m: Msg);
	BEGIN
		IF (nd.role = preCandidate) & (m.term = nd.term + 1) & m.ok THEN
			INCL(nd.preVotes, m.from);
			IF CountSet(nd.preVotes, nd.voters) >= Majority(nd) THEN Campaign(nd) END
		END
	END HandlePreVoteResp;

	PROCEDURE HandleVote (nd: Node; m: Msg);
		VAR r: Msg; upToDate, canVote: BOOLEAN; lt: INTEGER;
	BEGIN
		(* голос — по свежести журнала; свой состав здесь не проверяется:
		   кворум считает кандидат по своему действующему составу, и голос
		   узла вне его не учитывается *)
		lt := nd.log[nd.last - nd.snapIndex].term;
		upToDate := (m.logTerm > lt) OR (m.logTerm = lt) & (m.index >= nd.last)
			OR (mutNoUpToDate IN mutations);
		canVote := (nd.vote = none) OR (nd.vote = m.from) OR (mutDoubleVote IN mutations);
		NEW(r); r.kind := voteResp; r.to := m.from;
		IF (m.term = nd.term) & upToDate & canVote THEN
			nd.vote := m.from; nd.hardDirty := TRUE; ResetTimeout(nd); r.ok := TRUE
		END;
		Send(nd, r)
	END HandleVote;

	PROCEDURE StoreEntry (nd: Node; i: INTEGER; IN e: Entry);
	(* положить запись i; запись состава вступает в силу по фиксации (AfterCommit) *)
	BEGIN
		Put(nd, i, e);
		IF e.kind # entryNormal THEN
			nd.confIndex := i;
			IF i <= nd.commit THEN Rebuild(nd, nd.snapVoters, nd.snapLearners)	(* подмена зафиксированного: пересчёт *)
			ELSE nd.confDirty := TRUE
			END
		END
	END StoreEntry;

	PROCEDURE HandleAppend (nd: Node; m: Msg);
		VAR r: Msg; k, i, lastNew, lt, prev, pterm, skip, cnt, old: INTEGER;
	BEGIN
		NEW(r); r.kind := appendResp; r.to := m.from; r.ctx := m.ctx;
		IF m.term = nd.term THEN
			IF nd.role # follower THEN nd.role := follower; nd.npend := 0; nd.preVotes := {} END;
			nd.leader := m.from; nd.leaderElapsed := 0; ResetTimeout(nd);
			prev := m.index; pterm := m.logTerm; skip := 0; cnt := m.n;
			IF prev < nd.snapIndex THEN	(* начало письма уже в снимке, а снимок — зафиксированное *)
				skip := nd.snapIndex - prev; IF skip > cnt THEN skip := cnt END;
				DEC(cnt, skip); prev := nd.snapIndex; pterm := nd.snapTerm
			END;
			IF (prev > nd.last) OR (TermAt(nd, prev) # pterm) THEN
				IF prev > nd.last THEN r.match := nd.last
				ELSE	(* пропустить весь срок конфликтующей записи: откат за один обмен, а не по одной *)
					i := prev - 1; lt := TermAt(nd, prev);
					WHILE (i > nd.commit) & (TermAt(nd, i) = lt) DO DEC(i) END;
					r.match := i
				END
			ELSE
				FOR k := 0 TO cnt - 1 DO
					i := prev + 1 + k;
					IF i <= nd.last THEN
						IF TermAt(nd, i) # m.ents[skip + k].term THEN
							IF ~(mutNoTruncate IN mutations) THEN Truncate(nd, i - 1); INC(nd.last)
							ELSIF nd.stable >= i THEN nd.stable := i - 1
							END;
							StoreEntry(nd, i, m.ents[skip + k])
						END
					ELSE
						INC(nd.last); StoreEntry(nd, i, m.ents[skip + k])
					END
				END;
				lastNew := prev + cnt;
				IF m.commit > nd.commit THEN
					old := nd.commit;
					IF m.commit < lastNew THEN nd.commit := m.commit ELSE nd.commit := lastNew END;
					AfterCommit(nd, old)
				END;
				r.ok := TRUE; r.match := lastNew
			END
		END;
		Send(nd, r)
	END HandleAppend;

	PROCEDURE HandleSnapshot (nd: Node; m: Msg);
		VAR r: Msg; a: Entries; k, keep: INTEGER;
	BEGIN
		NEW(r); r.kind := appendResp; r.to := m.from; r.ctx := m.ctx;
		IF m.term = nd.term THEN
			IF nd.role # follower THEN nd.role := follower; nd.npend := 0; nd.preVotes := {} END;
			nd.leader := m.from; nd.leaderElapsed := 0; ResetTimeout(nd);
			IF m.index > nd.commit THEN
				IF (m.index <= nd.last) & (TermAt(nd, m.index) = m.logTerm) THEN keep := nd.last - m.index
				ELSE keep := 0	(* журнал не согласуется со снимком: весь долой *)
				END;
				NEW(a, keep + 16); a[0].term := m.logTerm;
				FOR k := 1 TO keep DO a[k] := nd.log[m.index + k - nd.snapIndex] END;
				nd.log := a; nd.snapIndex := m.index; nd.snapTerm := m.logTerm; nd.snap := m.snap;
				nd.last := m.index + keep;
				IF keep = 0 THEN nd.stable := m.index END;
				IF nd.stable < m.index THEN nd.stable := m.index END;
				nd.commit := m.index; nd.applied := m.index; nd.snapNew := TRUE;
				Rebuild(nd, m.voters, m.learners)
			END;
			r.ok := TRUE; r.match := nd.snapIndex;
			IF nd.commit > r.match THEN r.match := nd.commit END;
			IF r.match > nd.last THEN r.match := nd.last END
		END;
		Send(nd, r)
	END HandleSnapshot;

	PROCEDURE Compact* (nd: Node; index: INTEGER; snap: Bytes);
	(** Заменить записи 1 .. index снимком snap автомата, применившего их.
		Пред: snapIndex < index <= applied — 20. Владелец сохраняет снимок сам
		(RaftStorage.Compact) — это не Ready: до сохранения хватает журнала.
		Состав на момент снимка — nd.snapVoters/nd.snapLearners. *)
		VAR a: Entries; k: INTEGER; sv, sl: SET;
	BEGIN
		ASSERT((index > nd.snapIndex) & (index <= nd.applied), 20);
		sv := nd.snapVoters; sl := nd.snapLearners;
		FOR k := nd.snapIndex + 1 TO index DO ApplyTo(sv, sl, nd.log[k - nd.snapIndex]) END;
		NEW(a, nd.last - index + 16); a[0].term := TermAt(nd, index);
		FOR k := 1 TO nd.last - index DO a[k] := nd.log[index + k - nd.snapIndex] END;
		nd.snapTerm := TermAt(nd, index); nd.log := a; nd.snapIndex := index; nd.snap := snap;
		IF nd.stable < index THEN nd.stable := index END;
		Rebuild(nd, sv, sl)
	END Compact;

	PROCEDURE HandleAppendResp (nd: Node; m: Msg);
		VAR p: INTEGER;
	BEGIN
		p := m.from;
		IF m.ctx > nd.ack[p] THEN nd.ack[p] := m.ctx END;
		nd.lastHeard[p] := nd.ticks;
		IF m.ok THEN
			IF m.match > nd.match[p] THEN nd.match[p] := m.match END;
			IF nd.next[p] <= nd.match[p] THEN nd.next[p] := nd.match[p] + 1 END;
			MaybeCommit(nd);
			IF nd.next[p] <= nd.last THEN SendAppend(nd, p) END
		ELSE
			(* подсказка последователя: его журнал совпадает не дальше m.match *)
			IF m.match + 1 < nd.next[p] THEN
				nd.next[p] := m.match + 1;
				IF nd.next[p] <= nd.match[p] THEN nd.next[p] := nd.match[p] + 1 END;
				SendAppend(nd, p)
			END
		END
	END HandleAppendResp;

	PROCEDURE Step* (nd: Node; m: Msg);
	(** Пред: m.to = id — 20. Принять письмо. *)
	BEGIN
		ASSERT(m.to = nd.id, 20);
		IF m.kind = prevoteReq THEN HandlePreVote(nd, m); RETURN END;
		IF m.kind = prevoteResp THEN HandlePreVoteResp(nd, m); RETURN END;
		IF m.term > nd.term THEN
			IF (m.kind = appendReq) OR (m.kind = snapReq) THEN BecomeFollower(nd, m.term, m.from)
			ELSE BecomeFollower(nd, m.term, none)
			END
		END;
		CASE m.kind OF
		| voteReq: HandleVote(nd, m)
		| voteResp:
			IF (nd.id IN nd.voters) & (nd.role = candidate) & (m.term = nd.term) & m.ok THEN
				INCL(nd.votes, m.from);
				IF CountSet(nd.votes, nd.voters) >= Majority(nd) THEN BecomeLeader(nd) END
			END
		| appendReq: HandleAppend(nd, m)
		| snapReq: HandleSnapshot(nd, m)
		| appendResp:
			IF (nd.role = leader) & (m.term = nd.term) THEN HandleAppendResp(nd, m) END
		END
	END Step;

	PROCEDURE ReadIndex* (nd: Node; ctx: INTEGER): BOOLEAN;
	(** Линеаризуемое чтение (ReadIndex, §6.4 диссертации Raft). Пост: TRUE —
		чтение ctx принято: лидер запомнил commit и разослал пульс; когда
		большинство ответит на этот или более поздний пульс, Ready.reads
		отдаст (ctx, index), и читать автомат можно после applied >= index.
		FALSE — узел не лидер, в своём сроке ещё ничего не зафиксировал или
		очередь чтений полна; клиент повторяет у лидера. *)
	BEGIN
		IF (nd.role # leader) OR (nd.log[nd.commit - nd.snapIndex].term # nd.term) OR (nd.npend = maxReads) THEN
			RETURN FALSE
		END;
		Broadcast(nd);
		nd.pend[nd.npend].ctx := ctx; nd.pend[nd.npend].index := nd.commit;
		nd.pend[nd.npend].seq := nd.hb;
		IF mutStaleRead IN mutations THEN nd.pend[nd.npend].seq := 0 END;
		INC(nd.npend);
		RETURN TRUE
	END ReadIndex;

	PROCEDURE Confirmed (nd: Node; seq: INTEGER): BOOLEAN;
		VAR p, k: INTEGER;
	BEGIN
		k := 0; IF nd.id IN nd.voters THEN k := 1 END;
		FOR p := 0 TO maxNodes - 1 DO
			IF (p # nd.id) & (p IN nd.voters) & (nd.ack[p] >= seq) THEN INC(k) END
		END;
		RETURN k >= Majority(nd)
	END Confirmed;

	(* --- смена состава --------------------------------------------------- *)

	PROCEDURE CanChange (nd: Node): BOOLEAN;
	(* лидер; прошлая смена зафиксирована; зафиксирована запись своего срока *)
	BEGIN
		IF nd.role # leader THEN RETURN FALSE END;
		IF nd.confDirty THEN RETURN FALSE END;
		IF nd.commit < nd.snapIndex THEN RETURN FALSE END;
		IF ~(mutNoConfBarrier IN mutations)
			& (nd.log[nd.commit - nd.snapIndex].term # nd.term) THEN RETURN FALSE
		END;
		RETURN TRUE
	END CanChange;

	PROCEDURE AppendConf (nd: Node; kind, id: INTEGER): INTEGER;
	(* запись состава: вступит в силу, когда зафиксируется (AfterCommit) *)
		VAR e: Entry;
	BEGIN
		e.term := nd.term; e.data := NIL; e.kind := kind; e.node := id;
		INC(nd.last); Put(nd, nd.last, e);
		nd.confIndex := nd.last; nd.confDirty := TRUE;
		Broadcast(nd);
		RETURN nd.last
	END AppendConf;

	PROCEDURE AddServer* (nd: Node; id: INTEGER): INTEGER;
	(** Добавить узел id: сначала учеником (догон), затем избирателем, когда
		он догнал commit (MaybePromote). Пост: индекс записи состава > 0 или
		0 — смена не начата: узел не лидер, прошлая смена не зафиксирована,
		запись своего срока не зафиксирована, id уже в составе или вне
		0 .. maxNodes-1. *)
	BEGIN
		IF ~CanChange(nd) THEN RETURN 0 END;
		IF (id < 0) OR (id >= maxNodes) THEN RETURN 0 END;
		IF (id IN nd.voters) OR (id IN nd.learners) THEN RETURN 0 END;
		IF mutNoLearner IN mutations THEN RETURN AppendConf(nd, entryAddVoter, id) END;
		RETURN AppendConf(nd, entryAddLearner, id)
	END AddServer;

	PROCEDURE RemoveServer* (nd: Node; id: INTEGER): INTEGER;
	(** Удалить узел id из состава (избирателя или ученика). Пост: индекс
		записи состава > 0 или 0 — смена не начата (см. AddServer) либо это
		последний избиратель. *)
	BEGIN
		IF ~CanChange(nd) THEN RETURN 0 END;
		IF (id < 0) OR (id >= maxNodes) THEN RETURN 0 END;
		IF ~(id IN nd.voters) & ~(id IN nd.learners) THEN RETURN 0 END;
		IF (id IN nd.voters) & (CountSet(nd.voters, nd.voters) <= 1) THEN RETURN 0 END;
		RETURN AppendConf(nd, entryRemove, id)
	END RemoveServer;

	PROCEDURE MaybePromote (nd: Node);
	(* ученик догнал commit — ввести его в избиратели (одна смена за раз) *)
		VAR id, idx: INTEGER;
	BEGIN
		IF ~CanChange(nd) THEN RETURN END;
		FOR id := 0 TO maxNodes - 1 DO
			IF (id IN nd.learners) & (nd.match[id] >= nd.commit) THEN
				idx := AppendConf(nd, entryAddVoter, id); RETURN
			END
		END
	END MaybePromote;

	PROCEDURE SetFeatures* (nd: Node; preVote, checkQuorum: BOOLEAN);
	(** Включить PreVote и CheckQuorum (для модельных прогонов). *)
	BEGIN
		nd.preVote := preVote; nd.checkQuorum := checkQuorum
	END SetFeatures;

	(* --- выход ----------------------------------------------------------- *)

	PROCEDURE HasReady* (nd: Node): BOOLEAN;
	BEGIN
		RETURN nd.hardDirty OR nd.snapNew OR (nd.stable < nd.last) OR (nd.out # NIL) OR (nd.applied < nd.commit)
			OR (nd.npend > 0) & Confirmed(nd, nd.pend[0].seq)
			OR (mutApplyUncommitted IN mutations) & (nd.applied < nd.last)
	END HasReady;

	PROCEDURE GetReady* (nd: Node; OUT r: Ready);
	(** Пост: r — что сделать до Advance. Записи from .. last читать EntryAt. *)
	BEGIN
		r.hard := nd.hardDirty; r.term := nd.term; r.vote := nd.vote;
		IF nd.snapNew THEN r.snap := nd.snap; r.snapIndex := nd.snapIndex; r.snapTerm := nd.snapTerm
		ELSE r.snap := NIL; r.snapIndex := 0; r.snapTerm := 0
		END;
		r.from := nd.stable + 1; r.last := nd.last;
		r.syncFirst := ~(mutSendBeforeSync IN mutations);
		r.msgs := nd.out;
		r.applyLo := nd.applied + 1; r.applyHi := nd.commit;
		IF mutApplyUncommitted IN mutations THEN r.applyHi := nd.last END;
		IF r.applyHi > nd.last THEN r.applyHi := nd.last END;
		r.nreads := 0;	(* подтверждённые чтения — по порядку, с головы очереди *)
		WHILE (r.nreads < nd.npend) & Confirmed(nd, nd.pend[r.nreads].seq) DO
			r.reads[r.nreads].ctx := nd.pend[r.nreads].ctx; r.reads[r.nreads].index := nd.pend[r.nreads].index;
			INC(r.nreads)
		END
	END GetReady;

	PROCEDURE Advance* (nd: Node; IN r: Ready);
	(** Пост: всё из r сделано. *)
		VAR k: INTEGER;
	BEGIN
		nd.hardDirty := FALSE; nd.snapNew := FALSE;
		IF r.last <= nd.last THEN nd.stable := r.last END;
		nd.out := NIL; nd.outTail := NIL;
		IF r.applyHi > nd.applied THEN nd.applied := r.applyHi END;
		IF (r.nreads > 0) & (r.nreads <= nd.npend) THEN
			FOR k := r.nreads TO nd.npend - 1 DO nd.pend[k - r.nreads] := nd.pend[k] END;
			DEC(nd.npend, r.nreads)
		END;
		IF nd.role = leader THEN MaybeCommit(nd) END;
		IF nd.role = leader THEN MaybePromote(nd) END
	END Advance;

	PROCEDURE SetBatch* (nd: Node; k: INTEGER);
	(** Пред: k > 0 — 20. Пост: в одном AppendEntries не больше k записей (по умолчанию 64). *)
	BEGIN
		ASSERT(k > 0, 20); nd.batch := k
	END SetBatch;

	(* --- создание -------------------------------------------------------- *)

	PROCEDURE New* (id, n: INTEGER; seed: LONGINT; electionTicks: INTEGER;
		term, vote: INTEGER; snapIndex, snapTerm: INTEGER; snap: Bytes; log: Entries; last: INTEGER): Node;
	(** Узел id после перезапуска со стойким состоянием: срок term, голос
		vote, снимок snap по запись snapIndex (срока snapTerm) и записи
		snapIndex+1 .. last в log[1 .. last-snapIndex] (log может быть NIL при
		last = snapIndex). Начальный состав — избиратели 0 .. n-1 (узел с
		id >= n — кандидат в ученики, пока запись состава не введёт его);
		если в журнале есть записи состава, владелец зовёт SetConfig с
		составом снимка. Автомат владелец восстанавливает из snap сам;
		commit = applied = snapIndex. PreVote и CheckQuorum включены.
		Пред: 0 <= id < maxNodes, 0 < n <= maxNodes — 20;
		electionTicks > heartbeat — 21; last >= snapIndex — 22. *)
		VAR nd: Node; k: INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < maxNodes) & (n > 0) & (n <= maxNodes), 20); ASSERT(electionTicks > heartbeat, 21);
		NEW(nd); nd.id := id; nd.n := n;
		nd.term := term; nd.vote := vote; nd.role := follower; nd.leader := none;
		ASSERT(last >= snapIndex, 22);
		nd.snapIndex := snapIndex; nd.snapTerm := snapTerm; nd.snap := snap;
		NEW(nd.log, last - snapIndex + 16); nd.log[0].term := snapTerm;
		FOR k := 1 TO last - snapIndex DO nd.log[k] := log[k] END;
		nd.last := last; nd.stable := last; nd.commit := snapIndex; nd.applied := snapIndex;
		nd.rnd := Int64.Mul(seed, 1000003); INC(nd.rnd, id + 1);
		nd.base := electionTicks; nd.batch := 64; ResetTimeout(nd); nd.leaderElapsed := nd.timeout;
		nd.voters := {}; FOR k := 0 TO n - 1 DO INCL(nd.voters, k) END;
		nd.learners := {}; nd.preVotes := {};
		nd.snapVoters := nd.voters; nd.snapLearners := {};
		nd.confIndex := snapIndex; nd.confDirty := FALSE;
		nd.preVote := TRUE; nd.checkQuorum := TRUE;
		FOR k := 0 TO maxNodes - 1 DO nd.next[k] := 0; nd.match[k] := 0; nd.ack[k] := 0; nd.lastHeard[k] := 0 END;
		RETURN nd
	END New;

END RaftCore.
