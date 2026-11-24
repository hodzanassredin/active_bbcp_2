# DemoSelectTimeout — select with a timeout

## Source

*Tour of Go*, "Select" and "Default Selection":

<https://go.dev/tour/concurrency/5>

and gobyexample "Timeouts":

<https://gobyexample.com/timeouts>

In Go the timeout is `case <-time.After(d)` in a `select`.

## Translation

`AsyncChan.Select` takes an array of alternatives. A receive is
`AsyncChan.RecvAlt`, a timeout is `AsyncChan.TimeoutAlt`; `Select`
returns the index of the alternative that fired and the received
message is in `ops[i].got`. The example checks both cases:

1. no sender — the 30 ms timeout wins (index 1);
2. a sender after 5 ms — the receive wins (index 0, value 42).

## Run

```sh
echo 'DemoSelectTimeout.Run' | tools/run64.sh
```

Output (times vary within a few milliseconds):

```
DemoSelectTimeout: timeoutIdx=1 timeoutMs=30 gotIdx=0 gotMs=5 value=42
```

## Test

`Demo_testsExamples.TestProcSelectTimeout` checks the indices, the value 42 and
that the timeout waited at least 20 ms.
