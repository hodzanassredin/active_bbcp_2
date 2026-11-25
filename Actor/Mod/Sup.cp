MODULE ActorSup;
(** Стратегия супервизора «один за одного» (OneForOne).

	Директива применяется только к отказавшему ребёнку. Перезапуски
	считаются в окне времени: окно открывает первый отказ, отказ позже
	конца окна открывает новое. Больше max перезапусков в окне — Stop.

	Инвариант OneForOne: directive IN {Resume, Restart, Stop, Escalate},
	max >= 0, within > 0. *)

	IMPORT Services, ActorCore;

	TYPE
		OneForOne = POINTER TO RECORD (ActorCore.Strategy)
			directive, max: INTEGER;
			within: LONGINT
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

END ActorSup.
