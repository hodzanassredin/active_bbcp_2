# DemoPhilo — dining philosophers

## Source

P. Reali, *Active Oberon Language Report*, ETH Zurich, 27 October 2004,
appendix C.1 "Dining Philosophers" and B.4 "Binary and Generic
Semaphores".

<https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf>

The original uses an active object with a `{ACTIVE} LOOP` body and
binary semaphores implemented with `AWAIT`:

```
PROCEDURE P*; BEGIN {EXCLUSIVE} AWAIT(~taken); taken := TRUE END P;
PROCEDURE V*; BEGIN {EXCLUSIVE} taken := FALSE END V;
```

## Translation

The active body became the `Run` method of a `Tasks.Task`; `Sem` is a
record with `[exclusive]` methods `P` and `V`. The original asymmetric
fork order is preserved: philosopher `N-1` takes fork 0 first, so the
circular wait is broken. `Think` and `Eat` are `Tasks.Yield` points.

## Run

```sh
echo 'DemoPhilo.Run' | tools/run64.sh
```

Output:

```
DemoPhilo: eaten=15
```

## Test

`DemoTests.TestProcPhilosophers` checks five philosophers × three
meals = 15 and no traps.
