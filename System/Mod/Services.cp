MODULE Services;
(**
	project	= "BlackBox 2.0, changes from 1.7.2 marked by green"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, Anton Dmitriev, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT SYSTEM, Kernel;

	CONST
		now* = 0; immediately* = -1;	(** DoLater notBefore **)
		resolution* = 1000;
		scale = resolution DIV Kernel.timeResolution;
		corr = resolution MOD Kernel.timeResolution;
		onlyInsistentActions = TRUE;

	TYPE
		Action* = POINTER TO ABSTRACT RECORD
			notBefore: LONGINT;
			next: Action	(* next element in linear list *)
		END;
		InsistentAction* = POINTER TO ABSTRACT RECORD (Action) END;

		(** Работа, отложенная в главный цикл. Единственная точка входа для
			будущих задач (этап 8): задача кладёт сюда процедуру, а главный
			цикл исполняет её на своём стеке. Сейчас очередь однонитевая;
			при переходе на нити ОС сюда добавится блокировка. *)
		Work* = PROCEDURE;
		WorkItem = POINTER TO RECORD
			next: WorkItem;
			work: Work
		END;

		SafeAction* = ABSTRACT RECORD
			(** Type-safe and extensible replacement for TryHandler *)
			trapped-: BOOLEAN
		END;
		SafeRecAction* = ABSTRACT RECORD (SafeAction)
			adr, typ: SYSTEM.ADDRESS;	(* address and type descriptor of VAR rec: ANYREC on stack; used to implement low-level call for DoRec with VAR-param *)
		END;

		ActionHook* = POINTER TO ABSTRACT RECORD END;
		StdHook = POINTER TO RECORD (ActionHook) END;
		GuardCleaner = POINTER TO RECORD (Kernel.TrapCleaner) END;


	VAR
		actionHook-: ActionHook;
		actions: Action;			(* list of actions *)
		candidates: Action;		(* list of action candidates in IterateOverActions,
												NIL during normal execution of commands *)
		hasImmediates: BOOLEAN;	(* this is a hint: one or more actions in some ring may be immediate actions *)
		trapCnt: INTEGER;
		actionsInTheQueue-: INTEGER;

		workHead, workTail: WorkItem;	(* FIFO отложенной работы *)
		executing-: Action;				(* действие внутри Exec; переживает трап *)
		executingAction-: BOOLEAN;		(* идёт Do действия: задача не должна ждать *)
		wakeHook: PROCEDURE;			(* платформенный «разбудить цикл» *)
		guardCleaner: GuardCleaner;	(* снять флаг при трапе внутри Do *)

	PROCEDURE Ticks* (): LONGINT;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		RETURN t * scale + t * corr DIV Kernel.timeResolution
	END Ticks;


	(** Action **)

	PROCEDURE (a: Action) Do- (), NEW, ABSTRACT;

	PROCEDURE In (l, a: Action): BOOLEAN;
	BEGIN
		WHILE (l # NIL) & (l # a) DO l := l.next END;
		RETURN l # NIL
	END In;

	PROCEDURE Incl (VAR l: Action; a: Action);
	BEGIN
		IF l # NIL THEN a.next := l END;
		l := a
	END Incl;

	PROCEDURE Excl (VAR l: Action; a: Action);
		VAR p0, p1: Action;
	BEGIN
		IF l = a THEN
			l := a.next; a.next := NIL
		ELSIF l # NIL THEN
			p0 := l; p1 := p0.next;
			(* (p0 # NIL) & (p0 # a) *)
			WHILE (p1 # NIL) & (p1 # a) DO p0 := p1; p1 := p0.next END;
			IF p1 = a THEN p0.next := a.next; a.next := NIL END
		END
	END Excl;

	PROCEDURE SetTrapGuardCleanup*;
	(** Снять флаг обработчика, если он остался после трапа в чужом
		контексте (страховка к GuardCleaner). *)
	BEGIN
		executingAction := FALSE	(* executing оставить: Cleanup вернёт действие в очередь *)
	END SetTrapGuardCleanup;

	PROCEDURE (c: GuardCleaner) Cleanup;
	(** Трап внутри Do: снять флаг обработчика, иначе задача навсегда
		считает, что ждать нельзя. *)
	BEGIN
		executingAction := FALSE	(* executing нужен Services.Cleanup *)
	END Cleanup;

	PROCEDURE Exec (a: Action);
		VAR t: Kernel.Type;
	BEGIN
		t := Kernel.TypeOf(a);
		IF t.mod.refcnt >= 0 THEN	(* execute action if its module is not unloaded *)
			Kernel.PushTrapCleaner(guardCleaner);
			executingAction := TRUE;
			a.Do;	(* warning: here the actions and candidates lists may be modified, or a trap may occur! *)
			executingAction := FALSE;
			Kernel.PopTrapCleaner(guardCleaner)
		END
	END Exec;

	PROCEDURE Cleanup;
		VAR p: Action;
	BEGIN
		executingAction := FALSE;	(* трап внутри Do не должен оставить флаг *)
		IF candidates # NIL THEN	(* trap handling *)
			p := candidates; WHILE p.next # NIL DO p := p.next END;	(* find last element of candidates list *)
			p.next := actions; actions := candidates; candidates := NIL	(* prepend candidates list to actions list *)
		END;
		IF executing # NIL THEN	(* трап внутри Exec: действие уже вынуто из candidates,
			но DoLater ещё не вызвано — возвращаем его в очередь, а не теряем молча *)
			IF ~In(actions, executing) & ~In(candidates, executing) THEN
				Incl(actions, executing)
			END;
			executing := NIL
		END;
		actionsInTheQueue := 0; p := actions;
		WHILE p # NIL DO p := p.next; INC(actionsInTheQueue); END;
		trapCnt := Kernel.trapCount	(* all traps are handled now *)
	END Cleanup;

	PROCEDURE DoLater* (a: Action; notBefore: LONGINT);
	(** Register action a. If a is already registered, its notBefore value is updated instead. **)
	BEGIN
		ASSERT(a # NIL, 20);
		IF ~In(actions, a) & ~In(candidates, a) THEN
			Incl(actions, a)
		END;
		a.notBefore := notBefore;	(* if a was already in a list, this statement updates the notBefore value *)
		IF notBefore = immediately THEN hasImmediates := TRUE END
	END DoLater;

	PROCEDURE RemoveAction* (a: Action);
	(** Unregister action a. If a is not registered, nothing happens **)
	BEGIN
		IF a # NIL THEN
			Excl(actions, a);
			Excl(candidates, a)
		END
	END RemoveAction;

	(** --- очередь в главный цикл (задел под задачи, этап 8) --- **)

	PROCEDURE Post* (w: Work);
	(** Отложить работу w до ближайшего шага главного цикла.
		Единственная точка входа для будущих задач: каркас однонитевой,
		задача не трогает модель напрямую, а кладёт сюда процедуру. **)
		VAR i: WorkItem;
	BEGIN
		IF w # NIL THEN
			NEW(i); i.work := w; i.next := NIL;
			IF workTail = NIL THEN workHead := i ELSE workTail.next := i END;
			workTail := i
		END
	END Post;

	PROCEDURE RunPosted;
	(** Исполнить всё, что накопилось в очереди. Вызывается со дна цикла. **)
		VAR i: WorkItem; w: Work;
	BEGIN
		WHILE workHead # NIL DO
			i := workHead; workHead := i.next;
			IF workHead = NIL THEN workTail := NIL END;
			w := i.work; i.work := NIL; i.next := NIL;
			w()	(* может снова вызвать Post; очередь подхватится этим же циклом *)
		END
	END RunPosted;

	PROCEDURE NextDeadline* (): LONGINT;
	(** Ближайший срок среди действий, в тиках Services.Ticks; -1 — ждать
		нечего. Немедленные действия (notBefore < 0) и отложенная работа
		означают «сейчас». Нужна главному циклу, чтобы не просыпаться
		20 раз в секунду на таймере. **)
		VAR p: Action; t, min: LONGINT;
	BEGIN
		IF workHead # NIL THEN RETURN Ticks() END;
		min := -1; p := actions;
		WHILE p # NIL DO
			t := p.notBefore;
			IF t < 0 THEN RETURN Ticks()
			ELSIF (min < 0) OR (t < min) THEN min := t
			END;
			p := p.next
		END;
		RETURN min
	END NextDeadline;

	PROCEDURE SetWakeHook* (h: PROCEDURE);
	(** Установить платформенную процедуру «разбудить главный цикл». **)
	BEGIN
		wakeHook := h
	END SetWakeHook;

	PROCEDURE Wake*;
	(** Разбудить главный цикл (точка входа из другой нити, этап 8):
		например, записать байт в eventfd/pipe, который ждёт главный цикл. **)
	BEGIN
		IF wakeHook # NIL THEN wakeHook END
	END Wake;

	PROCEDURE IterateOverActions (time: LONGINT; onlyInsistentActions: BOOLEAN);
		VAR p: Action;
	BEGIN
		Cleanup;	(* trap handling, if necessary *)
		(* candidates = NIL *)
		candidates := actions; actions := NIL;		(* move action list to candidates list *)
		WHILE candidates # NIL DO					(* for every candidate: execute it or put it back into actions list *)
			p := candidates; candidates := p.next; (* remove head element from candidates list *)
			IF (~onlyInsistentActions OR (p IS InsistentAction))
				& ((0 <= p.notBefore) & (p.notBefore <= time)
					OR (p.notBefore <= time) & (time < 0)) THEN
				p.next := NIL;
				executing := p;	(* если Do трапает, Cleanup вернёт p в очередь *)
				Exec(p);
				executing := NIL
			ELSE
				p.next := actions; actions := p		(* move to actions list for later processing *)
			END
		END
	END IterateOverActions;


	PROCEDURE (h: ActionHook) Step*, NEW, ABSTRACT;
	PROCEDURE (h: ActionHook) StepInsistent*, NEW, ABSTRACT;

	PROCEDURE (h: ActionHook) Loop*, NEW, ABSTRACT;


	PROCEDURE (h: StdHook) Step;
	BEGIN
		RunPosted;	(* очередь главного цикла: работы будущих задач *)
		IF (candidates = NIL) OR (trapCnt < Kernel.trapCount) THEN
			IterateOverActions(Ticks(), ~onlyInsistentActions)
		END
	END Step;

	PROCEDURE (h: StdHook) StepInsistent;
	BEGIN
		IF (candidates = NIL) OR (trapCnt < Kernel.trapCount) THEN
			IterateOverActions(Ticks(), onlyInsistentActions)
		END
	END StepInsistent;

	PROCEDURE (h: StdHook) Loop;
	BEGIN
		RunPosted;	(* очередь главного цикла *)
		IF hasImmediates THEN
			ASSERT((candidates = NIL) OR (trapCnt < Kernel.trapCount), 100);
			hasImmediates := FALSE;
			IterateOverActions(immediately, ~onlyInsistentActions);
		END
	END Loop;


	(* type handling functions *)

	PROCEDURE ThisDesc (IN type: ARRAY OF CHAR; load: BOOLEAN): Kernel.Type;
		CONST record = 1; pointer = 3;
		VAR m: Kernel.Module; t: Kernel.Type; i, j: INTEGER; ch: CHAR;
			typ, mod: Kernel.Name;
	BEGIN
		ASSERT(type # "", 20);
		i := 0; ch := type[0];
		WHILE (ch # ".") & (ch # 0X) DO mod[i] := ch; INC(i); ch := type[i] END;
		ASSERT(ch = ".", 21);
		mod[i] := 0X; INC(i); t := NIL;
		IF load THEN
			m := Kernel.ThisMod(mod)
		ELSE
			m := Kernel.ThisLoadedMod(mod)
		END;
		
		IF m # NIL THEN
			j := 0; REPEAT ch := type[i]; typ[j] := ch; INC(i); INC(j) UNTIL ch = 0X;
			t := Kernel.ThisType(m, typ);
			IF t = NIL THEN typ[j - 1] := "^"; typ[j] := 0X; t := Kernel.ThisType(m, typ) END
		END;
		IF t # NIL THEN
			IF t.id MOD 4 = pointer THEN t := t.base[0] END;
			IF t.id MOD 4 # record THEN t := NIL END
		END;
		RETURN t
	END ThisDesc;

	PROCEDURE GetTypeName* (IN rec: ANYREC; OUT type: ARRAY OF CHAR);
		VAR i, j: INTEGER; ch: CHAR; t: Kernel.Type; modName, name: Kernel.Name;
	BEGIN
		t := Kernel.TypeOf(rec);
		Kernel.GetModName(t.mod, modName);
		Kernel.GetTypeName(t, name);
		type := modName$;
		i := 0; ch := type[0]; WHILE ch # 0X DO INC(i); ch := type[i] END;
		type[i] := "."; INC(i);
		j := 0; REPEAT ch := name[j]; type[i] := ch; INC(i); INC(j) UNTIL ch = 0X;
		IF type[i - 2] = "^" THEN type[i - 2] := 0X END
		(*
		type := modName$ + "." + name$;
		i := LEN(type$); 
		IF type[i - 1] = "^" THEN type[i - 1] := 0X END
		*)
	END GetTypeName;

	PROCEDURE SameType* (IN ra, rb: ANYREC): BOOLEAN;
	BEGIN
		RETURN Kernel.TypeOf(ra) = Kernel.TypeOf(rb)
	END SameType;

	PROCEDURE IsExtensionOf* (IN ra, rb: ANYREC): BOOLEAN;
		VAR ta, tb: Kernel.Type;
	BEGIN
		ta := Kernel.TypeOf(ra); tb := Kernel.TypeOf(rb);
		RETURN ta.base[tb.id DIV 16 MOD 16] = tb
	END IsExtensionOf;

	PROCEDURE Is* (IN rec: ANYREC; IN type: ARRAY OF CHAR): BOOLEAN;
		VAR ta, tb: Kernel.Type;
	BEGIN
		ta := Kernel.TypeOf(rec); tb := ThisDesc(type, FALSE);
		IF tb # NIL THEN RETURN ta.base[tb.id DIV 16 MOD 16] = tb
		ELSE RETURN FALSE
		END
	END Is;

	PROCEDURE Extends* (IN type, base: ARRAY OF CHAR): BOOLEAN;
		VAR ta, tb: Kernel.Type;
	BEGIN
		ASSERT((type # "") & (base # ""), 20);
		ta := ThisDesc(type, TRUE); tb := ThisDesc(base, FALSE);
		IF (ta # NIL) & (tb # NIL) THEN RETURN ta.base[tb.id DIV 16 MOD 16] = tb
		ELSE RETURN FALSE
		END
	END Extends;

	PROCEDURE Level* (IN type: ARRAY OF CHAR): INTEGER;
		VAR t: Kernel.Type;
	BEGIN
		t := ThisDesc(type, TRUE);
		RETURN t.id DIV 16 MOD 16
	END Level;

	PROCEDURE TypeLevel* (IN rec: ANYREC): INTEGER;
		VAR t: Kernel.Type;
	BEGIN
		t := Kernel.TypeOf(rec);
		IF t = NIL THEN RETURN -1
		ELSE RETURN t.id DIV 16 MOD 16
		END
	END TypeLevel;

	PROCEDURE AdrOf* (IN rec: ANYREC): LONGINT;
	BEGIN
		RETURN SYSTEM.ADR(rec)
	END AdrOf;

	PROCEDURE Collect*;
	BEGIN
		Kernel.FastCollect
	END Collect;

	PROCEDURE (VAR a: SafeAction) Do*, NEW, ABSTRACT;
	PROCEDURE (VAR a: SafeRecAction) DoRec* (VAR rec: ANYREC), NEW, ABSTRACT;
	PROCEDURE (VAR a: SafeRecAction) Do*;
	BEGIN a.DoRec(SYSTEM.THISRECORD(a.adr, a.typ))
	END Do;

	PROCEDURE TryHandler (VAR a: SafeAction; dummy: INTEGER); BEGIN a.Do END TryHandler;

	PROCEDURE Try* (VAR a: SafeAction);
		VAR count: INTEGER;
	BEGIN
		count := Kernel.trapCount; a.trapped := FALSE;
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler,
			SYSTEM.ADR(TryHandler)), SYSTEM.ADR(a), SYSTEM.TYP(a), -1);
		a.trapped := Kernel.trapCount # count
	END Try;

	PROCEDURE TryRec* (VAR a: SafeRecAction; VAR rec: ANYREC);
		VAR count: INTEGER;
	BEGIN
		count := Kernel.trapCount; a.trapped := FALSE;
		a.adr := SYSTEM.ADR(rec); a.typ := SYSTEM.TYP(rec);
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler,
			SYSTEM.ADR(TryHandler)), SYSTEM.ADR(a), SYSTEM.TYP(a), -1);
		a.trapped := Kernel.trapCount # count
	END TryRec;


	PROCEDURE Init;
		VAR h: StdHook;
	BEGIN
		actionsInTheQueue := 0;
		NEW(h); actionHook := h;
		NEW(guardCleaner)
	END Init;

BEGIN
	Init
END Services.
