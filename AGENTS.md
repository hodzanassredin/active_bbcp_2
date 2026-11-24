# BlackBox Component Builder - Agent Guidelines

BlackBox Component Builder 2.0 (bbcb2) is development framework for Component Pascal programming language (description in file `Docu/CP-Lang.md`). This special version of framework uses utf8 text sources stored in `.cp`.

`OLD/` is an archived checkout. Do not compile sources or run tests from it.

`dev0Linux` is command line version of bbcb2, which includes compiler.

Run `./dev0Linux` from the project root directory, where the executable is located.

## Project Structure

Each subsystem is a folder in project directory.

Modules, documentation and compilation results organized as follows:

```
<Subsystem>/
  Mod/      - Source modules (.cp files in Component Pascal)
  Mod64/    - Source modules specific for AMD64 (.cp files in Component Pascal)
  Code/     - Compiled object code (.ocf binary files)
  Sym/      - Symbol files for binding modules (.osf binary files)
  Docu/     - Documentation in English (.md files)
  Docu/ru     - Documentation in Russian (.md files)
```

## How to find module sources?

Module name can be one word from upper letter.
In that case sources will be stored in subsystem "System".
For example module `Files` will be located in `System/Mod/Files.cp` or `System/Mod64/Files.cp` (for AMD64 target).

Also module name can be combined from several words (each from upper letter).
The first word is the name of subsystem.
All other words define file name with sources for that module.
For example module  `TextModels` will be located in `Text/Mod/Models.cp`.

## Running Commands

Commands are procedures without arguments exported by special mark `*`.

```componentpascal
MODULE ModuleName;

	IMPORT Log;

	PROCEDURE ProcName*;
	BEGIN
		Log.String("hello world"); Log.Ln
	END ProcName;

END ModuleName.
```

The interpreter `dev0Linux` can accept commands via stdin.

```bash
# Run a command
echo 'ModuleName.ProcName' | ./dev0Linux
```

## Compiling Modules

Use `DevCompiler.CompileThis` commands to compile:

```bash
# Compile specific modules
echo "DevCompiler.CompileThis DemoExternalVariable" | ./dev0Linux
```

## Rebuild the command-line host

Run `./pack-dev0-linux` to link and pack dev0Linux using the existing host,
DevLinker1, DevPacker and the fixed OCF list in the shell script. No Python is
required. It uses precompiled i386 objects in normal Code folders; compile
changed host modules with DevCompiler beforehand. It checks standalone startup
before replacing dev0Linux. The previous executable is saved in
`build/dev0-linux/dev0Linux.previous`. See `Dev/Docu/Dev0-Linux.md`.

## AMD64 source selection

Before every AMD64 compile or link session, enable `DevLibrarian.EnableDevLib(outputDirectory, 'Mod64')`. It selects `<Subsystem>/Mod64/<Name>.cp` when present, otherwise `<Subsystem>/Mod/<Name>.cp`. Module names have no `__x64` suffix. Code and symbol files go to the output directory; keep i386, Linux AMD64 and Windows AMD64 outputs separate. `DevLibrarian.DisableDevLib` restores the standard librarian. Each new `dev0Linux` process must enable the librarian again.

## Build Linux x64

Example how to enable Librarian for x64 (take modules from `Mod64` if exists) and put results of compilation to `BB64LIN` directory.

```bash
echo "DevLibrarian.EnableDevLib('build/BB64LIN', 'Mod64');
DevCompiler64.CompileSubs @Lin Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Unit;
DevLinker1.LinkElfExe64 'build/BB64LIN/blackbox' := Kernel+ LinKernel Files LinFiles StdLoader LinLoader" | ./dev0Linux
for subsystem in Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket; do
	if [ -d "$subsystem/Rsrc" ]; then
		mkdir -p "build/BB64LIN/$subsystem"
		cp -R "$subsystem/Rsrc" "build/BB64LIN/$subsystem/"
	fi
done
cp -R Docu build/BB64LIN/
```

## Build Windows x64

Example how to enable Librarian for x64 (take modules from `Mod64` if exists) and put results of compilation to `BB64WIN` directory.

