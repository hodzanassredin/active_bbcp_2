# DemoReaderWriter — readers and writers

## Source

P. Reali, *Active Oberon Language Report*, ETH Zurich, 27 October 2004,
appendix B.1 "Readers and Writers".

<https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf>

The original monitor state is:

```
n = 0  empty
n < 0  n writers
n > 0  n readers
```

with `EnterReader` waiting for `n >= 0`, `EnterWriter` for `n = 0`.

## Translation

`{EXCLUSIVE}` became `[exclusive]`. The demonstration adds
instrumentation (`readersInside`, `writersInside`, `Bad`) around the
critical section: a reader that sees a writer inside, or a writer that
sees a reader inside, increments `Bad`. Three readers make five passes
and two writers make three passes.

## Run

```sh
echo 'DemoReaderWriter.Run' | tools/run64.sh
```

Output:

```
DemoReaderWriter: reads=15 writes=6 bad=0
```

## Test

`Test64Demo.TestProcReadersWriters` checks 15 reads, 6 writes, no
overlap and no traps.
