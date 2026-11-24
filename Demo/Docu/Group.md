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

`Tasks.Group` provides the ownership, the error propagation and the
wait: `Tasks.NewGroup`, `Tasks.StartIn(t, g)`; the parent's `Run`
returns while `TaskBoot` waits for the group to become empty. Worker 3
traps after all four workers pass the `Arrive` barrier. `TaskBoot`
marks the group failed and calls the kernel's `CancelGroup`, which
cancels the other three at their next wait point. The parent finishes
only when the group is empty.

A trap in the console host would normally be fatal (there is no trap
window), so the command installs an empty trap viewer for the duration
of the run and restores the previous one afterwards — the same
technique as `UnitRunner`. The trap reports in the journal are
expected: they are the error and the three cancellations. The summary
line follows them.

## Run

```sh
echo 'DemoGroup.Run' | tools/run64.sh
```

Output:

```
DemoGroup: entered=8 completed=0 cancelled=3 failedWorker=1 groupFailed=1 alive=0
```

## Test

`DemoTests.TestProcGroupCancel` checks one error, one trapped member,
three cancelled members, zero completed, an empty failed group and no
unexpected failure of the run.
