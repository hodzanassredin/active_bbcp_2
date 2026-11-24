#!/bin/sh
# Прогон i386-мира: Unit_testsMain и расширение этапов 2–6 на 32-битной цели.
#
#   tools/test32.sh              # build32.sh, затем все модули
#   tools/test32.sh System_testsTasks  # только указанные модули (мир не собирается)
#   tools/test32.sh --list
#
# Каждый модуль — отдельный процесс blackbox-console (i386). Итог берётся
# из отчёта UnitRunner («failed 0») и кода завершения. В конце
# проверяется, что прогон не добавил в /tmp имён odc + 9 цифр (файлы
# других процессов стенда не считаются).
#
# libtest32.so собирается флагами tools/i386-toolchain.sh (без sudo).
# Нет 32-битного C-тулчейна — System_testsCoTests пропускается с причиной;
# остальные модули от C-помощника не зависят.
set -u
. "$(dirname "$0")/env.sh"

INSTALL="${BB32INSTALL:-$BB/build/BB32LIN}"
INTERP32="$INSTALL/blackbox-console"

# <модуль>|<marks>|<таймаут>
MANIFEST='Unit_testsMain||180
Lin_testsFiles|proc slow|600
System_testsTasks|proc slow|900
System_testsMonitors|proc|300
Dev_testsActive|proc|300
Dev_testsYield|proc|300
System_testsCoTests|proc|300
System_testsUnloadGuard|proc|300'

LIST=0; only=""
for a in "$@"; do
	case "$a" in
		--list) LIST=1;;
		*) only="$only $a";;
	esac
done
if [ "$LIST" -eq 1 ]; then
	printf '%s\n' "$MANIFEST"; exit 0
fi

if [ -z "$only" ]; then
	if ! "$BB/tools/build32.sh"; then
		echo "test32: сборка i386 не удалась" >&2; exit 1
	fi
fi
[ -x "$INTERP32" ] || { echo "test32: нет $INTERP32 — tools/build32.sh" >&2; exit 2; }

# Поставочный build32.sh подсистемы *_tests не собирает: тестовые модули
# i386 компилируются здесь по требованию (Unit и мир уже собраны).
compile_tests32() {
	log="${TMPDIR:-/tmp}/compile32-$$.log"
	{
		echo 'DevCompiler.InitLogMarks'
		echo "DevLibrarian.EnableDevLib('$INSTALL', 'Mod')"
		for m in Unit_testsProbe Unit_testsMain System_testsClock \
			System_testsSupport System_testsCRaw System_testsCo \
			System_testsBasic System_testsGcStack System_testsTrapStack \
			System_testsCoTests System_testsTasks System_testsMonitors \
			Dev_testsActive Lin_testsFiles 'Dev_testsYield^' \
			System_testsUnloadGuardCode System_testsUnloadGuard; do
			opt=""
			case "$m" in *\^) opt="^"; m="${m%^}";; esac
			echo "DevCompiler.CompileThis $m $opt"
		done
		echo 'DevLibrarian.DisableDevLib'
	} > "$log.cmd"
	if ! (cd "$BB" && "$DEV0" < "$log.cmd" > "$log" 2>&1); then
		echo "test32: компиляция тестов не удалась:" >&2
		tail -20 "$log" >&2; rm -f "$log" "$log.cmd"; return 1
	fi
	if grep -qE 'error:|not found|~TRAP|[0-9]+ error[s]? detected' "$log"; then
		echo "test32: ошибки компиляции тестов:" >&2
		grep -nE 'error:|not found|~TRAP|[0-9]+ error' "$log" | head -10 >&2
		rm -f "$log" "$log.cmd"; return 1
	fi
	rm -f "$log" "$log.cmd"
}
compile_tests32 || exit 1

# libtest32.so — C-помощник сопрограмм (ucontext, tools/test64.c).
# Пустой stdout tools/i386-toolchain.sh — тулчейна нет: System_testsCoTests
# пропускается, остальные модули от C-помощника не зависят.
have_lib=0
if I386FLAGS=$("$BB/tools/i386-toolchain.sh"); then
	if [ -n "$I386FLAGS" ]; then
		# shellcheck disable=SC2086
		if gcc $I386FLAGS -shared -fPIC -O2 -o "$INSTALL/libtest32.so" "$BB/tools/test64.c"; then
			have_lib=1
		else
			echo "test32: сборка libtest32.so не удалась" >&2
		fi
	fi
fi
if [ "$have_lib" -eq 0 ] && [ -f "$INSTALL/libtest32.so" ]; then have_lib=1; fi

run_one() {
	mod="$1"; marks="$2"; tmo="$3"
	out="${TMPDIR:-/tmp}/unit32-$$.out"
	printf 'UnitRunner.Run %s %s\n' "$mod" "$marks" \
		| (cd "$INSTALL" && LD_LIBRARY_PATH="$INSTALL${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
			timeout -k 5 "$tmo" ./blackbox-console) >"$out" 2>&1
	rc=$?
	grep -E '^(PASS|FAIL|EMPTY|SKIP|== Unit)' "$out" || true
	if [ $rc -ne 0 ]; then
		echo "test32: $mod — процесс завершился с кодом $rc" >&2; tail -15 "$out" >&2
		rm -f "$out"; return 1
	fi
	if grep -q '^== Unit: ' "$out" && grep -q 'failed 0,' "$out" && grep -q '^PASS ' "$out"; then
		rm -f "$out"; return 0
	fi
	echo "test32: $mod — есть провалы или нет итоговой строки" >&2
	tail -15 "$out" >&2; rm -f "$out"; return 1
}

# В /tmp могут жить временные файлы других процессов стенда (например,
# давних исполнителей RobustParallel). Проверяем только прирост.
tmp_before=$(find /tmp -maxdepth 1 -type f -name 'odc[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]' 2>/dev/null | wc -l)

ok=0; bad=0; skipped=0; total=0
while IFS='|' read -r mod marks tmo; do
	[ -n "$mod" ] || continue
	if [ -n "$only" ]; then
		case " $only " in *" $mod "*) ;; *) continue;; esac
	fi
	if [ "$have_lib" -eq 0 ] && [ "$mod" = System_testsCoTests ]; then
		echo "SKIP System_testsCoTests: нет libtest32.so (32-битный C-тулчейн недоступен)"
		skipped=$((skipped + 1)); continue
	fi
	total=$((total + 1))
	echo "test32: $mod (marks='${marks:-}', ${tmo}s)"
	if run_one "$mod" "$marks" "$tmo"; then
		ok=$((ok + 1)); echo "PASS32 $mod"
	else
		bad=$((bad + 1)); echo "FAIL32 $mod"
	fi
done <<EOF
$MANIFEST
EOF

# Проверка уборки: прогон не должен добавить имён odc + 9 цифр.
left=$(find /tmp -maxdepth 1 -type f -name 'odc[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]' 2>/dev/null | wc -l)
if [ "$left" -gt "$tmp_before" ]; then
	echo "test32: прогон оставил $((left - tmp_before)) временных файлов (было $tmp_before, стало $left)" >&2
	bad=$((bad + 1))
fi

echo "test32: этапов $total, ok $ok, skip $skipped, bad $bad"
[ "$bad" -eq 0 ]
