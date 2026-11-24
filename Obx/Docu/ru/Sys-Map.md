# Карта «Обзора на примерах»: задачи, мониторы, каналы, акторы, хранилище по содержимому

Полная карта подсистемы `Obx` — английская, `Obx/Docu/Sys-Map.md`. Здесь — разделы примеров задач,
мониторов, каналов и акторов (исходники в `Obx/Mod64`) и хранилища по содержимому (исходники в `Obx/Mod`); у
каждого примера есть страница в этом каталоге.

Задачи, мониторы и каналы
ObxTasks0  docu/sources		две задачи делят счётчик без монитора
ObxTasks1  docu/sources		ожидание: Park и Unpark, Sleep, Join
ObxTasks2  docu/sources		сервер и сто клиентов на мониторе модуля
ObxTasks3  docu/sources		группа задач: провал ребёнка отменяет остальных
ObxTasks4  docu/sources		тупик: планировщик печатает, кто чего ждёт
ObxChan0  docu/sources		встреча: канал без хранения сообщений
ObxChan1  docu/sources		ограниченный буфер даёт производителю забежать вперёд
ObxChan2  docu/sources		почтовый ящик: отправка не ждёт
ObxSelect0  docu/sources		выбор с тайм-аутом
ObxProto0  docu/sources		протокол диалога, проверка при каждой отправке
ObxOnce  docu/sources		однократная переменная
ObxStm0  docu/sources		транзакция: трап в переводе не теряет денег
ObxStm1  docu/sources		Retry: ожидание условия над двумя переменными
ObxStm2  docu/sources		OrElse: оплата с одной карты или с другой
ObxWork0  docu/sources		блокирующий вызов в рабочей нити
ObxComputon  docu/sources		компутон в отдельном процессе
ObxActor0  docu/sources		первый актор: Spawn, Tell, Ask, Shutdown
ObxActor1  docu/sources		надзор: трап и перезапуск теряет состояние
ObxActor2  docu/sources		банк: Resume сохраняет остатки счетов
ObxActor3  docu/sources		счёт с журналом в Kv переживает перезапуск
ObxActor4  docu/sources		снимки и история изменений (аудит)
ObxActor5  docu/sources		CQRS: отчёт «обороты по счетам» догоняет журнал
ObxActor6  docu/sources		два узла: Ask через узел, поддельный адрес, потеря узла
ObxActor7  docu/sources		сага: перевод между узлами переживает падение координатора
ObxActor8  docu/sources		виртуальный счёт: по номеру, переезд при падении узла
ObxBank0  docu/sources		учебник: действие и задача делают одну работу
ObxBank1  docu/sources		учебник: кассиры, Join, Sleep, отмена, группа
ObxBank2  docu/sources		учебник: ожидание денег без монитора и с AWAIT
ObxBank3  docu/sources		учебник: тупик на мониторах двух счетов
ObxBank4  docu/sources		учебник: переводы транзакциями, Retry, OrElse
ObxBank5  docu/sources		учебник: кассир владеет счетами, заявки через ящик
ObxBank6  docu/sources		учебник: выбор из заявки, закрытия и тайм-аута; обещание

Хранилище по содержимому
ObxCas0  docu/sources		блок называется своим хешем: дедупликация
ObxCas1  docu/sources		нарезка двух почти одинаковых входов по содержимому
ObxCas2  docu/sources		два автора публикуют наборы изменений, один переносит свой

Хранилище «ключ → значение»
ObxKv0  docu/sources		хранилище Kv: открыть, записать, прочитать, обойти диапазон
ObxKv1  docu/sources		журнал событий: дозапись, хвост, снимок читателя
ObxKv2  docu/sources		перевод между счетами в одной транзакции и отказ Abort
