# DevLnkWriteElf: ELF32 с динамическими связями

[Исходник](../Mod/LnkWriteElf.cp) · [Драйвер](Linker1.md) · [Данные](LnkBase.md)

Записывает ELF32 x86 из глобального состояния LB: executable или shared library согласно выбранной цели. Это не генератор 64-битного ELF, несмотря на возможную 64-битную host ОС.

## Контракт

Init определяет секции/сегменты и служебные таблицы. GetBases возвращает bases code/data/vars. Драйвер затем вызывает LB.SetAddr и LB.DoFixups, после чего WriteOut выполняет запись.

Основные внутренние этапы: DefSegments/DefSections, CollectImport/CollectExport, CalculateLayout, WriteElfHeader, WriteProgramHeaderTable, WriteInterpreter, WritePlt, WriteCode/WriteData, WriteSymbolTable/WriteHash, WriteReloc/WriteDynamic, WriteSectionTable. Поддержаны OS-specific notes через WriteObsdNote/WriteFbsdNote/WriteNbsdNotes.

Параметры interpreter, OSABI, OS и rpath берутся из LB.opt; различать runtime virtual addresses, offsets в файле и индексы symbol/string tables. Изменения одного из них затрагивают несколько заголовков и relocation-записей.

WriteOut регистрирует файл и обращается к LB.Chmod. Действительная установка прав зависит от [платформенного hook](Chmod__Lin.md). Проверка будущих изменений должна включать разбор созданного ELF и запуск на соответствующей целевой системе; одного успешного WriteOut недостаточно.
