# Unit testing in BlackBox

Subsystem `Unit` provides module tests for Component Pascal: `UnitAssert`
(checks) and `UnitRunner` (discovery, fixtures, isolation, reports).
A test is an exported parameterless procedure whose name starts with
`Test`; no registration is needed.

## Writing a test

```
MODULE Text_testsWriter;
	IMPORT TextModels, UnitAssert;

	VAR t: TextModels.Model;

	PROCEDURE Setup*;	(* optional, before every test *)
	BEGIN t := TextModels.dir.New()
	END Setup;

	PROCEDURE TestWriteAtEnd*;
		VAR w: TextModels.Writer;
	BEGIN
		w := t.NewWriter(NIL); w.WriteChar("a"); w.WriteChar("b");
		UnitAssert.EqualInt(2, t.Length(), "length after two chars")
	END TestWriteAtEnd;

END Text_testsWriter.
```

Tests live in a separate `<Subsystem>_tests` subsystem next to the one
under test: the file is `<Subsystem>_tests/Mod/<Theme>.cp` and the module
is `<Subsystem>_tests<Theme>` (`Text_testsWriter` in
`Text_tests/Mod/Writer.cp`). An entry point without a theme is
`<Subsystem>_testsMain` (`Text_tests/Mod/Main.cp`). AMD64-only tests use
`Mod64` in the same subsystem (`Async_tests/Mod64/Channels.cp`, module
`Async_testsChannels`). Production builds do not include `*_tests`
subsystems; tests are built on demand by `tools/unit.sh`.

## Rules

1. **Discovery.** Exported procedures with no parameters named `Test…`
   are found through `Meta.Scanner`; the order is the module export
   order. No registry, like `test_*` in pytest.
2. **Fixtures.** Optional `Setup*`/`Teardown*` run around every test;
   `SetupModule*`/`TeardownModule*` once per module. `Teardown` runs even
   after a trap.
3. **Checks.** `UnitAssert.True`, `EqualInt`, `EqualReal`,
   `EqualStr`, `EqualSet`, `NotNil`, `Same`, `Traps`, `Fail`. A plain
   `ASSERT` also works: any unexpected trap is a failed test with the
   kernel's trap report. Failure text follows “expected X, got Y: msg”.
4. **No checks — a failure.** A test that made no `UnitAssert` call and
   did not trap is reported `EMPTY` and counted as failed.
5. **Isolation.** Every test and every fixture runs under `Kernel.Try`;
   a trap fails only that test. `Kernel.Collect` runs after a trapped
   test. `UnitRunner` installs a no-op trap viewer for the run, so a
   trap inside `Kernel.Try` is not fatal in the console, and restores
   the previous viewer afterwards.
6. **Marks by name prefix.** `TestSlow…` (long), `TestGui…` (needs a
   display), `TestProc…` (better in its own process: kernel, collector,
   signals, `Kernel.Quit`). By default only ordinary tests run; pass the
   marks explicitly. Mouse drag simulation is forbidden by the stand
   rules.
7. **Report.** One line per test: `PASS Module.Test 0 ms`,
   `FAIL Module.Test 0 ms: message`, `EMPTY …`, `SKIP … (mark)`; then
   `== Unit: passed N, failed M, empty K, skipped S`. In the console the
   runner calls `Kernel.Quit(0|1)`, so the shell sees a status. `tap`
   prints TAP, `junit=FILE` writes a JUnit XML file (relative to the
   working directory of the console).
8. **Temporary files.** `UnitRunner.TempDir` returns `UnitTmp`. The
   BlackBox process has no portable `mkdir`; the external launcher
   (`tools/unit.sh`) creates and removes the directory.
9. **A defect test is red first.** Mention “was red” in the change
   description.
10. **One behaviour — one test.** Name says what is checked:
    `TestDeleteOutOfRangeTraps`, not `Test3`.

## Running

```
echo 'UnitRunner.Run Unit_testsMain' | tools/run64.sh
echo 'UnitRunner.Run Text_testsWriter slow proc' | tools/run64.sh

tools/test.sh                 # modules from tools/tests.txt
tools/test.sh --list
tools/unit.sh Check_testsMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

`UnitRunner.Run` arguments: a module name, a subsystem name, or `all`;
`slow`, `gui`, `proc` enable those marks; `junit=FILE`, `tap`, `noquit`.
A subsystem name selects the loaded test modules of that subsystem:
`System` finds `System_tests*`, `Async` finds `Async_tests*`. `all`
selects every loaded module whose name contains `_tests`. The modules
must already be loaded (linked) into the world.

`tools/unit.sh` compiles the module with `tools/go64.sh` (up to three
passes), runs it in a separate `blackbox-console`, and succeeds only when
the summary says `failed 0` and the process exits with 0. This is the
external launcher for `TestProc…`: a test that calls `Kernel.Quit` or
crashes the process cannot take other modules down.

## Self-check

`Unit_testsMain` (in `Unit_tests/Mod/Main.cp`) verifies the runner itself:
`Unit_testsProbe` contains a passing, a trapping, a failing, an empty and
a slow test plus fixtures; `TestNestedRunner` runs the probe quietly and
compares `PASS/FAIL/EMPTY/SKIP` counts.
