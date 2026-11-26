# Windows callbacks и unwind в DevCompiler64

Реализовано и проверено под Wine 2026-11-09. Windows callbacks и unwind-метаданные
используются в [WinKernel](../../../Win/Docu/Kernel-x64.md).

## Использование

Указать `[ms64]` нужно и в определении процедуры, и в процедурном типе:

```componentpascal
TYPE Callback = PROCEDURE [ms64] (arg: SYSTEM.ADDRESS): INTEGER;
PROCEDURE [ms64] Handler (arg: SYSTEM.ADDRESS): INTEGER;
BEGIN
	RETURN 1
END Handler;
```

Поддержаны вызов из Windows, прямой вызов из CP и вызов через процедурную
переменную. Параметры/результаты — целочисленные скаляры, адреса и указатели;
VAR-параметр должен занимать один адресный слот. Для BOOL/DWORD/LONG Windows
нужен INTEGER, для адреса или handle — SYSTEM.ADDRESS. Обычный указатель на
CP-процедуру не заменяет указатель на `[ms64]` callback.

Адрес именованной процедуры: `SYSTEM.ADR(Handler)`. Для процедурной переменной
передаётся её значение, а не адрес ячейки хранения. Float-параметры/результаты,
структуры по значению, tagged VAR записи/открытые массивы и вложенные callbacks
отклоняются ошибкой 200. `[ccall]` сохраняет значение SysV AMD64.

## Как устроен код

`DevCPCamd64.Ms64Entry` создаёт Windows-вход перед внутренним CP-телом.
Именно Windows-вход используется при экспорте и получении адреса процедуры.
Он сохраняет RBX, RBP, RSI, RDI, R12–R15 и XMM6–XMM15, переносит аргументы
из RCX/RDX/R8/R9 и стека в восьмибайтные CP-слоты, вызывает внутреннее тело,
восстанавливает регистры и возвращает результат в RAX. Внутренний ABI CP
не меняется; тело снимает собственные аргументы.

Фиксированная часть Windows-кадра занимает 232 байта: восемь сохранённых GPR
и ещё 168 байт для XMM, управляющего слова x87, MXCSR и выравнивания.
При нормальном возврате восстанавливаются настройки FPU вызывающего, в том числе
если внутри callback выполнялись вещественные вычисления. RBP расположен на 224 байта выше RSP
фиксированного кадра. Пролог описан PUSH_NONVOL, ALLOC_LARGE, SET_FPREG,
SAVE_XMM128; эпилог имеет распознаваемый Windows вид ADD RSP / POP / RET.

Обычные CP-тела описываются через RBP. Эти записи восстанавливают цепочку
CP-кадров; полный Windows-контекст невременных регистров восстанавливает
внешний Windows-вход. Описаны также тела модулей и стартовый код PE.
Эпилоги используют LEA RSP,[RBP+offset]. Помощник пробирования стека теперь
не изменяет RSP: он касается страниц через временные R10/R11, а память
выделяет вызывающая процедура с обычным кадром. Это позволяет развернуть
стек и при исключении внутри пробирования.

Для `[noframe]` и SysV callbacks Windows unwind не создаётся. Ассемблерный
код, произвольно изменяющий RSP/RBP, нельзя корректно описать автоматически.

## Передача метаданных

`DevCPUnwind` собирает записи; `DevCPE` дописывает после UseBlk OCF процессора 12
необязательный блок UW64. Заголовки существующих блоков, исправления адресов,
структуры типов/модулей и формат OSF не меняются.

Все числа блока — little-endian INTEGER по четыре байта:

| Поле | Значение |
| --- | --- |
| magic | 34365755H, байты `UW64` |
| version | 1 |
| count | Число описаний |
| start, end | Начало включительно, конец исключительно относительно блока кода |
| size | Размер следующего UNWIND_INFO, кратный четырём |
| bytes | UNWIND_INFO версии 1, flags = 0, с выравнивающими нулями |

Последние три строки повторяются count раз. Диапазоны возрастают и не
пересекаются; вход callback и его CP-тело описываются отдельно.
`DevLnkLoad64` проверяет версию, границы, порядок и размеры. Старые OCF без
блока принимаются, но unwind для них не восстанавливается: необходимые
для обработки исключений модули следует перекомпилировать.

