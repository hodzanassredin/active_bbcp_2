#!/bin/sh
# Мутации пробы CopyStack: тесты обязаны падать на каждой порче.
#
#   tools/iso.sh tools/test-copystack-mut.sh
#
# Чистый прогон печатает три PASS и завершается кодом 0. Каждая мутация
# включается отдельной командой в том же процессе (состояние модуля живёт
# между строками) и обязана кончить процесс ненулевым кодом:
#   no-register — копия участка не корень сборщика;
#   shift       — участок возвращается со сдвигом на 16 байт;
#   stay        — после трапа объект остаётся «внутри» (Running, не Failed).
set -u
. "$(dirname "$0")/env.sh"

[ -x "$INTERP" ] || { echo "mut: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }
if command -v cc >/dev/null 2>&1; then
	cc -shared -fPIC -O2 -o "$INSTALL/libtest64.so" "$BB/tools/test64.c" 2>/dev/null || true
fi

echo "mut: компилирую CopyStack и Copy_devStack"
if ! "$BB/tools/go64.sh" CopyStack Copy_devStack >/dev/null 2>&1; then
	echo "mut: компиляция не удалась" >&2; exit 2
fi

out="${TMPDIR:-/tmp}/copystack-mut-$$.log"
trap 'rm -f "$out"' EXIT

# Интерпретатор запускается напрямую, чтобы timeout убивал именно его:
# сдвиг участка уводит продолжение в мусор, и процесс может крутиться.
run_case () {
	name="$1"; cmds="$2"; wantzero="$3"
	( cd "$INSTALL" && printf '%s\n' "$cmds" | \
		LD_LIBRARY_PATH="$INSTALL${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		timeout -s KILL 10 "$INTERP" ) >"$out" 2>&1
	rc=$?
	if [ "$wantzero" -eq 1 ]; then
		if [ $rc -eq 0 ] && grep -q '^PASS' "$out"; then
			echo "PASS mut: $name (код 0)"
			return 0
		fi
		echo "FAIL mut: $name — ожидался успех, код $rc" >&2
		tail -8 "$out" >&2
		return 1
	fi
	if [ $rc -ne 0 ] || grep -q '^FAIL' "$out"; then
		echo "PASS mut: $name поймана (код $rc)"
		return 0
	fi
	echo "FAIL mut: $name не поймана" >&2
	tail -8 "$out" >&2
	return 1
}

bad=0
run_case "чистый контроль" 'Copy_devStack.Mut' 1 || bad=1
run_case "копия не зарегистрирована" 'CopyStack.MutNoRegisterOn
Copy_devStack.MutGc' 0 || bad=1
run_case "сдвиг участка на 16 байт" 'CopyStack.MutShift16
Copy_devStack.MutRun' 0 || bad=1
run_case "объект остаётся внутри после трапа" 'CopyStack.MutStayOn
Copy_devStack.MutTrap' 0 || bad=1

if [ $bad -eq 0 ]; then
	echo "mut: все мутации пойманы"
	exit 0
fi
echo "mut: есть непойманные мутации" >&2
exit 1
