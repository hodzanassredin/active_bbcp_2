MODULE System_devStackMoveCode;
(** Задачи для пробы переноса стека (Stackmove). Модуль без SYSTEM и собирается с ключом "%":
	по правилу модулей его кадры переносимы. Каждая задача уходит в рекурсию глубже
	стартового стека и держит на стеке адрес стека своего вида: VAR-параметр на локальную
	верхнего кадра, статическую связь вложенной процедуры, открытый массив, получателя-запись,
	временный адрес посреди выражения. Результат — поле ok. *)

	IMPORT Kernel, Tasks;

	CONST depth* = 300;

	TYPE
		Acc = RECORD sum: INTEGER END;

		Probe* = POINTER TO ABSTRACT RECORD (Tasks.Task)
			ok*: BOOLEAN;
			got*, want*: INTEGER
		END;
		VarT* = POINTER TO RECORD (Probe) END;
		NestT* = POINTER TO RECORD (Probe) END;
		Nest2T* = POINTER TO RECORD (Probe) END;
		ArrT* = POINTER TO RECORD (Probe) END;
		ValArrT* = POINTER TO RECORD (Probe) END;
		RecvT* = POINTER TO RECORD (Probe) END;
		ExprT* = POINTER TO RECORD (Probe) END;
		ArgT* = POINTER TO RECORD (Probe) END;
		GcT* = POINTER TO RECORD (Probe) END;
		TrapT* = POINTER TO RECORD (Probe) END;
		SleepT* = POINTER TO RECORD (Probe) END;
		Box = POINTER TO RECORD v: INTEGER; next: Box END;

	VAR stop*: BOOLEAN;

	(* VAR-параметр: адрес x в кадре верхней процедуры передаётся вниз через все кадры *)
	PROCEDURE DeepVar (VAR x: INTEGER; n: INTEGER);
		VAR pad: ARRAY 8 OF INTEGER;
	BEGIN
		pad[n MOD 8] := n;
		IF n > 0 THEN DeepVar(x, n - 1) END;
		INC(x, pad[n MOD 8] - n + 1)
	END DeepVar;

	PROCEDURE (t: VarT) Run*;
		VAR x: INTEGER;
	BEGIN
		x := 0; DeepVar(x, depth); t.got := x; t.want := depth + 1; t.ok := t.got = t.want
	END Run;

	(* Статическая связь: вложенная процедура меняет локальную внешней *)
	PROCEDURE Outer (n: INTEGER): INTEGER;
		VAR acc: INTEGER;

		PROCEDURE In (k: INTEGER);
			VAR pad: ARRAY 8 OF INTEGER;
		BEGIN
			pad[k MOD 8] := k; INC(acc);
			IF k > 0 THEN In(k - 1) END;
			INC(acc, pad[k MOD 8] - k)
		END In;

	BEGIN
		acc := 0; In(n); RETURN acc
	END Outer;

	PROCEDURE (t: NestT) Run*;
	BEGIN
		t.got := Outer(depth); t.want := depth + 1; t.ok := t.got = t.want
	END Run;

	(* Статическая связь в слоте [FP-8]: каждая процедура с вложенной сохраняет там RBX
	   вызывающего, вложенная зовёт её же глубже; после возврата RBX берётся из слота *)
	PROCEDURE NestDeep (n: INTEGER): INTEGER;
		VAR acc: INTEGER;

		PROCEDURE In;
		BEGIN
			INC(acc);
			IF n > 0 THEN acc := acc + NestDeep(n - 1) END;
			INC(acc)
		END In;

	BEGIN
		acc := 0; In; RETURN acc
	END NestDeep;

	PROCEDURE (t: Nest2T) Run*;
	BEGIN
		t.got := NestDeep(depth); t.want := 2 * (depth + 1); t.ok := t.got = t.want
	END Run;

	(* Открытый массив по ссылке: адрес локального массива верхнего кадра *)
	PROCEDURE SumIn (IN a: ARRAY OF INTEGER; n: INTEGER): INTEGER;
		VAR pad: ARRAY 8 OF INTEGER;
	BEGIN
		pad[n MOD 8] := a[n MOD LEN(a)];
		IF n = 0 THEN RETURN pad[0] END;
		RETURN SumIn(a, n - 1) + pad[n MOD 8]
	END SumIn;

	PROCEDURE (t: ArrT) Run*;
		VAR a: ARRAY 10 OF INTEGER; i, s: INTEGER;
	BEGIN
		FOR i := 0 TO 9 DO a[i] := i + 1 END;
		s := 0; FOR i := 0 TO depth DO s := s + a[i MOD 10] END;
		t.want := s; t.got := SumIn(a, depth); t.ok := t.got = t.want
	END Run;

	(* Открытый массив-значение: копия в своём кадре, слот параметра указывает на неё *)
	PROCEDURE SumVal (a: ARRAY OF INTEGER; n: INTEGER): INTEGER;
	BEGIN
		a[0] := a[0] + 0;
		IF n = 0 THEN RETURN a[0] END;
		RETURN SumVal(a, n - 1) + a[n MOD LEN(a)]
	END SumVal;

	PROCEDURE (t: ValArrT) Run*;
		VAR a: ARRAY 4 OF INTEGER; i, s: INTEGER;
	BEGIN
		FOR i := 0 TO 3 DO a[i] := 2 * i + 1 END;
		s := a[0]; FOR i := 1 TO depth DO s := s + a[i MOD 4] END;
		t.want := s; t.got := SumVal(a, depth); t.ok := t.got = t.want
	END Run;

	(* Получатель-запись на стеке *)
	PROCEDURE (VAR r: Acc) Add (n: INTEGER), NEW;
		VAR pad: ARRAY 8 OF INTEGER;
	BEGIN
		pad[n MOD 8] := n;
		IF n > 0 THEN r.Add(n - 1) END;
		INC(r.sum, pad[n MOD 8] - n + 1)
	END Add;

	PROCEDURE (t: RecvT) Run*;
		VAR r: Acc;
	BEGIN
		r.sum := 0; r.Add(depth); t.got := r.sum; t.want := depth + 1; t.ok := t.got = t.want
	END Run;

	(* Перенос посреди выражения: левая часть уже вычислена, вызов справа растит стек *)
	PROCEDURE Deep (n: INTEGER): INTEGER;
		VAR pad: ARRAY 8 OF INTEGER;
	BEGIN
		pad[n MOD 8] := 1;
		IF n = 0 THEN RETURN 1 END;
		RETURN Deep(n - 1) + pad[n MOD 8]
	END Deep;

	PROCEDURE (t: ExprT) Run*;
		VAR a: ARRAY 3 OF INTEGER; i: INTEGER;
	BEGIN
		a[0] := 7; a[1] := 11; a[2] := 13; i := 1;
		t.got := a[i] * 1000 + Deep(depth) + a[i + 1] * 100000;
		t.want := 11 * 1000 + depth + 1 + 13 * 100000; t.ok := t.got = t.want
	END Run;

	(* Вызов в аргументе вызова: адрес VAR-аргумента x положен на стек до вызова Deep *)
	PROCEDURE Put (VAR x: INTEGER; v: INTEGER);
	BEGIN
		x := v
	END Put;

	PROCEDURE (t: ArgT) Run*;
		VAR x: INTEGER;
	BEGIN
		x := 0; Put(x, Deep(depth)); t.got := x; t.want := depth + 1; t.ok := t.got = t.want
	END Run;

	(* Сборка мусора после переноса: указатели в локальных перенесённых кадров *)
	PROCEDURE Chain (n: INTEGER; prev: Box): INTEGER;
		VAR b: Box; s: INTEGER;
	BEGIN
		NEW(b); b.v := n; b.next := prev;
		IF n = 0 THEN
			Kernel.Collect;
			s := 0; WHILE b # NIL DO INC(s, b.v); b := b.next END;
			RETURN s
		END;
		RETURN Chain(n - 1, b)
	END Chain;

	PROCEDURE (t: GcT) Run*;
	BEGIN
		t.got := Chain(depth, NIL); t.want := depth * (depth + 1) DIV 2; t.ok := t.got = t.want
	END Run;

	(* Трап после переноса *)
	PROCEDURE Fall (n: INTEGER);
		VAR pad: ARRAY 8 OF INTEGER;
	BEGIN
		pad[n MOD 8] := n;
		IF n = 0 THEN HALT(42) END;
		Fall(n - 1); INC(pad[0])
	END Fall;

	PROCEDURE (t: TrapT) Run*;
	BEGIN
		t.ok := TRUE; Fall(depth)
	END Run;

	(* Задача спит с адресом своей локальной в кадре, пока другая переносит стек *)
	PROCEDURE Hold (VAR x: INTEGER; n: INTEGER);
	BEGIN
		IF n > 0 THEN Hold(x, n - 1)
		ELSE
			WHILE ~stop DO Tasks.Sleep(1) END;
			INC(x)
		END
	END Hold;

	PROCEDURE (t: SleepT) Run*;
		VAR x: INTEGER;
	BEGIN
		x := 41; Hold(x, 3); t.got := x; t.want := 42; t.ok := t.got = t.want
	END Run;

END System_devStackMoveCode.
