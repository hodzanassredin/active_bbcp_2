# Снимок Ивана DIA261123: разбор и сравнение с b3e3b9d0

Источник: `/tmp/claude-1000/ivan-dia261123/oberonai` (не git, ≈5700 файлов,
GTK4, x64). Ветка `ivan-dia261123` — этот снимок одним коммитом поверх
`b3e3b9d0`; ветка `active2` — наши этапы 1–2 и стенд, перенесённые на
`.cp`-исходники снимка.

## 1. Как у Ивана собирать и запускать

Правила из его `AGENTS.md` (дословно по смыслу):

- `OLD/` — архив, не компилировать и не запускать тесты оттуда.
- `dev0Linux` — консольная версия с компилятором; запускать из корня
  репозитория, где лежит исполняемый файл.
- Исходники — `.cp` (UTF-8), документация — `.md`; в `Mod64` лежат
  варианты для amd64, имя модуля без суффикса `__x64`.
- Табуляция — только табы, не пробелы; пустая строка перед новым блоком
  (`IMPORT`, `CONST`, `TYPE`, `PROCEDURE`).
- Не запускать автоматические тесты перетаскивания мышью (был краш
  системы); file/text drag-and-drop проверять вручную.
- Параметры команде — через `DevCommanders.par`; внешние переменные —
  `./dev0Linux --ИМЯ=значение` и `Dialog.GetPar`.

Сборка x64 (пример из его `AGENTS.md`):

```sh
echo "DevLibrarian.EnableDevLib('build/BB64LIN', 'Mod64');
DevCompiler64.CompileSubs @Lin Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket;
DevLinker1.LinkElfExe64 'build/BB64LIN/blackbox' := Kernel+ LinKernel Files LinFiles StdLoader LinLoader" | ./dev0Linux
```

Для консоли вместо `LinLoader` линкуется `LinIntInit`:

```sh
DevLinker1.LinkElfExe64 'out/blackbox-console' := Kernel+ LinKernel Files LinFiles StdLoader LinIntInit
```

`LinLoader` всегда стартует GUI; `blackbox --console` консольный режим не
выбирает. Команды консоли читаются со stdin: `echo 'Модуль.Процедура' |
./blackbox-console`.

Проверки (из `AGENTS.md` и `Dev/Docu/Building-AMD64.md`):

```sh
python3 Dev/Test/check_mod64_layout.py
python3 Lin/Test/check_linux64.py --out build/blackbox-lin64
python3 Lin/Test/check_gtk4.py
python3 Win/Test/check_subsystem64.py --out build/blackbox-win64
python3 Win/Test/build_mdi64.py --out build/blackbox-mdi-win64 --test
```

Каждый процесс компилятора/линковщика должен сам вызвать
`DevLibrarian.EnableDevLib('<out>', 'Mod64')`; librarian выбирает
`<Sub>/Mod64`, иначе `<Sub>/Mod`, и пишет Code/Sym в `<out>`. Выводы i386,
Linux AMD64 и Windows AMD64 держать раздельно. `pack-dev0-linux` в снимке
отсутствует (есть только `Dev/Tools/__pycache__/pack_dev0_linux.pyc`);
поэтому `dev0Linux` в коммите оставлен бинарником.

## 2. Что нового в снимке

- **Исходники — `.cp`** (713 файлов) вместо `.odc`; `.odc` остались только
  ресурсами (`*/Rsrc/*.odc`). Документация — `Docu/*.md`, `Dev/Docu/*.md`,
  `Lin/Docu/*.md`.
- **GTK4-бэкенд**: новые `Lin/Mod/Gtk4Bootstrap`, `Gtk4IO`, `Gtk4Util`,
  `Gtk4Gdk`, `Gtk4Gtk`, `Gtk4Print`, `Gtk4Files`, `Gtk4X11`, `X11`,
  `Fenv`; `LinPorts` переписан на Cairo-поверхности; `LinGui`,
  `LinBackends`, `LinClipboard`, `LinDialog`, `LinFileDrops`,
  `LinPrinters`, `LinPrintPorts`, `LinRastersPng`, `LinFonts`,
  `LinGdkPixbuf` переведены на GTK4. `Lin/Mod/Init` импортирует GTK4 и
  `Fenv`, `FetchEvent` крутит `GU.Iterate`.
