# DemoBuffer — bounded buffer

## Source

P. Reali, *Active Oberon Language Report*, ETH Zurich, 27 October 2004,
appendix B.6 "Bounded Buffer".

<https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf>

The original is a circular buffer whose `Put` and `Get` methods are
`{EXCLUSIVE}` and wait on `AWAIT` conditions:

```
PROCEDURE Put* (i: INTEGER);
BEGIN {EXCLUSIVE} AWAIT ((in + 1) MOD BufLen # out);
	data[in] := i; in := (in + 1) MOD BufLen END Put;
```

## Translation

`{EXCLUSIVE}` became `[exclusive]`, `AWAIT` became `ACTIVE.AWAIT`.
The `& Init` initialiser is not needed: `NEW` zeroes `in` and `out`.
`BufLen` is 8 as in the original; the demonstration uses three
producers and three consumers with 40 items each.

## Run

```sh
echo 'DemoBuffer.Run' | tools/run64.sh
```

Output:

```
DemoBuffer: produced=120 consumed=120 sum=2460
```

`DemoBuffer.Buffer` can also be used directly from other modules;
`Put` and `Get` are exported `[exclusive]` methods.

## Test

`Test64Demo.TestProcBoundedBuffer` checks 120 produced, 120 consumed,
sum 2460 and no task traps.
