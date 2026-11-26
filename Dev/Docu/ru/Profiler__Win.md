# DevProfiler__Win: Windows-источник выборок профиля

[Исходник](../../Mod/Profiler__Win.cp) · [Команды профилировщика](Profiler.md)

Платформенная реализация DevProfiler.ProfilerHook. При загрузке создаёт hook и регистрирует его через SetProfilerHook. Сам модуль не добавляет команд запуска: пользователь вызывает DevProfiler.Start/Stop.

StartRecording дублирует handle текущего потока, получает параметры multimedia timer, выбирает период `wPeriodMin * 3`, вызывает timeBeginPeriod и timeSetEvent. Callback Recorder читает CONTEXT_CONTROL через GetThreadContext и передаёт Eip в hook.Count. StopRecording останавливает timer и завершает выбранный период.

Это sampling, поэтому счётчик показывает попадания таймера, а не точное число вызовов процедуры. Реализация использует WinApi/WinMM и 32-битный Eip; её нельзя считать доступной на Linux только из-за наличия основного DevProfiler.

При изменениях проверять срок жизни timer/handle и результат получения context. В текущем тексте несколько кодов возврата сохраняются в res_, но не проверяются; документация не подтверждает успешность этих вызовов на любой Windows.
