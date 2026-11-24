# DemoSieve — sieve of Eratosthenes on channels

## Source

Rob Pike, *Go Concurrency Patterns* (Google, 2012), "Sieve of
Eratosthenes": every stage receives the first number as a prime and
passes on the numbers not divisible by it.

<https://go.dev/talks/2012/concurrency.slide#48>

The same algorithm in Active Oberon is appendix C.2 of P. Reali,
*Active Oberon Language Report* — there the stages are active objects
connected by bounded buffers. This example follows the Go structure,
where the stages are tasks and the channels are rendezvous.

## Translation

A `Sieve` task receives its first number, records it as a prime,
creates the next `Sieve` with a fresh channel, and forwards every
number not divisible by its prime. The sentinel `-1` travels to the end
of the chain and stops every stage. The generator sends 2…99.

## Run

```sh
echo 'DemoSieve.Run' | tools/run64.sh
```

Output:

```
DemoSieve: primes=25 sum=1060 max=97
```

## Test

`DemoTests.TestProcSieve` checks 25 primes, sum 1060, largest 97 and
no traps.
