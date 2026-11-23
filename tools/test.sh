#!/bin/sh
# Прогон тестов без окон: консольный 64-битный blackbox-console + Test64.
#
#   tools/test.sh                # все тесты из Test64/tests.txt
#   tools/test.sh Test64Basic.Do # только указанные
#   tools/test.sh --list
#
# Формат Test64/tests.txt: <команда>|<ожидание: pass|fail>|<таймаут, с>
# PASS — напечатан TEST64-PASS; FAIL — TEST64-FAIL; всё прочее — CRASH.
set -u
. "$(dirname "$0")/env.sh"

manifest="$BB/Test64/tests.txt"
[ -f "$manifest" ] || { echo "test: нет $manifest" >&2; exit 2; }

LIST=""
for a in "$@"; do
	case "$a" in
		--list) LIST=1;;
		*) ARGS="$*";;
	esac
done
if [ -n "${LIST:-}" ]; then
	grep -v '^[[:space:]]*#' "$manifest" | grep -v '^[[:space:]]*$'; exit 0
fi

[ -x "$INTERP" ] || { echo "test: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }
command -v cc >/dev/null 2>&1 || { echo "test: нет cc (нужен libtest64.so)" >&2; exit 2; }
cc -shared -fPIC -O2 -o "$INSTALL/libtest64.so" "$BB/tools/test64.c" || {
	echo "test: сборка libtest64.so не удалась" >&2; exit 2; }

# Порядок: каркас и C-помощник раньше остальных.
mods="Test64Support Test64CRaw Test64Co"
for f in "$BB"/Test64/Mod/*.cp; do
	[ -e "$f" ] || continue
	m="Test64$(basename "$f" .cp)"
	case " $mods " in *" $m "*) ;; *) mods="$mods $m";; esac
done
echo "test: компилирую:$mods"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"${TMPDIR:-/tmp}/test64-compile.log" 2>&1; then
		break
	fi
	if [ $pass -eq 3 ]; then
		echo "test: компиляция тестов не удалась (проход $pass):" >&2
		cat "${TMPDIR:-/tmp}/test64-compile.log" >&2; exit 1
	fi
done

ok=0; bad=0; total=0; summary=""
for line in $(grep -v '^[[:space:]]*#' "$manifest" | grep -v '^[[:space:]]*$'); do
	cmd=${line%%|*}; rest=${line#*|}; expect=${rest%%|*}; tmo=${rest#*|}
	case "$tmo" in ''|*[!0-9]*) tmo=120;; esac
	if [ -n "${ARGS:-}" ]; then
		case " $ARGS " in *" $cmd "*) ;; *) continue;; esac
	fi
	total=$((total + 1))
	if [ "${cmd%%:*}" = "compile" ]; then
		m=${cmd#compile:}
		if "$BB/tools/go64.sh" "$m" >/dev/null 2>&1; then obs=pass; else obs=crash; fi
		out="compile $m"; rc=0
	else
		outf="${TMPDIR:-/tmp}/test64-$$.out"
		echo "$cmd" | timeout -k 5 "$tmo" "$BB/tools/run64.sh" > "$outf" 2>&1
		rc=$?; out=$(cat "$outf")
		if grep -q '^TEST64-FAIL' "$outf"; then obs=fail
		elif grep -q '^TEST64-PASS' "$outf"; then obs=pass
		elif [ $rc -eq 124 ]; then obs=crash; out="timeout ${tmo}s"
		else obs=crash
		fi
		rm -f "$outf"
	fi
	if [ "$obs" = "$expect" ]; then
		ok=$((ok + 1)); echo "PASS $cmd (ожидалось $expect, получено $obs)"
		summary="$summary
  OK   $cmd -> $obs"
	else
		bad=$((bad + 1))
		case "$obs" in
			crash) why="CRASH (TRAP/код $rc/нет маркера)";;
			*) why="НЕОЖИДАННО: получено $obs, ожидалось $expect";;
		esac
		echo "FAIL $cmd ($why)"
		printf '%s\n' "$out" | grep -E 'TEST64|FAIL |~TRAP|TRAP REPORT|Aborting|command error|code file' | head -12
		summary="$summary
  BAD  $cmd -> $obs (ожидалось $expect)"
	fi
done
echo "== test: всего $total, совпало с ожиданием $ok, расхождений $bad"
[ -n "$summary" ] && printf '%s\n' "$summary"
[ $bad -eq 0 ]
