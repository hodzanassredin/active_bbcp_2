# DemoBoring — boring generator and multiplexer

## Source

Rob Pike, *Go Concurrency Patterns* (Google, 2012), the "boring"
generator and the multiplexer (fan-in):

<https://go.dev/talks/2012/concurrency.slide#25>

<https://go.dev/talks/2012/concurrency.slide#30>

In Go the multiplexer is a `select` over two input channels.

## Translation

Two generator tasks send eight numbered messages each and then a
sentinel. The multiplexer task rebuilds two receive alternatives on
every iteration and calls `AsyncChan.Select`; it counts the messages
from each generator and stops after both sentinels. This example
exercised a defect in `AsyncSelect.Select` (a decision on the first
channel while the selecting task waited for the monitor of the second
channel); the fix is in `Async/Mod/Select.cp` and the regression test is
`AsyncTestsChannels.TestProcSelectWhileMonitorWait`.

## Run

```sh
echo 'DemoBoring.Run' | tools/run64.sh
```

Output:

```
DemoBoring: messages=16 fromA=8 fromB=8
```

## Test

`DemoTests.TestProcBoring` checks 16 messages, 8 from each generator
and no traps.
