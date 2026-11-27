# Карта «Обзора на примерах»: задачи, мониторы, каналы, акторы, хранилище по содержимому

Полная карта подсистемы `Obx` — английская, `Obx/Docu/Sys-Map.md`. Здесь — разделы примеров задач,
мониторов, каналов и акторов (исходники в `Obx/Mod64`) и хранилища по содержимому (исходники в `Obx/Mod`); у
каждого примера есть страница в этом каталоге.

Задачи с пропуском и монитор-объект
[ObxBufCtx](BufCtx.md)  [docu](BufCtx.md)/[sources](../../Mod64/BufCtx.cp)		буфер на мониторе-объекте: пропуск, операция, флаги и условие
[ObxBankCtx](BankCtx.md)  [docu](BankCtx.md)/[sources](../../Mod64/BankCtx.cp)		перевод повторным входом в монитор и с двумя мониторами в руках
[ObxDownCtx](DownCtx.md)  [docu](DownCtx.md)/[sources](../../Mod64/DownCtx.cp)		родитель, дети и срок на мониторе-объекте
[ObxDownChan](DownChan.md)  [docu](DownChan.md)/[sources](../../Mod64/DownChan.cp)		та же загрузка на канале с пропуском
[ObxPipeCtx](PipeCtx.md)  [docu](PipeCtx.md)/[sources](../../Mod64/PipeCtx.cp)		конвейер из трёх стадий на каналах с пропуском
[ObxRestart](Restart.md)  [docu](Restart.md)/[sources](../../Mod64/Restart.cp)		одна задача запускается многократно, без выделений

Задачи, мониторы и каналы
[ObxTasks0](Tasks0.md)  [docu](Tasks0.md)/[sources](../../Mod64/Tasks0.cp)		две задачи делят счётчик без монитора
[ObxTasks1](Tasks1.md)  [docu](Tasks1.md)/[sources](../../Mod64/Tasks1.cp)		ожидание: Park и Unpark, Sleep, Join
[ObxTasks2](Tasks2.md)  [docu](Tasks2.md)/[sources](../../Mod64/Tasks2.cp)		сервер и сто клиентов на мониторе модуля
[ObxTasks3](Tasks3.md)  [docu](Tasks3.md)/[sources](../../Mod64/Tasks3.cp)		группа задач: провал ребёнка отменяет остальных
[ObxTasks4](Tasks4.md)  [docu](Tasks4.md)/[sources](../../Mod64/Tasks4.cp)		тупик: планировщик печатает, кто чего ждёт
[ObxChan0](Chan0.md)  [docu](Chan0.md)/[sources](../../Mod64/Chan0.cp)		встреча: канал без хранения сообщений
[ObxChan1](Chan1.md)  [docu](Chan1.md)/[sources](../../Mod64/Chan1.cp)		ограниченный буфер даёт производителю забежать вперёд
[ObxChan2](Chan2.md)  [docu](Chan2.md)/[sources](../../Mod64/Chan2.cp)		почтовый ящик: отправка не ждёт
[ObxSelect0](Select0.md)  [docu](Select0.md)/[sources](../../Mod64/Select0.cp)		выбор с тайм-аутом