```bash
echo "DevLibrarian.EnableDevLib('build/BB64WIN', 'Mod64');
DevCompiler64.CompileSubs @Win Win System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Mdi;
DevLinker1.LinkPeExe64 'build/BB64WIN/blackbox.exe' := Kernel+ WinKernel Files WinFiles StdLoader WinLoader 1 Applogo.ico 1 BlackBox.res
DevLinker1.LinkPeExe64 'build/BB64WIN/blackboxMDI.exe' := Kernel+ WinKernel Files WinFiles StdLoader MdiLoader 1 Applogo.ico 1 BlackBox.res" | ./dev0Linux
for subsystem in Win System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Mdi; do
	if [ -d "$subsystem/Rsrc" ]; then
		mkdir -p "build/BB64WIN/$subsystem"
		cp -R "$subsystem/Rsrc" "build/BB64WIN/$subsystem/"
	fi
done
cp -R Docu build/BB64WIN/
```

## AMD64 tests

Run scripts from the repository root. They enable DevLibrarian in each compiler process:

```bash
python3 Dev/Test/check_mod64_layout.py
python3 Lin/Test/check_linux64.py --out build/blackbox-lin64
python3 Lin/Test/check_gtk4.py
python3 Win/Test/check_subsystem64.py --out build/blackbox-win64
python3 Win/Test/build_mdi64.py --out build/blackbox-mdi-win64 --test
```

The Linux test script links `blackbox-console` with LinIntInit for stdin commands. Current LinLoader always starts the GUI; do not assume `blackbox --console` selects console mode. See `Dev/Docu/Building-AMD64.md` for further checks and prerequisites.

Never run automatic mouse drag tests: a previous test crashed the user's system. Verify drag-and-drop manually. Ordinary GUI tests and console memory tests are separate from mouse drag simulation.

## Pass parameters to commands

Text after command can be accessed by reading `DevCommanders.par.text` structure from `DevCommanders.par.beg` to `DevCommanders.par.end` positions.

```bash
# Example, how to pass parameters to command
echo "DemoParameters.Do some parameters" | ./dev0Linux
```

See Demo/Mod/Parameters.cp module as an example.

## Pass external variables to module

```bash
# Example, how to pass external variables to BlackBox module
echo "DemoExternalVariable.Do" | ./dev0Linux --VAR_NAME="some data"
```

`VAR_NAME` can be any name accessed then in sources by Dialog.GetPar("VAR_NAME", val, res)

there:

- first argument is name of external variable
- second argument is variable, where result of read should be stored (type of variable should be `ARRAY n OF CHAR`) 
- third argument is INTEGER variable with result of varialbe access (0 if no error, 2 - not found, 3 - conversion error) 

See Demo/Mod/ExternalVariable.cp module as an example.

```bash
# Example, how to pass multiple parameters to BlackBox module
echo "DemoExternalVariable.Do" | ./dev0Linux --PAR_NAME1="some data" --PAR_NAME2="some data"
```

## Formatting of sources

Don't use spaces for tabulation.

Put empty line before new sources block (IMPORT, CONST, TYPE, PROCEDURE).

## Стенд active2 (наши дополнения)

Ветка `active2` — снимок DIA261123 плюс этапы 1–2 из ветки `active`,
перенесённые на `.cp`. Правила выше (`.cp`, `OLD/` не собирать, `Mod64`
через `DevLibrarian`, табы, ручной drag-and-drop) действуют полностью.
Ветка `active` на `.odc` не переписывалась; её правила и `tools/sync-odc.sh`
остаются там.

- Каталог установки по умолчанию — `.work/BB64LIN` (в git не входит,
  `BB64INSTALL` переопределяет). Отдельного мира-скелета нет: сборка идёт
  прямо туда, `dev0Linux` запускается из корня репозитория.
- Консоль — `blackbox-console` (`LinIntInit`), GUI — `blackbox`
  (`LinLoader`); `blackbox --console` консольный режим не выбирает.

```sh
tools/build64.sh                  # все подсистемы, консоль + GUI
tools/build64.sh --clean          # стереть .ocf/.osf установки
tools/build64.sh --console-only   # только консоль
echo 'Команда' | tools/run64.sh
tools/go64.sh System_devBasic         # amd64 CompileThis
tools/go32.sh DevCPE              # i386 CompileThis -> .work/i386
tools/build32.sh                  # i386-мир + консоль в build/BB32LIN
tools/test32.sh                   # i386: Unit, Files, Tasks, Monitors, Active, Yield, CoTests, UnloadGuard
tools/i386-toolchain.sh           # 32-битный C-тулчейн без sudo (флаги gcc)
tools/test.sh                     # 21 этап, 108 тестов
tools/test-resolver.sh            # этап 8: резолвер в unshare -rmn
tools/test-robust.sh              # RobustParallel на 1/4/16 процессах
tools/test-mpi.sh                 # MPI-транспорт, mpiexec -n 5
tools/test-gui.sh                 # GUI под gtk4-broadwayd, без экрана
tools/test-win64.sh               # Win64 под wine: Unit_devMain, WinTasks
tools/check-reject.sh             # 8 отказов компилятора этапа 6
tools/bootstrap.sh                # три прохода Dev, B и C побайтно
tools/test.sh --list
tools/unit.sh Check_devMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
tools/bench-gc.sh System_devGcBench.DoSmall   # 1 и 10 млн, без 50 млн
```

