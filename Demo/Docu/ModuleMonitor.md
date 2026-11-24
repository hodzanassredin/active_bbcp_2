# DemoModuleMonitor — производитель-потребитель на мониторе модуля

Пример производителя и потребителя с процедурами `{EXCLUSIVE}` из курса ETH *System
Construction*:

<https://lec.inf.ethz.ch/syscon/2019/download/syscon06.pdf>

```
PROCEDURE Produce (x: object);
BEGIN {EXCLUSIVE} AWAIT(free # 0); DEC(free);
	buf[tail] := x; tail := (tail + 1) MOD N; INC(available) END;
```

## Перевод

Процедуры — процедуры верхнего уровня модуля, помеченные `[exclusive]`. Все исключающие
процедуры модуля разделяют один скрытый объект — **монитор модуля**, поэтому `Produce` и
`Consume` взаимно исключаются без явного `Lock`. Три производителя и три потребителя двигают по
40 элементов.

## Запуск

```sh
echo 'DemoModuleMonitor.Run' | tools/run64.sh
```

Вывод:

```
DemoModuleMonitor: produced=120 consumed=120 sum=7260
```

## Тест

`Demo_devExamples.TestProcModuleMonitor` проверяет 120 произведённых, 120 потреблённых, сумму
7260 и отсутствие трапов.
