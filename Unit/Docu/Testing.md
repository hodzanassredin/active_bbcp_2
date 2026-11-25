# Unit testing in BlackBox

A programmer changes the procedure `Strings.IntToString` and wants to know whether negative numbers
still work. Without tests he opens a work window, types a command, looks at the output, and a week
later forgets to repeat the check. The `Unit` subsystem turns such a check into a module that runs with
one command and prints an unambiguous result. The subsystem consists of two modules: `UnitAssert`
checks values, `UnitRunner` finds the tests, executes them one by one and prints a report.

## 1. The first test

A *test* is an exported procedure without parameters whose name starts with `Test`. It need not be
registered anywhere: the runner finds such procedures itself by walking the module's exports with
`Meta.Scanner`.

Tests do not live in the subsystem under test but next to it, in a subsystem with the suffix `_dev`.
Tests of the `Text` subsystem lie in `Text_dev/Mod/<Topic>.cp`, and the module is called
`Text_dev<Topic>`. We put the first test into the subsystem `Unit_dev`, file
`Unit_dev/Mod/Example.cp`:

```
MODULE Unit_devExample;

	IMPORT Strings, UnitAssert;

	VAR s: ARRAY 32 OF CHAR;

	PROCEDURE Setup*;
	BEGIN s := ""
	END Setup;

	PROCEDURE TestIntToStringNegative*;
	BEGIN
		Strings.IntToString(-42, s);
		UnitAssert.EqualStr("-42", s, "знак и цифры")
	END TestIntToStringNegative;

	PROCEDURE TestFindMissing*;
		VAR pos: INTEGER;
	BEGIN
		Strings.Find("abc", "x", 0, pos);
		UnitAssert.EqualInt(-1, pos, "нет образца")
	END TestFindMissing;

END Unit_devExample.
```

Each test calls the procedure under test and compares the result with the expected one. The first
argument of a comparison is the expected value, the second the actual value, the third an explanation
that goes into the report on failure. The procedure `Setup` is not a test: the runner calls it before
every test, so every test starts with an empty string `s`.

We run the module with the external launcher. It compiles `Unit` and the test module, starts a separate
console BlackBox and executes `UnitRunner.Run` in it:

```
$ tools/unit.sh Unit_devExample
== Unit: run Unit_devExample
PASS Unit_devExample.TestFindMissing 0 ms
PASS Unit_devExample.TestIntToStringNegative 0 ms
== Unit: passed 2, failed 0, empty 0, skipped 0
```

One line is printed per test: outcome, name and time. Tests run in the order of the module's export
table, that is alphabetically, not in text order. The summary line counts the outcomes. The exit code
of `tools/unit.sh` is 0 only if the summary says `failed 0` and the process exited with code 0.

## 2. How a run works

A test that traps must not take the other tests with it, and its garbage must not reach the next one.
Therefore `UnitRunner` executes every test and every fixture under `Kernel.Try`. A trap becomes a
failure of that test with the message `test trapped`; the kernel prints its trap report, and the
runner calls `Kernel.Collect` and moves on to the next test. The runner does not replace the trap
viewer: `Kernel.Try` catches the trap without it.

*Fixtures* are optional procedures with special names. `Setup` and `Teardown` run around each test,
`SetupModule` and `TeardownModule` once per module, before the first test and after the last.
`Teardown` runs after a trapped test too: it closes what `Setup` opened. If `SetupModule` traps, the
module's tests have nothing to run on: the runner prints the failure `SetupModule trapped` and marks the
module's tests `SKIP` with the same reason.

A test fails if it, `Setup` or `Teardown` trapped, or if one of the `UnitAssert` checks did not pass;
the message comes from the last failed check. A test that did not trap and made no check gets the mark
`EMPTY`. Such a test proves nothing: it would pass on broken code too. For example, the test

```
PROCEDURE TestNothing*;
BEGIN
	Strings.IntToString(1, s)
END TestNothing;
```

