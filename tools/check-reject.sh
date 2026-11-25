#!/bin/sh
# Негативные проверки: обычные правила языка на Tasks.Ctx должны давать
# ошибку компиляции, а не трап.
#
#   tools/check-reject.sh
#
# Фикстуры лежат в Dev_dev/Reject; сценарий временно кладёт каждую на путь,
# который выберет DevLibrarian по имени модуля, компилирует и ждёт ошибку.
set -u
. "$(dirname "$0")/env.sh"

CC64="$BB/tools/go64.sh"
ok=0; bad=0

cleanup () {
	rm -f "$BB/Dev/Mod/RejCtxVar.cp" "$BB/Dev/Mod/RejCtxCopy.cp" "$BB/Dev/Mod/RejExclusive.cp"
	rm -f "$BB"/.work/BB64LIN/Dev/Code/{RejCtxVar,RejCtxCopy,RejExclusive}.ocf 2>/dev/null
}
trap cleanup EXIT INT TERM

# run <фикстура> <куда положить> <компилятор> <модуль> <что должно быть>
run () {
	fix="$1"; dest="$2"; cc="$3"; mod="$4"; why="$5"
	cp "$BB/Dev_dev/Reject/$fix" "$dest" || { echo "BAD  $why: не скопировать $fix" >&2; bad=$((bad+1)); return; }
	if out=$("$cc" "$mod" 2>&1); then
		echo "BAD  $why: компиляция прошла" >&2
		printf '%s\n' "$out" | tail -5 >&2
		bad=$((bad + 1))
	elif printf '%s\n' "$out" | grep -q 'error:'; then
		echo "PASS $why: $(printf '%s\n' "$out" | grep 'error:' | head -1)"
		ok=$((ok + 1))
	else
		echo "BAD  $why: не ошибка компиляции, а трап/сбой" >&2
		printf '%s\n' "$out" | head -8 >&2
		bad=$((bad + 1))
	fi
	rm -f "$dest"
}

run TasksCtxVar.cp "$BB/Dev/Mod/RejCtxVar.cp" "$CC64" DevRejCtxVar \
	"переменная Tasks.Ctx вне Tasks"
run TasksCtxCopy.cp "$BB/Dev/Mod/RejCtxCopy.cp" "$CC64" DevRejCtxCopy \
	"копия пропуска Tasks.Ctx"
run TasksExclusiveCall.cp "$BB/Dev/Mod/RejExclusive.cp" "$CC64" DevRejExclusive \
	"вызов Exclusive снаружи"

echo "== reject: прошло $ok, провалов $bad"
[ $bad -eq 0 ]
