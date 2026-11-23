# DevLnkWriteElfStatic: статический ELF32

[Исходник](../Mod/LnkWriteElfStatic.cp) · [Драйвер](Linker1.md) · [Данные](LnkBase.md)

Отдельный writer для цели tgtElfStatic. Использует общие секции и уже разрешённые адреса LB, формирует ELF header, program header table, код и данные.

Init/DefSegments подготавливают размещение; GetBases сообщает адреса драйверу. После SetAddr/DoFixups вызывается WriteOut, внутри которого CalculateLayout согласует размеры и смещения, затем WriteElfHeader/WriteProgramHeaderTable/WriteCode/WriteData выпускают файл.

В отличие от динамического [LnkWriteElf](LnkWriteElf.md), здесь нет аналогичного набора PLT, dynamic symbol/hash tables и interpreter. Это отдельная ветвь реализации, а не просто переключатель форматирования вывода.

AddReloc присутствует в экспортируемом интерфейсе, но его тело в текущем исходнике пусто; оно не создаёт PE-подобную таблицу relocation. WriteOut вызывает LB.Chmod после записи; нужен установленный платформенный hook. При изменении базы/выравнивания проверять весь путь GetBases → SetAddr → CalculateLayout.
