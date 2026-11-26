# Карта «Обзора на примерах» подсистемы Demo

Подсистема `Demo` — переводы известных примеров параллелизма на задачи, мониторы и каналы. У
каждого примера есть страница в этом каталоге, исходник в `Demo/Mod` и тест в
`Demo_dev/Mod/Examples.cp`. Прикладные задачи примеров — `Tasks.Job` с телом `Body-`, и на мониторе-объекте, и на каналах;
`Tasks.Task` с `Run` остался только у `DemoGroup.Parent`, которому нужно ждать опустошения своей
группы (нижний уровень). Подсистема собирается только для amd64 (`tools/build64.sh Demo`);
тесты — `tools/test.sh Demo_devExamples`.

Active Oberon
[DemoBuffer](Buffer.md)  [docu](Buffer.md)/[sources](../Mod/Buffer.cp)		ограниченный буфер на мониторе объекта (Reali, B.6)
[DemoPhilo](Philo.md)  [docu](Philo.md)/[sources](../Mod/Philo.cp)		обедающие философы на семафорах (Reali, C.1 и B.4)
[DemoReaderWriter](ReaderWriter.md)  [docu](ReaderWriter.md)/[sources](../Mod/ReaderWriter.cp)		читатели и писатели (Reali, B.1)
[DemoBarber](Barber.md)  [docu](Barber.md)/[sources](../Mod/Barber.cp)		спящий парикмахер (ETH System Construction)
[DemoModuleMonitor](ModuleMonitor.md)  [docu](ModuleMonitor.md)/[sources](../Mod/ModuleMonitor.cp)		производитель и потребитель на мониторе модуля

Go
[DemoPingPong](PingPong.md)  [docu](PingPong.md)/[sources](../Mod/PingPong.cp)		пинг-понг через один канал (Pike)
[DemoPipeline](Pipeline.md)  [docu](Pipeline.md)/[sources](../Mod/Pipeline.cp)		конвейер с разветвлением и слиянием (Ajmani)
[DemoSieve](Sieve.md)  [docu](Sieve.md)/[sources](../Mod/Sieve.cp)		решето Эратосфена на каналах (Pike; Reali, C.2)
[DemoSelectTimeout](SelectTimeout.md)  [docu](SelectTimeout.md)/[sources](../Mod/SelectTimeout.cp)		выбор с тайм-аутом (Tour of Go)
[DemoWorkerPool](WorkerPool.md)  [docu](WorkerPool.md)/[sources](../Mod/WorkerPool.cp)		пул рабочих (gobyexample)
[DemoBoring](Boring.md)  [docu](Boring.md)/[sources](../Mod/Boring.cp)		генератор и мультиплексор (Pike)
[DemoGroup](Group.md)  [docu](Group.md)/[sources](../Mod/Group.cp)		отмена группы задач (Ajmani; errgroup)

Задачи и окно
[DemoAnts](Ants.md)  [docu](Ants.md)/[sources](../Mod/Ants.cp)		тысяча муравьёв на поле с окном

Помощник
[DemoSupport](Support.md)  [docu](Support.md)/[sources](../Mod/Support.cp)		насос задач и вывод в консоль для команд без окна