- **`Aitools`**: `ImportCp`/`ExportBlackBox` — конвертация `.cp` ↔ `.odc`.
- **`Check`**: ~100 модулей проверок (ELF64, PE64, callback, heap, GTK4,
  signals, Math64, WinAPI layout и т. п.) и `Check/Mod64`.
- **`Comm`/`Demo`**: `Comm`-подсистема, `DemoParameters`,
  `DemoExternalVariable`, `DemoMD5`.
- **Компилятор**: `Dev/Mod/CPT` больше не вставляет недокументированный
  `ADRINT`; `CPM` — правка комментария; `CPCamd64` — шапка. Генератор
  amd64 (`CPVamd64`, `CPLamd64`, `CPUnwind`) по смыслу не менялся.
- **Каркас**: `System/Mod/Kernel` переведён с локального `ADDRESS` на
  `S.ADDRESS`; `Std/Mod/Scrollbars` исправляет закрытие старого
  scrollbar-кадра; `Dev/Mod/HeapSpy` — HiDPI и разрешение карты.

## 3. Сравнение с b3e3b9d0

Сравнивались тексты: `.odc` из b3e3b9d0 экспортированы `OdcTextU` (текст
без вложенных views; свёрнутые/удалённые фрагменты не считаются кодом),
`.cp` — как есть. После снятия шапки `(** ... **)` и view-тегов:

| Итог | Файлов |
|---|---:|
| Совпало по смыслу | 551 |
| Изменилось по смыслу | 41 |
| Новых `.cp` | 121 |

Почти все «изменения» в остальных 400+ файлах — только шапка:
`changes=""`, `issues=""` (Иван вычистил историю из конвертированных
файлов). Это не путать с настоящими правками.

### Ключевые модули

| Модуль | Что изменилось в снимке | Пересечение с нашими правками |
|---|---|---|
| `System/Mod/Kernel.cp` | `ADDRESS = INTEGER` удалён, везде `S.ADDRESS`; смысл тот же | Нет (мы правили только `Mod64`) |
| `System/Mod64/Kernel.cp` | По смыслу совпадает с b3e3b9d0 (старые строки свёрнуты) | Да: этап 2 перенесён без конфликтов |
| `Lin/Mod/Kernel.cp` (32-бит) | `S.ADDRESS`, `SIGBUS`, отдельный `InitSignalStack`, `InstallSignals` — только 6 fault-сигналов, иначе `trap.top` | Нет (наш этап 2 — в `Mod64`) |
| `Lin/Mod64/Kernel.cp` | То же + убран отладочный лог трапа, `trap.top` восстанавливается всегда | Да: `Get/SetTryContext`, `InRegisteredStack`, регистрация сигнального стека; конфликт снят |
| `System/Mod/Services.cp` | Без изменений | Да: этап 1 (очередь `Post`, `NextDeadline`, `executing`) |
| `System/Mod/Console.cp`, `Lin/Mod/Console.cp` | Без изменений | Да: `WaitInput`/`poll` |
| `Lin/Mod/Libc.cp`, `Lin/Mod64/Libc.cp` | Без изменений | Да: `pollfd`, `setvbuf`, `poll` |
| `Cons/Mod/Interp.cp` | Буфер команды `1024*8` → `4096` | Да: цикл `Services.actionHook.Step` + `WaitInput` |
| `Dev/Mod/CP*.cp` | Мелкие: `ADRINT` убран, комментарии, шапка | Нет |
| `System/Mod/Loop.cp` | Без изменений | Нет |
| `Lin/Mod/Init.cp` | GTK4, `Fenv`, `GU.Iterate` | Нет |
| `Lin/Mod/Ports`, `Backends`, `Clipboard`, `Dialog`, `Gui`, `FileDrops`, `Printers`, `PrintPorts`, `RastersPng`, `Fonts`, `GdkPixbuf` | Крупный переход на GTK4/Cairo | Нет |
| `Std/Mod/Scrollbars`, `Dev/Mod/HeapSpy` | Исправления кадров и HiDPI | Нет |

