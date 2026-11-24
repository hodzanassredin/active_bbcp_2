# DemoPingPong — пинг-понг через один канал

Rob Pike, *Go Concurrency Patterns* (Google, 2012), пример ping-pong: две горутины обмениваются
мячом через общий канал.

<https://go.dev/talks/2012/concurrency.slide#14>

## Перевод

Две горутины — объекты `Tasks.Task`. Мяч — `AsyncSelect.Msg` (указатель на расширяемую запись), а
стол — `AsyncChan.Chan`, то есть встреча: каждая отправка ждёт приёма. Мяч с `n = -1` завершает
игру. На каждый бросок выделяется новое сообщение; после отправки сообщение не меняется.

## Запуск

```sh
echo 'DemoPingPong.Run' | tools/run64.sh
```

Вывод:

```
DemoPingPong: hits=10 sum=45
```

## Тест

`Demo_devExamples.TestProcPingPong` проверяет 10 ударов и сумму 0+…+9 = 45.
