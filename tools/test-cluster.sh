#!/bin/bash
# Кластер этапа 1: узлы ActorNet и Raft в отдельных процессах blackbox-console.
#
#   tools/test-cluster.sh                 # 20 прогонов всего сценария
#   tools/test-cluster.sh --runs 3        # три прогона
#   tools/test-cluster.sh --quick         # один короткий прогон
#   tools/test-cluster.sh --veth          # две «машины»: два netns с veth
#   tools/test-cluster.sh --keep          # не удалять каталог прогона
#   tools/test-cluster.sh --only raft     # один сценарий: ask, raft, saga, grain, membership, bench
#
# Прогон: ask — Ask и Watch между процессами, kill -9 → byNodeLoss;
# raft3/raft5 — выборы, запись, kill -9 лидера, перевыборы, перезапуск
# узла с тем же хранилищем, согласие журналов; saga — перевод между
# счетами A и B с убийством координатора C посреди шага и перезапуском;
# grain — виртуальные акторы на трёх процессах, kill -9 владельца ключа.
# membership — служба состава из пяти процессов S0..S4: три избирателя,
# вступление S3 и S4, выход S4; участники G0..G2 (четвёртый G3 вступает
# позже) берут состав из службы и пишут через RaftJournal; kill -9
# владельца ключа и лидера службы, аренда истекшего участника.
# bench — запись в секунду по TCP на хранилище в памяти.
#
# Все процессы живут в своём сетевом пространстве (unshare -rmn),
# поэтому порты не сталкиваются с чужими программами. В режиме --veth
# создаются два вложенных сетевых пространства, соединённые парой veth
# (10.10.0.1 и 10.10.0.2): так проверяется запуск на двух машинах.
#
# Перед прогоном нужна собранная установка: tools/build64.sh --no-help.
# Сам сценарий компилирует Actor_devCluster с зависимостями в установку.
set -u
. "$(dirname "$0")/env.sh"

RUNS=20
QUICK=0
VETH=0
KEEP=0
ONLY=all
OPS=60
KILLAT=5
BENCH_OPS=400
TICK=20

while [ $# -gt 0 ]; do
	case "$1" in
		--runs) RUNS="${2:-20}"; shift 2;;
		--quick) QUICK=1; RUNS=1; OPS=20; KILLAT=3; BENCH_OPS=100; shift;;
		--veth) VETH=1; RUNS=1; shift;;
		--keep) KEEP=1; shift;;
		--only) ONLY="${2:-all}"; shift 2;;
		--ops) OPS="${2:-30}"; shift 2;;
		--kill-at) KILLAT="${2:-5}"; shift 2;;
		--bench-ops) BENCH_OPS="${2:-400}"; shift 2;;
		-h|--help) sed -n '2,24p' "$0"; exit 0;;
		--inner) shift; INNER=1; break;;
		*) echo "test-cluster: неизвестный ключ $1" >&2; exit 2;;
	esac
done

# --- внутренняя часть: всё это исполняется в сетевом пространстве -----------------

