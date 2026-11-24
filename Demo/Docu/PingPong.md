# DemoPingPong — ping-pong over one channel

## Source

Rob Pike, *Go Concurrency Patterns* (Google, 2012), the ping-pong
example: two goroutines exchange a ball over a shared channel.

<https://go.dev/talks/2012/concurrency.slide#14>

## Translation

The two goroutines are `Tasks.Task` objects. The ball is an
`AsyncSelect.Msg` (a pointer to an extensible record) and the table is
an `AsyncChan.Chan` — a rendezvous, so every send waits for the receive.
A ball with `n = -1` ends the game. A new message is allocated for each
throw; a message is never modified after sending.

## Run

```sh
echo 'DemoPingPong.Run' | tools/run64.sh
```

Output:

```
DemoPingPong: hits=10 sum=45
```

## Test

`Demo_devExamples.TestProcPingPong` checks 10 hits and the sum 0+…+9 = 45.
