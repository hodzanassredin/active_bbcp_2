#!/bin/sh
# Компиляция модулей в i386-установку (build/BB32LIN) — для сценариев,
# которые гоняют тестовые модули на 32-битной сборке (test-cluster.sh
# --world 32, test-crypto-tls.sh --world 32). Мир собирает build32.sh.
#
#   tools/compile32.sh A B C                 # CompileThis по порядку, фильтр Mod
#   tools/compile32.sh --mod64 "X Y" A B     # X и Y — после, с фильтром Mod64
#
# Фильтр Mod64 меняет только выбор исходника (<Sub>/Mod64/<Name>.cp, иначе
# Mod); компилятор и установка остаются i386. Им собираются переносимые
# примеры из Obx/Mod64 (ObxCluster), которые build32.sh не берёт.
# Код возврата 0 — без ошибок компиляции; журнал при ошибке — в stderr.
set -u
. "$(dirname "$0")/env.sh"

INSTALL="${BB32INSTALL:-$BB/build/BB32LIN}"
MOD64=""
if [ "${1:-}" = --mod64 ]; then MOD64="${2:-}"; shift 2; fi
[ -x "$DEV0" ] || { echo "compile32: нет $DEV0" >&2; exit 2; }
[ -d "$INSTALL" ] || { echo "compile32: нет $INSTALL — tools/build32.sh" >&2; exit 2; }
clean_host_temp

log="${TMPDIR:-/tmp}/compile32-$$.log"
trap 'rm -f "$log" "$log.cmd"' EXIT
{
	echo 'DevCompiler.InitLogMarks'
	echo "DevLibrarian.EnableDevLib('$INSTALL', 'Mod')"
	for m in "$@"; do echo "DevCompiler.CompileThis $m"; done
	echo 'DevLibrarian.DisableDevLib'
	if [ -n "$MOD64" ]; then
		echo "DevLibrarian.EnableDevLib('$INSTALL', 'Mod64')"
		for m in $MOD64; do echo "DevCompiler.CompileThis $m"; done
		echo 'DevLibrarian.DisableDevLib'
	fi
} > "$log.cmd"
if ! (cd "$BB" && "$DEV0" < "$log.cmd" > "$log" 2>&1); then
	echo "compile32: dev0Linux завершился с ошибкой:" >&2; tail -20 "$log" >&2; exit 1
fi
if grep -qE 'error:|not found|~TRAP|[0-9]+ error[s]? detected' "$log"; then
	echo "compile32: ошибки компиляции:" >&2
	grep -nE 'error:|not found|~TRAP|[0-9]+ error' "$log" | head -20 >&2; exit 1
fi
