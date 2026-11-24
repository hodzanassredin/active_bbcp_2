# DemoSelectTimeout — выбор с тайм-аутом

*Tour of Go*, «Select» и «Default Selection»:

<https://go.dev/tour/concurrency/5>

и gobyexample «Timeouts»:

<https://gobyexample.com/timeouts>

В Go тайм-аут — это `case <-time.After(d)` внутри `select`.

## Перевод

`AsyncChan.Select` принимает массив альтернатив. Приём — `AsyncChan.RecvAlt`, тайм-аут —
`AsyncChan.TimeoutAlt`; `Select` возвращает индекс сработавшей альтернативы, принятое сообщение
лежит в `ops[i].got`. Пример проверяет оба случая:

1. отправителя нет — побеждает тайм-аут 30 мс (индекс 1);
2. отправитель появляется через 5 мс — побеждает приём (индекс 0, значение 42).

## Запуск

```sh
echo 'DemoSelectTimeout.Run' | tools/run64.sh
```

Вывод (время меняется в пределах нескольких миллисекунд):

```
DemoSelectTimeout: timeoutIdx=1 timeoutMs=30 gotIdx=0 gotMs=5 value=42
```

## Тест

`Demo_devExamples.TestProcSelectTimeout` проверяет индексы, значение 42 и то, что тайм-аут ждал
не меньше 20 мс.
