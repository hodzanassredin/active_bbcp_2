MODULE DemoSieve;
(** Решето Эратосфена на каналах.

	Перенесено из доклада Rob Pike "Go Concurrency Patterns" (Google,
	2012), слайд "Sieve of Eratosthenes": каждая стадия — горутина,
	которая принимает первое число как простое и передаёт дальше все
	не кратные ему. Источник:
	  https://go.dev/talks/2012/concurrency.slide#48
	В Active Oberon тот же алгоритм есть в Reali, Active Oberon Language
	Report, C.2 "Sieve of Eratosthenes" — там стадии соединены
	ограниченными буферами и активными объектами. Здесь взят вариант
	с цепочкой задач Tasks и каналами-встречами, как в Go.

	Команда DemoSieve.Run: числа 2..99. Итог детерминирован:
	25 простых, сумма 1060, максимум 97. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, DemoSupport;

	CONST
		limit = 100;	(* проверяем 2..limit-1 *)

	TYPE
		IntMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Sieve = POINTER TO RECORD (Tasks.Task)
			in: AsyncChan.Chan;
			prime: INTEGER
		END;

		Generator = POINTER TO RECORD (Tasks.Task) out: AsyncChan.Chan END;

	VAR
		Primes*: INTEGER;
		Sum*: INTEGER;
		MaxPrime*: INTEGER;
		Failed*: BOOLEAN;
		first: Sieve;
		gen: Generator;

	PROCEDURE NewInt (n: INTEGER): IntMsg;
		VAR m: IntMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END NewInt;

	PROCEDURE (s: Sieve) Run;
		VAR m: AsyncSelect.Msg; n: INTEGER;
			out: AsyncChan.Chan; next: Sieve; prime: INTEGER;
	BEGIN
		s.in.Recv(m);
		WITH m: IntMsg DO prime := m.n ELSE prime := -1 END;
		s.prime := prime;
		IF prime < 0 THEN RETURN END;
		INC(Primes); INC(Sum, prime);
		IF prime > MaxPrime THEN MaxPrime := prime END;
		out := AsyncChan.NewChan();
		NEW(next); next.in := out; Tasks.Start(next);
		LOOP
			s.in.Recv(m);
			WITH m: IntMsg DO n := m.n ELSE n := -1 END;
			IF n < 0 THEN EXIT END;
			IF n MOD prime # 0 THEN out.Send(NewInt(n)) END
		END;
		out.Send(NewInt(-1))	(* конец цепочки *)
	END Run;

	PROCEDURE (g: Generator) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 2 TO limit - 1 DO g.out.Send(NewInt(i)) END;
		g.out.Send(NewInt(-1))
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: цепочка решёт для 2..99. *)
		VAR src: AsyncChan.Chan; ok: BOOLEAN;
	BEGIN
		Primes := 0; Sum := 0; MaxPrime := 0; Failed := FALSE;
		src := AsyncChan.NewChan();
		NEW(first); first.in := src;
		NEW(gen); gen.out := src;
		Tasks.Start(first); Tasks.Start(gen);
		ok := DemoSupport.Pump(15000);
		Failed := ~ok OR Tasks.Failed(first) OR Tasks.Failed(gen);
		DemoSupport.LogInt("DemoSieve: primes=", Primes);
		DemoSupport.LogInt(" sum=", Sum);
		DemoSupport.LogInt(" max=", MaxPrime);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoSieve.
