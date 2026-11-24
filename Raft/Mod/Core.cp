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
	номера 0 .. n-1. Случайность (тайм-аут выборов) — от зерна узла, поэтому
	прогон с теми же входами повторяется шаг в шаг. *)

	CONST
		(** роль *)
		follower* = 0; candidate* = 1; leader* = 2;
		(** вид письма *)
		voteReq* = 1; voteResp* = 2; appendReq* = 3; appendResp* = 4;
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
		maxReads = 64;

	TYPE
		Bytes* = POINTER TO ARRAY OF BYTE;

		Entry* = RECORD
			term*: INTEGER;
			data*: Bytes	(** команда; NIL — пустая запись лидера *)
		END;

		Entries* = POINTER TO ARRAY OF Entry;

		Msg* = POINTER TO RECORD
			kind*, from*, to*, term*: INTEGER;
			index*, logTerm*: INTEGER;	(** voteReq: последний индекс и срок; appendReq: prevIndex, prevTerm *)
			commit*: INTEGER;	(** appendReq: commitIndex лидера *)
			ents*: Entries; n*: INTEGER;	(** appendReq: записи ents[0 .. n-1] *)
			ok*: BOOLEAN;	(** voteResp: голос дан; appendResp: принято *)
			match*: INTEGER;	(** appendResp: при ok — последний совпавший индекс, иначе подсказка *)
			ctx*: INTEGER;	(** appendReq: номер пульса лидера; appendResp — его эхо *)
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
			reads*: ARRAY maxReads OF Read; nreads*: INTEGER	(** подтверждённые чтения ReadIndex *)
		END;

		Pending = RECORD ctx, index, seq: INTEGER END;

		Node* = POINTER TO RECORD
			id-, n-: INTEGER;
			term-, vote-: INTEGER;
			role-, leader-: INTEGER;
			log: Entries; last-: INTEGER;
			commit-, applied-: INTEGER;
			stable: INTEGER;	(* записи 1 .. stable сохранены *)
			hardDirty: BOOLEAN;
			elapsed, timeout, base: INTEGER;
			votes: SET;
			next, match: ARRAY maxNodes OF INTEGER;
			batch: INTEGER;	(* записей в одном AppendEntries *)
			rnd: LONGINT;
			out, outTail: Msg;
			hb: INTEGER;	(* номер последнего пульса лидера *)
			ack: ARRAY maxNodes OF INTEGER;	(* наибольший пульс, на который ответил узел *)
			pend: ARRAY maxReads OF Pending; npend: INTEGER
		END;

	VAR
		mutations*: SET;	(** включённые мутации; пусто в рабочем пути *)

	(* --- служебное ------------------------------------------------------- *)

	PROCEDURE Rand (nd: Node): INTEGER;
	BEGIN
		nd.rnd := nd.rnd * 6364136223846793005 + 1442695040888963407;	(* LCG Кнута, 64 бита *)
		RETURN SHORT(ASH(nd.rnd, -33) MOD 1000000)
	END Rand;

	PROCEDURE ResetTimeout (nd: Node);
	BEGIN
		nd.elapsed := 0; nd.timeout := nd.base + Rand(nd) MOD nd.base
	END ResetTimeout;

	PROCEDURE Majority (nd: Node): INTEGER;
	BEGIN
		RETURN nd.n DIV 2 + 1
	END Majority;

	PROCEDURE Count (s: SET): INTEGER;
		VAR i, k: INTEGER;
	BEGIN
		k := 0; FOR i := 0 TO maxNodes - 1 DO IF i IN s THEN INC(k) END END;
		RETURN k
	END Count;

	PROCEDURE TermAt* (nd: Node; i: INTEGER): INTEGER;
	(** Пред: 0 <= i <= last — 20. *)
	BEGIN
		ASSERT((i >= 0) & (i <= nd.last), 20);
		RETURN nd.log[i].term
	END TermAt;

	PROCEDURE EntryAt* (nd: Node; i: INTEGER; OUT e: Entry);
	(** Пред: 1 <= i <= last — 20. *)
	BEGIN
		ASSERT((i >= 1) & (i <= nd.last), 20);
		e := nd.log[i]
	END EntryAt;

	PROCEDURE Put (nd: Node; i: INTEGER; IN e: Entry);
		VAR a: Entries; k: INTEGER;
	BEGIN
		IF i >= LEN(nd.log^) THEN
			NEW(a, 2 * LEN(nd.log^) + 16);
			FOR k := 0 TO LEN(nd.log^) - 1 DO a[k] := nd.log[k] END;
			nd.log := a
		END;
		nd.log[i] := e
	END Put;

	PROCEDURE Truncate (nd: Node; last: INTEGER);
	(* отбросить записи после last *)
		VAR k: INTEGER;
	BEGIN
		FOR k := last + 1 TO nd.last DO nd.log[k].data := NIL END;
		nd.last := last;
		IF nd.stable > last THEN nd.stable := last END
	END Truncate;

	PROCEDURE Send (nd: Node; m: Msg);
	BEGIN
		m.from := nd.id; m.term := nd.term; m.next := NIL;
		IF nd.outTail = NIL THEN nd.out := m ELSE nd.outTail.next := m END;
		nd.outTail := m
	END Send;

	PROCEDURE SetTerm (nd: Node; term: INTEGER);
	BEGIN
		nd.term := term; nd.vote := none; nd.hardDirty := TRUE
	END SetTerm;

	PROCEDURE BecomeFollower (nd: Node; term, lead: INTEGER);
	BEGIN
		IF term # nd.term THEN SetTerm(nd, term) END;
		nd.role := follower; nd.leader := lead; nd.npend := 0; ResetTimeout(nd)
	END BecomeFollower;

	PROCEDURE SendAppend (nd: Node; to: INTEGER);
		VAR m: Msg; prev, k, cnt: INTEGER;
	BEGIN
		prev := nd.next[to] - 1;
		cnt := nd.last - prev; IF cnt > nd.batch THEN cnt := nd.batch END;
		NEW(m); m.kind := appendReq; m.to := to;
		m.index := prev; m.logTerm := nd.log[prev].term; m.commit := nd.commit;
		m.n := cnt; m.ctx := nd.hb;
		IF cnt > 0 THEN
			NEW(m.ents, cnt); FOR k := 0 TO cnt - 1 DO m.ents[k] := nd.log[prev + 1 + k] END
		END;
		nd.next[to] := prev + cnt + 1;	(* оптимистично: следующее письмо — дальше; отказ вернёт назад *)
		Send(nd, m)
	END SendAppend;

	PROCEDURE Broadcast (nd: Node);
		VAR p: INTEGER;
	BEGIN
		INC(nd.hb);
		FOR p := 0 TO nd.n - 1 DO IF p # nd.id THEN SendAppend(nd, p) END END
	END Broadcast;

	PROCEDURE MaybeCommit (nd: Node);
	(* наибольший N > commit: большинство хранит N и запись N — своего срока *)
		VAR i, p, k: INTEGER;
	BEGIN
		i := nd.last;
		WHILE i > nd.commit DO
			IF (nd.log[i].term = nd.term) OR (mutOldTermCommit IN mutations) THEN
				k := 0;
				FOR p := 0 TO nd.n - 1 DO
					IF p = nd.id THEN IF nd.stable >= i THEN INC(k) END
					ELSIF nd.match[p] >= i THEN INC(k)
					END
				END;
				IF k >= Majority(nd) THEN nd.commit := i; RETURN END
			END;
			DEC(i)
		END
	END MaybeCommit;

	PROCEDURE BecomeLeader (nd: Node);
		VAR p: INTEGER; e: Entry;
	BEGIN
		nd.role := leader; nd.leader := nd.id; nd.elapsed := 0;
		FOR p := 0 TO nd.n - 1 DO nd.next[p] := nd.last + 1; nd.match[p] := 0; nd.ack[p] := 0 END;
		nd.hb := 0; nd.npend := 0;
		e.term := nd.term; e.data := NIL;	(* пустая запись своего срока *)
		INC(nd.last); Put(nd, nd.last, e);
		Broadcast(nd)
	END BecomeLeader;

	PROCEDURE Campaign (nd: Node);
		VAR p: INTEGER; m: Msg;
	BEGIN
		SetTerm(nd, nd.term + 1); nd.vote := nd.id;
		nd.role := candidate; nd.leader := none; ResetTimeout(nd);
		nd.votes := {nd.id};
		IF Majority(nd) = 1 THEN BecomeLeader(nd)
		ELSE
			FOR p := 0 TO nd.n - 1 DO
				IF p # nd.id THEN
					NEW(m); m.kind := voteReq; m.to := p;
					m.index := nd.last; m.logTerm := nd.log[nd.last].term;
					Send(nd, m)
				END
			END
		END
	END Campaign;

	(* --- входы ----------------------------------------------------------- *)

	PROCEDURE Tick* (nd: Node);
	(** Один тик часов узла. *)
	BEGIN
		INC(nd.elapsed);
		IF nd.role = leader THEN
			IF nd.elapsed >= heartbeat THEN nd.elapsed := 0; Broadcast(nd) END
		ELSIF nd.elapsed >= nd.timeout THEN Campaign(nd)
		END
	END Tick;

	PROCEDURE Propose* (nd: Node; data: Bytes): INTEGER;
	(** Пост: результат > 0 — команда записана в журнал лидера под этим
		индексом (ещё не зафиксирована); 0 — узел не лидер (см. leader). *)
		VAR e: Entry;
	BEGIN
		IF nd.role # leader THEN RETURN 0 END;
		e.term := nd.term; e.data := data;
		INC(nd.last); Put(nd, nd.last, e);
		Broadcast(nd);
		RETURN nd.last
	END Propose;

	PROCEDURE HandleVote (nd: Node; m: Msg);
		VAR r: Msg; upToDate, canVote: BOOLEAN; lt: INTEGER;
	BEGIN
		lt := nd.log[nd.last].term;
		upToDate := (m.logTerm > lt) OR (m.logTerm = lt) & (m.index >= nd.last)
			OR (mutNoUpToDate IN mutations);
		canVote := (nd.vote = none) OR (nd.vote = m.from) OR (mutDoubleVote IN mutations);
		NEW(r); r.kind := voteResp; r.to := m.from;
		IF (m.term = nd.term) & upToDate & canVote THEN
			nd.vote := m.from; nd.hardDirty := TRUE; ResetTimeout(nd); r.ok := TRUE
		END;
		Send(nd, r)
	END HandleVote;

	PROCEDURE HandleAppend (nd: Node; m: Msg);
		VAR r: Msg; k, i, lastNew, lt: INTEGER;
	BEGIN
		NEW(r); r.kind := appendResp; r.to := m.from; r.ctx := m.ctx;
		IF m.term = nd.term THEN
			IF nd.role # follower THEN nd.role := follower; nd.npend := 0 END;
			nd.leader := m.from; ResetTimeout(nd);
			IF (m.index > nd.last) OR (nd.log[m.index].term # m.logTerm) THEN
				IF m.index > nd.last THEN r.match := nd.last
				ELSE	(* пропустить весь срок конфликтующей записи: откат за один обмен, а не по одной *)
					i := m.index - 1; lt := nd.log[m.index].term;
					WHILE (i > nd.commit) & (nd.log[i].term = lt) DO DEC(i) END;
					r.match := i
				END
			ELSE
				FOR k := 0 TO m.n - 1 DO
					i := m.index + 1 + k;
					IF i <= nd.last THEN
						IF nd.log[i].term # m.ents[k].term THEN
							IF ~(mutNoTruncate IN mutations) THEN Truncate(nd, i - 1); INC(nd.last)
							ELSIF nd.stable >= i THEN nd.stable := i - 1
							END;
							Put(nd, i, m.ents[k])
						END
					ELSE
						INC(nd.last); Put(nd, i, m.ents[k])
					END
				END;
				lastNew := m.index + m.n;
				IF m.commit > nd.commit THEN
					IF m.commit < lastNew THEN nd.commit := m.commit ELSE nd.commit := lastNew END
				END;
				r.ok := TRUE; r.match := lastNew
			END
		END;
		Send(nd, r)
	END HandleAppend;

	PROCEDURE HandleAppendResp (nd: Node; m: Msg);
		VAR p: INTEGER;
	BEGIN
		p := m.from;
		IF m.ctx > nd.ack[p] THEN nd.ack[p] := m.ctx END;
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
		IF m.term > nd.term THEN
			IF m.kind = appendReq THEN BecomeFollower(nd, m.term, m.from)
			ELSE BecomeFollower(nd, m.term, none)
			END
		END;
		CASE m.kind OF
		| voteReq: HandleVote(nd, m)
		| voteResp:
			IF (nd.role = candidate) & (m.term = nd.term) & m.ok THEN
				INCL(nd.votes, m.from);
				IF Count(nd.votes) >= Majority(nd) THEN BecomeLeader(nd) END
			END
		| appendReq: HandleAppend(nd, m)
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
		IF (nd.role # leader) OR (nd.log[nd.commit].term # nd.term) OR (nd.npend = maxReads) THEN
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
		k := 1;
		FOR p := 0 TO nd.n - 1 DO IF (p # nd.id) & (nd.ack[p] >= seq) THEN INC(k) END END;
		RETURN k >= Majority(nd)
	END Confirmed;

	(* --- выход ----------------------------------------------------------- *)

	PROCEDURE HasReady* (nd: Node): BOOLEAN;
	BEGIN
		RETURN nd.hardDirty OR (nd.stable < nd.last) OR (nd.out # NIL) OR (nd.applied < nd.commit)
			OR (nd.npend > 0) & Confirmed(nd, nd.pend[0].seq)
			OR (mutApplyUncommitted IN mutations) & (nd.applied < nd.last)
	END HasReady;

	PROCEDURE GetReady* (nd: Node; OUT r: Ready);
	(** Пост: r — что сделать до Advance. Записи from .. last читать EntryAt. *)
	BEGIN
		r.hard := nd.hardDirty; r.term := nd.term; r.vote := nd.vote;
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
		nd.hardDirty := FALSE;
		IF r.last <= nd.last THEN nd.stable := r.last END;
		nd.out := NIL; nd.outTail := NIL;
		IF r.applyHi > nd.applied THEN nd.applied := r.applyHi END;
		IF (r.nreads > 0) & (r.nreads <= nd.npend) THEN
			FOR k := r.nreads TO nd.npend - 1 DO nd.pend[k - r.nreads] := nd.pend[k] END;
			DEC(nd.npend, r.nreads)
		END;
		IF nd.role = leader THEN MaybeCommit(nd) END
	END Advance;

	PROCEDURE SetBatch* (nd: Node; k: INTEGER);
	(** Пред: k > 0 — 20. Пост: в одном AppendEntries не больше k записей (по умолчанию 64). *)
	BEGIN
		ASSERT(k > 0, 20); nd.batch := k
	END SetBatch;

	(* --- создание -------------------------------------------------------- *)

	PROCEDURE New* (id, n: INTEGER; seed: LONGINT; electionTicks: INTEGER;
		term, vote: INTEGER; log: Entries; last: INTEGER): Node;
	(** Узел id из n, после перезапуска со стойким состоянием term, vote и
		записями log[1 .. last] (log может быть NIL при last = 0).
		Пред: 0 <= id < n <= maxNodes — 20; electionTicks > heartbeat — 21. *)
		VAR nd: Node; k: INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < n) & (n <= maxNodes), 20); ASSERT(electionTicks > heartbeat, 21);
		NEW(nd); nd.id := id; nd.n := n;
		nd.term := term; nd.vote := vote; nd.role := follower; nd.leader := none;
		NEW(nd.log, last + 16); nd.log[0].term := 0;
		FOR k := 1 TO last DO nd.log[k] := log[k] END;
		nd.last := last; nd.stable := last;
		nd.rnd := seed * 1000003 + id + 1;
		nd.base := electionTicks; nd.batch := 64; ResetTimeout(nd);
		RETURN nd
	END New;

END RaftCore.
