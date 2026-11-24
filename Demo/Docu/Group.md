# DemoGroup — cancelling a group of tasks

## Source

Sameer Ajmani, *Go Concurrency Patterns: Pipelines and Cancellation*
(Go Blog, 2014), the `context` cancellation section:

<https://go.dev/blog/pipelines>

and the `errgroup` package:

<https://pkg.go.dev/golang.org/x/sync/errgroup>

In Go the first error cancels the `context`; the parent waits for all
members with `errgroup.Wait`.

## Translation

`Tasks.Group` provides the ownership and the wait: `Tasks.NewGroup`,
`Tasks.StartIn(t, g)`, and the parent's `Run` returns while `TaskBoot`
waits for the group to become empty. The `context.Done()` role is
played by the module flag `Stopped`: every worker checks it in its own
wait loop and leaves cooperatively.

The error is reported by worker 3 after all four workers pass the
`Arrive` barrier. All four stop, `Completed` is 0 and the group is
empty. Cancellation through a **trap** (`Tasks.GroupFailed` and the
kernel's `CancelGroup`) is not used in the command: a trap in a task is
fatal in the console host (there is no trap view), so that path is
covered by `Test64Tasks`.

## Run

```sh
echo 'DemoGroup.Run' | tools/run64.sh
```

Output:

```
DemoGroup: entered=10 completed=0 cancelled=4 errors=1 alive=0
```

## Test

`Test64Demo.TestProcGroupCancel` checks one error, four stopped
members, zero completed, an empty group and no traps.