- Тесты: подсистема `Unit` (`UnitAssert`, `UnitRunner`; правила —
  `Unit/Docu/ru/Testing.md`), манифест `tools/tests.txt` в формате
  `<модуль>|<marks>|<таймаут>|<доп. модули>|<опции>`. Пятое поле —
  опции модуля для `CompileThis` (например, `^` — ключ точек уступки;
  `tools/unit.sh --opts '^'`). `tools/test.sh` зовёт `tools/unit.sh`
  на каждый модуль: отдельный процесс, итог `failed 0`.
  Тесты лежат в отдельных подсистемах рядом с проверяемой: `<Sub>_dev`
  (`_dev` — как принято в сообществе: `Comm_dev` рядом с `Comm`; там же
  замеры, пробные модули, фикстуры отказов и техническая документация),
  файл `<Sub>_dev/Mod[64]/<Тема>.cp`, модуль `<Sub>_dev<Тема>`
  (`System_dev/Mod/Tasks.cp` — `System_devTasks`;
  `Dev_dev/Mod/Yield.cp` — `Dev_devYield`; точка входа без темы —
  `<Sub>_devMain`, например `System_devMain`). `UnitRunner.Run System`
  выбирает загруженные `System_dev*`; `all` — все модули, в имени
  которых есть `_dev`.
  Сценарии ядра — `System_devMain` с пометкой `proc`; задачи —
  `System_devTasks` (`proc slow`); этап 6 — `Dev_devActive`, `Dev_devYield`.
  Отдельные этапы `tools/test.sh`: `Comm_devResolver` (резолвер в
  `unshare -rmn`; без прав — `SKIP`), `Robust_devParallel`
  (1/4/16 процессов через `ROBUST_SIZE`), `Async_devMpi` (`mpiexec -n 5`,
  без MPICH — `SKIP`), `Demo_devGui` (под `gtk4-broadwayd`, без него —
  `SKIP`). Модуль `Lin_devFiles` (`proc slow`) занимает все 100000
  имён `odcNNNNN` и проверяет уникальные временные имена `LinFiles`.
  `tools/test32.sh` гоняет Unit, Files, Tasks, Monitors, Active, Yield,
  CoTests и UnloadGuard на i386 (`tools/build32.sh`, ELF32
  `blackbox-console`). Сопрограммы этапа 2 проверяет `System_devCoTests`
  через `libtest32.so`: `tools/i386-toolchain.sh` без sudo готовит
  `.work/i386dev` (`apt-get download libc6-dev-i386` и
  `lib32gcc-N-dev`, `dpkg-deb -x`, правка `libc.so`) и печатает флаги
  gcc; нет сети/пакета — SKIP, `System_devCoTests` пропускается.
  Итог — 21 этап, 108 тестов, 0 провалов.
- Поставочные сборки (`build64.sh`, `build32.sh`, `test-win64.sh`) не
  включают подсистемы `*_dev`: их нет в списке `CompileSubs`. Тесты
  собирает `tools/unit.sh`/`tools/go64.sh` по требованию; i386 —
  `tools/test32.sh`. Ключ `^` разрешён модулям `Dev_dev*`:
  `FrameworkModule` сравнивает имя подсистемы точно, и `Dev_dev`
  каркасом не считается.
- Трап внутри `Kernel.Try` не фатален и в консоли без `trapViewer`:
  `Kernel.HandleTrap` зовёт `platform.FatalError` только при
  `tryCount = 0`. Каркас зритель трапов не подменяет; консольный
  запускатель передаёт `UnitRunner.Run` ключ `quit`, чтобы получить код
  выхода. Экспорт `Kernel.GetTrapViewer` удалён.
- Этап 1: `Services.Post/Wake/NextDeadline`, `Console.WaitInput`,
  `ConsInterp.Run` крутит `Services.actionHook.Step`.
