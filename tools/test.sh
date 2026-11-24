#!/bin/sh
# Прогон модульных тестов без окон: подсистема Unit + blackbox-console.
#
#   tools/test.sh                 # все модули из tools/tests.txt
#   tools/test.sh System_testsMain # только указанные модули
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

# Тест этапа 8 требует отдельного сетевого пространства (unshare -rmn).
# Нет прав/утилит — обвязка печатает SKIP с причиной; это не провал.
runresolver=1
if [ -n "$only" ]; then
	case " $only " in *" Comm_testsResolver "*) ;; *) runresolver=0;; esac
fi
if [ "$runresolver" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Comm_testsResolver (marks='proc', сеть и mount без root)"
	rout=$("$BB/tools/test-resolver.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP Comm_testsResolver"; summary="$summary
  SKIP Comm_testsResolver" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS Comm_testsResolver"; summary="$summary
  OK   Comm_testsResolver"
			else
				bad=$((bad + 1)); echo "FAIL Comm_testsResolver"; summary="$summary
  BAD  Comm_testsResolver"
			fi ;;
	esac
fi

runrobust=1
if [ -n "$only" ]; then
	case " $only " in *" Robust_testsParallel "*) ;; *) runrobust=0;; esac
fi
if [ "$runrobust" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Robust_testsParallel (marks='proc', RobustParallel 1/4/16)"
	if "$BB/tools/test-robust.sh"; then
		ok=$((ok + 1)); echo "PASS Robust_testsParallel"; summary="$summary
  OK   Robust_testsParallel"
	else
		bad=$((bad + 1)); echo "FAIL Robust_testsParallel"; summary="$summary
  BAD  Robust_testsParallel"
	fi
fi

runmpi=1
if [ -n "$only" ]; then
	case " $only " in *" Async_testsMpi "*) ;; *) runmpi=0;; esac
fi
if [ "$runmpi" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Async_testsMpi (marks='proc', mpiexec на 5 рангах)"
	rout=$("$BB/tools/test-mpi.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP Async_testsMpi"; summary="$summary
  SKIP Async_testsMpi" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS Async_testsMpi"; summary="$summary
  OK   Async_testsMpi"
			else
				bad=$((bad + 1)); echo "FAIL Async_testsMpi"; summary="$summary
  BAD  Async_testsMpi"
			fi ;;
	esac
fi

rungui=1
if [ -n "$only" ]; then
	case " $only " in *" Demo_testsGui "*) ;; *) rungui=0;; esac
fi
if [ "$rungui" -eq 1 ]; then
	total=$((total + 1))
	echo "test: Demo_testsGui (GUI под gtk4-broadwayd, без экрана)"
	rout=$("$BB/tools/test-gui.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP Demo_testsGui"; summary="$summary
  SKIP Demo_testsGui" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS Demo_testsGui"; summary="$summary
  OK   Demo_testsGui"
			else
				bad=$((bad + 1)); echo "FAIL Demo_testsGui"; summary="$summary
  BAD  Demo_testsGui"
			fi ;;
	esac
fi

rungui=1
if [ -n "$only" ]; then
	case " $only " in *" DemoAntsGui "*) ;; *) rungui=0;; esac
fi
if [ "$rungui" -eq 1 ]; then
	total=$((total + 1))
	echo "test: DemoAntsGui (окно DemoAnts под gtk4-broadwayd)"
	rout=$("$BB/tools/test-demo-gui.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP DemoAntsGui"; summary="$summary
  SKIP DemoAntsGui" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS DemoAntsGui"; summary="$summary
  OK   DemoAntsGui"
			else
				bad=$((bad + 1)); echo "FAIL DemoAntsGui"; summary="$summary
  BAD  DemoAntsGui"
			fi ;;
	esac
fi

echo "== test: всего $total, прошло $ok, провалов $bad, пропущено $skipped"
[ -n "$summary" ] && printf '%s\n' "$summary"
[ $bad -eq 0 ]
