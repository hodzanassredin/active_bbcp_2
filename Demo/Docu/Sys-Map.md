# Demo subsystem — map

`Demo` contains small, runnable translations of well-known concurrency
examples. Every example is a separate module with a parameterless
command; every command has a Unit test in `Demo/Mod/Tests.cp` with a
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
tools/test.sh DemoTests       # Unit tests of all examples
tools/test-demo-gui.sh         # headless window check of DemoAnts
```

`tools/build64.sh` with no arguments builds `Demo` too (`Demo` is in
`DEFAULT_SUBSYSTEMS`). `tools/test.sh` runs `DemoTests` from
`tools/tests.txt` and the separate `DemoAntsGui` stage
(`tools/test-demo-gui.sh`).

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
| `DemoAnts` | `DemoAnts.Open`, `Start`, `Stop`, `Pause`, `Resume`, `Step`, `Faster`, `Slower`, `Reset`, `Run`, `Simulate`, `Bench` | 1000 ants on a 160×100 field with four walls and passages: food, nest, pheromone; model speed 1–200 steps/s (default 10) separate from redraw; field monitor, evaporator task, `Views.View` and a `Services` timer |

`DemoSupport` is the shared helper (task pump and console output); it has
no command of its own. `MD5` is the pre-existing hashing example and is
not part of the concurrency set.

## Tests

| Test | Checks |
|---|---|
| `DemoTests.TestProcBoundedBuffer` | 120 items, sum 2460 |
| `DemoTests.TestProcPhilosophers` | 15 meals, no traps |
| `DemoTests.TestProcReadersWriters` | 15 reads, 6 writes, no overlap |
| `DemoTests.TestProcBarber` | 3 served, 3 turned away |
| `DemoTests.TestProcModuleMonitor` | 120 items, sum 7260 |
| `DemoTests.TestProcPingPong` | 10 hits, sum 45 |
| `DemoTests.TestProcPipeline` | 10 results, sum 385 |
| `DemoTests.TestProcSieve` | 25 primes, sum 1060, max 97 |
| `DemoTests.TestProcSelectTimeout` | timeout index 1, receive index 0, value 42 |
| `DemoTests.TestProcWorkerPool` | 9 results, sum 285 |
| `DemoTests.TestProcBoring` | 16 messages, 8 from each generator |
| `DemoTests.TestProcGroupCancel` | one trapped, three cancelled, group failed and empty |
| `DemoTests.TestProcAnts` | 1000 ants × 300 steps: food > 0, count preserved, all finished |
| `DemoTests.TestProcAntsWalls` | ≥ 3 walls, no ant in a wall, walls deflect moves |
| `DemoTests.TestProcAntsReachable` | BFS from the nest reaches every food cell |
| `DemoTests.TestProcAntsSpeed` | `Faster` clamps at 200, `Slower` at 1 |
| `DemoTests.TestProcAntsFoodWalls` | 200 ants × 1200 steps: food delivered around the walls |
| `DemoTests.TestProcAntsMany` | 1000 ants finish, `Tasks.Count() = 0`, no traps |
| `DemoTests.TestProcAntsPauseStep` | `Pause` + `Step` performs exactly one round |

The headless GUI stage `tools/test-demo-gui.sh` opens the `DemoAnts`
window under `gtk4-broadwayd`, logs an ASCII frame every 40 redraw ticks
and requires `DemoAnts: ok=1` (ticks, food, frames, frame time, live
ants). No mouse is emulated and no user display is used. The window is
~1000×650 px, the cell size follows the window and is at least 3 px.
