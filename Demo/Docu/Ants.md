# DemoAnts — many coordinating objects with a view

`DemoAnts` is the multi-object example of the Demo subsystem: 200–1000
ants live on a 48×32 field, search for food, carry it to the nest and
leave a pheromone trail that a separate task evaporates. The field is
the monitor; the window is a `Views.View`.

## Model

- `Field` is a record with `food`, `pher`, ant positions and counters.
  Its `[exclusive]` methods `Step` and `Evaporate` are the only access
  to the arrays — the whole field is one monitor.
- Every ant is a `Tasks.Task`: `Ant.Run` calls `Field.Step` once,
  checks the stop condition and `Tasks.Yield`s. 200 ants therefore mean
  200 stacks and 200 scheduler entries.
- `Evaporator` is one more task: every 2 ms it decrements every
  pheromone cell.
- `Restore` only reads the field and draws: food (green), pheromone
  (light grey), the nest (olive) and ants (black dots). The view does
  not own the simulation; `Services.Action` ticks every 100 ms and calls
  `Views.Update`.
- Because the system is cooperatively single-threaded, `Restore` runs
  between scheduler steps and reads a consistent field without taking
  the monitor.

## Commands

```
DemoAnts.Open      (* GUI: create the field, start ants, open the window *)
DemoAnts.Start
DemoAnts.Stop
DemoAnts.Reset     (* 200 ants, food patches restored, pheromone cleared *)
DemoAnts.Run       (* console: 200 ants, 100 steps, ASCII frame every 25 *)
DemoAnts.Simulate  (* API: Simulate(ants, steps) — no output, for Unit *)
```

The console command prints four ASCII frames (nest `#`, ant `*`, food
`+`, pheromone `.`) and a summary:

```
DemoAnts: ants=200 moves=... food=... alive=0
```

`food` is the amount delivered to the nest; it grows during the run.

## Headless GUI check

`tools/test-demo-gui.sh` starts `gtk4-broadwayd` in a private runtime
directory, runs `blackbox --BB_RUN=DemoAnts.GuiCheck`, and requires
`DemoAnts: ok=1`. `GuiCheck` opens the window, logs an ASCII frame and
the counters every 10 timer ticks, and after 40 ticks checks that the
timer ran, food grew, all ants are alive and there is no deadlock.
`Restores` counts actual `Restore` calls (it is non-zero under the
headless Broadway backend), so the journal proves that the window is
alive and frames are produced. No mouse is emulated and the user's
display is not touched.

## Test

`Test64Demo.TestProcAnts` calls `Simulate(200, 150)` and checks:

- food delivered to the nest is greater than zero;
- the number of ants is still 200;
- all ant tasks finished (`Left = 0`);
- no traps and no deadlock.

The exact amount of food is not asserted: the scheduler and the
real-time evaporator make it depend on the machine, but the outcome
class (food grows, ants survive, no deadlock) is deterministic.

## AMD64 only

The `Demo` subsystem is built for AMD64. The task and monitor core also
works on i386, but the `Async` library (channels) is not built there;
`DemoAnts` itself needs only `Tasks`, `Views` and `Services`, but it is
part of the same subsystem and is built with the rest of `Demo`.
