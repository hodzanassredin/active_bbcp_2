DevCompiler

DEFINITION DevCompiler;

	PROCEDURE Compile;
	PROCEDURE CompileAndUnload;
	PROCEDURE CompileModuleList;
	PROCEDURE CompileSelection;
	PROCEDURE CompileThis;
	PROCEDURE CompileSubs;
	PROCEDURE MakeList;
	... plus some private commands ...

END DevCompiler.


Command package for the Component Pascal compiler. The compiler provides several options for exceptional situations. The options are defined after the name of the module to which they apply in the command DevCompiler.CompileModuleList. See Platform-Specific Issues for details. Safety-critical runtime checks are always performed (type guards, array range checks, etc.), while non-critical runtime checks may not be generated (SHORT, integer overflows, testing of set membership). "Critical" means that non-local memory may be destroyed, with unknown global effects.

Typical menu:

MENU
	"&Compile"	""	"DevCompiler.Compile"	"TextCmds.FocusGuard"
	"Compile and Unload"	""	"DevCompiler.CompileAndUnload"	"TextCmds.FocusGuard"
	"Compile &Selection"	""	"DevCompiler.CompileSelection"	"TextCmds.SelectionGuard"
	"Com&pile Module List"	""	"DevCompiler.CompileModuleList"	"TextCmds.SelectionGuard"
END


PROCEDURE Compile
Guard: TextCmds.FocusGuard
Compile the Component Pascal module whose source is in the focus view.

PROCEDURE CompileAndUnload
Guard: TextCmds.FocusGuard
Compile the module whose source is in the focus view. If compilation is successful, it is attempted to unload the old version of this module. CompileAndUnload is convenient when developing top-level modules, i.e., modules which are not imported by any other modules, and thus can be unloaded individually.

PROCEDURE CompileModuleList
Guard: TextCmds.SelectionGuard
Compile the list of modules whose names are selected. When the first error is detected, the offending source is opened to show the error and the remaining uncompiled modules are selected. For generating a compile list for a set of subsystems see MakeList.

PROCEDURE CompileSelection
Guard: TextCmds.SelectionGuard
Compile the module, whose beginning is selected.

PROCEDURE CompileThis
Used in a text with a DevCommanders.View. This command takes the text following it and interprets it as a list of modules that should be compiled. Similar to CompileModuleList except that no selection is necessary. Examples are:
DevCompiler.CompileThis ObxHello0
DevCompiler.CompileThis ObxViews1 ObxViews2

PROCEDURE CompileSubs
Used in a text with a DevCommanders.View. This command takes the text following it and interprets it as a list of subsystems that should be compiled. The special symbol '*' refers to all subsystems, '+' refers to all subsystems except the standard subsystems. Directories and file names are expected to be capitalized according to the related module names. When the first error is detected, the offending source is opened to show the error and compilation is terminated. Examples are:
DevCompiler.CompileSubs +
DevCompiler.CompileSubs Obx
DevCompiler.CompileSubs Obx Text System (Attention: this recompiles a part of your BlackBox system.)

PROCEDURE MakeList
Used in a text with a DevCommanders.View. This command takes the text following it and interprets it as a list of subsystems that should be compiled. The special symbol '*' refers to all subsystems, '+' refers to all subsystems except the standard subsystems. Directories and file names are expected to be capitalized according to the related module names. The command generates and opens a text view that contains a properly ordered list of modules to be compiled with CompileModuleList. When a syntax error is detected while analyzing the import dependencies, the offending source is opened to show the error. Examples are:
DevCompiler.MakeList +
DevCompiler.MakeList Obx
DevCompiler.MakeList Obx Text System

... plus some private commands ...


## Внутренняя реализация bbcb2

[Архитектура и порядок стадий](Compiler-Architecture.md) · [DevCPT](CPT.md) · [Сопоставление с Crelier](Compiler-Crelier.md) · [Карта Dev](Sys-Map.md)

Исходник: [Compiler.cp](../../Mod/Compiler.cp). Помимо команд выше текущий модуль экспортирует программный вход `CompileText(text, beg, OUT error)` и `CompileOpt(opt)` для текста в фокусе. CompileText проверяет границы beg и запускает обычный проход с defopt.

CompileOpt разбирает строку опций: повторные `-` последовательно отключают srcpos, allref, ref и выпуск object file; `!` сначала отключает assert, затем checks; `+` включает allchecks, `?` — hint, `@` — errorTrap, `$` — режим Oberon. Это описание текущего тела CompileOpt, а не опций OP2 из книги.

CompileThis читает список из DevCommanders.par. GetSource обращается к Librarian за исходником и преобразованием в view/text. MakeList/CompileSubs сначала анализируют IMPORT для определения порядка; эта задача отличается от полной компиляции тела модуля.

Для CLI команды выполняются из корня проекта, например:

```bash
printf 'DevCompiler.CompileThis DemoParameters\n' | bbcb2-cli
```

Порядок собственно компиляции находится в приватной Module: CPP.Module → CPV.Init/Allocate → CPT.Export → CPV.Module; регистрация `.osf` выполняется после успешного завершения. Подробности и обработка ошибок описаны в статье об архитектуре.
