# Карта «Обзора на примерах» подсистемы Demo

Подсистема `Demo` — переводы известных примеров параллелизма на задачи, мониторы и каналы. У
каждого примера есть страница в этом каталоге, исходник в `Demo/Mod` и тест в
`Demo_dev/Mod/Examples.cp`. Пример, который входит в монитор-объект, описывает задачу через
`Tasks.Job` с `Body-`; пример на каналах или группах — через `Tasks.Task` с `Run`, потому что
пропуск `Tasks.Ctx` ему не нужен. Подсистема собирается только для amd64 (`tools/build64.sh Demo`);
тесты — `tools/test.sh Demo_devExamples`.

Active Oberon
DemoBuffer  docu/sources		ограниченный буфер на мониторе объекта (Reali, B.6)
DemoPhilo  docu/sources		обедающие философы на семафорах (Reali, C.1 и B.4)
DemoReaderWriter  docu/sources		читатели и писатели (Reali, B.1)
DemoBarber  docu/sources		спящий парикмахер (ETH System Construction)
DemoModuleMonitor  docu/sources		производитель и потребитель на мониторе модуля

Go
DemoPingPong  docu/sources		пинг-понг через один канал (Pike)
DemoPipeline  docu/sources		конвейер с разветвлением и слиянием (Ajmani)
DemoSieve  docu/sources		решето Эратосфена на каналах (Pike; Reali, C.2)
DemoSelectTimeout  docu/sources		выбор с тайм-аутом (Tour of Go)
DemoWorkerPool  docu/sources		пул рабочих (gobyexample)
DemoBoring  docu/sources		генератор и мультиплексор (Pike)
DemoGroup  docu/sources		отмена группы задач (Ajmani; errgroup)

Задачи и окно
DemoAnts  docu/sources		тысяча муравьёв на поле с окном

Помощник
DemoSupport  docu/sources		насос задач и вывод в консоль для команд без окна
