#!/bin/sh
# Замер сборщика мусора.
#
#   tools/bench-gc.sh            # System_devGcBench.Do
#   tools/bench-gc.sh <команда>
#
# Строки "BENCH ..." печатает сам модуль; RSS — /usr/bin/time -v.
# В манифест tools/tests.txt тест не входит: слишком долго.
set -u
. "$(dirname "$0")/env.sh"

cmd="${1:-System_devGcBench.Do}"
tmo="${BENCH_TIMEOUT:-1800}"
[ -x "$INTERP" ] || { echo "bench-gc: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }
command -v cc >/dev/null 2>&1 || { echo "bench-gc: нет cc" >&2; exit 2; }
command -v /usr/bin/time >/dev/null 2>&1 || { echo "bench-gc: нет /usr/bin/time" >&2; exit 2; }
cc -shared -fPIC -O2 -o "$INSTALL/libtest64.so" "$BB/tools/test64.c" || {
	echo "bench-gc: сборка libtest64.so не удалась" >&2; exit 2; }

mods="System_devSupport System_devCRaw System_devClock System_devGcBench"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"${TMPDIR:-/tmp}/bench-compile.log" 2>&1; then break; fi
	if [ $pass -eq 3 ]; then
		echo "bench-gc: компиляция не удалась:" >&2
		cat "${TMPDIR:-/tmp}/bench-compile.log" >&2; exit 1
	fi
done
out="${TMPDIR:-/tmp}/bench-gc-$$.out"; err="${TMPDIR:-/tmp}/bench-gc-$$.err"
printf '%s\n' "$cmd" | timeout -k 10 "$tmo" /usr/bin/time -v "$BB/tools/run64.sh" \
	>"$out" 2>"$err"
rc=$?
grep '^BENCH ' "$out" || true
if grep -q '^SYSTEMTESTS-PASS' "$out"; then echo "bench-gc: SYSTEMTESTS-PASS"
elif grep -q '^SYSTEMTESTS-FAIL' "$out"; then
	echo "bench-gc: SYSTEMTESTS-FAIL"; grep -E '^  FAIL|SYSTEMTESTS-FAIL|~TRAP' "$out" | head -20
else
	echo "bench-gc: нет маркера (код $rc), хвост вывода:" >&2; tail -30 "$out" >&2
fi
rss=$(grep 'Maximum resident set size' "$err" | sed 's/.*: //')
elapsed=$(grep 'Elapsed (wall clock)' "$err" | sed 's/.*): //')
echo "bench-gc: RSS=${rss:-?} KB, время=${elapsed:-?}, код=$rc"
rm -f "$out" "$err"; exit $rc
