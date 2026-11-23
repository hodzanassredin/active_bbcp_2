MODULE Test64Regs;
(** Модуль только для дизассемблера (этап 0, вопрос о регистрах).
	Проверяем: сохраняет ли amd64-компилятор живые указатели в слоты стека
	перед вызовом процедуры/NEW (Kernel.NewRec/NewArr), или держит их в
	регистрах через вызов. От этого зависит, достаточно ли MarkLocals
	сканировать только стек (kernel-report §2.4, «Что проверить до начала»).

	Сборка: tools/go64.sh Test64Regs
	Разбор:  tools/ocf.py refs <мир>/BB64LIN/Test64/Code/Regs.ocf
	         tools/ocf.py dis  <ocf> <offset Build> [0x200]  (или objdump). *)

	TYPE Node = POINTER TO RECORD val: INTEGER; next: Node END;

	VAR head: Node;

	PROCEDURE Build* (v: INTEGER): Node;
		VAR a, b: Node;
	BEGIN
		NEW(a); a.val := v;	(* NewRec #1 *)
		NEW(b);	(* NewRec #2: a должен пережить вызов сборщика *)
		b.val := a.val + 1;
		a.next := b;
		RETURN a
	END Build;

	PROCEDURE Chain*;
		VAR p: Node;
	BEGIN
		NEW(p); p.val := 1;
		(* p жив через вызов NewRec внутри Build: p.next := Build(p.val+1) *)
		p.next := Build(p.val + 1);
		head := p
	END Chain;

END Test64Regs.