- Этап 2: реестр `Kernel.Stack` (`RegisterStack`,
  `RegisterSignalStack`, `UnregisterStack(At)`, `SwitchSave/Restore`),
  `Get/SetTryContext` в `LinKernel`; `UnloadMod` не выгружает занятый
  стеком модуль. `StackOf` ищет по отсортированному индексу стеков.
- Этап 3: `System/Mod64/Tasks.cp` — задачи со своим стеком:
  `Start/StartIn`, `Yield`, `Sleep`, `Join`, `Cancel`, `Park/Unpark`,
  группы, отмена трапом в точке ожидания, планировщик —
  `Services.Action`, переключатель — [code] на amd64 (без .so). Стеки:
  `Kernel.AllocStack/FreeStack` (в `LinKernel` mmap + сторожевая
  страница), `Platform.AllocateStackMem` по умолчанию возвращает 0.
  Тесты — `System_devTasks` (`System_dev/Mod/Tasks.cp`), подробности —
  `docs/tasks.md`.
- Этап 4: `Kernel.Lock/Unlock/Wait/Await`, таблица мониторов по адресу
  объекта, цепочка взятий у стека, `Kernel.Try` снимает до своей
  глубины, сборщик держит объекты, журнал тупика, `Tasks.SetScheduler`.
  Тесты — `System_devMonitors`, описание — `Docu/Async.md`.
- Этап 5: `Docu/Async.md` и `Docu/ru/Async.md`.
- Этап 6: компилятор (`Dev/Mod`): псевдомодуль `ACTIVE` и
  `ACTIVE.AWAIT` (скрытая процедура `@awaitN`, кадр в `BX` через
  `Kernel.CallCond`), `[exclusive]` (пролог/эпилог `Kernel.Lock/
  Unlock`, монитор модуля), ключ `^` (`Kernel.Poll` на обратном
  переходе циклов), скрытый импорт `"$$"`. Тесты — `Dev_devActive`,
  `Dev_devYield`; отказы — `tools/check-reject.sh`; бутстрап —
  `tools/bootstrap.sh` (второй проход совпадает, кроме 12-байтовой
  метки времени в `.ocf`). На i386 то же расширение работает:
  `CPT.Import`/`Compiler` разрешают `ACTIVE` и `^` для `processor = 10`,
  `CPC486` содержит пролог/эпилог `[exclusive]`, ядро `System/Mod` —
  стеки, мониторы, `Poll`, `CallCond` (кадр в `BX`), `System/Mod/Tasks`
  — переключатель x86. Тесты — `tools/test32.sh`.
- Этап 7: подсистема `Async/Mod` — `AsyncOnce` (Fill/Read),
  `AsyncChan` (`Chan`-встреча, `Buffer`, `Mailbox`) на
  `[exclusive]`+`ACTIVE.AWAIT`, `AsyncSelect` (`Alt`/`Select`:
  предложения в каналах, общий флаг решения, nack, тайм-аут) и
  `AsyncIO` («читать/писать или ждать»). `System/Mod/Poller.cp` —
  реестр готовности дескрипторов; `LinConsole.WaitInput` опрашивает
  stdin и его одним `poll`. `CommTCP__Lin` переведён с `select` на
  `poll` (fd_set < 1024), очередь accept — 128. Тесты —
  `Async_devChannels`, `Async_devIO` (эхо/HTTP, 1000 соединений, покой).
- Этап 8: `Async/Mod64/Work.cp` — фасад `AsyncWork` с делегатом
  `AsyncWork.Impl`; `Async/Mod64/WorkLin.cp` — пул pthread (задания и
  результаты в статических слотах, готовность — байт в pipe, `Poller`
  будит задачу); `Async/Mod64/WorkWin.cp` — пул `CreateThread`
  (очередь под `CRITICAL_SECTION`, готовность забирает действие
  `Services`); `Async/Mod64/WinSock.cp` — `getaddrinfo`. Делегата
  ставят `LinIntInit`/`WinIntInit`/`WinInit` через `Dialog.Call`.
  `Execute` из задачи — точка ожидания. `LinThreads`/`LinNative` —
  отдельные модули объявлений (символы `LinLibc` в хосте не обновить).
  `Kernel.WorkerAllocs` — отладочный счётчик `NEW` из рабочей нити.
  Тесты — `Async_devWorkers`, `Async_devWinWorkers`. Худший случай
  (недоступный сервер имён) — `Comm_devResolver` в `unshare -rmn`:
  10 010 мс ожидания в нити, 9 449 шагов главного цикла.
