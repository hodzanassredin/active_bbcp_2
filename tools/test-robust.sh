#!/bin/sh
# Тест фасада RobustParallel (этап 9) на 1, 4 и 16 процессах.
#
#   tools/test-robust.sh
#
# Клиент Test64RobustClient написан только против опубликованного
# определения RobustParallel в стиле MPI: rank 0 раздаёт работу, ранги
# 1..size-1 считают. Размер мира — внешняя переменная ROBUST_SIZE.
set -u
. "$(dirname "$0")/env.sh"

MOD=Test64RobustParallel
MARKS=proc
TMO=180
DEPS="RobustParallel Test64RobustClient AsyncComputon AsyncIO AsyncSelect AsyncChan AsyncOnce LinNative LinThreads"

[ -x "$INTERP" ] || { echo "test-robust: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }

# Компиляция: Unit, фасад, клиент, зависимости.
mods="UnitAssert UnitRunner UnitTestsProbe UnitTests $DEPS"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"${TMPDIR:-/tmp}/robust-compile.log" 2>&1 \
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
		printf 'UnitRunner.Run %s %s\n' "$MOD" "$MARKS" \
			| LD_LIBRARY_PATH="$INSTALL" timeout -k 5 "$TMO" "$INTERP" --ROBUST_SIZE="$n"
	) >"$out" 2>&1
	rc=$?
	grep -E '^(PASS|FAIL|EMPTY|SKIP|== Unit|Test64RobustParallel:)' "$out" || true
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
