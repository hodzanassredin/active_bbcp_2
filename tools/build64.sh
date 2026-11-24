#!/bin/sh
# Сборка 64-битного (amd64) BlackBox для Linux способом снимка Ивана.
#
#   tools/build64.sh [Подсистема ...]     # по умолчанию все из DEFAULT_SUBSYSTEMS
#   tools/build64.sh --clean             # сначала стереть .ocf/.osf установки
#   tools/build64.sh --console-only      # не линковать GUI и не копировать Rsrc
#
# dev0Linux запускается из корня репозитория; DevLibrarian.EnableDevLib
# выбирает <Sub>/Mod64 (приоритет), иначе <Sub>/Mod, и пишет Code/Sym в
# каталог установки (.work/BB64LIN по умолчанию). CompileSubs компилирует
# подсистемы в порядке списка и сам разрешает зависимости между ними.
set -e
. "$(dirname "$0")/env.sh"

clean=0; gui=1; subs=""
for a in "$@"; do
	case "$a" in
		--clean) clean=1;;
		--console-only) gui=0;;
		-*) echo "build64: неизвестный ключ $a" >&2; exit 2;;
		*) subs="$subs $a";;
	esac
done
[ -z "$subs" ] && subs="$DEFAULT_SUBSYSTEMS"

[ -x "$DEV0" ] || { echo "build64: нет $DEV0" >&2; exit 2; }
clean_host_temp
mkdir -p "$INSTALL"
if [ "$clean" -eq 1 ]; then
	echo "build64: стираю .ocf/.osf в $INSTALL"
	find "$INSTALL" \( -name '*.ocf' -o -name '*.osf' \) -delete
fi

LOG="${TMPDIR:-/tmp}/build64-$$.log"
trap 'rm -f "$LOG" "$LOG.cmd"' EXIT

# Demo собирается отдельно: её модули импортируют Async, а Async идёт
# после основного списка. Из общего списка её имя убираем.
core=""
for s in $subs; do
	[ "$s" = Demo ] || core="$core $s"
done

