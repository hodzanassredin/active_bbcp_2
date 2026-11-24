# DemoSupport — shared helper of the examples

`DemoSupport` is not an example; it holds the two things every command
needs in a console host:

```
DemoSupport.Pump(ms)         (* run the Services action hook until no tasks remain *)
DemoSupport.Log / LogInt / LogLn / LogLine
```

`Pump` drives `Services.actionHook.Step`, the same step the main loop
of the GUI uses. That makes `Select` timeouts and `Tasks.Sleep` work in
a command run through `tools/run64.sh`, where no window loop exists.
It returns `TRUE` when all tasks have finished and `FALSE` on timeout.

The log helpers write to `Console` (stdout). The examples use them for
one summary line; the Unit tests inspect exported counters rather than
the text.
