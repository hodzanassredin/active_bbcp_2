# Demo subsystem — map

`Demo` contains small, runnable translations of well-known concurrency
examples. Every example is a separate module with a parameterless
command; every command has a Unit test in `Test64/Mod/Demo.cp` with a
deterministic result.

All examples use the extensions of this branch:

- `Tasks` — cooperative tasks with their own stack;
- `Kernel.Lock/Unlock/Wait/Await` and `[exclusive]` / `ACTIVE.AWAIT`;
- `Async` — channels (`AsyncChan`), `Select` (`AsyncSelect`) and the
  asynchronous `Services` timers.

The examples are built for **AMD64** only. The channel library
(`Async/Mod`) and therefore the Go-style examples are not built for
i386; tasks, monitors and `ACTIVE.AWAIT` themselves also work on i386
(see `Docu/Async.md`).

## Building and running

```sh
tools/build64.sh Demo          # DemoSupport and all examples
echo 'DemoBuffer.Run' | tools/run64.sh
tools/test.sh Test64Demo       # Unit tests of all examples
tools/test-demo-gui.sh         # headless window check of DemoAnts
```

`tools/build64.sh` with no arguments builds `Demo` too (`Demo` is in
`DEFAULT_SUBSYSTEMS`). `tools/test.sh` runs `Test64Demo` from
`Test64/tests.txt` and the separate `Test64DemoGui` stage.

## Examples

### Active Oberon

| Module | Command | Source |
|---|---|---|
| `DemoBuffer` | `DemoBuffer.Run` | Reali, *Active Oberon Language Report*, B.6 "Bounded Buffer" |
| `DemoPhilo` | `DemoPhilo.Run` | Reali, C.1 "Dining Philosophers" and B.4 "Semaphores" |
| `DemoReaderWriter` | `DemoReaderWriter.Run` | Reali, B.1 "Readers and Writers" |
| `DemoBarber` | `DemoBarber.Run` | Klemm/ETH *System Construction* monitor "Barber Shop", translated to `AWAIT` |
| `DemoModuleMonitor` | `DemoModuleMonitor.Run` | *System Construction*, producer/consumer with `{EXCLUSIVE}` |

### Go

| Module | Command | Source |
|---|---|---|
| `DemoPingPong` | `DemoPingPong.Run` | R. Pike, *Go Concurrency Patterns*, ping-pong |
| `DemoPipeline` | `DemoPipeline.Run` | S. Ajmani, *Go Concurrency Patterns: Pipelines and Cancellation*; Pike's fan-in |
| `DemoSieve` | `DemoSieve.Run` | Pike, *Sieve of Eratosthenes*; also Reali C.2 |
| `DemoSelectTimeout` | `DemoSelectTimeout.Run` | *Tour of Go* "Select"; gobyexample "Timeouts" |
| `DemoWorkerPool` | `DemoWorkerPool.Run` | gobyexample "Worker Pools" |
| `DemoBoring` | `DemoBoring.Run` | Pike, *boring* generator and multiplexer |
| `DemoGroup` | `DemoGroup.Run` | Ajmani, cancellation; `golang.org/x/sync/errgroup` |

### Coordination with a view

| Module | Commands | Description |
|---|---|---|
| `DemoAnts` | `DemoAnts.Open`, `Start`, `Stop`, `Reset`, `Run` | 200 ants on a 48×32 field: food, nest, pheromone; field monitor, evaporator task, `Views.View` and a `Services` timer |

`DemoSupport` is the shared helper (task pump and console output); it has
no command of its own. `MD5` is the pre-existing hashing example and is
not part of the concurrency set.

## Tests

| Test | Checks |
|---|---|
| `Test64Demo.TestProcBoundedBuffer` | 120 items, sum 2460 |
| `Test64Demo.TestProcPhilosophers` | 15 meals, no traps |
| `Test64Demo.TestProcReadersWriters` | 15 reads, 6 writes, no overlap |
| `Test64Demo.TestProcBarber` | 3 served, 3 turned away |
| `Test64Demo.TestProcModuleMonitor` | 120 items, sum 7260 |
| `Test64Demo.TestProcPingPong` | 10 hits, sum 45 |
| `Test64Demo.TestProcPipeline` | 10 results, sum 385 |
| `Test64Demo.TestProcSieve` | 25 primes, sum 1060, max 97 |
| `Test64Demo.TestProcSelectTimeout` | timeout index 1, receive index 0, value 42 |
| `Test64Demo.TestProcWorkerPool` | 9 results, sum 285 |
| `Test64Demo.TestProcBoring` | 16 messages, 8 from each generator |
| `Test64Demo.TestProcGroupCancel` | one error, all members stopped, group empty |
| `Test64Demo.TestProcAnts` | food > 0 in 150 steps, 200 ants, no deadlock |

The headless GUI stage `tools/test-demo-gui.sh` opens the `DemoAnts`
window under `gtk4-broadwayd`, logs an ASCII frame every 10 timer ticks
and requires `DemoAnts: ok=1` (ticks, food, frames, live ants). No mouse
is emulated and no user display is used.
