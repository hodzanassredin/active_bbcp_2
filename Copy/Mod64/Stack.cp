MODULE CopyStack;
(** Проба: объект ждёт на общем стеке исполнителя, без своего стека.

	Исполнитель один на модуль. Он берёт у ядра общий стек 1 МБ и входит
	на него переключателем Taskarch.Switch; планировщик остаётся на
	главном стеке. Объект входит в общий стек через ObjBoot и Do. На
	время ожидания участок от сохранённого указателя стека до базы Do
	уезжает в массив байт в куче и возвращается на те же адреса перед
	пробуждением. Адреса кадров, VAR-параметров и статической связи не
	меняются, поэтому продолжение верно без карт кадров и без правок
	компилятора.

	Копии лежат в аренах — массивах байт в куче, зарегистрированных в
	реестре ядра целиком (Kernel.RegisterStackSegment): сборщик сканирует
	арену консервативно, как спящий стек, и не освобождает объекты, на
	которые ссылаются только кадры ждущих.

	Состояние ловли трапа (Kernel.Try, tryCount, цепочка уборщиков)
	снимается с общего стека перед уходом (Kernel.TakeContext) и
	возвращается перед продолжением: буфер Try лежит в участке и
	возвращается вместе с ним.

	Ограничения:
	- только Linux amd64;
	- один исполнитель; объекты одного исполнителя входят в Do с одной
	  глубины (иначе трап 31);
	- вложенный Do запрещён: вызов Do с глубины больше нуля — трап 30;
	- пока объект ждёт, никто не должен держать адрес в его участке
	  (кадр условия Monitors, аргумент задания AsyncWork на стеке и т. п.);
	- мониторы на время ожидания не удерживаются. *)

	IMPORT S := SYSTEM, Kernel, Taskarch, Libc := LinLibc, Console;

	CONST
		word = Taskarch.word;
		entryOff = Taskarch.entryOff;
		stackHead = Taskarch.stackHead;
		execSize = 1024 * 1024;	(* общий стек исполнителя *)
		SP = 4;	(* номер регистра указателя стека для SYSTEM.GETREG *)
		sliceMs = 5;
		nameLen = 64;

		(** состояния объекта **)
		New* = 0; Ready* = 1; Running* = 2; Waiting* = 3;
		Finished* = 4; Failed* = 5;

		(** номера трапов **)
		noStack = 20;	(* Do/Wait не на общем стеке исполнителя *)
		badState = 21;	(* объект не в том состоянии *)
		noMemory = 22;	(* нет общего стека или места под копию *)
		nested = 30;	(* вложенный Do *)
		baseChanged = 31;	(* база участка у объектов разная *)

	TYPE
		(** Арена копий: один массив байт, зарегистрированный в реестре ядра
			целиком. Копии участков кладутся в него подряд; сборщик сканирует
			арену одним диапазоном, а не тысячами записей. *)
		Arena = POINTER TO RECORD
			next: Arena;
			buf: POINTER TO ARRAY OF SHORTCHAR;
			lo: LONGINT;	(* адрес данных; ADDRESS в куче сборщику не показываем *)
			used, cap, live: INTEGER;	(* занято байт, ёмкость, живых копий *)
			seg: Kernel.Stack
		END;

		Object* = POINTER TO ABSTRACT RECORD
			rnext: Object;	(* очередь готовых *)
			sp: S.ADDRESS;	(* сохранённый контекст ожидания *)
			state: INTEGER;
			signaled, needSave, bodyOk, failed: BOOLEAN;
			arena: Arena;	(* арена с копией участка, пока объект ждёт *)
			off, segLen: INTEGER;	(* смещение и настоящая длина участка *)
			ctx: Kernel.Context;	(* состояние ловли трапа ждущего *)
			name: ARRAY nameLen OF CHAR
		END;

	VAR
		lo, hi: S.ADDRESS;	(* общий стек [lo, hi) *)
		execBase: S.ADDRESS;	(* SP на входе в Do — верх участка *)
		spawnSP: S.ADDRESS;	(* начальный контекст ObjBoot *)
		schedSP: S.ADDRESS;	(* сохранённый контекст планировщика *)
		execRec, schedRec: Kernel.Stack;
		bootObj, curObj, readyHead, readyTail: Object;
		depth: INTEGER;	(* 1 — объект исполняется, 0 — планировщик *)
		initialized: BOOLEAN;
		slice: LONGINT;
		switches, nWaits: LONGINT;
		nDo, nWaitGo, nExit, nFinal: LONGINT;
		arenaHead, curArena: Arena;
		arenaCapNext: INTEGER;

		(* Мутации для проверки, что тесты падают. *)
		mutNoRegister: BOOLEAN;	(* копия не корень сборщика *)
		mutShift: INTEGER;	(* участок возвращается со сдвигом *)
		mutStayAfterTrap: BOOLEAN;	(* после трапа объект остаётся «внутри» *)

	(** Тело объекта на общем стеке. Наследник пишет Exclusive;
		Do зовёт его под Kernel.Try и уходит к планировщику. *)
	PROCEDURE (o: Object) Exclusive-, NEW, ABSTRACT;

	(* --- очередь готовых -------------------------------------------------- *)

	PROCEDURE Enqueue (o: Object);
	BEGIN
		o.rnext := NIL;
		IF readyTail = NIL THEN readyHead := o ELSE readyTail.rnext := o END;
		readyTail := o
	END Enqueue;

	PROCEDURE Dequeue (): Object;
		VAR o: Object;
	BEGIN
		o := readyHead;
		IF o # NIL THEN
			readyHead := o.rnext;
			IF readyHead = NIL THEN readyTail := NIL END;
			o.rnext := NIL
		END;
		RETURN o
	END Dequeue;

	(* --- арены копий ------------------------------------------------------ *)

	PROCEDURE UnlinkArena (a: Arena);
		VAR p: Arena;
	BEGIN
		IF arenaHead = a THEN arenaHead := a.next
		ELSE
			p := arenaHead;
			WHILE (p # NIL) & (p.next # a) DO p := p.next END;
			IF p # NIL THEN p.next := a.next END
		END;
		a.next := NIL
	END UnlinkArena;

	PROCEDURE NewArena (need: INTEGER);
	(* Ёмкость растёт в полтора раза до 64 МБ: меньше арен — меньше
	   обходов кучи сборщиком, но и без большого перерасхода. *)
		VAR a: Arena; cap: INTEGER; data, endAdr: S.ADDRESS;
	BEGIN
		cap := arenaCapNext;
		IF need + 64 > cap THEN cap := need + 65536 END;
		IF cap < 64 * 1024 * 1024 THEN arenaCapNext := cap + cap DIV 2 END;
		NEW(a);
		IF a = NIL THEN HALT(noMemory) END;
		NEW(a.buf, cap);
		IF a.buf = NIL THEN HALT(noMemory) END;
		data := S.ADR(a.buf[0]);
		a.lo := S.VAL(LONGINT, data);
		a.cap := cap; a.used := 0; a.live := 0;
		endAdr := data + cap;
		IF ~mutNoRegister THEN
			a.seg := Kernel.RegisterStackSegment(data, endAdr);
			IF a.seg = NIL THEN HALT(noMemory) END
		END;
		a.next := arenaHead; arenaHead := a; curArena := a
	END NewArena;

	PROCEDURE AllocSlot (o: Object; need: INTEGER);
	(* Пост: на арене есть место need байт; o.off и o.arena заданы. *)
		VAR a: Arena;
	BEGIN
		a := curArena;
		IF (a = NIL) OR (a.used + need > a.cap) THEN NewArena(need); a := curArena END;
		o.arena := a; o.off := a.used; o.segLen := need;
		a.used := a.used + need; INC(a.live)
	END AllocSlot;

	PROCEDURE Trim*;
	(** Вернуть ядру пустые арены, включая текущую: после волны ждущих
		память возвращается. Следующий Wait заведёт арену заново. *)
		VAR a, n: Arena;
	BEGIN
		a := arenaHead;
		WHILE a # NIL DO
			n := a.next;
			IF a.live = 0 THEN
				UnlinkArena(a);
				IF a.seg # NIL THEN Kernel.UnregisterStack(a.seg); a.seg := NIL END;
				a.buf := NIL;
				IF a = curArena THEN curArena := NIL END
			END;
			a := n
		END;
		arenaCapNext := 1024 * 1024
	END Trim;

	PROCEDURE ReleaseSlot (o: Object);
	(* Пост: копия o больше не нужна; пустая арена освобождена. *)
		VAR a: Arena;
	BEGIN
		a := o.arena;
		IF a = NIL THEN RETURN END;
		DEC(a.live);
		IF a.live = 0 THEN
			IF a = curArena THEN
				a.used := 0
			ELSE
				UnlinkArena(a);
				IF a.seg # NIL THEN Kernel.UnregisterStack(a.seg); a.seg := NIL END;
				a.buf := NIL
			END
		END;
		o.arena := NIL;
		(* Все копии кончились — следующая волна снова начинает с 1 МБ. *)
		IF (curArena # NIL) & (curArena.live = 0) & (curArena.used = 0) THEN
			arenaCapNext := 1024 * 1024
		END
	END ReleaseSlot;

	(* --- общий стек ------------------------------------------------------- *)

	PROCEDURE^ ObjBoot;

	PROCEDURE Seed;
	(* Пост: в верхних словах общего стека лежит начальный контекст
	   ObjBoot: 13 нулевых регистров и адрес входа. *)
		VAR sp, a, zero: S.ADDRESS;
	BEGIN
		sp := hi - stackHead;
		zero := 0; a := sp;
		WHILE a < hi DO S.PUT(a, zero); INC(a, word) END;
		S.PUT(sp + entryOff, S.ADR(ObjBoot));
		spawnSP := sp
	END Seed;

	PROCEDURE Body (a, b, c: S.ADDRESS);
	(* Тело под Kernel.Try: зовёт Exclusive, отмечает нормальный выход. *)
		VAR o: Object;
	BEGIN
		o := S.VAL(Object, a);
		o.Exclusive;
		o.bodyOk := TRUE
	END Body;

	(* --- методы объекта --------------------------------------------------- *)

	PROCEDURE (o: Object) Do*, NEW;
	(** Вход объекта на общем стеке: база участка, Kernel.Try вокруг
		Exclusive, затем уход к планировщику. Трап внутри гарантирует
		выход: объект помечается Failed, остальные живут. Не возвращается. *)
		VAR sp: S.ADDRESS; c: Kernel.Context;
	BEGIN
		S.GETREG(SP, sp);
		IF Kernel.StackOf(sp) # execRec THEN HALT(noStack) END;
		IF depth > 0 THEN
			Console.WriteStr("CopyStack: nested Do is not supported"); Console.WriteLn;
			HALT(nested)
		END;
		IF (execBase = 0) OR (sp >= execBase) THEN HALT(baseChanged) END;
		depth := 1;
		INC(nDo);
		o.failed := FALSE; o.bodyOk := FALSE;
		curObj := o;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(Body)), S.VAL(S.ADDRESS, o), 0, 0);
		(* Нормальный выход или трап, пойманный Try. *)
		IF ~o.bodyOk THEN o.failed := TRUE END;
		IF o.failed & mutStayAfterTrap THEN
			o.state := Running
		ELSIF o.failed THEN
			o.state := Failed
		ELSE
			o.state := Finished
		END;
		Kernel.TakeContext(c);
		Kernel.SwitchSave(execRec);
		INC(nExit);
		depth := 0;
		INC(switches);
		Taskarch.Switch(sp, schedSP);	(* на главный стек *)
		HALT(100)	(* недостижимо *)
	END Do;

	PROCEDURE ObjBoot;
	(* Точка входа общего стека (адрес возврата из Switch). База участка —
	   указатель стека здесь, до кадра Do: участок обязан включать кадр Do
	   целиком, иначе self и локальные Do затрёт следующий объект. *)
		VAR sp: S.ADDRESS; o: Object;
	BEGIN
		o := bootObj; bootObj := NIL;
		IF o = NIL THEN HALT(noStack) END;
		S.GETREG(SP, sp);
		IF execBase = 0 THEN execBase := sp
		ELSIF execBase # sp THEN
			Console.WriteStr("CopyStack: executor stack base changed"); Console.WriteLn;
			HALT(baseChanged)
		END;
		o.Do;
		HALT(100)	(* Do не возвращается *)
	END ObjBoot;

	PROCEDURE (o: Object) Wait*, NEW;
	(** Сохранить участок, уйти к планировщику и вернуться, когда разбудят.
		Ждать можно только из Exclusive; вызов вне объекта — трап 20. *)
		VAR sp: S.ADDRESS;
	BEGIN
		S.GETREG(SP, sp);
		IF (depth = 0) OR (Kernel.StackOf(sp) # execRec) THEN HALT(noStack) END;
		IF o.state # Running THEN HALT(badState) END;
		IF o.signaled THEN o.signaled := FALSE; RETURN END;
		o.state := Waiting;
		o.needSave := TRUE;
		INC(nWaits);
		(* Место под копию — до ухода: после Switch память выделять нельзя,
		   сборка увидела бы участок только на общем стеке. Запас покрывает
		   кадр Switch и параметр. *)
		AllocSlot(o, S.VAL(INTEGER, execBase - sp) + 192);
		Kernel.TakeContext(o.ctx);
		(* Для сборщика: пока объект ждёт, его участок ещё лежит на общем
		   стеке; SwitchSave ставит границу скана ниже Switch. Состояние
		   ловли трапа уже снято TakeContext и в записи не нужно. *)
		Kernel.SwitchSave(execRec);
		depth := 0;
		INC(switches);
		Taskarch.Switch(o.sp, schedSP);
		INC(nWaitGo);
		(* Планировщик вернул участок на место и восстановил o.ctx. *)
		o.needSave := FALSE;
		o.state := Running;
		depth := 1;
		IF o.signaled THEN o.signaled := FALSE END
	END Wait;

	PROCEDURE (o: Object) Wake*, NEW;
	(** Разбудить ждущий объект. Сигнал, пришедший до Wait, запоминается:
		следующий Wait вернётся сразу. *)
	BEGIN
		IF o = NIL THEN RETURN END;
		IF o.state IN {Finished, Failed} THEN RETURN END;
		o.signaled := TRUE;
		IF o.state = Waiting THEN o.state := Ready; Enqueue(o) END
	END Wake;

	PROCEDURE (o: Object) FINALIZE-;
	(** Ждущий объект, на который не осталось ссылок, освобождает место в
		арене: иначе реестр ядра держал бы диапазон освобождённой памяти.
		Наследник со своим FINALIZE обязан сделать то же. *)
	BEGIN
		ReleaseSlot(o);
		INC(nFinal)
	END FINALIZE;

	(* --- планировщик ------------------------------------------------------ *)

	PROCEDURE Init*;
	(** Пред: платформа дала стек (иначе трап 22). Пост: исполнитель готов. *)
		VAR probe: INTEGER;
	BEGIN
		IF initialized THEN RETURN END;
		IF ~Kernel.AllocStack(execSize, lo, hi) THEN HALT(noMemory) END;
		(* Общий стек — обычная запись реестра: пока стек не текущий,
		   сборщик сканирует его от sp, сохранённого SwitchSave при уходе
		   объекта. Сканировать весь мегабайт незачем. *)
		execRec := Kernel.RegisterStack(lo, hi);
		IF execRec = NIL THEN HALT(noMemory) END;
		schedRec := Kernel.StackOf(S.ADR(probe));
		IF schedRec = NIL THEN HALT(noMemory) END;
		Seed;
		slice := sliceMs;
		initialized := TRUE
	END Init;

	PROCEDURE Start* (o: Object);
	(** Поставить новый объект в очередь готовых. *)
	BEGIN
		Init;
		IF o = NIL THEN HALT(badState) END;
		IF o.state # New THEN HALT(badState) END;
		o.state := Ready; Enqueue(o)
	END Start;

	PROCEDURE SaveSeg (o: Object);
	(* Пост: участок [o.sp, execBase) скопирован в арену. Зовётся
	   планировщиком сразу после ухода объекта. *)
		VAR len: INTEGER; dummy: S.ADDRESS; a: Arena;
	BEGIN
		IF (o.sp < lo) OR (o.sp >= hi) THEN
			Console.WriteStr("CopyStack: saved sp outside the exec stack"); Console.WriteLn; HALT(badState)
		END;
		len := S.VAL(INTEGER, execBase - o.sp);
		IF (len <= 0) OR (len > o.segLen) THEN HALT(badState) END;
		o.segLen := len;
		a := o.arena;
		IF a = NIL THEN HALT(badState) END;
		dummy := Libc.memcpy(S.VAL(Libc.PtrVoid, S.VAL(S.ADDRESS, a.lo + o.off)),
			S.VAL(Libc.PtrVoid, o.sp), len)
	END SaveSeg;

	PROCEDURE RestoreSeg (o: Object);
	(* Пост: участок снова на общем стеке; место в арене освобождено. *)
		VAR len: INTEGER; dst: S.ADDRESS; dummy: S.ADDRESS; a: Arena;
	BEGIN
		IF (o.sp < lo) OR (o.sp >= hi) THEN
			Console.WriteStr("CopyStack: resume sp outside the exec stack"); Console.WriteLn; HALT(badState)
		END;
		len := o.segLen;
		a := o.arena;
		IF a = NIL THEN HALT(badState) END;
		dst := o.sp;
		IF mutShift # 0 THEN dst := dst + mutShift END;
		dummy := Libc.memcpy(S.VAL(Libc.PtrVoid, dst),
			S.VAL(Libc.PtrVoid, S.VAL(S.ADDRESS, a.lo + o.off)), len);
		ReleaseSlot(o)
	END RestoreSeg;

	PROCEDURE Step* (): INTEGER;
	(** Исполнить готовые объекты не дольше slice мс. Пост: объекты,
		которые ждут, сохранены в куче; завершённые помечены. *)
		VAR o: Object; n: INTEGER; t0: LONGINT; sp: S.ADDRESS; fresh: BOOLEAN;
	BEGIN
		Init;
		IF depth > 0 THEN HALT(badState) END;
		t0 := Kernel.Time(); n := 0;
		LOOP
			o := Dequeue();
			IF o = NIL THEN EXIT END;
			IF o.state # Ready THEN HALT(badState) END;
			curObj := o;
			fresh := o.sp = 0;
			IF ~fresh THEN RestoreSeg(o) END;
			o.state := Running;
			IF fresh THEN bootObj := o; Seed; sp := spawnSP ELSE sp := o.sp END;
			Kernel.SwitchSave(schedRec);
			IF ~fresh THEN Kernel.RestoreContext(o.ctx) END;
			INC(switches);
			Taskarch.Switch(schedSP, sp);
			Kernel.SwitchRestore(schedRec);
			o := curObj; curObj := NIL;
			IF o.state = Waiting THEN SaveSeg(o) END;
			INC(n);
			IF Kernel.Time() - t0 >= slice THEN EXIT END
		END;
		RETURN n
	END Step;

	PROCEDURE Run* (ms: LONGINT);
	(** Крутить Step, пока есть готовые и не истекло ms. *)
		VAR t0: LONGINT; n: INTEGER;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Kernel.Time() - t0 <= ms) & (readyHead # NIL) DO n := Step() END
	END Run;

	PROCEDURE SetSlice* (ms: LONGINT);
	BEGIN slice := ms END SetSlice;

	(* --- состояние и счётчики --------------------------------------------- *)

	PROCEDURE State* (o: Object): INTEGER;
	BEGIN
		IF o = NIL THEN RETURN New END;
		RETURN o.state
	END State;

	PROCEDURE IsFailed* (o: Object): BOOLEAN;
	BEGIN
		RETURN (o # NIL) & o.failed
	END IsFailed;

	PROCEDURE HasReady* (): BOOLEAN;
	BEGIN
		RETURN readyHead # NIL
	END HasReady;

	PROCEDURE Switches* (): LONGINT;
	BEGIN
		RETURN switches
	END Switches;

	PROCEDURE Waits* (): LONGINT;
	BEGIN
		RETURN nWaits
	END Waits;

	PROCEDURE Stats* (OUT doN, waitN, exitN: LONGINT);
	BEGIN
		doN := nDo; waitN := nWaitGo; exitN := nExit
	END Stats;

	PROCEDURE Finalized* (): LONGINT;
	BEGIN
		RETURN nFinal
	END Finalized;

	PROCEDURE Base* (): S.ADDRESS;
	BEGIN
		RETURN execBase
	END Base;


	PROCEDURE StackLo* (): S.ADDRESS;
	BEGIN
		RETURN lo
	END StackLo;

	PROCEDURE StackHi* (): S.ADDRESS;
	BEGIN
		RETURN hi
	END StackHi;

	PROCEDURE SegLen* (o: Object): INTEGER;
	BEGIN
		IF o = NIL THEN RETURN 0 END;
		RETURN o.segLen
	END SegLen;

	PROCEDURE SetName* (o: Object; IN s: ARRAY OF CHAR);
	BEGIN
		IF o # NIL THEN o.name := s$ END
	END SetName;

	PROCEDURE GetName* (o: Object; OUT s: ARRAY OF CHAR);
	BEGIN
		IF o = NIL THEN s := "" ELSE s := o.name$ END
	END GetName;

	(* --- мутации ---------------------------------------------------------- *)

	PROCEDURE SetMutNoRegister* (on: BOOLEAN);
	BEGIN mutNoRegister := on END SetMutNoRegister;

	PROCEDURE SetMutShift* (bytes: INTEGER);
	BEGIN mutShift := bytes END SetMutShift;

	PROCEDURE SetMutStayAfterTrap* (on: BOOLEAN);
	BEGIN mutStayAfterTrap := on END SetMutStayAfterTrap;

	PROCEDURE Mutations* (OUT noRegister: BOOLEAN; OUT shift: INTEGER; OUT stay: BOOLEAN);
	BEGIN
		noRegister := mutNoRegister; shift := mutShift; stay := mutStayAfterTrap
	END Mutations;

	PROCEDURE MutNoRegisterOn*;
	BEGIN mutNoRegister := TRUE END MutNoRegisterOn;

	PROCEDURE MutShift16*;
	BEGIN mutShift := 16 END MutShift16;

	PROCEDURE MutStayOn*;
	BEGIN mutStayAfterTrap := TRUE END MutStayOn;

	PROCEDURE MutOff*;
	BEGIN mutNoRegister := FALSE; mutShift := 0; mutStayAfterTrap := FALSE END MutOff;

	(* --- отладка ---------------------------------------------------------- *)

	PROCEDURE Dump* (o: Object);
		VAR s: ARRAY 80 OF CHAR;
	BEGIN
		IF o = NIL THEN Console.WriteStr("CopyStack: NIL"); Console.WriteLn; RETURN END;
		s := o.name$;
		Console.WriteStr("CopyStack: "); Console.WriteStr(s);
		Console.WriteStr(" state=");
		IF o.state = New THEN Console.WriteStr("new")
		ELSIF o.state = Ready THEN Console.WriteStr("ready")
		ELSIF o.state = Running THEN Console.WriteStr("running")
		ELSIF o.state = Waiting THEN Console.WriteStr("waiting")
		ELSIF o.state = Finished THEN Console.WriteStr("finished")
		ELSIF o.state = Failed THEN Console.WriteStr("failed")
		ELSE Console.WriteStr("?")
		END;
		Console.WriteLn
	END Dump;

BEGIN
	depth := 0; initialized := FALSE;
	switches := 0; nWaits := 0; nDo := 0; nWaitGo := 0; nExit := 0; nFinal := 0; execBase := 0;
	readyHead := NIL; readyTail := NIL; curObj := NIL; bootObj := NIL;
	arenaHead := NIL; curArena := NIL; arenaCapNext := 1024 * 1024;
	mutNoRegister := FALSE; mutShift := 0; mutStayAfterTrap := FALSE
END CopyStack.
