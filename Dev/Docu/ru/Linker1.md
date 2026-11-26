# DevLinker1: команды сборки ELF и PE

[Реализованная сборка Windows x64](Linker1-Win64.md) — новая команда `LinkPeExe64`.

[План поддержки AMD64](Linker1-AMD64-Plan.md) — анализ от 2026-11-07, сохранённый для последующей реализации.

[Исходник](../../Mod/Linker1.cp) · [Общая модель](LnkBase.md) · [Чтение OCF](LnkLoad.md)

Модуль координирует разбор команды, загрузку модулей и выбранный writer. Это отдельная реализация относительно [DevLinker](Linker.md); её команды надо брать из Linker1, а не переносить из старого руководства Linker.

## Команды

| Команда | Цель |
|---|---|
| LinkPeExe64 | Windows x64 PE32+ из Dev OCF AMD64 |
| LinkPeExe / LinkPe | PE executable; соответственно dynaInit=FALSE / TRUE |
| LinkPeDll | PE DLL |
| LinkElfExe / LinkElf | ELF executable; соответственно dynaInit=FALSE / TRUE |
| LinkElfDll | ELF shared library |
| LinkElfStatic | Статический ELF |

Каждая команда вызывает LB.Init(target), затем LinkIt. Формат параметров: `[OS] output := modules`. OS читается для ELF executable: Linux, FreeBSD, OpenBSD, NetBSD; устанавливаются ABI и interpreter. Суффикс `+` у модуля выбирает KernelName, `$` — mainName. Обычные имена передаются AddModule или ExportModule в зависимости от цели.

Пример синтаксиса из исходника: `DevLinker1.LinkElfExe Linux BlackBox1 := Kernel$+ HostFiles StdLoader`. Это пример разбора, не рекомендация пересобрать текущий исполняемый файл.

## Последовательность LinkIt0

1. LB.BeginLinking подготавливает общую модель.
2. Init выбранного writer рассчитывает необходимое размещение.
3. GetBases возвращает bases кода, данных и переменных.
4. LB.SetAddr назначает адреса секциям; LB.DoFixups исправляет ссылки.
5. WriteOut записывает ELF/PE; результат выводится в журнал при отсутствии LB.error.

В writer передаётся глобальное состояние LB, а не независимый immutable объект сборки. Одновременные вызовы не предусмотрены. Старые команды `Dev2...` после END в исходнике — исторический текст; фактические импорты называются DevLnk*.
