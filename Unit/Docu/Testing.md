# Unit testing in BlackBox

Subsystem `Unit` provides module tests for Component Pascal: `UnitAssert`
(checks) and `UnitRunner` (discovery, fixtures, isolation, reports).
A test is an exported parameterless procedure whose name starts with
`Test`; no registration is needed.

## Writing a test

```
MODULE Text_devWriter;
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

END Text_devWriter.
```

Tests live in a separate `<Subsystem>_dev` subsystem next to the one
under test: the file is `<Subsystem>_dev/Mod/<Theme>.cp` and the module
is `<Subsystem>_dev<Theme>` (`Text_devWriter` in
`Text_dev/Mod/Writer.cp`). An entry point without a theme is
`<Subsystem>_devMain` (`Text_dev/Mod/Main.cp`). AMD64-only tests use
`Mod64` in the same subsystem (`Async_dev/Mod64/Channels.cp`, module
`Async_devChannels`). Production builds do not include `*_dev`
subsystems; tests are built on demand by `tools/unit.sh`.

## Rules

1. **Discovery.** Exported procedures with no parameters named `Test…`
   are found through `Meta.Scanner`; the order is the module export
   order. No registry, like `test_*` in pytest.
2. **Fixtures.** Optional `Setup*`/`Teardown*` run around every test;
   `SetupModule*`/`TeardownModule*` once per module. `Teardown` runs even
   after a trap. If `SetupModule` traps, it is reported as a failure and
   the tests of the module do not run: they are `SKIP` with the reason
   `SetupModule trapped`.
3. **Checks.** The procedures of `UnitAssert` are listed in the
   section "Interface". A plain
   `ASSERT` also works: any unexpected trap is a failed test with the
   kernel's trap report. Failure text is `expected X, got Y: msg`;
   for strings — `expected <...>, got <...>`. The text is cut at 255
   characters (`UnitAssert.Message`); long strings do not trap.
4. **No checks — a failure.** A test that made no `UnitAssert` call and
   did not trap is reported `EMPTY` and counted as failed.
5. **Isolation.** Every test and every fixture runs under `Kernel.Try`;
   a trap fails only that test. `Kernel.Collect` runs after a trapped
   test. The runner does not replace the trap viewer: `Try` catches the
   trap whether or not a viewer is installed.
6. **Marks by name prefix.** `TestSlow…` (long), `TestGui…` (needs a
   display), `TestProc…` (better in its own process: kernel, collector,
   signals, `Kernel.Quit`). By default only ordinary tests run; pass the
   marks explicitly. Mouse drag simulation is forbidden by the stand
   rules.
7. **Report.** One line per test: `PASS Module.Test 0 ms`,
   `FAIL Module.Test 0 ms: message`, `EMPTY …`, `SKIP … (mark)`; then
   `== Unit: passed N, failed M, empty K, skipped S`. With the `quit` key
   the runner calls `Kernel.Quit(0|1)`, so the shell sees a status. `tap`
   prints TAP, `junit=FILE` writes a JUnit XML file in UTF-8 (relative to
   the working directory of the console); a skipped test carries its
   reason in `<skipped message="…"/>`.
8. **Temporary files.** `UnitRunner.TempDir` returns `UnitTmp`. The
   BlackBox process has no portable `mkdir`; the external launcher
   (`tools/unit.sh`) creates and removes the directory.
9. **A defect test fails first.** Make sure the test fails on the old
   code and passes on the new one.
10. **One behaviour — one test.** Name says what is checked:
    `TestDeleteOutOfRangeTraps`, not `Test3`.
11. **A test requires no changes to the module under test or the
    kernel.** A test uses only what an ordinary client uses: public
    procedures, globals, and observable effects (counters, output,
    state). The kernel checks internal invariants itself (`ASSERT` in a
    debug spot) instead of exporting a checker; a diagnostic export
    with no users outside `*_dev` is removed.

## Running

