#!/bin/sh
# Windows x64: сборка подсистем и консольные тесты Unit/Tasks под wine.
#
#   tools/test-win64.sh [--rebuild]
#
# Собирает @Win-подсистемы в build/BB64WIN, линкует консольный PE
# (WinIntInit), переключает подсистему PE с GUI на console (линкер
# пишет 2, правим заголовок на 3) и гоняет UnitTests + Test64WinTasks.
# Каталог wine — .work/wine64 (WINEARCH=win64), экран пользователя не
# используется. Нет wine — тест пропускается с причиной.
set -u
. "$(dirname "$0")/env.sh"

OUT="$BB/build/BB64WIN"
PREFIX="$BB/.work/wine64"
WINEDEBUG=-all
export WINEDEBUG
rebuild=0
for a in "$@"; do
	case "$a" in
		--rebuild) rebuild=1;;
		*) echo "test-win64: неизвестный ключ $a" >&2; exit 2;;
	esac
done

skip() {
	echo "SKIP Test64Win64: $*"
	exit 0
}

command -v wine >/dev/null 2>&1 || skip "нет wine"
[ -x "$DEV0" ] || { echo "test-win64: нет $DEV0" >&2; exit 2; }
[ -x "$INSTALL/blackbox-console" ] || { echo "test-win64: соберите сначала tools/build64.sh" >&2; exit 2; }
mkdir -p "$OUT" "$PREFIX" || exit 1
export WINEPREFIX="$PREFIX" WINEARCH=win64

# 1. Каркас @Win. Первый прогон долгий; повторно — только с --rebuild.
if [ "$rebuild" -eq 1 ] || [ ! -f "$OUT/System/Code/Kernel.ocf" ]; then
	echo "test-win64: сборка каркаса @Win"
	printf "DevCompiler64.InitLogMarks\nDevLibrarian.EnableDevLib('$OUT', 'Mod64')\nDevCompiler64.CompileSubs @Win Win System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket\nDevLibrarian.DisableDevLib\n" \
		| timeout 1200 "$DEV0" >"$OUT/build.log" 2>&1 || {
			echo "test-win64: сборка @Win не удалась:" >&2
			tail -30 "$OUT/build.log" >&2; exit 1; }
	grep -qE 'error:|not found|~TRAP|[0-9]+ error[s]? detected' "$OUT/build.log" && {
		echo "test-win64: ошибки в журнале @Win:" >&2
		grep -nE 'error:|not found|~TRAP|[0-9]+ error' "$OUT/build.log" | head -20 >&2; exit 1; }
fi

# 2. Unit, Tasks (System/Mod64), пул нитей и Win-тесты задач.
printf "DevCompiler64.InitLogMarks\nDevLibrarian.EnableDevLib('$OUT', 'Mod64')\nDevCompiler64.CompileSubs @Win Unit\nDevCompiler64.CompileThis Tasks\nDevCompiler64.CompileThis AsyncWinSock\nDevCompiler64.CompileThis AsyncWork\nDevCompiler64.CompileThis AsyncWorkWin\nDevCompiler64.CompileThis WinIntInit\nDevCompiler64.CompileThis WinInit\nDevCompiler64.CompileThis WinKernel\nDevCompiler64.CompileThis Test64WinTasks\nDevCompiler64.CompileThis Test64WinWorkers\nDevLibrarian.DisableDevLib\n" \
	| timeout 600 "$DEV0" >"$OUT/unit-build.log" 2>&1 || {
		echo "test-win64: компиляция Unit/Tasks не удалась:" >&2
		tail -30 "$OUT/unit-build.log" >&2; exit 1; }
grep -qE 'error:|not found|~TRAP|[0-9]+ error[s]? detected' "$OUT/unit-build.log" && {
	echo "test-win64: ошибки Unit/Tasks:" >&2
	grep -nE 'error:|not found|~TRAP|[0-9]+ error' "$OUT/unit-build.log" | head -20 >&2; exit 1; }

# 3. Консольный PE и подсистема console.
printf "DevLibrarian.EnableDevLib('$OUT', 'Mod64')\nDevLinker1.LinkPeExe64 '$OUT/blackbox-console.exe' := Kernel+ WinKernel Files WinFiles StdLoader WinIntInit\$\nDevLibrarian.DisableDevLib\n" \
	| timeout 300 "$DEV0" >"$OUT/link.log" 2>&1 || {
		echo "test-win64: линковка PE не удалась:" >&2; tail -20 "$OUT/link.log" >&2; exit 1; }
grep -q '^ok$' "$OUT/link.log" || { echo "test-win64: линковка: $(cat "$OUT/link.log")" >&2; exit 1; }
python3 -I - "$OUT/blackbox-console.exe" <<'PY' || exit 1
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
e = int.from_bytes(d[0x3c:0x40], 'little')
off = e + 92
if int.from_bytes(d[off:off+2], 'little') != 3:
	assert int.from_bytes(d[off:off+2], 'little') == 2, 'unexpected PE subsystem'
	d[off:off+2] = (3).to_bytes(2, 'little')
	open(p, 'wb').write(d)
PY

# 4. Префикс wine и прогоны.
timeout 180 wineboot -u >"$OUT/wineboot.log" 2>&1 || {
	echo "test-win64: wineboot не удался:" >&2; tail -10 "$OUT/wineboot.log" >&2; exit 1; }

run() {
	mod="$1"; marks="$2"; expect="$3"
	out="$OUT/run-$mod.out"
	(cd "$OUT" && printf 'UnitRunner.Run %s %s\n' "$mod" "$marks" \
		| timeout 180 wine ./blackbox-console.exe) >"$out" 2>&1
	rc=$?
	grep -E '^(PASS|FAIL|EMPTY|SKIP|== Unit|Test64WinTasks:)' "$out" || true
	if [ $rc -ne 0 ] || ! grep -q 'failed 0,' "$out" || ! grep -q "$expect" "$out"; then
		echo "test-win64: $mod — провал (код $rc)" >&2
		tail -25 "$out" >&2; exit 1
	fi
}

run UnitTests "" "passed 4"
run Test64WinTasks proc "passed 5"
run Test64WinWorkers proc "passed 4"
exit 0
