#!/bin/sh
# Прогон модульных тестов без окон: подсистема Unit + blackbox-console.
#
#   tools/test.sh                 # все модули из Test64/tests.txt
#   tools/test.sh Test64Tests     # только указанные модули
#   tools/test.sh --list
#
# Формат Test64/tests.txt: <модуль>|<marks>|<таймаут>|<доп. модули>.
# Один модуль — один процесс (tools/unit.sh); итог берётся из отчёта
# UnitRunner («failed 0») и кода завершения.
set -u
. "$(dirname "$0")/env.sh"

manifest="$BB/Test64/tests.txt"
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

# Тест этапа 8 требует отдельного сетевого пространства (unshare -rmn).
# Нет прав/утилит — обвязка печатает SKIP с причиной; это не провал.
runresolver=1
if [ -n "$only" ]; then
	case " $only " in *" Test64Resolver "*) ;; *) runresolver=0;; esac
fi
if [ "$runresolver" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Test64Resolver (marks='proc', сеть и mount без root)"
	rout=$("$BB/tools/test-resolver.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP Test64Resolver"; summary="$summary
  SKIP Test64Resolver" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS Test64Resolver"; summary="$summary
  OK   Test64Resolver"
			else
				bad=$((bad + 1)); echo "FAIL Test64Resolver"; summary="$summary
  BAD  Test64Resolver"
			fi ;;
	esac
fi

runrobust=1
if [ -n "$only" ]; then
	case " $only " in *" Test64RobustParallel "*) ;; *) runrobust=0;; esac
fi
if [ "$runrobust" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Test64RobustParallel (marks='proc', RobustParallel 1/4/16)"
	if "$BB/tools/test-robust.sh"; then
		ok=$((ok + 1)); echo "PASS Test64RobustParallel"; summary="$summary
  OK   Test64RobustParallel"
	else
		bad=$((bad + 1)); echo "FAIL Test64RobustParallel"; summary="$summary
  BAD  Test64RobustParallel"
	fi
fi

runmpi=1
if [ -n "$only" ]; then
	case " $only " in *" Test64Mpi "*) ;; *) runmpi=0;; esac
fi
if [ "$runmpi" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Test64Mpi (marks='proc', mpiexec на 5 рангах)"
	rout=$("$BB/tools/test-mpi.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP Test64Mpi"; summary="$summary
  SKIP Test64Mpi" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS Test64Mpi"; summary="$summary
  OK   Test64Mpi"
			else
				bad=$((bad + 1)); echo "FAIL Test64Mpi"; summary="$summary
  BAD  Test64Mpi"
			fi ;;
	esac
fi

rungui=1
if [ -n "$only" ]; then
	case " $only " in *" Test64Gui "*) ;; *) rungui=0;; esac
fi
if [ "$rungui" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Test64Gui (GUI под gtk4-broadwayd, без экрана)"
	rout=$("$BB/tools/test-gui.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP Test64Gui"; summary="$summary
  SKIP Test64Gui" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS Test64Gui"; summary="$summary
  OK   Test64Gui"
			else
				bad=$((bad + 1)); echo "FAIL Test64Gui"; summary="$summary
  BAD  Test64Gui"
			fi ;;
	esac
fi

rungui=1
if [ -n "$only" ]; then
	case " $only " in *" Test64DemoGui "*) ;; *) rungui=0;; esac
fi
if [ "$rungui" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Test64DemoGui (окно DemoAnts под gtk4-broadwayd)"
	rout=$("$BB/tools/test-demo-gui.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP Test64DemoGui"; summary="$summary
  SKIP Test64DemoGui" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS Test64DemoGui"; summary="$summary
  OK   Test64DemoGui"
			else
				bad=$((bad + 1)); echo "FAIL Test64DemoGui"; summary="$summary
  BAD  Test64DemoGui"
			fi ;;
	esac
fi

echo "== test: всего $total, прошло $ok, провалов $bad, пропущено $skipped"
[ -n "$summary" ] && printf '%s\n' "$summary"
[ $bad -eq 0 ]
