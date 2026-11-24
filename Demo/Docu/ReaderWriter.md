# DemoReaderWriter — читатели и писатели

P. Reali, *Active Oberon Language Report*, ETH Zurich, 27 октября 2004, приложение B.1 «Readers and
Writers».

<https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf>

Состояние монитора в оригинале:

```
n = 0  пусто
n < 0  n писателей
n > 0  n читателей
```

`EnterReader` ждёт `n >= 0`, `EnterWriter` — `n = 0`.

## Перевод

`{EXCLUSIVE}` стал `[exclusive]`. Демонстрация добавляет измерения (`readersInside`,
`writersInside`, `Bad`) вокруг критической секции: читатель, увидевший внутри писателя, или
писатель, увидевший читателя, увеличивает `Bad`. Три читателя делают по пять проходов, два
писателя — по три.

## Запуск

```sh
echo 'DemoReaderWriter.Run' | tools/run64.sh
```

Вывод:

```
DemoReaderWriter: reads=15 writes=6 bad=0
```

## Тест

`Demo_devExamples.TestProcReadersWriters` проверяет 15 чтений, 6 записей, отсутствие пересечения
и трапов.
