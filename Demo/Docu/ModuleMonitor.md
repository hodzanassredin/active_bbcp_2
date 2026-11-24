# DemoModuleMonitor — producer/consumer on a module monitor

## Source

The producer/consumer example with `{EXCLUSIVE}` procedures from the ETH
*System Construction* course:

<https://lec.inf.ethz.ch/syscon/2019/download/syscon06.pdf>

```
PROCEDURE Produce (x: object);
BEGIN {EXCLUSIVE} AWAIT(free # 0); DEC(free);
	buf[tail] := x; tail := (tail + 1) MOD N; INC(available) END;
```

## Translation

The procedures are top-level procedures of the module, marked
`[exclusive]`. All `[exclusive]` top-level procedures of a module share
one hidden monitor object — the **module monitor** — so `Produce` and
`Consume` mutually exclude each other without any explicit `Lock`.
Three producers and three consumers move 40 items each.

## Run

```sh
echo 'DemoModuleMonitor.Run' | tools/run64.sh
```

Output:

```
DemoModuleMonitor: produced=120 consumed=120 sum=7260
```

## Test

`Test64Demo.TestProcModuleMonitor` checks 120 produced, 120 consumed,
sum 7260 and no traps.
