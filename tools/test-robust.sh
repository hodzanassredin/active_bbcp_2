#!/bin/sh
# Тест фасада RobustParallel (этап 9) на 1, 4 и 16 процессах.
#
#   tools/test-robust.sh
#   tools/test-robust.sh --world 32   # на i386-сборке build/BB32LIN (tools/build32.sh)
#
# Клиент Robust_devClient написан только против опубликованного
# определения RobustParallel в стиле MPI: rank 0 раздаёт работу, ранги
# 1..size-1 считают. Размер мира — внешняя переменная ROBUST_SIZE.
set -u
. "$(dirname "$0")/env.sh"

MOD=Robust_devParallel
MARKS=proc
TMO=180
DEPS="RobustParallel Robust_devClient AsyncComputon AsyncIO AsyncSelect AsyncChan AsyncOnce LinNative LinThreads"

WORLD=64
while [ $# -gt 0 ]; do
	case "$1" in
		--world) WORLD="${2:-64}"; shift 2;;
		*) echo "test-robust: неизвестный ключ $1" >&2; exit 2;;
	esac
done
case "$WORLD" in
	64) ;;
	32) INSTALL="${BB32INSTALL:-$BB/build/BB32LIN}"; INTERP="$INSTALL/blackbox-console";;
	*) echo "test-robust: --world 32 или 64, а не '$WORLD'" >&2; exit 2;;
esac
[ -x "$INTERP" ] || { echo "test-robust: нет $INTERP — соберите tools/build$WORLD.sh" >&2; exit 2; }

# Компиляция: Unit, фасад, клиент, зависимости.
mods="UnitAssert UnitRunner Unit_devProbe Unit_devMain $DEPS"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if [ "$WORLD" = 32 ]; then
		# i386: фасад, компутоны и Unit собраны build32.sh; клиент и тест
		# лежат в Robust_dev/Mod64 — фильтр Mod64 (tools/compile32.sh).
		if "$BB/tools/compile32.sh" --mod64 "Robust_devClient $MOD" >"${TMPDIR:-/tmp}/robust-compile.log" 2>&1; then
			break
		fi
	elif "$BB/tools/go64.sh" $mods >"${TMPDIR:-/tmp}/robust-compile.log" 2>&1 \
			&& "$BB/tools/go64.sh" "$MOD" >>"${TMPDIR:-/tmp}/robust-compile.log" 2>&1; then
		break
	fi
	if [ $pass -eq 3 ]; then
		echo "test-robust: компиляция не удалась (проход $pass):" >&2
		tail -40 "${TMPDIR:-/tmp}/robust-compile.log" >&2; exit 1
	fi
done

for n in 1 4 16; do
	out="${TMPDIR:-/tmp}/robust-$$-$n.out"
	(
		cd "$INSTALL" &&
		printf 'UnitRunner.Run %s %s quit\n' "$MOD" "$MARKS" \
			| LD_LIBRARY_PATH="$INSTALL" timeout -k 5 "$TMO" "$INTERP" --ROBUST_SIZE="$n"
	) >"$out" 2>&1
	rc=$?
	grep -E '^(PASS|FAIL|EMPTY|SKIP|== Unit|Robust_devParallel:)' "$out" || true
	if [ $rc -eq 124 ]; then
		echo "test-robust: $MOD size=$n — таймаут ${TMO}s" >&2
		rm -f "$out"; exit 1
	fi
	if [ $rc -ne 0 ] || ! grep -q 'failed 0,' "$out"; then
		echo "test-robust: $MOD size=$n — провал (код $rc)" >&2
		tail -20 "$out" >&2; rm -f "$out"; exit 1
	fi
	rm -f "$out"
done
exit 0
