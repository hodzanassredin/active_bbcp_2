#!/bin/sh
# Внешний запускатель Unit: один модуль = один процесс blackbox-console.
#
#   tools/unit.sh Module [--marks slow,proc] [--timeout 300] [--compile "A B ..."]
#
# Компилирует Unit, зависимости из --compile и сам модуль (до трёх проходов),
# затем выполняет  UnitRunner.Run Module marks  и возвращает 0, только если
# раннер напечатал «failed 0» и процесс завершился с кодом 0.
# TestProc… внутри модуля при этом идут в отдельном процессе; тест, который
# зовёт Kernel.Quit или роняет процесс, не уносит остальные модули.
set -u
. "$(dirname "$0")/env.sh"

MOD=""; MARKS=""; TMO=300; DEPS=""; OPTS=""
while [ $# -gt 0 ]; do
	case "$1" in
		--marks) MARKS="${2:-}"; shift 2;;
		--timeout) TMO="${2:-300}"; shift 2;;
		--compile) DEPS="${2:-}"; shift 2;;
		--opts) OPTS="${2:-}"; shift 2;;
		-*) echo "unit: неизвестный ключ $1" >&2; exit 2;;
		*) MOD="$1"; shift;;
	esac
done
[ -n "$MOD" ] || { echo "unit: укажите модуль тестов" >&2; exit 2; }
[ -x "$INTERP" ] || { echo "unit: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }

# libtest64.so нужен System_devCo (GcStack/TrapStack) и замеру.
if command -v cc >/dev/null 2>&1; then
	cc -shared -fPIC -O2 -o "$INSTALL/libtest64.so" "$BB/tools/test64.c" 2>/dev/null || true
fi

# Unit — первым: от него зависят адаптеры System_devSupport и тесты.
# Тестируемый модуль компилируется отдельно: у него могут быть свои опции
# (например, ^ — ключ точек уступки).
mods="UnitAssert UnitRunner Unit_devProbe Unit_devMain $DEPS"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"${TMPDIR:-/tmp}/unit-compile.log" 2>&1 \
			&& "$BB/tools/go64.sh" "$MOD$OPTS" >>"${TMPDIR:-/tmp}/unit-compile.log" 2>&1; then
		break
	fi
	if [ $pass -eq 3 ]; then
		echo "unit: компиляция не удалась (проход $pass):" >&2
		tail -40 "${TMPDIR:-/tmp}/unit-compile.log" >&2; exit 1
	fi
done

out="${TMPDIR:-/tmp}/unit-$$.out"
printf 'UnitRunner.Run %s %s\n' "$MOD" "$MARKS" | timeout -k 5 "$TMO" "$BB/tools/run64.sh" >"$out" 2>&1
rc=$?

# Строки отчёта — в stdout пользователя; служебные (TRAP REPORT) не прячем.
grep -E '^(PASS|FAIL|EMPTY|SKIP|== Unit)' "$out" || true

if [ $rc -eq 124 ]; then
	echo "unit: $MOD — таймаут ${TMO}s" >&2
	rm -f "$out"; exit 1
fi
if [ $rc -ne 0 ]; then
	echo "unit: $MOD — процесс завершился с кодом $rc" >&2
	tail -20 "$out" >&2; rm -f "$out"; exit 1
fi
if grep -q '^== Unit: ' "$out" && grep -q 'failed 0,' "$out"; then
	rm -f "$out"; exit 0
fi
echo "unit: $MOD — есть провалы или нет итоговой строки" >&2
tail -20 "$out" >&2; rm -f "$out"; exit 1
