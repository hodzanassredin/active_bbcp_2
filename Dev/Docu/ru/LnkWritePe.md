# DevLnkWritePe: исполняемый PE и DLL

[Исходник](../Mod/LnkWritePe.cp) · [Драйвер](Linker1.md) · [Данные](LnkBase.md)

Writer PE для семейства DevLinker1. Получает подготовленные code/data/vars, импорты, экспорты и relocation из DevLnkBase. Он не занимается разбором Component Pascal или вычислением CPT fingerprints.

Экспортируемый контракт состоит из Init, GetBases и WriteOut. Порядок вызовов задаёт LinkIt0: инициализация writer, получение bases, LB.SetAddr, LB.DoFixups, запись файла. Режим executable/DLL определяется общей целью LB; это не команды DevLinker.LinkExe/LinkDll из другого модуля.

При чтении исходника проследить заголовки PE, таблицы импортов/экспортов, раскладку секций и relocation. Для адреса различать абсолютный адрес, RVA относительно ImageBase и файловое смещение; параметры базы и subsystem находятся в LB.opt.

Изменения writer проверять на executable и DLL, с импортами и экспортами, а также при отличной от стандартной image base. Корректность генерации `.ocf` не доказывает корректность результирующего PE.
