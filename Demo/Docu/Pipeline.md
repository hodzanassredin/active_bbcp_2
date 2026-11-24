# DemoPipeline — конвейер с fan-out и fan-in

Sameer Ajmani, *Go Concurrency Patterns: Pipelines and Cancellation*, Go Blog, 2014:

<https://go.dev/blog/pipelines>

и слайд Rob Pike о fan-in:

<https://go.dev/talks/2012/concurrency.slide#33>

Версия на Go соединяет горутины каналами: генератор → квадрат → fan-out на двух рабочих → fan-in.

## Перевод

Каждая стадия — `Tasks.Task`; каналы — встречи `AsyncChan.Chan`. Конец потока — `IntMsg` с
`n = -1` (закрытия канала нет). Диспетчер раздаёт числа по кругу; две задачи-объединителя
складывают результаты в общие счётчики. Порядок результатов не фиксирован, сумма — да.

## Запуск

```sh
echo 'DemoPipeline.Run' | tools/run64.sh
```

Вывод:

```
DemoPipeline: count=10 sum=385
```

## Тест

`Demo_devExamples.TestProcPipeline` проверяет 10 результатов и 1²+…+10² = 385.