События
[ObxEvent0](Event0.md)  [docu](Event0.md)/[sources](../../Mod64/Event0.cp)		канал обмена: две задачи меняются значениями за одну синхронизацию
[ObxEvent1](Event1.md)  [docu](Event1.md)/[sources](../../Mod64/Event1.cp)		буферизованный канал из встреч на сервере
[ObxEvent2](Event2.md)  [docu](Event2.md)/[sources](../../Mod64/Event2.cp)		сервер и отмена клиента через withNack
[ObxEvent3](Event3.md)  [docu](Event3.md)/[sources](../../Mod64/Event3.cp)		мультикаст: выбор решает первого получателя, действие — второго
[ObxEvent4](Event4.md)  [docu](Event4.md)/[sources](../../Mod64/Event4.cp)		срок, построенный при синхронизации, через Guard
[ObxEvent5](Event5.md)  [docu](Event5.md)/[sources](../../Mod64/Event5.cp)		обедающие философы: официант выдаёт две вилки выбором
[ObxProto0](Proto0.md)  [docu](Proto0.md)/[sources](../../Mod64/Proto0.cp)		протокол диалога, проверка при каждой отправке
[ObxOnce](Once.md)  [docu](Once.md)/[sources](../../Mod64/Once.cp)		однократная переменная
[ObxStm0](Stm0.md)  [docu](Stm0.md)/[sources](../../Mod64/Stm0.cp)		транзакция: трап в переводе не теряет денег
[ObxStm1](Stm1.md)  [docu](Stm1.md)/[sources](../../Mod64/Stm1.cp)		Retry: ожидание условия над двумя переменными
[ObxStm2](Stm2.md)  [docu](Stm2.md)/[sources](../../Mod64/Stm2.cp)		OrElse: оплата с одной карты или с другой
[ObxWork0](Work0.md)  [docu](Work0.md)/[sources](../../Mod64/Work0.cp)		блокирующий вызов в рабочей нити
[ObxComputon](Computon.md)  [docu](Computon.md)/[sources](../../Mod64/Computon.cp)		компутон в отдельном процессе
[ObxActor0](Actor0.md)  [docu](Actor0.md)/[sources](../../Mod64/Actor0.cp)		первый актор: Spawn, Tell, Ask, Shutdown
[ObxActor1](Actor1.md)  [docu](Actor1.md)/[sources](../../Mod64/Actor1.cp)		надзор: трап и перезапуск теряет состояние
[ObxActor2](Actor2.md)  [docu](Actor2.md)/[sources](../../Mod64/Actor2.cp)		банк: Resume сохраняет остатки счетов
[ObxActor3](Actor3.md)  [docu](Actor3.md)/[sources](../../Mod64/Actor3.cp)		счёт с журналом в Kv переживает перезапуск
[ObxActor4](Actor4.md)  [docu](Actor4.md)/[sources](../../Mod64/Actor4.cp)		снимки и история изменений (аудит)
[ObxActor5](Actor5.md)  [docu](Actor5.md)/[sources](../../Mod64/Actor5.cp)		CQRS: отчёт «обороты по счетам» догоняет журнал
[ObxActor6](Actor6.md)  [docu](Actor6.md)/[sources](../../Mod64/Actor6.cp)		два узла: Ask через узел, поддельный адрес, потеря узла
[ObxActor7](Actor7.md)  [docu](Actor7.md)/[sources](../../Mod64/Actor7.cp)		сага: перевод между узлами переживает падение координатора
[ObxActor8](Actor8.md)  [docu](Actor8.md)/[sources](../../Mod64/Actor8.cp)		виртуальный счёт: по номеру, переезд при падении узла
[ObxActor9](Actor9.md)  [docu](Actor9.md)/[sources](../../Mod64/Actor9.cp)		групповые стратегии: все за одного и остальные за одного
[ObxActor10](Actor10.md)  [docu](Actor10.md)/[sources](../../Mod64/Actor10.cp)		маршрутизаторы: по кругу, всем, рандеву-хеш
[ObxActor11](Actor11.md)  [docu](Actor11.md)/[sources](../../Mod64/Actor11.cp)		поток событий: подписки, предикат, мёртвые письма
[ObxRaft0](Raft0.md)  [docu](Raft0.md)/[sources](../../Mod64/Raft0.cp)		Raft: три узла в памяти, выборы, запись, чтение, гибель лидера
[ObxRaft1](Raft1.md)  [docu](Raft1.md)/[sources](../../Mod64/Raft1.cp)		стойкий счётчик на журнале Raft переживает гибель своего узла
[ObxBank0](Bank0.md)  [docu](Bank0.md)/[sources](../../Mod64/Bank0.cp)		учебник: действие и задача делают одну работу
[ObxBank1](Bank1.md)  [docu](Bank1.md)/[sources](../../Mod64/Bank1.cp)		учебник: кассиры, Join, Sleep, отмена, группа
[ObxBank2](Bank2.md)  [docu](Bank2.md)/[sources](../../Mod64/Bank2.cp)		учебник: ожидание денег без монитора и с монитором-объектом
[ObxBank3](Bank3.md)  [docu](Bank3.md)/[sources](../../Mod64/Bank3.cp)		учебник: тупик на мониторах двух счетов
[ObxBank4](Bank4.md)  [docu](Bank4.md)/[sources](../../Mod64/Bank4.cp)		учебник: переводы транзакциями, Retry, OrElse
[ObxBank5](Bank5.md)  [docu](Bank5.md)/[sources](../../Mod64/Bank5.cp)		учебник: кассир владеет счетами, заявки через ящик
[ObxBank6](Bank6.md)  [docu](Bank6.md)/[sources](../../Mod64/Bank6.cp)		учебник: выбор из заявки, закрытия и тайм-аута; обещание

Хранилище по содержимому
[ObxCas0](Cas0.md)  [docu](Cas0.md)/[sources](../../Mod/Cas0.cp)		блок называется своим хешем: дедупликация
[ObxCas1](Cas1.md)  [docu](Cas1.md)/[sources](../../Mod/Cas1.cp)		нарезка двух почти одинаковых входов по содержимому
[ObxCas2](Cas2.md)  [docu](Cas2.md)/[sources](../../Mod/Cas2.cp)		два автора публикуют наборы изменений, один переносит свой

Хранилище «ключ → значение»
[ObxKv0](Kv0.md)  [docu](Kv0.md)/[sources](../../Mod64/Kv0.cp)		хранилище Kv: открыть, записать, прочитать, обойти диапазон
[ObxKv1](Kv1.md)  [docu](Kv1.md)/[sources](../../Mod64/Kv1.cp)		журнал событий: дозапись, хвост, снимок читателя
[ObxKv2](Kv2.md)  [docu](Kv2.md)/[sources](../../Mod64/Kv2.cp)		перевод между счетами в одной транзакции и отказ Abort

Карта примеров подсистемы Demo
Demo  subsystem  map		переводы известных примеров параллелизма на задачи и каналы (Demo/Docu/Sys-Map)
