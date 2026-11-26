# Обзор на примерах: DemoSieve

Решето Эратосфена на каналах.

Каждая стадия решета получает первое число как простое и передаёт дальше только числа, которые на
него не делятся. Цепочка стадий растёт по мере того, как находятся простые. Пример взят из R. Pike,
*Go Concurrency Patterns* (<https://go.dev/talks/2012/concurrency.slide#48>); в Active Oberon тот
же алгоритм — приложение C.2 P. Reali, *Active Oberon Language Report*, где стадии соединены
ограниченными буферами. Здесь — структура Go: стадии — задачи, каналы — встречи.

```
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
	с цепочкой задач Tasks.Job и каналами-встречами, как в Go.

	Команда DemoSieve.Run: числа 2..99. Итог детерминирован:
	25 простых, сумма 1060, максимум 97. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, DemoSupport;

	CONST
		limit = 100;	(* проверяем 2..limit-1 *)

	TYPE
		IntMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Sieve = POINTER TO RECORD (Tasks.Job)
			in: AsyncChan.Chan;
			prime: INTEGER
		END;

		Generator = POINTER TO RECORD (Tasks.Job) out: AsyncChan.Chan END;

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

	PROCEDURE (s: Sieve) Body- (VAR ctx: Tasks.Ctx);
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
	END Body;

	PROCEDURE (g: Generator) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		FOR i := 2 TO limit - 1 DO g.out.Send(NewInt(i)) END;
		g.out.Send(NewInt(-1))
	END Body;

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
```

Модуль собирает `tools/build64.sh` (исходник — `Demo/Mod/Sieve.cp`; подсистема `Demo` — только для
amd64). В окне BlackBox команду исполняет коммандер:

```
   DemoSieve.Run
```

В консоли:

```sh
echo 'DemoSieve.Run' | tools/run64.sh
```

В журнале появится:

```
DemoSieve: primes=25 sum=1060 max=97
```

## Что произошло

Генератор посылает числа 2…99. Задача `Sieve` получает первое число, запоминает его как простое,
создаёт следующую `Sieve` со свежим каналом и передаёт ей каждое число, не делящееся на своё
простое. Признак `-1` проходит всю цепочку и останавливает каждую стадию.

Итог — 25 простых, их сумма 1060, наибольшее 97.

## Тест

`Demo_devExamples.TestProcSieve` проверяет 25 простых, сумму 1060, наибольшее 97 и отсутствие трапов.

В этом примере мы увидели задачи, которые создают задачи, и цепочку каналов, растущую во
время работы.