- Этап 9: `Async/Mod64/Computon.cp` — `Computon = Stores.Store` с
  `Do`, `Push/Pull/Try/Wait/Stop/SetHook/Unload`, транспорт —
  `fork+exec` `blackbox-console` и три pipe; гибель исполнителя —
  повтор, ошибка `Do` — код. Фасад `Robust/Mod64/Parallel.cp`
  (`RobustParallel`, ранги MPI: 0 — управляющий, 1..size−1 —
  исполнители, размер мира — `ROBUST_SIZE`). MPI-транспорт —
  `Async/Mod64/Mpi.cp` поверх MPICH из `.work/venv`
  (`SetTransport("mpi")`, ранг 0 — тест, ранги 1..n —
  `AsyncMpi.WorkerLoop`). Тесты — `Async_devComputons` (1/4/16 процессов,
  убийство, `AltPull`), `tools/test-robust.sh`, `tools/test-mpi.sh`.
- Этап 10: `Async/Mod/Proto.cp` — необязательный протокол канала по
  образцу Zonnon (автомат из описания, `AsyncChan.SetProtocol`, трап с
  текстом на нарушение; проверяются и прямые `Send`, и `SendAlt` в
  точке исполнения предложения). Транспорт
  компутонов `pipe`/`tcp` — `SetTransport` или `ASYNC_TRANSPORT`,
  TCP по localhost, сокет исполнителя блокирующий. `Unload(module)`
  выгружает код у всех исполнителей (отказ `20`, если занят) —
  `System_devUnload` перекомпилирует `System_devUnloadCode` и проверяет новый
  `Do`. Метка времени компиляции — `BB_COMPILE_TIME` в `DevCPE`;
  `tools/bootstrap.sh` сравнивает проходы B и C побайтно без маски.
  Тесты — `Async_devProtocol`, `System_devUnload`, TCP-варианты в
  `Async_devComputons`.
- Подсистема `Async` собирается в `tools/build64.sh` **отдельным**
  вызовом `CompileSubs @Lin Async`: ей нужны `Tasks`, `LinNative` и
  `LinThreads`, которые собираются выше отдельными `CompileThis`.
  Следом так же отдельно собирается `Robust` (фасад `RobustParallel`
  зависит от `AsyncComputon`).
- GUI-прогон: `LinInit` читает внешнюю переменную `BB_RUN=Модуль.
  Процедура` и зовёт команду до `Loop.Start`; `tools/test-gui.sh`
  поднимает `gtk4-broadwayd` в своём `XDG_RUNTIME_DIR`. Системный
  `broadwayd` — от GTK3 и с клиентом GTK4 падает; нужен
  `gtk4-broadwayd` (пакет `libgtk-4-bin`, в стенде распакован в
  `.work/gtk4-bin`). Мышь не эмулируется.
- Windows x64: `WinKernel` получил `AllocateStackMem` (`VirtualAlloc`
  + `PAGE_NOACCESS`), `Get/SetTryContext` и загрузку библиотек
  динамически загружаемых interface-модулей; `tools/test-win64.sh`
  собирает @Win, линкует консольный PE (`WinIntInit`, подсистема
  правится на console) и под wine гоняет `Unit_devMain` 4/4,
  `System_devWinTasks` 5/5 и `Async_devWinWorkers` 4/4 (пул `CreateThread`,
  файл, `getaddrinfo`).
- `OdcTextU`/`tools/sync-odc.sh` в `active2` не перенесены: исходники —
  `.cp`, конвертировать нечего. `DevOnce` заменён на
  `DevCompiler64.CompileThis`, потому что `Views.Old` не открывает `.cp`
  в консольном x64.
- Открыто: GUI-прогон Windows (пул регистрируется из `WinInit`, но
  отдельного теста под wine нет); повтор и `Unload` в MPI-транспорте
  (mpiexec гасит мир); `pack-dev0-linux` (в снимке отсутствует;
  `tools/bootstrap.sh` пересобирает только `Dev`), вытеснение по
  сигналу и нити ОС; библиотека `Async/Mod64` (каналы, рабочие нити,
  компутоны) на i386 не собирается, индекс кластеров шага 2а остался
  только в Mod64; отложенные финализаторы, нагрузочный тест «1000
  задач час».
- Подробности: `docs/ivan-dia261123.md` (разбор снимка и таблица
  изменений), `docs/stand.md` (стенд), `docs/gc-bench.md` (замер),
  `docs/async-library.md` (каналы, рабочие нити, компутоны).