if [ "${INNER:-0}" = 1 ]; then
	WORK="$1"; INSTALL="$2"; RUNS="$3"; VETH="$4"; OPS="$5"; KILLAT="$6"; BENCH_OPS="$7"; TICK="$8"; KEEP="$9"; ONLY="${10}"
	declare -A PORT_OF HOST_OF HOLDERS PIDS
	# Ниже диапазона временных портов ядра (обычно 32768..60999):
	# иначе исходящее соединение может занять номер, который мы хотим слушать.
	NEXT_PORT=20000
	FAILED=0
	MEAS=""

	say() { printf 'cluster: %s\n' "$*"; }

	nextport() {
		NEXT_PORT=$((NEXT_PORT + 1))
		if [ "$NEXT_PORT" -ge 30000 ]; then NEXT_PORT=20000; fi
	}

	# host 1 — 127.0.0.1 или 10.10.0.1; host 2 — 10.10.0.2
	hostaddr() { if [ "$VETH" = 1 ]; then if [ "$1" = 2 ]; then echo 10.10.0.2; else echo 10.10.0.1; fi; else echo 127.0.0.1; fi; }

	alloc_role() { role=$1; host=${2:-1}; nextport; PORT_OF[$role]=$NEXT_PORT; HOST_OF[$role]=$host; }
	addr_of() { echo "$(hostaddr "${HOST_OF[$1]:-1}"):${PORT_OF[$1]}"; }

	LASTPID=0
	launch_role() {
		local dir=$1 role=$2 h holder
		shift 2
		h=${HOST_OF[$role]:-1}
		if [ "$VETH" = 1 ]; then
			holder=${HOLDERS[$h]}
			( cd "$INSTALL" && exec env LD_LIBRARY_PATH="$INSTALL" \
				nsenter -F -t "$holder" -n ./blackbox-console --DIR="$dir" --ROLE="$role" "$@" \
				< "$dir/cmd" >> "$dir/log-$role.txt" 2>&1 ) &
		else
			( cd "$INSTALL" && exec env LD_LIBRARY_PATH="$INSTALL" \
				./blackbox-console --DIR="$dir" --ROLE="$role" "$@" \
				< "$dir/cmd" >> "$dir/log-$role.txt" 2>&1 ) &
		fi
		LASTPID=$!
	}

	waited=0
	waitfile() {
		local f=$1 n=${2:-1000} i
		for ((i = 0; i < n; i++)); do
			[ -f "$f" ] && return 0
			sleep 0.02
		done
		return 1
	}

	killpid() {
		local p=$1
		kill -9 "$p" 2>/dev/null
		wait "$p" 2>/dev/null
	}

	stop_role() {
		local dir=$1 role=$2 p
		p=${PIDS[$role]:-}
		if [ -n "$p" ]; then
			touch "$dir/stop"
			sleep 0.25
			killpid "$p"
		fi
	}

	stop_all() {
		local dir=$1 role
		touch "$dir/stop"
		sleep 0.25
		for role in "${!PIDS[@]}"; do killpid "${PIDS[$role]}"; done
		PIDS=()
	}

	bad() {
		local dir=$1 what=$2
		echo "FAIL $what (каталог $dir)" >&2
		local role
		for role in "${!PIDS[@]}"; do
			echo "--- $role" >&2
			tail -25 "$dir/log-$role.txt" 2>/dev/null >&2
		done
		for role in "${!PIDS[@]}"; do killpid "${PIDS[$role]}"; done
		PIDS=()
		FAILED=1
		return 1
	}

	check_no_traps() {
		local dir=$1 role
		for role in "${!PIDS[@]}"; do
			if grep -q 'TRAP REPORT' "$dir/log-$role.txt" 2>/dev/null; then
				echo "трап в $role" >&2
				return 1
			fi
		done
		return 0
	}

	field() { grep -o "$2=[0-9-]*" "$1" 2>/dev/null | head -1 | cut -d= -f2; }

	find_leader() {
		# Пост: номер узла с наибольшим сроком и ролью leader; пусто — нет.
		local dir=$1 n=$2 i t mt role best
		mt=-1
		for ((i = 0; i < n; i++)); do
			[ -f "$dir/status-R$i" ] || continue
			t=$(field "$dir/status-R$i" term)
			[ "${t:--1}" -gt "$mt" ] && mt=$t
		done
		best=""
		for ((i = 0; i < n; i++)); do
			[ -f "$dir/status-R$i" ] || continue
			t=$(field "$dir/status-R$i" term); role=$(field "$dir/status-R$i" role)
			if [ "$t" = "$mt" ] && [ "$role" = 2 ]; then best=$i; break; fi
		done
		echo "$best"
	}

	max_term() {
		local dir=$1 n=$2 i t mt
		mt=-1
		for ((i = 0; i < n; i++)); do
			[ -f "$dir/status-R$i" ] || continue
			t=$(field "$dir/status-R$i" term)
			[ "${t:--1}" -gt "$mt" ] && mt=$t
		done
		echo "$mt"
	}

	# --- ask -----------------------------------------------------------------------

	run_ask() {
		local dir=$1 pE pQ ok
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		alloc_role E 1; alloc_role Q 2
		printf 'E %s\nQ %s\n' "$(addr_of E)" "$(addr_of Q)" > "$dir/nodes"
		PIDS=()
		launch_role "$dir" Q --SCENARIO=ask --BURST=300; pQ=$LASTPID; PIDS[Q]=$pQ
		launch_role "$dir" E --SCENARIO=ask --BURST=300; pE=$LASTPID; PIDS[E]=$pE
		waitfile "$dir/Q.ready" 1000 || { bad "$dir" "ask: Q не готов"; return 1; }
		waitfile "$dir/E.cap" 1000 || { bad "$dir" "ask: E не готов"; return 1; }
		waitfile "$dir/E.burstSent" 2000 || { bad "$dir" "ask: E не послал пачку"; return 1; }
		waitfile "$dir/Q.burst" 3000 || { bad "$dir" "ask: Q не принял пачку"; return 1; }
		waitfile "$dir/Q.watching" 2000 || { bad "$dir" "ask: Q не наблюдает"; return 1; }
		[ "$(cat "$dir/Q.ask" 2>/dev/null)" = "ok 42" ] || { bad "$dir" "ask: нет ответа"; return 1; }
		kill -9 "$pE"; wait "$pE" 2>/dev/null
		waitfile "$dir/Q.result" 1500 || { bad "$dir" "ask: нет Terminated"; return 1; }
		[ "$(cat "$dir/Q.result" 2>/dev/null)" = "2" ] || { bad "$dir" "ask: причина не byNodeLoss"; return 1; }
		# Узел E поднимается снова: его первый кадр должен пройти окно повторов.
		rm -f "$dir/E.ready"
		launch_role "$dir" E --SCENARIO=ask --BURST=300; PIDS[E]=$LASTPID
		waitfile "$dir/E.afterSent" 2000 || { bad "$dir" "ask: E не поднялся"; return 1; }
		waitfile "$dir/Q.after" 3000 || { bad "$dir" "ask: кадр нового воплощения не дошёл"; return 1; }
		check_no_traps "$dir" || { bad "$dir" "ask: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- raft ----------------------------------------------------------------------

	start_raft_node() {
		local dir=$1 i=$2
		rm -f "$dir/R$i.ready" "$dir/status-R$i"
		launch_role "$dir" "R$i" --SCENARIO=raft --IDX="$i" --N="$N" --TICK="$TICK" --MEM="$MEM"
		PIDS[R$i]=$LASTPID
	}

	run_raft() {
		local dir=$1 n=$2 ops=$3 mem=$4 restart=$5 killat=$6
		local i p c leader waited_leader conv max last lastTerm commit applied lt lc la term
		local oks ms wps recovery changes
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		for ((i = 0; i < n; i++)); do alloc_role "R$i" "$((i % 2 + 1))"; done
		alloc_role C 1
		: > "$dir/nodes"
		for ((i = 0; i < n; i++)); do echo "R$i $(addr_of "R$i")" >> "$dir/nodes"; done
		echo "C $(addr_of C)" >> "$dir/nodes"
		N=$n MEM=$mem
		for ((i = 0; i < n; i++)); do start_raft_node "$dir" "$i"; done
		if [ "$killat" -gt 0 ]; then echo "$killat" > "$dir/C.pauseAfter"; fi
		launch_role "$dir" C --SCENARIO=raft --N="$n" --TICK="$TICK" --OPS="$ops" --MEM="$mem"
		PIDS[C]=$LASTPID
		for ((i = 0; i < n; i++)); do
			waitfile "$dir/R$i.ready" 1500 || { bad "$dir" "raft$n: R$i не готов"; return 1; }
		done
		waitfile "$dir/C.ready" 1500 || { bad "$dir" "raft$n: клиент не готов"; return 1; }

		if [ "$killat" -gt 0 ]; then
			# клиент останавливается после killat подтверждённых команд;
			# лидер — тот, кто ответил последним (C.leader)
			waitfile "$dir/C.paused" 3000 || { bad "$dir" "raft$n: клиент не дошёл до паузы"; return 1; }
			leader=$(cat "$dir/C.leader" 2>/dev/null)
			case "$leader" in ''|*[!0-9]*) leader=$(find_leader "$dir" "$n");; esac
			[ -n "$leader" ] && [ "$leader" -ge 0 ] && [ "$leader" -lt "$n" ] \
				|| { bad "$dir" "raft$n: лидер не назван"; return 1; }
			oldTerm=$(max_term "$dir" "$n")
			killAt=$(date +%s%3N)
			kill -9 "${PIDS[R$leader]}"; wait "${PIDS[R$leader]}" 2>/dev/null
			unset "PIDS[R$leader]"
			touch "$dir/C.resume"
			# время выборов: от kill -9 до первого статуса лидера со сроком больше
			election=""
			for ((waited = 0; waited < 2500; waited++)); do
				for ((i = 0; i < n; i++)); do
					[ -f "$dir/status-R$i" ] || continue
					t=$(field "$dir/status-R$i" term); role=$(field "$dir/status-R$i" role)
					if [ "$role" = 2 ] && [ "${t:-0}" -gt "$oldTerm" ]; then
						election=$(( $(date +%s%3N) - killAt )); break 2
					fi
				done
				sleep 0.02
			done
			[ -n "$election" ] || { bad "$dir" "raft$n: новый лидер не выбран"; return 1; }
			MEAS="$MEAS raft$n-electionMs=$election"
			if [ "$restart" = 1 ]; then
				start_raft_node "$dir" "$leader"
				waitfile "$dir/R$leader.ready" 1500 || { bad "$dir" "raft$n: R$leader не поднялся"; return 1; }
			fi
		fi

		waitfile "$dir/C.result" 5000 || { bad "$dir" "raft$n: клиент не закончил"; return 1; }
		oks=$(field "$dir/C.result" oks); ms=$(field "$dir/C.result" ms)
		recovery=$(field "$dir/C.result" recoveryMs); changes=$(field "$dir/C.result" changes)
		[ "${oks:-0}" = "$ops" ] || { bad "$dir" "raft$n: подтверждено $oks из $ops"; return 1; }
		[ "$(field "$dir/C.result" bad)" = 0 ] || { bad "$dir" "raft$n: ответ ok без применения команды"; return 1; }
		if [ "$killat" -gt 0 ]; then
			[ "${changes:-0}" -ge 1 ] || { bad "$dir" "raft$n: смены лидера не было"; return 1; }
		fi
		if [ "$ms" -gt 0 ]; then wps=$((oks * 1000 / ms)); else wps=0; fi
		MEAS="$MEAS raft$n oks=$oks ms=$ms wps=$wps changes=${changes:-0} recoveryMs=${recovery:-0}"

		if [ "$mem" = 0 ]; then
			# согласие журналов: все узлы догнали один last, commit = applied = last
			conv=0
			for ((waited = 0; waited < 2500; waited++)); do
				max=0; conv=1; lastTerm=-1
				for ((i = 0; i < n; i++)); do
					[ -f "$dir/status-R$i" ] || { conv=0; continue; }
					last=$(field "$dir/status-R$i" last)
					[ "${last:-0}" -gt "$max" ] && max=$last
				done
				for ((i = 0; i < n; i++)); do
					last=$(field "$dir/status-R$i" last); commit=$(field "$dir/status-R$i" commit)
					applied=$(field "$dir/status-R$i" applied); lt=$(field "$dir/status-R$i" lastTerm)
					[ "$last" = "$max" ] && [ "$commit" = "$max" ] && [ "$applied" = "$max" ] || conv=0
					if [ "$lastTerm" -lt 0 ]; then lastTerm=$lt; elif [ "$lt" != "$lastTerm" ]; then conv=0; fi
				done
				[ "$conv" = 1 ] && break
				sleep 0.02
			done
			[ "$conv" = 1 ] || { bad "$dir" "raft$n: журналы не сошлись (max=$max)"; return 1; }
			MEAS="$MEAS raft$n-last=$max"
		fi

		check_no_traps "$dir" || { bad "$dir" "raft$n: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- saga ----------------------------------------------------------------------

	run_saga() {
		local dir=$1 pC owner
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		alloc_role A 1; alloc_role B 2; alloc_role C 1
		printf 'A %s\nB %s\nC %s\n' "$(addr_of A)" "$(addr_of B)" "$(addr_of C)" > "$dir/nodes"
		PIDS=()
		launch_role "$dir" A --SCENARIO=saga; PIDS[A]=$LASTPID
		launch_role "$dir" B --SCENARIO=saga; PIDS[B]=$LASTPID
		launch_role "$dir" C --SCENARIO=saga; pC=$LASTPID; PIDS[C]=$pC
		waitfile "$dir/C.ready" 1500 || { bad "$dir" "saga: C не готов"; return 1; }
		waitfile "$dir/C.withdrawn" 3000 || { bad "$dir" "saga: нет шага withdrawn"; return 1; }
		[ -s "$dir/C.jrn" ] || { bad "$dir" "saga: шаг не записан в журнал C"; return 1; }
		kill -9 "$pC"; wait "$pC" 2>/dev/null
		touch "$dir/C.resume"
		rm -f "$dir/C.ready" "$dir/C.states"
		launch_role "$dir" C --SCENARIO=saga; PIDS[C]=$LASTPID
		waitfile "$dir/C.result" 5000 || { bad "$dir" "saga: нет итога"; return 1; }
		[ "$(cat "$dir/C.result" 2>/dev/null)" = "A=700 B=1300 sum=2000 dup=700" ] \
			|| { bad "$dir" "saga: итог $(cat "$dir/C.result" 2>/dev/null)"; return 1; }
		# после перечитывания журнала первое состояние — withdrawn (1);
		# -1 или 0 значили бы, что журнал не прочитан и сага начата заново
		[ "$(cut -c1 "$dir/C.states" 2>/dev/null)" = 1 ] \
			|| { bad "$dir" "saga: состояние после перезапуска $(cat "$dir/C.states" 2>/dev/null)"; return 1; }
		check_no_traps "$dir" || { bad "$dir" "saga: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- grain ---------------------------------------------------------------------

	run_grain() {
		local dir=$1 i owner new bal key
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		for ((i = 0; i < 3; i++)); do alloc_role "G$i" "$((i % 2 + 1))"; done
		for ((i = 0; i < 3; i++)); do alloc_role "R$i" "$((i % 2 + 1))"; done
		: > "$dir/nodes"
		for ((i = 0; i < 3; i++)); do echo "G$i $(addr_of "G$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 3; i++)); do echo "R$i $(addr_of "R$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 3; i++)); do
			launch_role "$dir" "G$i" --SCENARIO=grain --IDX="$i" --N=3 --TICK="$TICK"
			PIDS[G$i]=$LASTPID
		done
		for ((i = 0; i < 3; i++)); do
			waitfile "$dir/G$i.ready" 4000 || { bad "$dir" "grain: G$i не готов"; return 1; }
		done
		waitfile "$dir/G0.deposited" 6000 || { bad "$dir" "grain: нет вкладов"; return 1; }
		owner=$(sed -n 's/.*old=\(G[0-9]\).*/\1/p' "$dir/G0.deposited")
		key=$(sed -n 's/.*key=\([0-9]*\).*/\1/p' "$dir/G0.deposited")
		[ -n "$owner" ] || { bad "$dir" "grain: владелец не назван"; return 1; }
		killpid "${PIDS[$owner]}"; unset "PIDS[$owner]"
		waitfile "$dir/G0.result" 8000 || { bad "$dir" "grain: нет итога"; return 1; }
		bal=$(sed -n 's/.*balance=\([0-9-]*\).*/\1/p' "$dir/G0.result")
		new=$(sed -n 's/.*new=\(G[0-9]\).*/\1/p' "$dir/G0.result")
		[ "$bal" = "175" ] || { bad "$dir" "grain: остаток $bal"; return 1; }
		[ -n "$new" ] && [ "$new" != "$owner" ] || { bad "$dir" "grain: ключ не переехал ($owner → $new)"; return 1; }
		check_no_traps "$dir" || { bad "$dir" "grain: трап"; return 1; }
		stop_all "$dir"
		return 0
	}


	# --- membership ---------------------------------------------------------------

	run_membership() {
		local dir=$1 i role r p g3 killed killedsvc start t
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		for ((i = 0; i < 5; i++)); do alloc_role "S$i" "$((i % 2 + 1))"; done
		for ((i = 0; i < 3; i++)); do alloc_role "R$i" "$((i % 2 + 1))"; done
		for ((i = 0; i < 4; i++)); do alloc_role "G$i" "$((i % 2 + 1))"; done
		: > "$dir/nodes"
		for ((i = 0; i < 5; i++)); do echo "S$i $(addr_of "S$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 3; i++)); do echo "R$i $(addr_of "R$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 4; i++)); do echo "G$i $(addr_of "G$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 5; i++)); do
			launch_role "$dir" "S$i" --SCENARIO=membership --IDX="$i" --TICK="$TICK"
			PIDS[S$i]=$LASTPID
		done
		for ((i = 0; i < 3; i++)); do
			launch_role "$dir" "G$i" --SCENARIO=membership --IDX="$i" --TICK="$TICK"
			PIDS[G$i]=$LASTPID
		done
		for ((i = 0; i < 5; i++)); do
			waitfile "$dir/S$i.ready" 8000 || { bad "$dir" "membership: S$i не готов"; return 1; }
		done
		for ((i = 0; i < 3; i++)); do
			waitfile "$dir/G$i.ready" 12000 || { bad "$dir" "membership: G$i не готов"; return 1; }
		done
		g3=0; killed=0; killedsvc=0; start=$(date +%s%3N)
		while [ ! -f "$dir/G0.result" ]; do
			if [ "$g3" = 0 ] && [ -f "$dir/startG3" ]; then
				launch_role "$dir" G3 --SCENARIO=membership --IDX=3 --TICK="$TICK"
				PIDS[G3]=$LASTPID; g3=1
			fi
			if [ "$killed" = 0 ] && [ -f "$dir/kill-owner" ]; then
				role=$(cat "$dir/kill-owner" 2>/dev/null)
				if [ -n "$role" ] && [ -n "${PIDS[$role]:-}" ]; then
					killpid "${PIDS[$role]}"; unset "PIDS[$role]"
				fi
				touch "$dir/killed"; killed=1
			fi
			if [ "$killedsvc" = 0 ] && [ -f "$dir/kill-svc" ]; then
				role=$(cat "$dir/kill-svc" 2>/dev/null)
				if [ -n "$role" ] && [ -n "${PIDS[$role]:-}" ]; then
					killpid "${PIDS[$role]}"; unset "PIDS[$role]"
				fi
				touch "$dir/killed-svc"; killedsvc=1
			fi
			if [ $(( $(date +%s%3N) - start )) -gt 120000 ]; then
				bad "$dir" "membership: нет итога за 120 с"; return 1
			fi
			sleep 0.05
		done
		local res; res=$(cat "$dir/G0.result")
		MEAS="$MEAS membership-ms=$(( $(date +%s%3N) - start ))"
		echo "membership: $res ($(( $(date +%s%3N) - start )) мс)"
		case "$res" in
			*"oks=5"*"bad=0"*"balance=185"*"good=1"*) ;;
			*) bad "$dir" "membership: итог $res"; return 1;;
		esac
		check_no_traps "$dir" || { bad "$dir" "membership: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- измерение -----------------------------------------------------------------

	run_bench() {
		local dir=$1 oks ms wps i
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		alloc_role R0 1; alloc_role R1 1; alloc_role R2 1; alloc_role C 1
		printf 'R0 %s\nR1 %s\nR2 %s\nC %s\n' "$(addr_of R0)" "$(addr_of R1)" "$(addr_of R2)" "$(addr_of C)" > "$dir/nodes"
		N=3 MEM=1
		for ((i = 0; i < 3; i++)); do start_raft_node "$dir" "$i"; done
		launch_role "$dir" C --SCENARIO=raft --N=3 --TICK="$TICK" --OPS="$BENCH_OPS" --MEM=1
		PIDS[C]=$LASTPID
		for ((i = 0; i < 3; i++)); do waitfile "$dir/R$i.ready" 1500 || { bad "$dir" "bench: R$i"; return 1; }; done
		waitfile "$dir/C.ready" 1500 || { bad "$dir" "bench: клиент"; return 1; }
		waitfile "$dir/C.result" 8000 || { bad "$dir" "bench: нет итога"; return 1; }
		oks=$(field "$dir/C.result" oks); ms=$(field "$dir/C.result" ms)
		[ "${oks:-0}" = "$BENCH_OPS" ] || { bad "$dir" "bench: $oks из $BENCH_OPS"; return 1; }
		[ "$(field "$dir/C.result" bad)" = 0 ] || { bad "$dir" "bench: ответ ok без применения команды"; return 1; }
		if [ "$ms" -gt 0 ]; then wps=$((oks * 1000 / ms)); else wps=0; fi
		MEAS="$MEAS bench oks=$oks ms=$ms wps=$wps"
		say "bench: $oks команд за $ms мс = $wps команд/с"
		check_no_traps "$dir" || { bad "$dir" "bench: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- окружение ----------------------------------------------------------------

	setup_veth() {
		unshare -n sleep 900 & HOLDERS[1]=$!
		unshare -n sleep 900 & HOLDERS[2]=$!
		sleep 0.3
		ip link add vethA type veth peer name vethB || return 1
		ip link set vethA netns "${HOLDERS[1]}" || return 1
		ip link set vethB netns "${HOLDERS[2]}" || return 1
		nsenter -t "${HOLDERS[1]}" -n ip link set lo up
		nsenter -t "${HOLDERS[1]}" -n ip addr add 10.10.0.1/24 dev vethA
		nsenter -t "${HOLDERS[1]}" -n ip link set vethA up
		nsenter -t "${HOLDERS[2]}" -n ip link set lo up
		nsenter -t "${HOLDERS[2]}" -n ip addr add 10.10.0.2/24 dev vethB
		nsenter -t "${HOLDERS[2]}" -n ip link set vethB up
		return 0
	}

	finish_veth() {
		local h
		for h in "${!HOLDERS[@]}"; do
			kill -9 "${HOLDERS[$h]}" 2>/dev/null
			wait "${HOLDERS[$h]}" 2>/dev/null
		done
	}

	# --- прогон ------------------------------------------------------------------

	ip link set lo up || exit 2
	if [ "$VETH" = 1 ]; then
		setup_veth || { echo "cluster: не удалось создать netns с veth" >&2; exit 2; }
	fi

	run=0
	while [ "$run" -lt "$RUNS" ] && [ "$FAILED" = 0 ]; do
		run=$((run + 1))
		d="$WORK/run-$run"
		mkdir -p "$d"
		say "прогон $run из $RUNS"
		want() { [ "$ONLY" = all ] || [ "$ONLY" = "$1" ]; }
		if want ask; then run_ask "$d/ask" || break; fi
		if want raft; then
			run_raft "$d/raft3" 3 "$OPS" 0 1 "$KILLAT" || break
			run_raft "$d/raft5" 5 "$OPS" 0 1 "$KILLAT" || break
		fi
		if want saga; then run_saga "$d/saga" || break; fi
		if want grain; then run_grain "$d/grain" || break; fi
		if want membership; then run_membership "$d/membership" || break; fi
		if want bench; then run_bench "$d/bench" || break; fi
		if [ "$KEEP" = 0 ]; then rm -rf "$d"; fi
		say "прогон $run: ок"
	done

	if [ "$VETH" = 1 ]; then finish_veth; fi

	if [ "$FAILED" = 0 ]; then
		say "все $RUNS прогонов прошли"
		printf '%s\n' "$MEAS" | sed 's/^ //' > "$WORK/measurements.txt"
		exit 0
	fi
	exit 1
fi

# --- внешняя часть: проверки, компиляция, запуск в сетевом пространстве ------------

skip() {
	echo "SKIP test-cluster: $*"
	exit 0
}

[ -x "$INTERP" ] || { echo "test-cluster: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }
probe=$(unshare -rmn true 2>&1) || skip "unshare -rmn недоступен: $probe"
if [ "$VETH" = 1 ]; then
	command -v ip >/dev/null 2>&1 || skip "нет команды ip"
	command -v nsenter >/dev/null 2>&1 || skip "нет команды nsenter"
	probe=$(unshare -rmn sh -c 'ip link set lo up; ip link add vethA type veth peer name vethB' 2>&1) \
		|| skip "veth в пространстве не создаётся: $probe"
fi

if [ "$KEEP" = 1 ]; then
	work="$BB/.work/cluster-keep"
	rm -rf "$work"
	mkdir -p "$work" || exit 1
else
	work=$(mktemp -d "${TMPDIR:-/tmp}/bbcluster.XXXXXX") || exit 1
fi
clean() { [ "$KEEP" = 1 ] && echo "cluster: каталог прогона $work" >&2 || rm -rf "$work"; }
trap clean EXIT INT TERM

mods="ActorCore ActorSup ActorRun ActorJournal ActorCodec ActorPersist CasHash CasStore ActorNet ActorNetTcp AsyncIO AsyncSelect AsyncChan LinNative LinThreads RaftCore RaftSm RaftStorage RaftKvStorage KvPages KvStore KvFault KvLin KvTree KvDb RaftNode RaftJournal ActorGrain ActorFileJournal RaftMembership RaftGrainMembership Actor_devCluster"
pass=0
while [ "$pass" -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"$work/compile.log" 2>&1; then break; fi
	if [ "$pass" -eq 3 ]; then
		echo "test-cluster: компиляция не удалась (проход $pass):" >&2
		tail -40 "$work/compile.log" >&2; exit 1
	fi
done

echo "cluster: прогонов $RUNS, mark=quick:$QUICK veth:$VETH"
unshare -rmn bash "$0" --inner "$work" "$INSTALL" "$RUNS" "$VETH" "$OPS" "$KILLAT" "$BENCH_OPS" "$TICK" "$KEEP" "$ONLY"
rc=$?
if [ -f "$work/measurements.txt" ]; then
	echo "cluster: замеры:"
	sed 's/^/  /' "$work/measurements.txt"
	cp "$work/measurements.txt" "$BB/.work/cluster-measurements.txt" 2>/dev/null || true
fi
exit $rc
