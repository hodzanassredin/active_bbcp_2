#!/bin/sh
# Прогон модульных тестов без окон: подсистема Unit + blackbox-console.
#
#   tools/test.sh                 # все модули из tools/tests.txt
#   tools/test.sh System_devMain # только указанные модули
#   tools/test.sh --list
#
# Формат tools/tests.txt: <модуль>|<marks>|<таймаут>|<доп. модули>.
# Один модуль — один процесс (tools/unit.sh); итог берётся из отчёта
# UnitRunner («failed 0») и кода завершения.
set -u
. "$(dirname "$0")/env.sh"

manifest="$BB/tools/tests.txt"
[ -f "$manifest" ] || { echo "test: нет $manifest" >&2; exit 2; }

LIST=0; only=""
for a in "$@"; do
	case "$a" in
		--list) LIST=1;;
		*) only="$only $a";;
	esac
done
if [ "$LIST" -eq 1 ]; then
	grep -v '^[[:space:]]*#' "$manifest" | grep -v '^[[:space:]]*$'; exit 0
fi
[ -x "$INTERP" ] || { echo "test: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }

ok=0; bad=0; skipped=0; total=0; summary=""
while IFS='|' read -r mod marks tmo deps opts; do
	case "$mod" in ''|\#*) continue;; esac
	case "$tmo" in ''|*[!0-9]*) tmo=300;; esac
	if [ -n "$only" ]; then
		case " $only " in *" $mod "*) ;; *) continue;; esac
	fi
	total=$((total + 1))
	echo "test: $mod (marks='${marks:-}', ${tmo}s)"
	if "$BB/tools/unit.sh" "$mod" --marks "$marks" --timeout "$tmo" --compile "$deps" --opts "$opts"; then
		ok=$((ok + 1)); echo "PASS $mod"; summary="$summary
  OK   $mod"
	else
		bad=$((bad + 1)); echo "FAIL $mod"; summary="$summary
  BAD  $mod"
	fi
done < "$manifest"

echo "== test: всего $total, прошло $ok, провалов $bad, пропущено $skipped"
[ -n "$summary" ] && printf '%s\n' "$summary"
[ $bad -eq 0 ]
