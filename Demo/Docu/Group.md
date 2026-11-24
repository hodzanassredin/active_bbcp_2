# DemoGroup — отмена группы задач

Sameer Ajmani, *Go Concurrency Patterns: Pipelines and Cancellation* (Go Blog, 2014), раздел об
отмене через `context`:

<https://go.dev/blog/pipelines>

и пакет `errgroup`:

<https://pkg.go.dev/golang.org/x/sync/errgroup>

В Go первая ошибка отменяет `context`; родитель ждёт всех участников через `errgroup.Wait`.

## Перевод

`Tasks.Group` даёт владение, распространение ошибки и ожидание: `Tasks.NewGroup`,
`Tasks.StartIn(t, g)`; `Run` родителя возвращается, а `TaskBoot` ждёт опустошения группы. Третий
рабочий трапает после того, как все четверо прошли барьер `Arrive`. `TaskBoot` помечает группу
`failed` и вызывает внутреннюю `CancelGroup`, которая отменяет остальных трёх в их ближайших
точках ожидания. Родитель завершается, только когда группа пуста.

Трап ловит `Kernel.Try` в `TaskBoot`, поэтому зритель трапов не нужен. Отчёты о трапах в журнале
ожидаемы: это ошибка рабочего и три отмены. Итоговая строка идёт после них.

## Запуск

```sh
echo 'DemoGroup.Run' | tools/run64.sh
```

Вывод:

```
DemoGroup: entered=8 completed=0 cancelled=3 failedWorker=1 groupFailed=1 alive=0
```

## Тест

`Demo_devExamples.TestProcGroupCancel` проверяет одну ошибку, одного трапнувшего участника, трех
отменённых, ноль завершённых, пустую группу с `failed` и отсутствие неожиданного сбоя прогона.
