#!/bin/sh
# Сборка 32-битного (i386) BlackBox для Linux: консольный blackbox-console.
#
#   tools/build32.sh              # мир + хост в build/BB32LIN
#   tools/build32.sh --clean      # сначала стереть .ocf/.osf установки
#
# Источники берутся из <Подсистема>/Mod, результаты — в build/BB32LIN
# (переопределяется BB32INSTALL). Компилятор — DevCompiler (i386) в
# упакованном dev0Linux; линковка — DevLinker1.LinkElfExe (ELF32).
# Tasks компилируется отдельным CompileThis: в общем списке CompileSubs
# его нет (это модуль Mod64). Тесты подсистем собирает tools/test32.sh
# по требованию — поставочная сборка подсистемы *_dev не включает.
#
# 32-битный C-помощник libtest32.so (ucontext, тот же tools/test64.c)
# собирается флагами tools/i386-toolchain.sh; без тулчейна — пропуск
# (тесты сопрограмм тогда не запускаются, см. tools/test32.sh).
set -e
. "$(dirname "$0")/env.sh"

INSTALL="${BB32INSTALL:-$BB/build/BB32LIN}"
clean=0
for a in "$@"; do
	case "$a" in
		--clean) clean=1;;
		-*) echo "build32: неизвестный ключ $a" >&2; exit 2;;
		*) echo "build32: лишний аргумент $a" >&2; exit 2;;
	esac
done

SUBS="Lin System Std Cons Text Fig Form Dev Comm Cas Obx Xhtml Sql Paket Unit Crypto"
# Переносимые модули из Mod64 (см. ниже, фильтр Mod64 при компиляции).
MOD64_PORTABLE="LinThreads LinNative AsyncWork AsyncWorkLin AsyncComputon RobustParallel"

[ -x "$DEV0" ] || { echo "build32: нет $DEV0" >&2; exit 2; }
clean_host_temp
mkdir -p "$INSTALL"
if [ "$clean" -eq 1 ]; then
	echo "build32: стираю .ocf/.osf в $INSTALL"
	find "$INSTALL" \( -name '*.ocf' -o -name '*.osf' \) -delete
fi

# libtest32.so: флаги готовит tools/i386-toolchain.sh (идемпотентно,
# без sudo). Пустой stdout — тулчейна нет, это пропуск, а не ошибка.
if I386FLAGS=$("$BB/tools/i386-toolchain.sh"); then
	if [ -n "$I386FLAGS" ]; then
		echo "build32: собираю libtest32.so"
		# shellcheck disable=SC2086
		if ! gcc $I386FLAGS -shared -fPIC -O2 -o "$INSTALL/libtest32.so" "$BB/tools/test64.c"; then
			echo "build32: сборка libtest32.so не удалась" >&2; exit 1
		fi
	else
		echo "build32: пропускаю libtest32.so (нет 32-битного C-тулчейна)"
	fi
else
	echo "build32: пропускаю libtest32.so (tools/i386-toolchain.sh не отработал)" >&2
fi

LOG="${TMPDIR:-/tmp}/build32-$$.log"
trap 'rm -f "$LOG" "$LOG.cmd" "$LOG.link"' EXIT

echo "build32: компиляция подсистем"
{
	echo 'DevCompiler.InitLogMarks'
	echo "DevLibrarian.EnableDevLib('$INSTALL', 'Mod')"
	echo "DevCompiler.CompileSubs @Lin $SUBS"
	echo "DevCompiler.CompileThis Taskarch"
	echo "DevCompiler.CompileThis Tasks"
	# Async/Mod (каналы, выбор, события, Once, IO, Stm) собирается после Tasks.
	echo "DevCompiler.CompileSubs @Lin Async"
	# Kv использует Tasks (очередь писателей), поэтому собирается после них.
	echo "DevCompiler.CompileSubs @Lin Kv"
	# Actor (в т. ч. ActorNetTls поверх Crypto) и Raft — после Async и Kv.
	echo "DevCompiler.CompileSubs @Lin Actor"
	echo "DevCompiler.CompileSubs @Lin Raft"
	echo 'DevLibrarian.DisableDevLib'
	# Переносимые модули, которые лежат в Mod64 вне обхода CompileSubs (так
	# их держит и build64.sh): объявления pthread и POSIX (LinThreads,
	# LinNative — размеры через SYSTEM.ADDRESS, записи мьютекса с запасом),
	# рабочие нити ОС и компутоны (AsyncWork, AsyncWorkLin, AsyncComputon) и
	# фасад RobustParallel. Фильтр 'Mod64' меняет только выбор исходника:
	# компилятор и установка остаются i386. AsyncMpi не собирается — ему
	# нужна C-библиотека MPI (tools/bbmpi.c, tools/test-mpi.sh).
	echo "DevLibrarian.EnableDevLib('$INSTALL', 'Mod64')"
	for m in $MOD64_PORTABLE; do echo "DevCompiler.CompileThis $m"; done
	echo 'DevLibrarian.DisableDevLib'
} > "$LOG.cmd"
if ! "$DEV0" < "$LOG.cmd" > "$LOG" 2>&1; then
	echo "build32: dev0Linux завершился с ошибкой, лог: $LOG" >&2
	tail -20 "$LOG" >&2; exit 1
fi
COMPILE_ERR='~TRAP|[0-9]+ error[s]? detected|error detected|error:|syntax error|compilation failed|not found|undeclared|undefined| missing$'
if grep -qE "$COMPILE_ERR" "$LOG"; then
	echo "build32: в журнале компиляции есть ошибки:" >&2
	grep -nE "$COMPILE_ERR" "$LOG" | head -20 >&2
	exit 1
fi
echo "build32: компиляция завершена"

echo "build32: линкую blackbox-console"
# Имя результата в LinkElfExe — относительно стартового каталога dev0Linux
# (корня репозитория): абсолютное имя Files.Register склеивает с этим
# каталогом и падает с «Register». Поэтому передаём путь от $BB.
case "$INSTALL" in
	"$BB"/*) OUT="${INSTALL#"$BB"/}/blackbox-console";;
	*) if command -v realpath >/dev/null 2>&1; then
		OUT="$(realpath --relative-to="$BB" "$INSTALL")/blackbox-console"
	else OUT="$INSTALL/blackbox-console"; fi;;
esac
if ! printf '%s\n' "DevLibrarian.EnableDevLib('$INSTALL', 'Mod')" \
	"DevLinker1.LinkElfExe Linux '$OUT' := Kernel+ LinKernel Files LinFiles StdLoader LinIntInit" \
	'DevLibrarian.DisableDevLib' | (cd "$BB" && "$DEV0" > "$LOG.link" 2>&1); then
	echo "build32: линковка не удалась:" >&2; cat "$LOG.link" >&2
	cp "$LOG.link" /tmp/build32-link-fail.log 2>/dev/null || true; exit 1
fi
grep -q '^ok$' "$LOG.link" || {
	echo "build32: линковка: $(cat "$LOG.link")" >&2
	cp "$LOG.link" /tmp/build32-link-fail.log 2>/dev/null || true; exit 1
}
chmod +x "$INSTALL/blackbox-console"

echo "build32: готово"
echo "  консоль: $INSTALL/blackbox-console"
