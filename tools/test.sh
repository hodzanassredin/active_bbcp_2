#!/bin/sh
# Прогон модульных тестов без окон: подсистема Unit + blackbox-console.
#
#   tools/test.sh                 # все модули из tools/tests.txt
#   tools/test.sh SystemTests     # только указанные модули
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
	case " $only " in *" CommTestsResolver "*) ;; *) runresolver=0;; esac
fi
if [ "$runresolver" -eq 1 ]; then
	total=$((total + 1))
	echo "test: CommTestsResolver (marks='proc', сеть и mount без root)"
	rout=$("$BB/tools/test-resolver.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP CommTestsResolver"; summary="$summary
  SKIP CommTestsResolver" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS CommTestsResolver"; summary="$summary
  OK   CommTestsResolver"
			else
				bad=$((bad + 1)); echo "FAIL CommTestsResolver"; summary="$summary
  BAD  CommTestsResolver"
			fi ;;
	esac
fi

runrobust=1
if [ -n "$only" ]; then
	case " $only " in *" RobustTestsParallel "*) ;; *) runrobust=0;; esac
fi
if [ "$runrobust" -eq 1 ]; then
	total=$((total + 1))
	echo "test: RobustTestsParallel (marks='proc', RobustParallel 1/4/16)"
	if "$BB/tools/test-robust.sh"; then
		ok=$((ok + 1)); echo "PASS RobustTestsParallel"; summary="$summary
  OK   RobustTestsParallel"
	else
		bad=$((bad + 1)); echo "FAIL RobustTestsParallel"; summary="$summary
  BAD  RobustTestsParallel"
	fi
fi

runmpi=1
if [ -n "$only" ]; then
	case " $only " in *" AsyncTestsMpi "*) ;; *) runmpi=0;; esac
fi
if [ "$runmpi" -eq 1 ]; then
	total=$((total + 1))
	echo "test: AsyncTestsMpi (marks='proc', mpiexec на 5 рангах)"
	rout=$("$BB/tools/test-mpi.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP AsyncTestsMpi"; summary="$summary
  SKIP AsyncTestsMpi" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS AsyncTestsMpi"; summary="$summary
  OK   AsyncTestsMpi"
			else
				bad=$((bad + 1)); echo "FAIL AsyncTestsMpi"; summary="$summary
  BAD  AsyncTestsMpi"
			fi ;;
	esac
fi

rungui=1
if [ -n "$only" ]; then
	case " $only " in *" DemoTestsGui "*) ;; *) rungui=0;; esac
fi
if [ "$rungui" -eq 1 ]; then
	total=$((total + 1))
	echo "test: DemoTestsGui (GUI под gtk4-broadwayd, без экрана)"
	rout=$("$BB/tools/test-gui.sh" 2>&1); rrc=$?
	printf '%s\n' "$rout"
	case "$rout" in
		SKIP*)
			skipped=$((skipped + 1)); echo "SKIP DemoTestsGui"; summary="$summary
  SKIP DemoTestsGui" ;;
		*)
			if [ $rrc -eq 0 ]; then
				ok=$((ok + 1)); echo "PASS DemoTestsGui"; summary="$summary
  OK   DemoTestsGui"
			else
				bad=$((bad + 1)); echo "FAIL DemoTestsGui"; summary="$summary
  BAD  DemoTestsGui"
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
