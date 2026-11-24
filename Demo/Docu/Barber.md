# DemoBarber — sleeping barber

## Source

The classical sleeping-barber monitor (Dijkstra's problem) as presented
in the ETH *System Construction* course, monitor "Barber Shop" with the
conditions `barberAvailable`, `chairOccupied`, `doorOpen` and
`clientLeft`:

<https://lec.inf.ethz.ch/syscon/2019/download/syscon06.pdf>

The Active Oberon semantics of `AWAIT` are described in P. Reali,
*Active Oberon Language Report*, §3.3: a boolean condition replaces
condition variables and `signal`; conditions are re-evaluated when a
task leaves the monitor.

## Translation

Each `while cond do wait` became one `ACTIVE.AWAIT(cond)`; the monitor
is the `Shop` object and its methods are `[exclusive]`. The state is:

- `barbers` — the barber is free (1) or busy (0);
- `waiting` — customers on the waiting chairs;
- `serving` — a customer has taken the barber;
- `open` — the haircut is finished but the customer has not left yet.

The demonstration starts six customers before the barber: the first
three take the chairs, the other three leave. The barber serves the
three seated customers.

## Run

```sh
echo 'DemoBarber.Run' | tools/run64.sh
```

Output:

```
DemoBarber: served=3 turnedAway=3
```

## Test

`Demo_testsExamples.TestProcBarber` checks 3 served, 3 turned away, no traps
and no deadlock.