gives the line `EMPTY Unit_devExample.TestNothing 0 ms: no checks`. The summary line shows empty tests
separately and includes them in the number of failed ones: `passed 2, failed 1, empty 1`. A process
with the `quit` key exits with code 1.

A plain `ASSERT` in a test works too: an unexpected trap is a failure. But a test with only `ASSERT`
turns out empty when it succeeds. If a test checks that code does not trap, it marks the check
explicitly by calling `UnitAssert.Checked`. If a test checks that code traps, it calls
`UnitAssert.Traps(p, …)`: the procedure `p` runs under `Kernel.Try`, and a failure is counted when no
trap happened.

Failure messages have the form `expected X, got Y: explanation`, for strings `expected <X>, got <Y>:
explanation`. A message is cut to 255 characters (`UnitAssert.Message`); a long string in a comparison
does not trap.

## 3. Marks and a separate process

Some tests cannot run on every change. A long test delays the run, a test with windows needs a screen,
and a test that calls `Kernel.Quit` or checks the collector can bring the process down. Such tests are
marked by a name prefix: `TestSlow…` is long, `TestGui…` needs a screen, `TestProc…` is better run in a
separate process. By default the runner skips them and prints `SKIP` with the reason `slow`, `gui` or
`proc`; the keys of the same names of the command `UnitRunner.Run` enable them.

`tools/unit.sh` provides the separate process: one module, one `blackbox-console` process. If a test
brings the process down, `tools/unit.sh` reports the exit code and the tail of the output, and the
other modules of the `tools/test.sh` list run in their own processes.

## 4. Rules

1. One behaviour, one test. The name says what is checked: `TestDeleteOutOfRangeTraps`, not `Test3`.
   Then the `FAIL` line in the report names the broken behaviour itself.
2. A test for a defect fails first. It is run on the code with the defect and shows `FAIL`, then on the
   fixed code and shows `PASS`. A test that never failed may have checked nothing.
3. A test requires no changes to the module under test or to the kernel. A test uses what an ordinary
   client uses: exports, global variables and observable effects — counters, output, state.
4. Temporary files lie in the directory `UnitRunner.TempDir` (`UnitTmp`). BlackBox has no portable
   `mkdir`, so `tools/unit.sh` creates and cleans the directory.

A violation of the third rule is not visible at once. Suppose a test wants to know that the heap of
sleeping tasks in `Tasks` is ordered. The easiest way is to add an exported procedure `HeapOk*` to
`Tasks` and call it from the test. The test passes, but the kernel gets an export that no client uses.
The new export changes the fingerprint of the symbol file, and all modules importing `Tasks` need
recompilation; the distribution carries a procedure that cannot be removed or changed without breaking
the test. The right way is different. The kernel checks an internal invariant itself, with an `ASSERT`
at the place where the invariant may break (in `Tasks` this is the `ASSERT` with number 100 when the
heap is compacted), and the test creates thousands of sleeping tasks with different deadlines and checks
what a client sees: the tasks wake up in deadline order. Trap 100 is then detected too, as a
`test trapped` failure. A diagnostic export without users outside `*_dev` is removed.

## 5. `_dev` subsystems

Tests are kept apart from the code because the distribution build does not include `*_dev`
subsystems: the user gets neither the tests nor their dependencies. `tools/unit.sh` builds the tests
on demand.

A module's name determines its file by the BlackBox rules: `Text_devWriter` lies in
`Text_dev/Mod/Writer.cp`. The entry point of a subsystem without a topic is `<Subsystem>_devMain`, for
example `Text_dev/Mod/Main.cp`. Tests for amd64 only lie in `Mod64` of the same subsystem: the module
`System_devTasksObj` is in `System_dev/Mod64/TasksObj.cp`.

The runner itself is checked by `Unit_devMain`. The module `Unit_devProbe` contains a passing, a
trapping, a failing, an empty and a slow test and fixtures; `TestNestedRunner` runs the probe quietly
through `UnitRunner.RunModuleCounts` and compares the counters.

## 6. Running and reports

