# DemoAnts — many coordinating objects with a view

`DemoAnts` is the multi-object example of the Demo subsystem: 1000 ant
tasks live on a 160×100 field with walls, search for food, carry it to
the nest and leave a pheromone trail that a separate task evaporates.
The field is the monitor; the window is a `Views.View`.

## Model

- `Field` is a record with `food`, `pher`, `wall`, ant positions and
  counters. Its `[exclusive]` methods `Step` and `Evaporate` are the
  only access to the arrays — the whole field is one monitor.
- Every ant is a `Tasks.Task`: `Ant.Run` performs one `Field.Step`,
  then sleeps `1000 DIV speed` ms and repeats. The default speed is
  10 steps per second, so a single ant is easy to follow; the model
  speed is independent of the redraw rate. Two thirds of the ants
  start in the nest, one third is already scattered over free cells,
  so the picture reads at once.
- `Evaporator` is one more task: every 120 ms it decrements every
  pheromone cell. A saturated trail (`maxPher = 100`) disappears in
  about 12 s, an abandoned one fades within 10–20 s.
- `Restore` only reads the field and draws. The view does not own the
  simulation; `Services.Action` ticks every 66 ms and calls
  `Views.Update` (an effective ~10 frames/s under the GTK4 host).
- Because the system is cooperatively single-threaded, `Restore` runs
  between scheduler steps and reads a consistent field without taking
  the monitor.

## Walls and food

Four walls with passages separate the nest from the food:

| Wall | Cells | Passage |
|---|---|---|
| left vertical | `x = 54`, `y = 0..99` | `y = 42..54` |
| right vertical | `x = 106`, `y = 0..99` | `y = 46..58` |
| upper horizontal | `y = 25`, `x = 58..102` | `x = 86..96` |
| lower horizontal | `y = 75`, `x = 58..102` | `x = 62..72` |

Six food patches (radius 4) lie beyond the walls: four corners and the
top/bottom centre. The straight line from the nest to every patch
crosses a wall, so the visible pheromone trail goes along the wall and
through a passage. Walls are impassable, and an ant sniffs pheromone
only in adjacent **free** cells: a wall hides the trail behind it.

`Reset` builds the walls first and puts food only into free cells; the
nest cell is never a wall. `FoodReachable` runs a breadth-first search
from the nest over free cells and checks that every food cell is
reachable (used by the Unit test).

## Commands

```
DemoAnts.Open      (* GUI: create the field, start ants, open the window *)
DemoAnts.Start     (* start / resume from pause; restarts after Stop *)
DemoAnts.Stop      (* terminate the ant and evaporator tasks *)
DemoAnts.Pause     (* freeze the model, keep tasks and window *)
DemoAnts.Resume    (* continue after Pause *)
DemoAnts.Step      (* one model step for every ant, only while paused *)
DemoAnts.Faster    (* speed ×2, at most 200 steps/s *)
DemoAnts.Slower    (* speed ÷2, at least 1 step/s *)
DemoAnts.Reset     (* 1000 ants, food restored, pheromone cleared *)
DemoAnts.Run       (* console: 1000 ants, 300 steps, reduced ASCII frames *)
DemoAnts.Simulate  (* API: Simulate(ants, steps) — no output, for Unit *)
DemoAnts.Bench     (* measurement: 1000 ants, 200 steps *)
```

`Simulate(ants, steps)` is unchanged: it runs `ants × steps` ant moves
as fast as possible (no pacing), stops all tasks, and returns the food
delivered to the nest. `DemoAnts.SetSpeed(n)` is the API behind
`Faster`/`Slower`; `DemoAnts.Speed()` reads the current value.

## Legend

| Element | Appearance |
|---|---|
| background | off-white `00F8F8F4H` |
| wall | dark grey `0034343CH` |
| nest | olive blob (radius 4 cells) with a darker core |
| food | green, pale `00AAEBBEH` (nearly eaten) → deep `00147814H` (full) |
| pheromone | blue, pale `00FFF0E1H` (weak) → deep `00C83C14H` (saturated) |
| ant without food | dark brown `00142332H` |
| ant carrying food | orange-red `001450E6H` |

The status line at the top shows `ants=`, `step=` (moves per ant),
`food=`, `speed=` (steps/s) and `paused`/`stopped` when applicable.

## Window and cell size

The default view size is `265 × 172 Ports.mm` (about 1000×650 px at
96 dpi). The cell is square and chosen as
`min((r-l) DIV W, (b-t) DIV H)` after a 6 mm status strip; it is never
smaller than 3 px. The field is centred and stretched to the window.

## Console frame

`DemoAnts.Run` prints a reduced frame (every 2nd cell: 80×50) every 100
steps. Nest `#`, ant `*`, wall `X`, food `+`, pheromone `.`, free cell
space. The same `PrintFrame` is used by the headless GUI check.

## Headless GUI check

`tools/test-demo-gui.sh` starts `gtk4-broadwayd` in a private runtime
directory, runs `blackbox --BB_RUN=DemoAnts.GuiCheck`, and requires
`DemoAnts: ok=1`. `GuiCheck` opens the window, logs an ASCII frame and
the counters (including `frame_ms`, `elapsed_ms`, `steps/s`) every 40
redraw ticks, and after 400 ticks checks that the timer ran, food grew,
all 1000 ants are alive and there is no deadlock. No mouse is emulated
and the user's display is not touched.

Measured with 1000 ants (default speed 10):

| What | Result |
|---|---|
| model step, 200k moves (`Bench`) | 248 ms, 1.24 µs per ant move; a full 1000-ant round ≈ 1.24 ms |
| frame (`Restore`, 160×100 + 1000 ants) | 309 ms for 407 frames → 0.76 ms/frame |
| effective speed | 9 ant steps/s at `speed=10` |
| first food delivered | ~10 s; 71 food by 40 s |
| tasks | 1000 ant tasks + 1 evaporator, all alive, no deadlock |

## Test

`Test64Demo` (marks `proc`) checks the ants:

- `TestProcAnts` — `Simulate(1000, 300)`: food delivered, 1000 ants,
  all finished, no traps, no deadlock;
- `TestProcAntsWalls` — at least three walls, no ant inside a wall, no
  wall violation, and walls really deflected moves (`WallHits > 0`);
- `TestProcAntsReachable` — breadth-first search from the nest reaches
  every food cell;
- `TestProcAntsSpeed` — `Faster` stops at 200, `Slower` at 1;
- `TestProcAntsFoodWalls` — 200 ants, 1200 steps: food is delivered
  around the walls;
- `TestProcAntsMany` — 1000 ants finish and are reaped:
  `Tasks.Count() = 0`, no traps;
- `TestProcAntsPauseStep` — `Pause` + `Step` performs exactly one round.

The exact amount of food is not asserted: the scheduler and the
real-time evaporator make it depend on the machine, but the outcome
class (food grows, ants survive, no deadlock) is deterministic.

## AMD64 only

The `Demo` subsystem is built for AMD64. The task and monitor core also
works on i386, but the `Async` library (channels) is not built there;
`DemoAnts` itself needs only `Tasks`, `Views` and `Services`, but it is
part of the same subsystem and is built with the rest of `Demo`.
