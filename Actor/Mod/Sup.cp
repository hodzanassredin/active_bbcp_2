MODULE ActorSup;
(** Стратегии супервизора: «один за одного» (OneForOne), «все за одного»
	(AllForOne) и «остальные за одного» (RestForOne).

	Директива OneForOne применяется только к отказавшему ребёнку.
	AllForOne и RestForOne решают судьбу группы: AllForOne — всех детей
	родителя, RestForOne — отказавшего и рождённых после него.
	Перезапуски считаются в окне времени: окно открывает первый отказ,
	отказ позже конца окна открывает новое. Больше max перезапусков в
	окне — Stop (всех или остальных). У AllForOne и RestForOne счётчик
	отказов общий для родителя, а не у каждого ребёнка: окно считает
	отказы детей вместе.

	Инвариант стратегий: directive IN {Resume, Restart, Stop, Escalate},
	max >= 0, within > 0. *)

	IMPORT Services, ActorCore;

	TYPE
		OneForOne = POINTER TO RECORD (ActorCore.Strategy)
			directive, max: INTEGER;
			within: LONGINT
		END;

		Group = POINTER TO RECORD (ActorCore.Strategy)
			directive, max, failures: INTEGER;
			within, since: LONGINT;
			rest: BOOLEAN	(* TRUE — RestForOne, FALSE — AllForOne *)
		END;

	PROCEDURE (s: OneForOne) Decide (child: ActorCore.Ref; reason: INTEGER;
		VAR st: ActorCore.Stats): INTEGER;
		VAR now: LONGINT;
	BEGIN
		IF s.directive # ActorCore.Restart THEN RETURN s.directive END;
		now := Services.Ticks();
		IF (st.failures = 0) OR (now - st.since > s.within) THEN st.failures := 0; st.since := now END;
		INC(st.failures);
		IF st.failures > s.max THEN RETURN ActorCore.Stop ELSE RETURN ActorCore.Restart END
	END Decide;

	PROCEDURE (s: Group) Decide (child: ActorCore.Ref; reason: INTEGER;
		VAR st: ActorCore.Stats): INTEGER;
		VAR now: LONGINT;
	BEGIN
		IF s.directive # ActorCore.Restart THEN
			IF s.directive = ActorCore.Stop THEN
				IF s.rest THEN RETURN ActorCore.StopRest ELSE RETURN ActorCore.StopAll END
			END;
			RETURN s.directive
		END;
		now := Services.Ticks();
		IF (s.failures = 0) OR (now - s.since > s.within) THEN s.failures := 0; s.since := now END;
		INC(s.failures);
		IF s.failures > s.max THEN
			IF s.rest THEN RETURN ActorCore.StopRest ELSE RETURN ActorCore.StopAll END
		ELSIF s.rest THEN RETURN ActorCore.RestartRest
		ELSE RETURN ActorCore.RestartAll
		END
	END Decide;

	PROCEDURE NewOneForOne* (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy;
	(** Пред: directive — Resume, Restart, Stop или Escalate (трап 20);
		max >= 0 (21); withinMs > 0 (22).
		Пост: стратегия отвечает на отказ ребёнка директивой directive;
		для Restart — Stop, если это (max+1)-й отказ ребёнка за withinMs
		мс от первого отказа окна. *)
		VAR s: OneForOne;
	BEGIN
		ASSERT((directive >= ActorCore.Resume) & (directive <= ActorCore.Escalate), 20);
		ASSERT(max >= 0, 21); ASSERT(withinMs > 0, 22);
		NEW(s); s.directive := directive; s.max := max; s.within := withinMs;
		RETURN s
	END NewOneForOne;

	PROCEDURE NewAllForOne* (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy;
	(** Пред: directive — Resume, Restart, Stop или Escalate (трап 20);
		max >= 0 (21); withinMs > 0 (22).
		Пост: для Restart — RestartAll, если отказ детей уложился в max за
		withinMs мс от первого отказа окна, иначе StopAll; для Stop —
		StopAll; Resume и Escalate — как у OneForOne.
		Окно общее для всех детей: отказы разных детей считаются вместе. *)
		VAR s: Group;
	BEGIN
		ASSERT((directive >= ActorCore.Resume) & (directive <= ActorCore.Escalate), 20);
		ASSERT(max >= 0, 21); ASSERT(withinMs > 0, 22);
		NEW(s); s.directive := directive; s.max := max; s.within := withinMs;
		s.failures := 0; s.since := 0; s.rest := FALSE;
		RETURN s
	END NewAllForOne;

	PROCEDURE NewRestForOne* (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy;
	(** Пред: directive — Resume, Restart, Stop или Escalate (трап 20);
		max >= 0 (21); withinMs > 0 (22).
		Пост: как NewAllForOne, но группа — отказавший ребёнок и дети,
		рождённые после него; рождённые раньше продолжают работу. *)
		VAR s: Group;
	BEGIN
		ASSERT((directive >= ActorCore.Resume) & (directive <= ActorCore.Escalate), 20);
		ASSERT(max >= 0, 21); ASSERT(withinMs > 0, 22);
		NEW(s); s.directive := directive; s.max := max; s.within := withinMs;
		s.failures := 0; s.since := 0; s.rest := TRUE;
		RETURN s
	END NewRestForOne;

END ActorSup.