`DevLnkWritePe` записывает RUNTIME_FUNCTION в `.pdata`, UNWIND_INFO в `.xdata`,
а каталог PE № 3 указывает на таблицу функций. Обе секции только для чтения;
используются RVA, дополнительные DIR64 для них не нужны. После каждого
UNWIND_INFO оставлен DWORD для совместимости со старыми binutils.
Стартовый кадр добавляет линкер. Ограничение адресов образа нижними 2 ГиБ сохраняется.

## Сборка и проверки

Из корня проекта:

```sh
echo "DevCompiler.CompileThis DevCPUnwind DevCPE DevCPCamd64 DevCPVamd64 DevCompiler64 DevLnkBase DevLnkLoad DevLnkLoad64 DevLnkWritePe DevLnkWriteElf DevLnkWriteElfStatic DevLinker1" | ./dev0Linux
mkdir -p /tmp/callback64-test

echo "DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('/tmp/callback64-test', 'Mod64');
DevCompiler64.CompileThis Kernel64 CheckCallback64Api CheckCallback64Native
DevLinker1.LinkPeExe64 '/tmp/callback64-test/native.exe' := Kernel64+ CheckCallback64Native\$ ~" | ./dev0Linux

WINEDEBUG=-all wine64 /tmp/callback64-test/native.exe
python3 Dev/Test/check_unwind64.py /tmp/callback64-test/native.exe
```

`native.exe` импортирует **только kernel32.dll**. Код завершения 0 означает,
что проверены callback InitOnceExecuteOnce, 64-битный аргумент, прямой и
косвенный вызовы `[ms64]`, а также CP-обработчик vectored exception от RaiseException.
Для этого примера не нужен C-компилятор.

Для подробной проверки ABI служит `Dev/Test/callback64_probe.c` — исключительно
тестовая DLL, не зависимость компилятора или готовых программ:

```sh
x86_64-w64-mingw32-gcc -shared -O0 -g -Wall -Wextra -Werror \
  -o /tmp/callback64-test/callback64_probe.dll Dev/Test/callback64_probe.c

echo "DevLibrarian.EnableDevLib('/tmp/callback64-test', 'Mod64');
DevCompiler64.CompileThis CheckCallback64Probe CheckCallback64
DevLinker1.LinkPeExe64 '/tmp/callback64-test/callback.exe' := Kernel64+ CheckCallback64\$ ~" | ./dev0Linux
WINEDEBUG=-all wine64 /tmp/callback64-test/callback.exe
```

Тест вызывает RtlVirtualUnwind на каждой границе инструкций пролога и
восстановления/эпилога, проверяя GPR и каждый XMM-регистр. Также проверены
шесть аргументов callback, значения больше 32 бит, живой стек CP → Windows-вход,
стек реального исключения Windows и локальный массив на 20 000 байт.
Статическая проверка `check_unwind64.py` проверяет диапазоны, порядок и
unwind-коды. Регрессии CheckPe64Main и CheckWinKernel64 с GC также проходят.
Проверка на настоящей Windows пока не выполнена.

## Интеграция с WinKernel

WinKernel уже использует CP `[ms64]` VEH callback и прямые импорты системных API.
Восстановление Kernel.Try/Start реализовано через RtlCaptureContext и замену
контекста исключения. Проверки и ограничения
описаны в документации runtime. Сама unwind-таблица описывает восстановление
стека, но не заменяет обработчик исключений и не выполняет cleanup чужих кадров.

Для динамических OCF [StdLoader AMD64](../../../Std/Docu/Loader.md)
читает UW64 и вызывает Kernel.RegisterCode. WinKernel регистрирует постоянную
таблицу RUNTIME_FUNCTION через RtlAddFunctionTable и удаляет её через
RtlDeleteFunctionTable перед освобождением кода и таблицы. Для статических PE
таблицу регистрирует Windows через каталог исключений. SEH cleanup/handlers,
TLS, COM guarded процедуры и вход в runtime из других потоков требуют
отдельной реализации.

Спецификации: [Windows x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention),
[unwind](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64),
[пролог и эпилог](https://learn.microsoft.com/en-us/cpp/build/prolog-and-epilog).
