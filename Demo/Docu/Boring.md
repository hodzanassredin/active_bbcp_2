# DemoBoring — генератор и мультиплексор

Rob Pike, *Go Concurrency Patterns* (Google, 2012): генератор «boring» и мультиплексор (fan-in).

<https://go.dev/talks/2012/concurrency.slide#25>

<https://go.dev/talks/2012/concurrency.slide#30>

В Go мультиплексор — это `select` по двум входным каналам.

## Перевод

Две задачи-генератора посылают по восемь пронумерованных сообщений и затем признак конца.
Задача-мультиплексор на каждой итерации строит два предложения приёма и вызывает
`AsyncChan.Select`; она считает сообщения от каждого генератора и останавливается после двух
признаков конца.

## Запуск

```sh
echo 'DemoBoring.Run' | tools/run64.sh
```

Вывод:

```
DemoBoring: messages=16 fromA=8 fromB=8
```

## Тест

`Demo_devExamples.TestProcBoring` проверяет 16 сообщений, по 8 от каждого генератора, и
отсутствие трапов.
