#!/bin/sh
# Быстрый цикл 32-битного компилятора (DevCompiler) — когда правим сам
# компилятор (Dev/Mod/CP*) или 32-битные модули.
#
#   tools/go32.sh DevCPE DevCPT
#   tools/go32.sh                 # имена из /tmp/compile1.txt
#
# Вывод — .work/i386 (отдельно от x64-установки .work/BB64LIN), поэтому
# i386 .osf не подмешиваются в amd64-сборку. Если базы ещё нет, первый
# вызов собирает её DevCompiler.CompileSubs по .cp.
set -e
. "$(dirname "$0")/env.sh"

I386="$BB/.work/i386"
SUBS="Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket"

if [ $# -gt 0 ]; then
	printf '%s\n' "$@" > /tmp/compile1.txt
else
	[ -f /tmp/compile1.txt ] || { echo "go32: нет аргументов и /tmp/compile1.txt" >&2; exit 2; }
	set -- $(tr '\n' ' ' < /tmp/compile1.txt)
fi
[ -x "$DEV0" ] || { echo "go32: нет $DEV0" >&2; exit 2; }

run_cmds() {
	if ! "$DEV0" < "$1" > "$2" 2>&1; then
		echo "go32: dev0Linux завершился с ошибкой:" >&2
		tail -30 "$2" >&2; return 1
	fi
	if grep -qE 'error:|[0-9]+ error[s]? detected|not found|~TRAP' "$2"; then
		grep -E 'error:|[0-9]+ error[s]? detected|not found|~TRAP' "$2" | head -30 >&2
		return 1
	fi
}

if [ ! -f "$I386/System/Sym/Kernel.osf" ]; then
	echo "go32: i386-базы нет — собираю CompileSubs в $I386" >&2
	mkdir -p "$I386"
	LOG="${TMPDIR:-/tmp}/go32-base-$$"
	trap 'rm -f "$LOG" "$LOG.cmd"' EXIT
	{
		echo 'DevCompiler.InitLogMarks'
		echo "DevLibrarian.EnableDevLib('$I386', 'Mod')"
		echo "DevCompiler.CompileSubs @Lin $SUBS"
		echo 'DevLibrarian.DisableDevLib'
	} > "$LOG.cmd"
	run_cmds "$LOG.cmd" "$LOG"
fi

LOG="${TMPDIR:-/tmp}/go32-$$"
trap 'rm -f "$LOG" "$LOG.cmd"' EXIT
{
	echo 'DevCompiler.InitLogMarks'
	echo "DevLibrarian.EnableDevLib('$I386', 'Mod')"
	for m in "$@"; do echo "DevCompiler.CompileThis $m"; done
	echo 'DevLibrarian.DisableDevLib'
} > "$LOG.cmd"
run_cmds "$LOG.cmd" "$LOG"
printf '%s\n' "$(cat "$LOG")" | grep -E '^compiling|error:' | tail -20