echo "build64: компиляция подсистем:$subs"
{
	echo 'DevCompiler64.InitLogMarks'
	echo "DevLibrarian.EnableDevLib('$INSTALL', 'Mod64')"
	[ -n "$core" ] && echo "DevCompiler64.CompileSubs @Lin $core"
	# Модули Mod64 вне списка CompileSubs (<Sub>/Mod). Tasks и LinNative/
	# LinThreads нужны самой подсистеме Async (AsyncSelect, AsyncComputon),
	# поэтому идут до неё.
	echo "DevCompiler64.CompileThis Taskarch"
	echo "DevCompiler64.CompileThis Tasks"
	echo "DevCompiler64.CompileThis LinThreads"
	echo "DevCompiler64.CompileThis LinNative"
	# Async — отдельным вызовом: ей нужны Tasks, LinNative и LinThreads,
	# которые собираются выше, а в общий список их не добавить (там
	# CompileSubs идёт до отдельных CompileThis).
	if [ -d "$BB/Async/Mod" ] || [ -d "$BB/Async/Mod64" ]; then
		echo "DevCompiler64.CompileSubs @Lin Async"
		# Модули Mod64 подсистемы Async: их нет в обходе CompileSubs.
		echo "DevCompiler64.CompileThis AsyncComputon"
		echo "DevCompiler64.CompileThis AsyncMpi"
		echo "DevCompiler64.CompileThis AsyncWork"
		echo "DevCompiler64.CompileThis AsyncWorkLin"
	fi
	# Robust — фасад RobustParallel над AsyncComputon (этап 9).
	if [ -d "$BB/Robust/Mod" ] || [ -d "$BB/Robust/Mod64" ]; then
		echo "DevCompiler64.CompileSubs @Lin Robust"
	fi
	# Kv — хранилище «ключ -> значение»; KvLin нужен LinLibc, поэтому
	# подсистема идёт после основного списка (на Windows её нет).
	if [ -d "$BB/Kv/Mod" ]; then
		echo "DevCompiler64.CompileSubs @Lin Kv"
	fi
	# Actor — акторы и супервизор над Tasks и Async (только amd64);
	# журнал ActorKvJournal импортирует Kv, поэтому Actor — после Kv.
	if [ -d "$BB/Actor/Mod" ]; then
		echo "DevCompiler64.CompileSubs @Lin Actor"
	fi
	# Примеры Obx на задачах и каналах лежат в Obx/Mod64: обход CompileSubs
	# их не берёт (им нужны Tasks и Async, которые собираются выше), поэтому
	# каждый — отдельным CompileThis. Примеры друг друга не импортируют.
	case " $subs " in
	*" Obx "*)
		for f in "$BB"/Obx/Mod64/*.cp; do
			[ -f "$f" ] || continue
			b=$(basename "$f" .cp)
			echo "DevCompiler64.CompileThis Obx$b"
		done
		;;
	esac
	# Demo — примеры задач, мониторов и каналов (ACTIVE + Async).
	case " $subs " in
	*" Demo "*)
		if [ -d "$BB/Demo/Mod" ]; then
			echo "DevCompiler64.CompileThis DemoSupport"
			for m in DemoAnts DemoBarber DemoBoring DemoBuffer DemoGroup \
				DemoModuleMonitor DemoPhilo DemoPipeline DemoPingPong \
				DemoReaderWriter DemoSelectTimeout DemoSieve DemoWorkerPool; do
				echo "DevCompiler64.CompileThis $m"
			done
		fi
		;;
	esac
	echo 'DevLibrarian.DisableDevLib'
} > "$LOG.cmd"
if ! "$BB/dev0Linux" < "$LOG.cmd" > "$LOG" 2>&1; then
	echo "build64: dev0Linux завершился с ошибкой, лог: $LOG" >&2
	tail -20 "$LOG" >&2; cp "$LOG" /tmp/build64-fail.log; exit 1
fi
COMPILE_ERR='~TRAP|[0-9]+ error[s]? detected|error detected|error:|syntax error|compilation failed|not found|undeclared|undefined| missing$'
if grep -qE "$COMPILE_ERR" "$LOG"; then
	echo "build64: в журнале компиляции есть ошибки:" >&2
	grep -nE "$COMPILE_ERR" "$LOG" | head -20 >&2
	cp "$LOG" /tmp/build64-fail.log; exit 1
fi
echo "build64: компиляция завершена"

# Линковка. Последний модуль списка — точка входа.
link() {
	name="$1"; shift
	echo "build64: линкую $name"
	if ! printf '%s\n' "DevLibrarian.EnableDevLib('$INSTALL', 'Mod64')" \
		"DevLinker1.LinkElfExe64 '$INSTALL/$name' := $*" \
		'DevLibrarian.DisableDevLib' | (cd "$BB" && "$DEV0" > "$LOG.link" 2>&1); then
		echo "build64: линковка $name не удалась:" >&2
		cat "$LOG.link" >&2; exit 1
	fi
	grep -q '^ok$' "$LOG.link" || { echo "build64: линковка $name: $(cat "$LOG.link")" >&2; exit 1; }
	chmod +x "$INSTALL/$name"
}
link blackbox-console 'Kernel+ LinKernel Files LinFiles StdLoader LinIntInit'
if [ "$gui" -eq 1 ]; then
	link blackbox 'Kernel+ LinKernel Files LinFiles StdLoader LinLoader'
fi

# Rsrc/Docu: нужны GUI и документации; для консоли не обязательны.
if [ "$gui" -eq 1 ]; then
	echo "build64: копирую Rsrc и Docu в $INSTALL"
	for sub in System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Lin Win; do
		[ -d "$BB/$sub/Rsrc" ] || continue
		mkdir -p "$INSTALL/$sub"
		cp -a "$BB/$sub/Rsrc/." "$INSTALL/$sub/Rsrc/"
	done
	for d in Docu Obx/Docu Demo/Docu Lin/Docu Unit/Docu Cas/Docu Actor/Docu; do
		[ -d "$BB/$d" ] || continue
		mkdir -p "$INSTALL/$d"
		cp -a "$BB/$d/." "$INSTALL/$d/"
	done
fi

echo "build64: готово"
echo "  консоль: $INSTALL/blackbox-console"
[ "$gui" -eq 1 ] && echo "  GUI:     $INSTALL/blackbox"
echo "  запуск:  tools/run64.sh"
