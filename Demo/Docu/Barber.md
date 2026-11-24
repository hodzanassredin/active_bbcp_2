# DemoBarber — спящий парикмахер

Классическая задача о мониторе (задача Дейкстры) в изложении курса ETH *System Construction*:
монитор «Barber Shop» с условиями `barberAvailable`, `chairOccupied`, `doorOpen` и `clientLeft`.

<https://lec.inf.ethz.ch/syscon/2019/download/syscon06.pdf>

Семантика `AWAIT` в Active Oberon описана в P. Reali, *Active Oberon Language Report*, §3.3:
булево условие заменяет условные переменные и `signal`; условия перевычисляются, когда задача
выходит из монитора.

## Перевод

Каждый цикл `while cond do wait` стал одним `ACTIVE.AWAIT(cond)`; монитор — объект `Shop`, его
методы исключающие. Состояние:

- `barbers` — парикмахер свободен (1) или занят (0);
- `waiting` — клиенты на стульях ожидания;
- `serving` — клиент занял парикмахера;
- `open` — стрижка закончена, но клиент ещё не ушёл.

Шесть клиентов приходят раньше парикмахера: первые три занимают стулья, остальные уходят.
Парикмахер обслуживает трех сидящих.

## Запуск

```sh
echo 'DemoBarber.Run' | tools/run64.sh
```

Вывод:

```
DemoBarber: served=3 turnedAway=3
```

## Тест

`Demo_devExamples.TestProcBarber` проверяет 3 обслуженных, 3 отказа, отсутствие трапов и тупика.