Inside BlackBox the command `UnitRunner.Run` runs tests, with arguments separated by spaces. The first
argument is the target: a module name, a subsystem name or `all`. A subsystem name selects the loaded
modules of its tests: `System` finds `System_dev*`; `all` selects all loaded
modules whose name contains `_dev`. The modules must already be loaded into the world. The keys `slow`,
`gui`, `proc` enable marks; `tap` prints a TAP report; `junit=FILE` writes a JUnit XML report; `quit`
at the end calls `Kernel.Quit(0)` or `Kernel.Quit(1)` so that an external launcher gets the code;
`noquit` cancels `quit`.

```
echo 'UnitRunner.Run Unit_devMain quit' | tools/run64.sh
echo 'UnitRunner.Run Text_devWriter slow proc quit' | tools/run64.sh

tools/test.sh                 # modules from tools/tests.txt
tools/test.sh --list
tools/unit.sh Check_devMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

`tools/unit.sh` compiles `Unit`, the modules from `--compile` and the test module with `tools/go64.sh`
(up to three passes, because the order of dependencies is not known in advance) and executes
`UnitRunner.Run` with the `quit` key and the marks from `--marks`. `tools/test.sh` walks the list
`tools/tests.txt`, where each module has its marks, timeout, additional modules and compiler options.

The JUnit report is read by the continuous integration system: it shows failures by test name. The
file is written in UTF-8, the path is relative to the console's working directory. Each test becomes a
`<testcase>` element; a skipped test carries its reason in `<skipped message="…"/>`.

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

The `UnitAssert` procedures check no preconditions and do not trap. Each check increments the check
counter; a failure increments the failure counter and remembers the message.

**PROCEDURE Reset**
Post: counters are 0, the message is empty.

**PROCEDURE Save (OUT s: State)**, **Restore (IN s: State)**
Post: `s` is a copy of the counters and the message; `Restore` restores them from `s`.

**PROCEDURE Checks (): LONGINT**, **Failures (): INTEGER**, **LastMessage (OUT m: Message)**
Post: number of checks, number of failures, message of the last failure.

**PROCEDURE Checked**
Counts a check without a condition.
Post: the check counter is incremented by 1.

**PROCEDURE True (cond: BOOLEAN; IN m: ARRAY OF CHAR)**
Post: `~cond` — failure with message `m`.

**PROCEDURE Fail (IN m: ARRAY OF CHAR)**
Post: failure with message `m`.

**PROCEDURE EqualInt (expected, actual: LONGINT; IN m: ARRAY OF CHAR)**
Post: `expected # actual` — failure `expected X, got Y: m`.

**PROCEDURE EqualReal (expected, actual, eps: REAL; IN m: ARRAY OF CHAR)**
Post: failure if `ABS(actual - expected) > eps`. With `eps <= 0` or `expected = 0` the comparison is
exact.

**PROCEDURE EqualStr (IN expected, actual: ARRAY OF CHAR; IN m: ARRAY OF CHAR)**
Post: strings differ — failure `expected <X>, got <Y>: m`.

**PROCEDURE EqualSet (expected, actual: SET; IN m: ARRAY OF CHAR)**
Post: sets differ — failure `expected set X, got Y: m`; sets are printed as numbers.

**PROCEDURE NotNil (p: ANYPTR; IN m: ARRAY OF CHAR)**
Post: `p = NIL` — failure.

**PROCEDURE Same (expected, actual: ANYPTR; IN m: ARRAY OF CHAR)**
Post: `expected # actual` — failure.

**PROCEDURE Traps (p: PROCEDURE; IN m: ARRAY OF CHAR)**
Calls `p` under `Kernel.Try`.
Post: `p` did not trap or `p = NIL` — failure.

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
Post: `c` holds the totals; the `UnitAssert` state is restored. Module not found — one failure.

**PROCEDURE TempDir (OUT path: ARRAY OF CHAR)**
Post: `path = "UnitTmp"`.

**PROCEDURE Run**
Command; the arguments are described in section 6.
Post: the summary is printed. With the `quit` key the process exits with code 1 on failures or empty
tests, otherwise with code 0.
