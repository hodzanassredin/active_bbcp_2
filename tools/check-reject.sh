#!/bin/sh
# Негативные проверки компилятора этапа 6: расширение должно отвергаться
# с обычной ошибкой компиляции, а не трапом.
#
#   tools/check-reject.sh
#
# Фикстуры лежат в Dev/Reject; сценарий временно кладёт каждую на путь,
# который выберет DevLibrarian по имени модуля, компилирует и ждёт ошибку.
set -u
. "$(dirname "$0")/env.sh"

CC64="$BB/tools/go64.sh"
CC32="$BB/tools/go32.sh"
ok=0; bad=0

cleanup () {
	rm -f "$BB/Dev/Mod/RejAwait.cp" "$BB/Dev/Mod/RejCall.cp" \
		"$BB/Dev/Mod/RejLocal.cp" "$BB/Dev/Mod/RejNoActive.cp" \
		"$BB/Dev/Mod/RejCode.cp" "$BB/Dev/Mod/KeyProbe.cp" "$BB/Dev/Mod/RejGuarded.cp" \
		"$BB/System/Mod/KeyProbe.cp" "$BB/System/Mod/Usermod.cp"
	rm -f "$BB"/.work/BB64LIN/Dev/Code/{RejAwait,RejCall,RejLocal,RejNoActive,RejCode,KeyProbe}.ocf 2>/dev/null
	rm -f "$BB"/.work/BB64LIN/System/Code/{KeyProbe,Usermod}.ocf 2>/dev/null
	rm -f "$BB"/.work/i386/System/Code/{KeyProbe,Usermod}.ocf 2>/dev/null
	rm -f "$BB"/.work/i386/Dev/Code/KeyProbe.ocf 2>/dev/null
}
trap cleanup EXIT INT TERM

# run <фикстура> <куда положить> <компилятор> <модуль[опции]> <что должно быть>
run () {
	fix="$1"; dest="$2"; cc="$3"; mod="$4"; why="$5"
	cp "$BB/Dev/Reject/$fix" "$dest" || { echo "BAD  $why: не скопировать $fix" >&2; bad=$((bad+1)); return; }
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

# Положительный контроль: однословное имя пользовательского модуля больше
# не делает его каркасом — ключ точек уступки разрешён. Старый код считал
# каркасом любой модуль с однословным именем и отвергал ключ.
run_ok () {
	fix="$1"; dest="$2"; cc="$3"; mod="$4"; why="$5"
	cp "$BB/Dev/Reject/$fix" "$dest" || { echo "BAD  $why: не скопировать $fix" >&2; bad=$((bad+1)); return; }
	if out=$("$cc" "$mod" 2>&1); then
		echo "PASS $why"
		ok=$((ok + 1))
	else
		echo "BAD  $why: компиляция отвергнута" >&2
		printf '%s\n' "$out" | tail -5 >&2
		bad=$((bad + 1))
	fi
	rm -f "$dest"
}

run AwaitOutsideExclusive.cp "$BB/Dev/Mod/RejAwait.cp" "$CC64" DevRejAwait \
	"AWAIT вне [exclusive]"
run AwaitWithCall.cp "$BB/Dev/Mod/RejCall.cp" "$CC64" DevRejCall \
	"AWAIT с вызовом процедуры в условии"
run LocalExclusive.cp "$BB/Dev/Mod/RejLocal.cp" "$CC64" DevRejLocal \
	"[exclusive] у вложенной процедуры"
run ExclusiveNoActive.cp "$BB/Dev/Mod/RejNoActive.cp" "$CC64" DevRejNoActive \
	"[exclusive] без IMPORT ACTIVE"
run ExclusiveWithCode.cp "$BB/Dev/Mod/RejCode.cp" "$CC64" DevRejCode \
	"[exclusive] вместе с [code]"
run KeyFramework.cp "$BB/System/Mod/KeyProbe.cp" "$CC64" 'SystemKeyProbe^' \
	"ключ точек уступки в модуле каркаса (System)"
run KeyFramework.cp "$BB/System/Mod/KeyProbe.cp" "$CC32" 'SystemKeyProbe^' \
	"ключ точек уступки на i386 в модуле каркаса"
run GuardedAmd64.cp "$BB/Dev/Mod/RejGuarded.cp" "$CC64" DevRejGuarded \
	"[guarded] на amd64 (SEH/GetComKernel)"
run_ok UserKey.cp "$BB/System/Mod/Usermod.cp" "$CC64" 'Usermod^' \
	"ключ точек уступки в однословном пользовательском модуле"

echo "== reject: прошло $ok, провалов $bad"
[ $bad -eq 0 ]
