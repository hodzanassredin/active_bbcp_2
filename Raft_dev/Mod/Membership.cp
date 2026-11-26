MODULE Raft_devMembership;
(** Тесты службы состава RaftMembership: записи журнала (вступление,
	выход, продление аренды), живость по аренде, снимок и порчи.

	Автомат проверяется напрямую: команды кодируются JoinCmd/LeaveCmd/
	RenewCmd, применяются Apply, читаются ListQuery/Query. Порчи
	(mutNoLeave, mutNoExpire, mutNoJoin, mutNoRenew) обязаны ломать те же
	проверки: так видно, что тест краснеет на испорченном коде. *)

	IMPORT Services, Tasks, Strings, ActorCore, ActorNet, ActorNetMem, ActorSim,
		Core := RaftCore, RaftSm, RaftStorage, RaftNode, RaftMembership, UnitAssert;

	TYPE
		Bytes = RaftSm.Bytes;

		Sink = POINTER TO RECORD (ActorCore.Actor)
			got, kind, res: INTEGER;
			data: Bytes
		END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		why: ARRAY 256 OF CHAR;
		net: ActorNetMem.Net;
		snd: ARRAY 5 OF ActorNet.Node;
		scfg: ARRAY 5 OF RaftNode.Config;
		nref: ARRAY 5 OF ActorCore.Ref;
		caps: RaftNode.Caps;
		cliNode: ActorNet.Node;
		cli: RaftMembership.Client;
		sinkRef: ActorCore.Ref;
		sink: Sink;
		nn, gen: INTEGER;
		seed: LONGINT;
		up: ARRAY 5 OF BOOLEAN;
		driverDone, mutJoined, roundDone: BOOLEAN;

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body; driverDone := TRUE
	END Run;

	PROCEDURE Until (VAR flag: BOOLEAN; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE ~flag & (Services.Ticks() - t <= ms) DO Services.actionHook.Step END;
		RETURN flag
	END Until;

	PROCEDURE Round;
		VAR i: INTEGER; t: RaftNode.Tick;
	BEGIN
		FOR i := 0 TO nn - 1 DO
			IF up[i] THEN NEW(t); ActorCore.Tell(nref[i], t) END
		END;
		IF cli # NIL THEN RaftMembership.Tick(cli) END;
		FOR i := 0 TO 31 DO
			IF ActorNetMem.Pump(net) THEN END;
			IF ActorSim.Step() THEN END
		END
	END Round;

	PROCEDURE Rounds (k: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO k DO Round END
	END Rounds;



	PROCEDURE NodeName (i: INTEGER; OUT s: ActorCore.Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(gen, d); s := "S" + d$ + "x"; s[LEN(s$) - 1] := CHR(ORD("a") + i)
	END NodeName;

	PROCEDURE^ NewSink (): ActorCore.Actor;

	PROCEDURE SetupCluster (total, initial: INTEGER);
		VAR i, j: INTEGER; name: ActorCore.Name; cap: ARRAY RaftNode.capLen OF CHAR; d: ARRAY 16 OF CHAR;
	BEGIN
		INC(gen); nn := total; cli := NIL; sink := NIL; sinkRef := NIL;
		ActorSim.Install; net := ActorNetMem.New(seed);
		FOR i := 0 TO total - 1 DO
			NodeName(i, name); snd[i] := ActorNet.New(name, net, 1000 + i); ActorNetMem.Add(net, snd[i]);
			scfg[i] := RaftNode.NewConfig(i, initial, snd[i], RaftStorage.NewMem(), RaftMembership.NewMachine);
			scfg[i].electionTicks := 10; scfg[i].seed := seed + i * 37 + gen; scfg[i].tickMs := 0;
			up[i] := TRUE
		END;
		FOR i := 0 TO total - 1 DO nref[i] := RaftNode.Start(scfg[i]) END;
		FOR i := 0 TO total - 1 DO ActorNet.Export(snd[i], nref[i], cap); caps[i] := cap$ END;
		FOR i := 0 TO total - 1 DO
			FOR j := 0 TO total - 1 DO
				IF (i # j) & (j < initial) THEN scfg[i].peers[j] := caps[j]$ END
			END
		END;
		Strings.IntToString(gen, d); cliNode := ActorNet.New("K" + d$, net, 77); ActorNetMem.Add(net, cliNode);
		cli := RaftMembership.New(cliNode, caps, total, 1); cli.tickMs := 0;
		sinkRef := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSink), "K" + d$);
		Rounds(5)
	END SetupCluster;

	PROCEDURE NewSink (): ActorCore.Actor;
		VAR s: Sink;
	BEGIN
		NEW(s); s.got := 0; sink := s; RETURN s
	END NewSink;

	PROCEDURE (s: Sink) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: RaftMembership.Result DO
			s.kind := m.kind; s.res := m.res; s.data := m.data; INC(s.got)
		ELSE
		END
	END Receive;


	PROCEDURE Call (write: BOOLEAN; kind: INTEGER; body: Bytes; OUT res: INTEGER): BOOLEAN;
		VAR before, k, max: INTEGER;
	BEGIN
		before := sink.got; res := -1;
		RaftMembership.Submit(cli, write, kind, body, sinkRef);
		max := 600; k := 0;
		WHILE (sink.got = before) & (k < max) DO Round; INC(k) END;
		IF sink.got = before THEN RETURN FALSE END;
		res := sink.res; RETURN TRUE
	END Call;

	PROCEDURE AliveIn (data: Bytes; id: INTEGER; OUT n: INTEGER): BOOLEAN;
		VAR members: RaftMembership.Members; i: INTEGER;
	BEGIN
		n := 0;
		IF RaftMembership.DecodeList(data, members) # RaftMembership.ok THEN RETURN FALSE END;
		n := members.n;
		i := 0; WHILE (i < members.n) & (members.m[i].id # id) DO INC(i) END;
		RETURN (i < members.n) & members.m[i].alive
	END AliveIn;

	PROCEDURE Leader (): INTEGER;
		VAR i, role, term, leader, last, lastTerm, commit, applied: INTEGER; broken: BOOLEAN;
	BEGIN
		FOR i := 0 TO nn - 1 DO
			IF up[i] THEN
				RaftNode.Status(scfg[i], role, term, leader, last, lastTerm, commit, applied, broken);
				IF role = Core.leader THEN RETURN i END
			END
		END;
		RETURN -1
	END Leader;

	PROCEDURE Voter (i: INTEGER): BOOLEAN;
		VAR voters, learners: SET; dirty, isV, isL: BOOLEAN;
	BEGIN
		RaftNode.ConfStatus(scfg[i], voters, learners, dirty, isV, isL); RETURN isV
	END Voter;

	PROCEDURE TeardownCluster;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nn - 1 DO IF up[i] THEN ActorNet.Down(snd[i]) END END;
		IF cli # NIL THEN RaftMembership.Close(cli) END;
		IF cliNode # NIL THEN ActorNet.Down(cliNode) END;
		Rounds(20);
		IF ActorSim.Drain(100000) THEN END;
		RaftNode.mutations := {}
	END TeardownCluster;

	PROCEDURE Fail (IN what: ARRAY OF CHAR);
	BEGIN
		IF why = "" THEN why := what$ END
	END Fail;

	PROCEDURE List (m: RaftMembership.Machine; now: LONGINT; OUT members: RaftMembership.Members): INTEGER;
		VAR q, d: Bytes; res: INTEGER;
	BEGIN
		q := RaftMembership.ListQuery(now); d := m.Query(q^, LEN(q^));
		res := RaftMembership.DecodeList(d, members); RETURN res
	END List;

	PROCEDURE At (members: RaftMembership.Members; id: INTEGER): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < members.n) & (members.m[i].id # id) DO INC(i) END;
		IF i = members.n THEN RETURN -1 ELSE RETURN i END
	END At;

	PROCEDURE Alive (members: RaftMembership.Members; id: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := At(members, id); RETURN (i >= 0) & members.m[i].alive
	END Alive;

	PROCEDURE Apply (m: RaftMembership.Machine; cmd: Bytes; OUT res: INTEGER);
		VAR r: Bytes;
	BEGIN
		r := m.Apply(1, cmd^, 0, LEN(cmd^)); res := r[0]
	END Apply;

	PROCEDURE Join (m: RaftMembership.Machine; id: INTEGER; IN addr: ARRAY OF CHAR; now, lease: LONGINT; OUT res: INTEGER);
		VAR cmd: Bytes; key: ARRAY 8 OF BYTE;
	BEGIN
		key[0] := SHORT(SHORT(id MOD 256)); key[1] := 7;
		cmd := RaftMembership.JoinCmd(id, addr, key, 2, now, lease); Apply(m, cmd, res)
	END Join;

	PROCEDURE Leave (m: RaftMembership.Machine; id: INTEGER; OUT res: INTEGER);
	BEGIN Apply(m, RaftMembership.LeaveCmd(id), res) END Leave;

	PROCEDURE Renew (m: RaftMembership.Machine; id: INTEGER; now, lease: LONGINT; OUT res: INTEGER);
	BEGIN Apply(m, RaftMembership.RenewCmd(id, now, lease), res) END Renew;

	PROCEDURE^ TestCaught (mut: INTEGER; IN what: ARRAY OF CHAR);

	PROCEDURE RunCheck (mut: SET; OUT msg: ARRAY OF CHAR): BOOLEAN;
	(* Полная проверка службы; FALSE — что-то не так, msg — первое нарушение. *)
		VAR m: RaftMembership.Machine; members: RaftMembership.Members; res: INTEGER; b: Bytes; ok: BOOLEAN;
	BEGIN
		why := ""; RaftMembership.mutations := mut;
		NEW(m); ok := TRUE;
		Join(m, 1, "A", 10, 5, res); IF res # RaftMembership.ok THEN Fail("join 1"); ok := FALSE END;
		Join(m, 2, "B", 10, 100, res); IF res # RaftMembership.ok THEN Fail("join 2"); ok := FALSE END;
		IF List(m, 14, members) # RaftMembership.ok THEN Fail("list 14"); ok := FALSE
		ELSIF (members.n # 2) OR ~Alive(members, 1) OR ~Alive(members, 2) THEN Fail("оба живы на 14"); ok := FALSE
		END;
		IF List(m, 20, members) # RaftMembership.ok THEN Fail("list 20"); ok := FALSE
		ELSIF Alive(members, 1) THEN Fail("аренда 1 истекла"); ok := FALSE
		ELSIF ~Alive(members, 2) THEN Fail("аренда 2 жива"); ok := FALSE
		END;
		Renew(m, 1, 20, 10, res); IF res # RaftMembership.ok THEN Fail("renew 1"); ok := FALSE END;
		IF List(m, 25, members) # RaftMembership.ok THEN Fail("list 25"); ok := FALSE
		ELSIF ~Alive(members, 1) THEN Fail("renew не продлил"); ok := FALSE
		END;
		Leave(m, 2, res); IF res # RaftMembership.ok THEN Fail("leave 2"); ok := FALSE END;
		IF List(m, 25, members) # RaftMembership.ok THEN Fail("list после leave"); ok := FALSE
		ELSIF Alive(members, 2) THEN Fail("leave не вывел"); ok := FALSE
		END;
		Join(m, 3, "C", 30, 10, res); IF res # RaftMembership.ok THEN Fail("join 3"); ok := FALSE END;
		Renew(m, 1, 31, 10, res); IF res # RaftMembership.ok THEN Fail("renew 1 снова"); ok := FALSE END;
		IF List(m, 35, members) # RaftMembership.ok THEN Fail("list 35"); ok := FALSE
		ELSIF members.n # 3 THEN Fail("в списке не три"); ok := FALSE
		ELSIF ~Alive(members, 1) OR ~Alive(members, 3) OR Alive(members, 2) THEN Fail("живость после смен"); ok := FALSE
		END;
		(* снимок и восстановление: состояние то же *)
		b := m.Snapshot(); m.Restore(b);
		IF List(m, 35, members) # RaftMembership.ok THEN Fail("list после restore"); ok := FALSE
		ELSIF members.n # 3 THEN Fail("restore потерял записи"); ok := FALSE
		ELSIF ~Alive(members, 1) OR ~Alive(members, 3) OR Alive(members, 2) THEN Fail("restore изменил живость"); ok := FALSE
		END;
		RaftMembership.mutations := {};
		msg := why$;
		RETURN ok
	END RunCheck;

	PROCEDURE TestBasic*;
	(* Вступление, аренда, продление, выход, повторное вступление, снимок. *)
		VAR msg: ARRAY 256 OF CHAR; ok: BOOLEAN;
	BEGIN
		ok := RunCheck({}, msg);
		UnitAssert.True(ok, msg)
	END TestBasic;

	PROCEDURE TestCorrupt*;
	(* Обрезанная и чужая команда — corrupt, автомат не меняется. *)
		VAR m: RaftMembership.Machine; r: Bytes; cmd: Bytes; members: RaftMembership.Members; res: INTEGER; empty: ARRAY 1 OF BYTE;
	BEGIN
		NEW(m); cmd := RaftMembership.LeaveCmd(1);
		r := m.Apply(1, cmd^, 0, 2); UnitAssert.EqualInt(RaftMembership.corrupt, r[0], "короткая команда");
		r := m.Apply(1, cmd^, 0, LEN(cmd^)); UnitAssert.EqualInt(RaftMembership.notFound, r[0], "leave без записи");
		Join(m, 1, "A", 10, 100, res); UnitAssert.EqualInt(RaftMembership.ok, res, "join");
		empty[0] := 0; cmd := RaftMembership.JoinCmd(1, "A", empty, 0, 10, 100);
		r := m.Apply(1, cmd^, 0, 3); UnitAssert.EqualInt(RaftMembership.corrupt, r[0], "обрезанный join");
		IF List(m, 20, members) # RaftMembership.ok THEN UnitAssert.True(FALSE, "list") END;
		UnitAssert.EqualInt(1, members.n, "состояние не изменилось");
		UnitAssert.True(Alive(members, 1), "запись жива")
	END TestCorrupt;


	(* --- служба в памяти: клиент, выборы, смена состава ------------------- *)

	PROCEDURE WaitLeader (maxRounds: INTEGER): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		REPEAT
			Rounds(1); INC(i)
		UNTIL (Leader() >= 0) OR (i >= maxRounds);
		RETURN Leader()
	END WaitLeader;

	PROCEDURE Key2 (id: INTEGER; OUT key: ARRAY OF BYTE);
	BEGIN
		key[0] := SHORT(SHORT(id MOD 256)); key[1] := 9
	END Key2;

	PROCEDURE JoinNode (id, now, lease: INTEGER; OUT res: INTEGER): BOOLEAN;
		VAR key: ARRAY 2 OF BYTE; body: Bytes; addr: ARRAY 8 OF CHAR;
	BEGIN
		Key2(id, key); addr := "addr"; addr[4] := CHR(ORD("0") + id); addr[5] := 0X;
		body := RaftMembership.JoinCmd(id, addr, key, 2, now, lease);
		RETURN Call(TRUE, 1, body, res)
	END JoinNode;

	PROCEDURE ClientBody;
		VAR i, lead, res, n: INTEGER; body: Bytes; ok: BOOLEAN;
	BEGIN
		SetupCluster(3, 3);
		lead := WaitLeader(300);
		UnitAssert.True(lead >= 0, "лидер выбран");
		FOR i := 0 TO 2 DO
			ok := JoinNode(i, 0, 1000, res);
			UnitAssert.True(ok & (res = RaftMembership.ok), "join")
		END;
		body := RaftMembership.ListQuery(600); ok := Call(FALSE, 2, body, res);
		UnitAssert.True(ok & (res = RaftMembership.ok), "list");
		FOR i := 0 TO 2 DO UnitAssert.True(AliveIn(sink.data, i, n), "жив на 600") END;
		UnitAssert.EqualInt(3, n, "три члена");
		(* лидер убит: чтение идёт к новому *)
		lead := Leader(); ActorNetMem.Crash(net, snd[lead].name); up[lead] := FALSE;
		body := RaftMembership.ListQuery(700); ok := Call(FALSE, 2, body, res);
		UnitAssert.True(ok & (res = RaftMembership.ok), "list после kill -9 лидера");
		FOR i := 0 TO 2 DO UnitAssert.True(AliveIn(sink.data, i, n), "жив после выборов") END;
		(* продление и выход *)
		body := RaftMembership.RenewCmd(0, 800, 1000); ok := Call(TRUE, 1, body, res);
		UnitAssert.True(ok & (res = RaftMembership.ok), "renew");
		body := RaftMembership.LeaveCmd(1); ok := Call(TRUE, 1, body, res);
		UnitAssert.True(ok & (res = RaftMembership.ok), "leave");
		body := RaftMembership.ListQuery(900); ok := Call(FALSE, 2, body, res);
		UnitAssert.True(ok & (res = RaftMembership.ok), "list после leave");
		UnitAssert.True(AliveIn(sink.data, 0, n), "продлённый жив");
		UnitAssert.True(~AliveIn(sink.data, 1, n), "вышедший мёртв");
		UnitAssert.True(AliveIn(sink.data, 2, n), "третий жив");
		TeardownCluster
	END ClientBody;

	PROCEDURE TestClientService*;
	(* Клиент службы: 3 узла, вступление, список, kill -9 лидера, аренда, выход. *)
		VAR d: Driver; t: LONGINT;
	BEGIN
		seed := 11; driverDone := FALSE;
		NEW(d); d.body := ClientBody; Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~driverDone & (Services.Ticks() - t <= 60000) DO Services.actionHook.Step END;
		UnitAssert.True(driverDone, "прогон завис")
	END TestClientService;

	PROCEDURE AddServer (lead, id: INTEGER);
	BEGIN RaftNode.AddServer(scfg[lead], id) END AddServer;

	PROCEDURE RemoveServer (lead, id: INTEGER);
	BEGIN RaftNode.RemoveServer(scfg[lead], id) END RemoveServer;

	PROCEDURE WaitVoter (id, maxRounds: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		REPEAT Rounds(1); INC(i) UNTIL Voter(id) OR (i >= maxRounds);
		RETURN Voter(id)
	END WaitVoter;

	PROCEDURE ConfHas (i, id: INTEGER): BOOLEAN;
		VAR voters, learners: SET; dirty, isV, isL: BOOLEAN;
	BEGIN
		RaftNode.ConfStatus(scfg[i], voters, learners, dirty, isV, isL);
		RETURN (id IN voters) OR (id IN learners)
	END ConfHas;

	PROCEDURE WaitNotMember (id, maxRounds: INTEGER): BOOLEAN;
	(* Ждём, пока все живые узлы, кроме самого id, выведут его из состава. *)
		VAR i, rounds: INTEGER; ok: BOOLEAN;
	BEGIN
		rounds := 0;
		REPEAT
			Rounds(1); INC(rounds); ok := TRUE;
			FOR i := 0 TO nn - 1 DO
				IF up[i] & (i # id) & ConfHas(i, id) THEN ok := FALSE END
			END
		UNTIL ok OR (rounds >= maxRounds);
		RETURN ok
	END WaitNotMember;

	PROCEDURE ChangeBody;
		VAR i, lead: INTEGER;
	BEGIN
		SetupCluster(4, 3);
		lead := WaitLeader(300);
		UnitAssert.True(lead >= 0, "лидер выбран");
		FOR i := 0 TO 2 DO RaftNode.SetPeer(scfg[i], 3, caps[3]) END;	(* пропуск нового узла *)
		Rounds(10);
		AddServer(lead, 3);
		UnitAssert.True(WaitVoter(3, 600), "узел 3 вошёл избирателем");
		Rounds(80);	(* смена состава зафиксирована: можно начинать следующую *)
		lead := Leader(); UnitAssert.True(lead >= 0, "лидер после роста");
		RemoveServer(lead, 3);
		UnitAssert.True(WaitNotMember(3, 600), "узел 3 выведен");
		TeardownCluster
	END ChangeBody;

	PROCEDURE TestAddRemoveServer*;
	(* Вступление 4-го узла через SetPeer/AddServer (ученик, догон,
		избиратель) и выход через RemoveServer. *)
		VAR d: Driver; t: LONGINT;
	BEGIN
		seed := 23; driverDone := FALSE;
		NEW(d); d.body := ChangeBody; Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~driverDone & (Services.Ticks() - t <= 60000) DO Services.actionHook.Step END;
		UnitAssert.True(driverDone, "прогон завис")
	END TestAddRemoveServer;

	PROCEDURE MutChangeBody (mut: SET; OUT joined: BOOLEAN);
		VAR i, lead: INTEGER;
	BEGIN
		RaftNode.mutations := mut;
		SetupCluster(4, 3);
		lead := WaitLeader(300);
		FOR i := 0 TO 2 DO RaftNode.SetPeer(scfg[i], 3, caps[3]) END;
		Rounds(10);
		AddServer(lead, 3);
		joined := WaitVoter(3, 400);
		RaftNode.mutations := {};
		TeardownCluster
	END MutChangeBody;

	PROCEDURE MutBody;
		VAR joined: BOOLEAN;
	BEGIN MutChangeBody({RaftNode.mutConfIgnore}, joined); mutJoined := joined END MutBody;

	PROCEDURE RemoveMutBody;
		VAR i, lead: INTEGER;
	BEGIN
		SetupCluster(4, 3);
		lead := WaitLeader(300);
		FOR i := 0 TO 2 DO RaftNode.SetPeer(scfg[i], 3, caps[3]) END;
		Rounds(10);
		AddServer(lead, 3);
		IF WaitVoter(3, 600) THEN
			Rounds(80);
			Core.mutations := {Core.mutRemoveNoEffect};
			lead := Leader();
			IF lead >= 0 THEN RemoveServer(lead, 3) END;
			mutJoined := ~WaitNotMember(3, 400)
		ELSE mutJoined := FALSE
		END;
		Core.mutations := {};
		TeardownCluster
	END RemoveMutBody;

	PROCEDURE TestMutRemoveNoEffect*;
	(* Порча ядра через API водителя: удаление не меняет состав. *)
		VAR d: Driver; t: LONGINT;
	BEGIN
		seed := 41; driverDone := FALSE;
		NEW(d); d.body := RemoveMutBody; Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~driverDone & (Services.Ticks() - t <= 60000) DO Services.actionHook.Step END;
		UnitAssert.True(driverDone, "прогон завис");
		UnitAssert.True(mutJoined, "порча mutRemoveNoEffect не поймана")
	END TestMutRemoveNoEffect;

	PROCEDURE MutPeerBody;
		VAR joined: BOOLEAN;
	BEGIN MutChangeBody({RaftNode.mutPeerStale}, joined); mutJoined := joined END MutPeerBody;

	PROCEDURE TestMutConfIgnore*;
	(* Порча: команда состава не доходит до ядра — новый узел не входит. *)
		VAR d: Driver; t: LONGINT;
	BEGIN
		seed := 31; driverDone := FALSE;
		NEW(d); d.body := MutBody; Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~driverDone & (Services.Ticks() - t <= 60000) DO Services.actionHook.Step END;
		UnitAssert.True(driverDone, "прогон завис");
		UnitAssert.True(~mutJoined, "порча mutConfIgnore не поймана")
	END TestMutConfIgnore;

	PROCEDURE TestMutPeerStale*;
	(* Порча: пропуск нового узла не дописывается — догнать его нечем. *)
		VAR d: Driver; t: LONGINT;
	BEGIN
		seed := 37; driverDone := FALSE;
		NEW(d); d.body := MutPeerBody; Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~driverDone & (Services.Ticks() - t <= 60000) DO Services.actionHook.Step END;
		UnitAssert.True(driverDone, "прогон завис");
		UnitAssert.True(~mutJoined, "порча mutPeerStale не поймана")
	END TestMutPeerStale;

	PROCEDURE TestCaught (mut: INTEGER; IN what: ARRAY OF CHAR);
	(* Проверка обязательно должна покраснеть: порча поймана. *)
		VAR msg: ARRAY 256 OF CHAR; ok: BOOLEAN;
	BEGIN
		ok := RunCheck({mut}, msg);
		UnitAssert.True(~ok, "порча " + what$ + " не поймана")
	END TestCaught;

	PROCEDURE TestMutNoLeave*; BEGIN TestCaught(RaftMembership.mutNoLeave, "mutNoLeave") END TestMutNoLeave;
	PROCEDURE TestMutNoExpire*; BEGIN TestCaught(RaftMembership.mutNoExpire, "mutNoExpire") END TestMutNoExpire;
	PROCEDURE TestMutNoJoin*; BEGIN TestCaught(RaftMembership.mutNoJoin, "mutNoJoin") END TestMutNoJoin;
	PROCEDURE TestMutNoRenew*; BEGIN TestCaught(RaftMembership.mutNoRenew, "mutNoRenew") END TestMutNoRenew;

END Raft_devMembership.