Остальные 27 смысловых правок — мелкие: `Gtk2GObject` (`GType = LONGINT`,
`g_object_ref_sink`), `Cairo` (6 новых функций для поверхностей),
`GdkPixbuf` (комментарии GHashTable), `MaxMemory` (512 → 128 массивов),
`TextConv`/`Browser`/`Config`/`Windows` — в основном шапки и пробелы.

### Новые модули (121)

- `Check/Mod/*` (≈100) и `Check/Mod64/LinPrintApi` — проверки.
- `Lin/Mod64/*`: `Backends`, `Cairo`, `CairoPango`, `Gtk2*`-варианты,
  `Gtk3*`, `Gtk4*` — сборка GTK2/3/4 под amd64.
- `Lin/Mod/*`: `Fenv`, `X11`, `Gtk3*`, `Gtk4*` — платформенный слой.
- `Aitools/Mod/*`, `Demo/Mod/*`.

## 4. Что вошло в коммит снимка

Ветка `ivan-dia261123` = `b3e3b9d0` + один коммит «Снимок Ивана
DIA261123». Дерево заменено содержимым снимка.

Выкинуто:

- `OLD/` — по правилу Ивана это архив, не источник сборки (41 МБ).
- `build/` — 1234 скомпилированных файла и логи проверок (пересобираются).
- все `*.ocf`/`*.osf` — 761 объект в подсистемах (пересобираются
  `CompileSubs`; проверено на чистом дереве).
- `__pycache__/`, `*.pyc` — генерируемое.

Оставлено:

- `dev0Linux` — bootstrap, без него снимок не собрать, а `pack-dev0-linux`
  в снимке нет.
- `Sciences_Crelier_1994.pdf` — документация, не артефакт сборки.
- `.gitignore` — `*.ocf`, `*.osf`, `build/`, `OLD/`, `.work/`,
  `__pycache__/`.

Проверка: дерево без `.ocf/.osf` собирается полностью
(`DevCompiler64.CompileSubs @Lin …` — 302 модуля) и линкует
`blackbox-console`.

## 5. Что перенесено в active2

| Наше | Куда | Как |
|---|---|---|
| Этап 1: `Services.Post/Wake/NextDeadline`, `executing` | `System/Mod/Services.cp` | применено к тексту снимка |
| `WaitInput` | `System/Mod/Console.cp`, `Lin/Mod/Console.cp` | + `poll`, `setvbuf` в оба `Libc` |
| Консольный цикл | `Cons/Mod/Interp.cp` | поверх буфера 4096 |
| Этап 2: реестр стеков | `System/Mod64/Kernel.cp` | полностью |
| `Get/SetTryContext`, сигнальный стек | `Lin/Mod64/Kernel.cp` | поверх `InitSignalStack` Ивана |
| Тесты | `<Подсистема>_tests/Mod/*.cp`, `tools/tests.txt` | `.odc.txt` → `.cp` без изменений |
| `tools/test64.c`, `tools/ocf.py` | `tools/` | без изменений |
| `tools/*.sh` | `tools/` | переписаны под `dev0Linux` и `blackbox-console` |

Не перенесено:

- `Odc/Mod/TextU` и `tools/sync-odc.sh` — в снимке исходники уже `.cp`;
  конвертировать нечего, `.odc` — только ресурсы. Инструмент остался в
  ветке `active`.
- `Dev/Mod/Once` — проверен: `Librarian.GetSpec`/`Views.Old` не открывают
  `.cp` в консольном x64 (нет конвертера). `CompileThis` стандартного
  компилятора решает ту же задачу; `tools/go64.sh` использует его.
- `tools/world64.sh` — у Ивана нет отдельного мира-скелета: сборка идёт
  прямо в каталог установки `.work/BB64LIN`.
