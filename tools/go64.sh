#!/bin/sh
# Быстрый цикл: скомпилировать модули amd64-компилятором в каталог установки.
#
#   tools/go64.sh System_testsGc Kernel
#   tools/go64.sh                 # имена из /tmp/compile1.txt
#
# Источник — <Sub>/Mod64/*.cp (приоритет), иначе <Sub>/Mod/*.cp.
# Используется стандартный DevCompiler64.CompileThis: DevOnce из ветки
# active не нужен, потому что .cp читает сам компилятор.
set -e
. "$(dirname "$0")/env.sh"

if [ $# -gt 0 ]; then
	printf '%s\n' "$@" > /tmp/compile1.txt
else
	[ -f /tmp/compile1.txt ] || { echo "go64: нет аргументов и /tmp/compile1.txt" >&2; exit 2; }
	set -- $(tr '\n' ' ' < /tmp/compile1.txt)
fi
[ $# -gt 0 ] || { echo "go64: пустой список модулей" >&2; exit 2; }
[ -x "$DEV0" ] || { echo "go64: нет $DEV0" >&2; exit 2; }
[ -d "$INSTALL" ] || { echo "go64: нет $INSTALL — соберите tools/build64.sh" >&2; exit 2; }
clean_host_temp

LOG="${TMPDIR:-/tmp}/go64-$$"
MARK=$(mktemp)
trap 'rm -f "$MARK" "$LOG" "$LOG.cmd"' EXIT
: > "$LOG.cmd"
echo 'DevCompiler64.InitLogMarks' >> "$LOG.cmd"
echo "DevLibrarian.EnableDevLib('$INSTALL', 'Mod64')" >> "$LOG.cmd"
for m in "$@"; do
	# Модуль с суффиксом опций: Dev_testsYield^ -> CompileThis Dev_testsYield ^
	opt=""
	case "$m" in *\^) opt="^"; m="${m%^}";; esac
	echo "DevCompiler64.CompileThis $m $opt" >> "$LOG.cmd"
done
echo 'DevLibrarian.DisableDevLib' >> "$LOG.cmd"
out=$("$DEV0" < "$LOG.cmd" 2>&1) || {
	echo "go64: dev0Linux завершился с ошибкой:" >&2
	printf '%s\n' "$out" | tail -30 >&2; exit 1; }
printf '%s\n' "$out" | grep -E '^compiling|error:|not found|~TRAP' | head -40
if printf '%s\n' "$out" | grep -qE 'error:|[0-9]+ error[s]? detected|not found|~TRAP'; then
	echo "go64: ошибка компиляции" >&2; exit 1
fi
exit 0
