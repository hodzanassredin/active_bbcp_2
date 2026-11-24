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

Хранилище по содержимому
ObxCas0  docu/sources		блок называется своим хешем: дедупликация
ObxCas1  docu/sources		нарезка двух почти одинаковых входов по содержимому
ObxCas2  docu/sources		два автора публикуют наборы изменений, один переносит свой