```
echo 'UnitRunner.Run Unit_devMain quit' | tools/run64.sh
echo 'UnitRunner.Run Text_devWriter slow proc quit' | tools/run64.sh

tools/test.sh                 # modules from tools/tests.txt
tools/test.sh --list
tools/unit.sh Check_devMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

`UnitRunner.Run` arguments: a module name, a subsystem name, or `all`;
`slow`, `gui`, `proc` enable those marks; `junit=FILE`, `tap`; `quit`
calls `Kernel.Quit(0|1)` at the end (so the console launcher gets a
status), `noquit` cancels it.
A subsystem name selects the loaded test modules of that subsystem:
`System` finds `System_dev*`, `Async` finds `Async_dev*`. `all`
selects every loaded module whose name contains `_dev`. The modules
must already be loaded (linked) into the world.

`tools/unit.sh` compiles the module with `tools/go64.sh` (up to three
passes), runs it in a separate `blackbox-console`, and succeeds only when
the summary says `failed 0` and the process exits with 0. This is the
external launcher for `TestProc…`: a test that calls `Kernel.Quit` or
crashes the process cannot take other modules down.

## Interface

```
DEFINITION UnitAssert;

	TYPE
		Message = ARRAY 256 OF CHAR;
		State = RECORD
			checks-: LONGINT;
			failures-: INTEGER;
			msg-: Message
		END;

	PROCEDURE Reset;
	PROCEDURE Save (OUT s: State);
	PROCEDURE Restore (IN s: State);
	PROCEDURE Checks (): LONGINT;
	PROCEDURE Failures (): INTEGER;
	PROCEDURE LastMessage (OUT m: Message);
	PROCEDURE Checked;
	PROCEDURE True (cond: BOOLEAN; IN m: ARRAY OF CHAR);
	PROCEDURE Fail (IN m: ARRAY OF CHAR);
	PROCEDURE EqualInt (expected, actual: LONGINT; IN m: ARRAY OF CHAR);
	PROCEDURE EqualReal (expected, actual, eps: REAL; IN m: ARRAY OF CHAR);
	PROCEDURE EqualStr (IN expected, actual: ARRAY OF CHAR; IN m: ARRAY OF CHAR);
	PROCEDURE EqualSet (expected, actual: SET; IN m: ARRAY OF CHAR);
	PROCEDURE NotNil (p: ANYPTR; IN m: ARRAY OF CHAR);
	PROCEDURE Same (expected, actual: ANYPTR; IN m: ARRAY OF CHAR);
	PROCEDURE Traps (p: PROCEDURE; IN m: ARRAY OF CHAR);

END UnitAssert.
```

The procedures of `UnitAssert` check no preconditions and do not trap. Every check increments the
check counter. A failure increments the failure counter and stores the message.

**PROCEDURE Reset**
Post: the counters are 0, the message is empty.

**PROCEDURE Save (OUT s: State)**, **Restore (IN s: State)**
Post: `s` is a copy of the counters and the message; `Restore` takes them back from `s`.

**PROCEDURE Checks (): LONGINT**, **Failures (): INTEGER**, **LastMessage (OUT m: Message)**
Post: the number of checks, the number of failures, the message of the last failure.

**PROCEDURE Checked**
Counts a check without a condition.
Post: the check counter is incremented by 1.

**PROCEDURE True (cond: BOOLEAN; IN m: ARRAY OF CHAR)**
Post: `~cond` — a failure with message `m`.

**PROCEDURE Fail (IN m: ARRAY OF CHAR)**
Post: a failure with message `m`.

**PROCEDURE EqualInt (expected, actual: LONGINT; IN m: ARRAY OF CHAR)**
Post: `expected # actual` — failure `expected X, got Y: m`.

**PROCEDURE EqualReal (expected, actual, eps: REAL; IN m: ARRAY OF CHAR)**
Post: a failure if `ABS(actual - expected) > eps`. With `eps <= 0` or `expected = 0` the
comparison is exact.

**PROCEDURE EqualStr (IN expected, actual: ARRAY OF CHAR; IN m: ARRAY OF CHAR)**
Post: the strings differ — failure `expected <X>, got <Y>: m`.

**PROCEDURE EqualSet (expected, actual: SET; IN m: ARRAY OF CHAR)**
Post: the sets differ — failure `expected set X, got Y: m`; sets are printed as numbers.

**PROCEDURE NotNil (p: ANYPTR; IN m: ARRAY OF CHAR)**
Post: `p = NIL` — a failure.

**PROCEDURE Same (expected, actual: ANYPTR; IN m: ARRAY OF CHAR)**
Post: `expected # actual` — a failure.

**PROCEDURE Traps (p: PROCEDURE; IN m: ARRAY OF CHAR)**
Calls `p` under `Kernel.Try`.
Post: `p` did not trap or `p = NIL` — a failure.

```
DEFINITION UnitRunner;

	TYPE
		Counts = RECORD
			passed, failed, empty, skipped: INTEGER
		END;

	PROCEDURE RunModuleCounts (IN module: ARRAY OF CHAR; IN marks: ARRAY OF CHAR;
		quietRun: BOOLEAN; OUT c: Counts);
	PROCEDURE TempDir (OUT path: ARRAY OF CHAR);
	PROCEDURE Run;

END UnitRunner.
```

**PROCEDURE RunModuleCounts (IN module, marks: ARRAY OF CHAR; quietRun: BOOLEAN; OUT c: Counts)**
Runs the tests of module `module` with marks `marks`; `quietRun` suppresses output.
Post: `c` holds the totals; the `UnitAssert` state is restored. A module not found — one failure.

**PROCEDURE TempDir (OUT path: ARRAY OF CHAR)**
Post: `path = "UnitTmp"`.

**PROCEDURE Run**
A command; its arguments are listed in the section "Running".
Post: the summary is printed. With the `quit` key the process exits with code 1 on failures and
empty tests, otherwise with code 0.

## Self-check

`Unit_devMain` (in `Unit_dev/Mod/Main.cp`) verifies the runner itself:
`Unit_devProbe` contains a passing, a trapping, a failing, an empty and
a slow test plus fixtures; `TestNestedRunner` runs the probe quietly and
compares `PASS/FAIL/EMPTY/SKIP` counts.
