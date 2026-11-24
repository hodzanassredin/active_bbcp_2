# DemoBuffer — ограниченный буфер

P. Reali, *Active Oberon Language Report*, ETH Zurich, 27 октября 2004, приложение B.6 «Bounded
Buffer».

<https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf>

В оригинале это кольцевой буфер, методы `Put` и `Get` которого помечены `{EXCLUSIVE}` и ждут на
условиях `AWAIT`:

```
PROCEDURE Put* (i: INTEGER);
BEGIN {EXCLUSIVE} AWAIT ((in + 1) MOD BufLen # out);
	data[in] := i; in := (in + 1) MOD BufLen END Put;
```

## Перевод

`{EXCLUSIVE}` стал `[exclusive]`, `AWAIT` — `ACTIVE.AWAIT`. Инициализатор `& Init` не нужен:
`NEW` обнуляет `in` и `out`. `BufLen` равна 8, как в оригинале; демонстрация использует трех
производителей и трех потребителей по 40 элементов.

## Запуск

```sh
echo 'DemoBuffer.Run' | tools/run64.sh
```

Вывод:

```
DemoBuffer: produced=120 consumed=120 sum=2460
```

`DemoBuffer.Buffer` можно использовать и напрямую из других модулей: `Put` и `Get` —
экспортированные исключающие методы.

## Тест

`Demo_devExamples.TestProcBoundedBuffer` проверяет 120 произведённых, 120 потреблённых,
сумму 2460 и отсутствие трапов задач.
