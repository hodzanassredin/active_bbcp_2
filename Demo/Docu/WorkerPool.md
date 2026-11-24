# DemoWorkerPool — worker pool

## Source

gobyexample "Worker Pools":

<https://gobyexample.com/worker-pools>

The Go version has a `jobs` channel, a `results` channel and several
worker goroutines; each worker is stopped by a sentinel.

## Translation

The jobs channel is a bounded `AsyncChan.Buffer(3)`, the results
channel is an unbounded `AsyncChan.Mailbox` (so workers never block on
results). Three worker tasks square a number, a controller task sends
nine jobs and three sentinels and then collects nine results. The
order of the results is not fixed; their sum is.

## Run

```sh
echo 'DemoWorkerPool.Run' | tools/run64.sh
```

Output:

```
DemoWorkerPool: workers=3 count=9 sum=285
```

## Test

`Test64Demo.TestProcWorkerPool` checks three workers, nine results and
1²+…+9² = 285.
