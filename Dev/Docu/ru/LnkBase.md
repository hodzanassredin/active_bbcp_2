# DevLnkBase: общая модель линкера

[Исходник](../../Mod/LnkBase.cp) · [Драйвер](Linker1.md)

Хранит секции, модули, экспортные/импортные символы, таблицы исправлений и параметры целевого ELF/PE. Его записи используют LnkLoad и все LnkWrite*.

## Основные записи

- Section: bytes, size, назначенный addr.
- Module: code/data/vars, codeFix/dataFix, смещение module descriptor, terminal address, DLL/export-признаки и список символов.
- Symbol: имя, секция/адрес, индексы строк/символов, тип procedure/object.
- FixTable/FixRec: место исправления, его вид и целевая секция.
- StringTable: растущий буфер имён с текущей длиной.

Init сбрасывает состояние и выбирает цель. RegMod/ThisDll ведут списки; ImportProc/ExportProc/ExportVar регистрируют внешние связи. Error выставляет общий признак error и сообщает причину.

## Размещение и исправления

NewSection создаёт место под секцию. FixCode/FixRel/FixData/FixCopy регистрируют исправления; окончательный DoFixups выполняется после SetAddr. AddReloc/SortReloc обслуживают таблицу relocation. BeginLinking подготавливает итоговые размеры и служебный код инициализации/завершения.

Нужно различать смещение внутри секции, конечный виртуальный адрес и позицию байтов в файле. Writers знают файловое размещение, LB назначает адреса секций, LnkLoad переводит chains `.ocf` в общие FixTable.

InitStringTable/AddName/AddHintName/AddDllName/WriteStringTable строят строки нужного формата; Aligned округляет позицию. ThisObjFile получает путь через Librarian категории Code.

## Платформенный hook

SetChmodHook устанавливает объект ChmodHook. Chmod делегирует ему установку прав; при NIL ничего не делает. Реализации DevChmod__Lin/Fbsd/Nbsd/Obsd регистрируются своим телом модуля. Наличие вызова Chmod в ELF writer само по себе не гарантирует наличие hook.

При изменении структур согласовывать loader и writers. Глобальные target/opt/modList и счётчики относятся к одной текущей сборке.
