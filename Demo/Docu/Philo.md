# DemoPhilo — обедающие философы

P. Reali, *Active Oberon Language Report*, ETH Zurich, 27 октября 2004, приложение C.1 «Dining
Philosophers» и B.4 «Binary and Generic Semaphores».

<https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf>

В оригинале активный объект с телом `{ACTIVE} LOOP` и бинарные семафоры на `AWAIT`:

```
PROCEDURE P*; BEGIN {EXCLUSIVE} AWAIT(~taken); taken := TRUE END P;
PROCEDURE V*; BEGIN {EXCLUSIVE} taken := FALSE END V;
```

## Перевод

Активное тело стало методом `Run` задачи `Tasks.Task`; `Sem` — запись с исключающими методами
`P` и `V`. Исходный асимметричный порядок вилок сохранён: философ `N-1` берёт вилку 0 первой,
поэтому круговое ожидание разорвано. `Think` и `Eat` — точки `Tasks.Yield`.

## Запуск

```sh
echo 'DemoPhilo.Run' | tools/run64.sh
```

Вывод:

```
DemoPhilo: eaten=15
```

## Тест

`Demo_devExamples.TestProcPhilosophers` проверяет пять философов по три обеда = 15 и отсутствие
трапов.
