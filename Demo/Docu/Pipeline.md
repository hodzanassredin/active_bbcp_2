# DemoPipeline — pipeline with fan-out and fan-in

## Source

Sameer Ajmani, *Go Concurrency Patterns: Pipelines and Cancellation*,
Go Blog, 2014:

<https://go.dev/blog/pipelines>

and Rob Pike's fan-in slide:

<https://go.dev/talks/2012/concurrency.slide#33>

The Go version connects goroutines with channels: generator → square →
fan-out to two workers → fan-in.

## Translation

Every stage is a `Tasks.Task`; channels are `AsyncChan.Chan` rendezvous
channels. The end of a stream is an `IntMsg` with `n = -1` (there is no
`close`). The dispatcher distributes the numbers round-robin; two
merger tasks add the results into shared counters. The order of the
results is not fixed, the sum is.

## Run

```sh
echo 'DemoPipeline.Run' | tools/run64.sh
```

Output:

```
DemoPipeline: count=10 sum=385
```

## Test

`DemoTests.TestProcPipeline` checks 10 results and
1²+…+10² = 385.
