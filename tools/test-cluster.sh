#!/bin/bash
# Кластер: узлы ActorNet и Raft в отдельных процессах blackbox-console.
#
#   tools/test-cluster.sh                 # 20 прогонов всего сценария
#   tools/test-cluster.sh --runs 3        # три прогона
#   tools/test-cluster.sh --quick         # один короткий прогон
#   tools/test-cluster.sh --veth          # две «машины»: два netns с veth
#   tools/test-cluster.sh --keep          # не удалять каталог прогона
#   tools/test-cluster.sh --tls           # узлы на TLS 1.3 со взаимной проверкой
#   tools/test-cluster.sh --only raft     # один сценарий: ask, reconnect, secure, raft, saga, grain, membership, lease, obxcluster, bench
#   tools/test-cluster.sh --obx-ops 16    # переводов в сценарии obxcluster
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
# obxcluster — пример ObxCluster в пяти процессах: восемь счетов на
# виртуальных акторах ActorGrain поверх RaftJournal, переводы сагой,
# служба состава из пяти узлов; узел P3 на время останавливается (фейк
# разрыва сети), два узла-лидера убиваются, затем поднимаются снова;
# проверяются сохранение суммы, отсутствие повторных применений и учёт
# отклонённых переводов.
# bench — запись в секунду по TCP на хранилище в памяти.
# reconnect — перезапуск узла и повторный Ask через заново открытое
# соединение. secure (только с --tls) — отрицательные случаи: чужой
# корень, подделанное «от кого», узел, удалённый из состава; в ask
# при --tls трафик идёт через прокси и в захвате ищется известная строка.
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
TLS=0
ONLY=all
OPS=60
KILLAT=5
BENCH_OPS=400
OBX_OPS="${OBX_OPS:-24}"
OBX_SUM=8000	# ObxCluster: accounts (8) * initial (1000)
TICK=20

while [ $# -gt 0 ]; do
	case "$1" in
		--runs) RUNS="${2:-20}"; shift 2;;
		--quick) QUICK=1; RUNS=1; OPS=20; KILLAT=3; BENCH_OPS=100; shift;;
		--veth) VETH=1; RUNS=1; shift;;
		--tls) TLS=1; shift;;
		--keep) KEEP=1; shift;;
		--only) ONLY="${2:-all}"; shift 2;;
		--ops) OPS="${2:-30}"; shift 2;;
		--kill-at) KILLAT="${2:-5}"; shift 2;;
		--bench-ops) BENCH_OPS="${2:-400}"; shift 2;;
		--obx-ops) OBX_OPS="${2:-24}"; shift 2;;
		-h|--help) sed -n '2,36p' "$0"; exit 0;;
		--inner) shift; INNER=1; break;;
		*) echo "test-cluster: неизвестный ключ $1" >&2; exit 2;;
	esac
done

# --- внутренняя часть: всё это исполняется в сетевом пространстве -----------------

if [ "${INNER:-0}" = 1 ]; then
	WORK="$1"; INSTALL="$2"; RUNS="$3"; VETH="$4"; OPS="$5"; KILLAT="$6"; BENCH_OPS="$7"; TICK="$8"; KEEP="$9"; ONLY="${10}"; TLS="${11:-0}"; CERTDIR="${12:-}"
	OBX_OPS="${OBX_OPS:-24}"
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

	TLSARGS=""
	if [ "$TLS" = 1 ]; then
		TLSARGS="--TLS=1 --CERTDIR=$CERTDIR"
	fi

	seed_dir() {
		local dir=$1
		[ "$TLS" = 1 ] || return 0
		[ -f "$dir/roster" ] || cp "$CERTDIR/roster" "$dir/roster"
	}

	LASTPID=0
	launch_role() {
		local dir=$1 role=$2 h holder
		shift 2
		seed_dir "$dir"
		h=${HOST_OF[$role]:-1}
		if [ "$VETH" = 1 ]; then
			holder=${HOLDERS[$h]}
			( cd "$INSTALL" && exec env LD_LIBRARY_PATH="$INSTALL" \
				nsenter -F -t "$holder" -n ./blackbox-console --DIR="$dir" --ROLE="$role" $TLSARGS "$@" \
				< "$dir/cmd" >> "$dir/log-$role.txt" 2>&1 ) &
		else
			( cd "$INSTALL" && exec env LD_LIBRARY_PATH="$INSTALL" \
				./blackbox-console --DIR="$dir" --ROLE="$role" $TLSARGS "$@" \
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

	check_conflict_traps() {
		# Обрыв сети (SIGSTOP) может оставить две копии ключа: журнал
		# отказывает второй записи с conflict, актор по устройству
		# останавливается, и следующее письмо поднимает его снова. Такие
		# трапы (70, ActorPersist.Actor.Complete) примеримы; любые другие —
		# провал.
		local dir=$1 role code
		for role in "${!PIDS[@]}"; do
			grep -q 'TRAP REPORT' "$dir/log-$role.txt" 2>/dev/null || continue
			while read -r code; do
				[ "$code" = 70 ] || { echo "трап $code в $role" >&2; return 1; }
			done < <(awk '/TRAP REPORT - BEGIN/{getline; getline; print $1}' "$dir/log-$role.txt" 2>/dev/null)
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
		local dir=$1 pE pQ ok proxyargs
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		alloc_role E 1; alloc_role Q 2
		printf 'E %s\nQ %s\n' "$(addr_of E)" "$(addr_of Q)" > "$dir/nodes"
		proxyargs=""
		PIDS=()
		launch_role "$dir" E --SCENARIO=ask --BURST=300; pE=$LASTPID; PIDS[E]=$pE
		waitfile "$dir/E.cap" 2000 || { bad "$dir" "ask: E не готов"; return 1; }
		if [ "$TLS" = 1 ] && [ "${CLUSTER_NO_CAPTURE:-0}" != 1 ]; then
			# Прослушка: прозрачный прокси перед E. TLS кончается на узлах,
			# поэтому в захвате должен быть только шифртекст.
			alloc_role P 3
			python3 "$BB/tools/tls-proxy.py" "$(addr_of P)" "$(addr_of E)" "$dir/capture.bin" &
			PIDS[P]=$!
			sleep 0.2
			proxyargs="--PROXY=E,$(addr_of P)"
		fi
		launch_role "$dir" Q --SCENARIO=ask --BURST=300 $proxyargs; pQ=$LASTPID; PIDS[Q]=$pQ
		waitfile "$dir/Q.ready" 1000 || { bad "$dir" "ask: Q не готов"; return 1; }
		waitfile "$dir/E.ready" 2000 || { bad "$dir" "ask: E не готов"; return 1; }
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
		MEAS="$MEAS ask-rtt20ms=$(cat "$dir/Q.rtt20" 2>/dev/null)"
		if [ "$TLS" = 1 ] && [ "${CLUSTER_NO_CAPTURE:-0}" != 1 ]; then
			[ -s "$dir/capture.bin" ] || { bad "$dir" "ask: прокси не записал трафик"; return 1; }
			if grep -aq 'BB-CLUSTER-PLAINTEXT-MARKER-42' "$dir/capture.bin"; then
				bad "$dir" "ask: в захвате открытый текст письма"; return 1
			fi
		fi
		check_no_traps "$dir" || { bad "$dir" "ask: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- переподключение после перезапуска -----------------------------------------

	run_reconnect() {
		local dir=$1 pS
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		alloc_role S 1; alloc_role C 2
		printf 'S %s\nC %s\n' "$(addr_of S)" "$(addr_of C)" > "$dir/nodes"
		launch_role "$dir" S --SCENARIO=reconnect; pS=$LASTPID; PIDS[S]=$pS
		launch_role "$dir" C --SCENARIO=reconnect; PIDS[C]=$LASTPID
		waitfile "$dir/C.first" 5000 || { bad "$dir" "reconnect: первый Ask не прошёл"; return 1; }
		[ "$(cat "$dir/C.first" 2>/dev/null)" = "ok 42" ] || { bad "$dir" "reconnect: первый ответ $(cat "$dir/C.first" 2>/dev/null)"; return 1; }
		kill -9 "$pS"; wait "$pS" 2>/dev/null; unset "PIDS[S]"
		rm -f "$dir/S.ready" "$dir/S.cap"
		launch_role "$dir" S --SCENARIO=reconnect; PIDS[S]=$LASTPID
		waitfile "$dir/S.ready" 5000 || { bad "$dir" "reconnect: S не поднялся"; return 1; }
		touch "$dir/go2"
		waitfile "$dir/C.second" 5000 || { bad "$dir" "reconnect: повторный Ask не прошёл"; return 1; }
		[ "$(cat "$dir/C.second" 2>/dev/null)" = "ok 43" ] || { bad "$dir" "reconnect: второй ответ $(cat "$dir/C.second" 2>/dev/null)"; return 1; }
		check_no_traps "$dir" || { bad "$dir" "reconnect: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- отрицательные случаи защищённого канала ---------------------------------

	field_at_least() {
		local f=$1 k=$2 want=$3 v
		v=$(field "$f" "$k")
		[ -n "$v" ] && [ "$v" -ge "$want" ]
	}

	run_secure() {
		local dir=$1 foreign_fp
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		alloc_role E 1; alloc_role E2 1
		printf 'E %s\nE2 %s\n' "$(addr_of E)" "$(addr_of E2)" > "$dir/nodes"
		seed_dir "$dir"
		# чужой корень: сертификат foreign кладём в список, чтобы
		# решала именно проверка цепочки, а не отсутствие записи
		foreign_fp=$(cat "$CERTDIR/foreign.fp" 2>/dev/null || \
			"${OPENSSL:-openssl}" x509 -in "$CERTDIR/foreign.pem" -outform DER | \
			"${OPENSSL:-openssl}" dgst -sha256 -hex | awk '{print $NF}')
		printf 'foreign %s\n' "$foreign_fp" >> "$dir/roster"
		# Y — верный сертификат корня, но узла нет в составе.
		awk '!/^Y /' "$dir/roster" > "$dir/roster.new" && mv "$dir/roster.new" "$dir/roster"

		launch_role "$dir" E --SCENARIO=secure; PIDS[E]=$LASTPID
		waitfile "$dir/E.ready" 5000 || { bad "$dir" "secure: E не готов"; return 1; }
		# E2 — второй действующий узел с тем же префиксом имени, что E.
		launch_role "$dir" E2 --SCENARIO=secure; PIDS[E2]=$LASTPID
		waitfile "$dir/E2.ready" 5000 || { bad "$dir" "secure: E2 не готов"; return 1; }

		# 1. сертификат чужого корня не принимается
		launch_role "$dir" F --SCENARIO=secure --CLAIM=F --SEND=1 \
			--CERT="$CERTDIR/foreign.pem" --KEY="$CERTDIR/foreign.key"
		PIDS[F]=$LASTPID
		waitfile "$dir/F.status" 5000 || { bad "$dir" "secure: F не закончил"; return 1; }
		sleep 0.3
		field_at_least "$dir/E.status" accf 1 || { bad "$dir" "secure: чужой корень принят"; return 1; }
		[ "$(field "$dir/E.status" cerr)" != 0 ] || { bad "$dir" "secure: причина отказа не сертификат"; return 1; }

		# 2. верный сертификат корня, но узла нет в составе
		launch_role "$dir" Y --SCENARIO=secure --CLAIM=Y --SEND=1 \
			--CERT="$CERTDIR/Y.pem" --KEY="$CERTDIR/Y.key"
		PIDS[Y]=$LASTPID
		waitfile "$dir/Y.status" 5000 || { bad "$dir" "secure: Y не закончил"; return 1; }
		sleep 0.3
		rej0=$(field "$dir/E.status" rej); [ -n "$rej0" ] || rej0=0
		[ "$rej0" -ge 1 ] || { bad "$dir" "secure: узел вне состава принят"; return 1; }
		[ ! -f "$dir/E.pings" ] || { bad "$dir" "secure: письмо узла вне состава доставлено"; return 1; }

		# 3. верное имя, но чужой ключ: отпечаток не тот
		launch_role "$dir" K --SCENARIO=secure --CLAIM=M --SEND=1 \
			--CERT="$CERTDIR/M-second.pem" --KEY="$CERTDIR/M-second.key"
		PIDS[K]=$LASTPID
		waitfile "$dir/K.status" 5000 || { bad "$dir" "secure: K не закончил"; return 1; }
		sleep 0.3
		rej1=$(field "$dir/E.status" rej); [ -n "$rej1" ] || rej1=0
		[ "$rej1" -gt "$rej0" ] || { bad "$dir" "secure: ключ участника не проверен"; return 1; }
		[ ! -f "$dir/E.pings" ] || { bad "$dir" "secure: письмо с чужим ключом доставлено"; return 1; }

		# 4. клиент без сертификата: сервер обязан отвергнуть рукопожатие
		accf0=$(field "$dir/E.status" accf); [ -n "$accf0" ] || accf0=0
		launch_role "$dir" N --SCENARIO=secure --CLAIM=N --SEND=1 --NOCLIENT=1 \
			--CERT="$CERTDIR/Y.pem" --KEY="$CERTDIR/Y.key"
		PIDS[N]=$LASTPID
		waitfile "$dir/N.status" 5000 || { bad "$dir" "secure: N не закончил"; return 1; }
		sleep 0.3
		accf1=$(field "$dir/E.status" accf); [ -n "$accf1" ] || accf1=0
		[ "$accf1" -gt "$accf0" ] || { bad "$dir" "secure: сервер принял клиента без сертификата"; return 1; }

		# 4. верный сертификат, но подделанное «от кого»
		launch_role "$dir" X --SCENARIO=secure --CLAIM=E --SEND=2 \
			--CERT="$CERTDIR/X.pem" --KEY="$CERTDIR/X.key"
		PIDS[X]=$LASTPID
		waitfile "$dir/X.status" 5000 || { bad "$dir" "secure: X не закончил"; return 1; }
		sleep 0.3
		field_at_least "$dir/E.status" forged 1 || { bad "$dir" "secure: подделка «от кого» принята"; return 1; }
		field_at_least "$dir/X.status" hso 1 || { bad "$dir" "secure: X не прошёл рукопожатие"; return 1; }

		# Тот же первый символ: сертификат E2, заявленное имя E.
		# E2 — действующий узел, имя E — префикс E2.
		f0=$(field "$dir/E.status" forged); [ -n "$f0" ] || f0=0
		launch_role "$dir" F1 --SCENARIO=secure --CLAIM=E --SEND=1 \
			--CERT="$CERTDIR/E2.pem" --KEY="$CERTDIR/E2.key"
		PIDS[F1]=$LASTPID
		waitfile "$dir/F1.status" 5000 || { bad "$dir" "secure: F1 не закончил"; return 1; }
		sleep 0.3
		f1=$(field "$dir/E.status" forged); [ -n "$f1" ] || f1=0
		[ "$f1" -gt "$f0" ] || { bad "$dir" "secure: подделка E2→E (тот же префикс) принята"; return 1; }
		field_at_least "$dir/F1.status" hso 1 || { bad "$dir" "secure: F1 не прошёл рукопожатие"; return 1; }
		[ ! -f "$dir/E.pings" ] || { bad "$dir" "secure: письмо E2→E доставлено"; return 1; }

		# Заявленное имя длиннее сертификата: сертификат E, заявлено E2.
		f1=$(field "$dir/E.status" forged); [ -n "$f1" ] || f1=0
		launch_role "$dir" F2 --SCENARIO=secure --CLAIM=E2 --SEND=1 \
			--CERT="$CERTDIR/E.pem" --KEY="$CERTDIR/E.key"
		PIDS[F2]=$LASTPID
		waitfile "$dir/F2.status" 5000 || { bad "$dir" "secure: F2 не закончил"; return 1; }
		sleep 0.3
		f2=$(field "$dir/E.status" forged); [ -n "$f2" ] || f2=0
		[ "$f2" -gt "$f1" ] || { bad "$dir" "secure: подделка E→E2 (длиннее) принята"; return 1; }
		field_at_least "$dir/F2.status" hso 1 || { bad "$dir" "secure: F2 не прошёл рукопожатие"; return 1; }
		[ ! -f "$dir/E.pings" ] || { bad "$dir" "secure: письмо E→E2 доставлено"; return 1; }

		# Регистр: сертификат e, заявленное имя E.
		f2=$(field "$dir/E.status" forged); [ -n "$f2" ] || f2=0
		launch_role "$dir" F3 --SCENARIO=secure --CLAIM=E --SEND=1 \
			--CERT="$CERTDIR/e.pem" --KEY="$CERTDIR/e.key"
		PIDS[F3]=$LASTPID
		waitfile "$dir/F3.status" 5000 || { bad "$dir" "secure: F3 не закончил"; return 1; }
		sleep 0.3
		f3=$(field "$dir/E.status" forged); [ -n "$f3" ] || f3=0
		[ "$f3" -gt "$f2" ] || { bad "$dir" "secure: подделка e→E (регистр) принята"; return 1; }
		field_at_least "$dir/F3.status" hso 1 || { bad "$dir" "secure: F3 не прошёл рукопожатие"; return 1; }
		[ ! -f "$dir/E.pings" ] || { bad "$dir" "secure: письмо e→E доставлено"; return 1; }

		# 3. узел, удалённый из состава: первое письмо доходит, после
		#    отзыва — нет, действующее соединение рвётся
		launch_role "$dir" M --SCENARIO=secure --CLAIM=M --SEND=1 --WAIT2=1 \
			--CERT="$CERTDIR/M.pem" --KEY="$CERTDIR/M.key"
		PIDS[M]=$LASTPID
		waitfile "$dir/M.status" 5000 || { bad "$dir" "secure: M не отправил"; return 1; }
		sleep 0.2
		[ "$(cat "$dir/E.pings" 2>/dev/null)" = "1" ] || { bad "$dir" "secure: M не дошёл до отзыва ($(cat "$dir/E.pings" 2>/dev/null))"; return 1; }
		awk '!/^M /' "$dir/roster" > "$dir/roster.new" && mv "$dir/roster.new" "$dir/roster"
		sleep 0.5
		touch "$dir/go2"
		waitfile "$dir/M.status2" 8000 || { bad "$dir" "secure: M не закончил"; return 1; }
		[ "$(cat "$dir/E.pings" 2>/dev/null)" = "1" ] || { bad "$dir" "secure: отозванный M принят"; return 1; }
		field_at_least "$dir/E.status" rev 1 || { bad "$dir" "secure: соединение с M не разорвано"; return 1; }
		[ "$(field "$dir/E.status" readers)" = "0" ] || { bad "$dir" "secure: читатель отозванного M не закрыт"; return 1; }
		# отозванный кадр отвергнут читателем, а не только сменой списка
		grep -q 'revoked-frame' "$dir/tls-audit" 2>/dev/null \
			|| { bad "$dir" "secure: нет записи об отозванном кадре в журнале"; return 1; }

		# запись в журнал о подделке (файл tls-audit ведёт транспорт)
		grep -q 'forged-frame' "$dir/tls-audit" 2>/dev/null \
			|| { bad "$dir" "secure: нет записи о подделке в журнале"; return 1; }

		# простой узел не крутит главный цикл на закрытом stdin: за секунду
		# меньше 50 тиков процессорного времени (занятый цикл даёт ~100)
		u0=$(awk '{print $14 + $15}' "/proc/${PIDS[E]}/stat" 2>/dev/null)
		[ -n "$u0" ] || u0=0
		sleep 1
		u1=$(awk '{print $14 + $15}' "/proc/${PIDS[E]}/stat" 2>/dev/null)
		[ -n "$u1" ] || u1=0
		[ $((u1 - u0)) -lt 50 ] \
			|| { bad "$dir" "secure: простой узел занимает процессор ($((u1 - u0)) тиков/с)"; return 1; }
		touch "$dir/stop"
		waitfile "$dir/E.exited" 2000 || true
		sleep 0.2

		check_no_traps "$dir" || { bad "$dir" "secure: трап"; return 1; }
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
		local dir=$1 i role r p g3 killed killedsvc start t nsvc
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		# Служба состава из пяти узлов; рост до пяти проверяется и на TLS.
		nsvc=5
		for ((i = 0; i < nsvc; i++)); do alloc_role "S$i" "$((i % 2 + 1))"; done
		for ((i = 0; i < 3; i++)); do alloc_role "R$i" "$((i % 2 + 1))"; done
		for ((i = 0; i < 4; i++)); do alloc_role "G$i" "$((i % 2 + 1))"; done
		# K<i> — канал водителя участника: клиент журнала и службы, вне
		# обоих кластеров, чтобы запрос лидеру не шёл сам себе.
		for ((i = 0; i < 4; i++)); do alloc_role "K$i" "$((i % 2 + 1))"; done
		: > "$dir/nodes"
		for ((i = 0; i < nsvc; i++)); do echo "S$i $(addr_of "S$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 3; i++)); do echo "R$i $(addr_of "R$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 4; i++)); do echo "G$i $(addr_of "G$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 4; i++)); do echo "K$i $(addr_of "K$i")" >> "$dir/nodes"; done
		for ((i = 0; i < nsvc; i++)); do
			launch_role "$dir" "S$i" --SCENARIO=membership --IDX="$i" --TICK="$TICK"
			PIDS[S$i]=$LASTPID
		done
		for ((i = 0; i < 3; i++)); do
			launch_role "$dir" "G$i" --SCENARIO=membership --IDX="$i" --TICK="$TICK"
			PIDS[G$i]=$LASTPID
		done
		for ((i = 0; i < nsvc; i++)); do
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
			local limit=120000
			[ "$TLS" = 1 ] && limit=600000
			if [ $(( $(date +%s%3N) - start )) -gt "$limit" ]; then
				bad "$dir" "membership: нет итога за $limit мс"; return 1
			fi
			sleep 0.05
		done
		local res; res=$(cat "$dir/G0.result")
		MEAS="$MEAS membership-ms=$(( $(date +%s%3N) - start ))"
		echo "membership: $res ($(( $(date +%s%3N) - start )) мс)"
		if [ "$TLS" = 1 ]; then
			# TLS: выход из состава сам убирает отпечаток — связи с
			# выбывшим рвутся без файла revoked.
			case "$res" in
				*"oks=5"*"bad=0"*"balance=185"*"revoked=1"*"good=1"*) ;;
				*) bad "$dir" "membership: итог $res"; return 1;;
			esac
		else
			case "$res" in
				*"oks=5"*"bad=0"*"balance=185"*"good=1"*) ;;
				*) bad "$dir" "membership: итог $res"; return 1;;
			esac
		fi
		check_no_traps "$dir" || { bad "$dir" "membership: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- истечение аренды -----------------------------------------------------------

	run_lease() {
		local dir=$1 i res start
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		for ((i = 0; i < 5; i++)); do alloc_role "S$i" "$((i % 2 + 1))"; done
		: > "$dir/nodes"
		for ((i = 0; i < 5; i++)); do echo "S$i $(addr_of "S$i")" >> "$dir/nodes"; done
		for ((i = 0; i < 3; i++)); do
			alloc_role "G$i" "$((i % 2 + 1))"
			alloc_role "R$i" "$((i % 2 + 1))"
			alloc_role "K$i" "$((i % 2 + 1))"
			echo "G$i $(addr_of "G$i")" >> "$dir/nodes"
			echo "R$i $(addr_of "R$i")" >> "$dir/nodes"
			echo "K$i $(addr_of "K$i")" >> "$dir/nodes"
		done
		for ((i = 0; i < 5; i++)); do
			launch_role "$dir" "S$i" --SCENARIO=lease --IDX="$i" --TICK="$TICK"
			PIDS[S$i]=$LASTPID
		done
		for ((i = 0; i < 3; i++)); do
			launch_role "$dir" "G$i" --SCENARIO=lease --IDX="$i" --TICK="$TICK"
			PIDS[G$i]=$LASTPID
		done
		start=$(date +%s%3N)
		waitfile "$dir/G0.result" 150000 || { bad "$dir" "lease: нет итога"; return 1; }
		res=$(cat "$dir/G0.result" 2>/dev/null)
		MEAS="$MEAS lease-ms=$(( $(date +%s%3N) - start ))"
		say "lease: $res ($(( $(date +%s%3N) - start )) мс)"
		case "$res" in
			*"expired=1"*"good=1"*) ;;
			*) bad "$dir" "lease: итог $res"; return 1;;
		esac
		[ "$(cat "$dir/G1.probe-1" 2>/dev/null)" = "ok" ] \
			|| { bad "$dir" "lease: проба до истечения аренды не прошла"; return 1; }
		[ "$(cat "$dir/G1.probe-2" 2>/dev/null)" = "no" ] \
			|| { bad "$dir" "lease: кадр из-под истёкшей аренды доставлен"; return 1; }
		check_no_traps "$dir" || { bad "$dir" "lease: трап"; return 1; }
		stop_all "$dir"
		return 0
	}

	# --- obxcluster ----------------------------------------------------------------

	leader_of() {
		# Пост: номер процесса с лидером узлов <префикс><i>; пусто — нет.
		local dir=$1 prefix=$2 i
		for ((i = 0; i < 5; i++)); do
			if [ -f "$dir/$prefix$i.status" ] && grep -q 'role=2' "$dir/$prefix$i.status" 2>/dev/null; then
				echo "$i"; return
			fi
		done
		echo ""
	}

	wait_members() {
		# Ждать, пока каждый из пяти процессов вступит в службу состава и
		# увидит want живых участников. Хватало одного процесса — и P3
		# замораживался, ещё не получив ответа на своё вступление
		# (на TLS вступление дольше): работа не начиналась никогда.
		local dir=$1 want=$2 ms=$3 t i v a ok
		t=0
		while [ "$t" -lt "$ms" ]; do
			ok=1
			for ((i = 0; i < 5; i++)); do
				v=$(sed -n 's/.*members=\([0-9]*\).*/\1/p' "$dir/P$i.status" 2>/dev/null)
				a=$(sed -n 's/.*alive=\([0-9]*\).*/\1/p' "$dir/P$i.status" 2>/dev/null)
				if [ "${v:-0}" -lt "$want" ] || [ "${a:-0}" -lt "$want" ] \
					|| ! grep -q 'joined=1' "$dir/P$i.status" 2>/dev/null; then ok=0; break; fi
			done
			[ "$ok" = 1 ] && return 0
			sleep 0.02; t=$((t + 20))
		done
		return 1
	}

	wait_progress() {
		# Ждать, пока хоть один процесс насчитает want законченных переводов.
		local dir=$1 want=$2 ms=$3 t i v
		t=0
		while [ "$t" -lt "$ms" ]; do
			for ((i = 0; i < 5; i++)); do
				v=$(cat "$dir/P$i.progress" 2>/dev/null)
				case "$v" in
					''|*[!0-9]*) ;;
					*) [ "$v" -ge "$want" ] && return 0;;
				esac
			done
			sleep 0.02; t=$((t + 20))
		done
		return 1
	}

	run_obxcluster() {
		local dir=$1 i v1 v2 c sum dups total applied expect done failed refunded
		local retries rejected coord verified verifybad conserved losses anycoord anyloss
		local start; start=$(date +%s%3N)
		mkdir -p "$dir"
		printf 'Actor_devCluster.Run\n' > "$dir/cmd"
		PIDS=()
		for ((i = 0; i < 5; i++)); do
			alloc_role "G$i" "$((i % 2 + 1))"
			alloc_role "J$i" "$((i % 2 + 1))"
			alloc_role "M$i" "$((i % 2 + 1))"
			alloc_role "K$i" "$((i % 2 + 1))"
			HOST_OF[P$i]="$((i % 2 + 1))"	# процесс живёт там же, где его узлы
		done
		: > "$dir/nodes"
		for ((i = 0; i < 5; i++)); do
			echo "G$i $(addr_of "G$i")" >> "$dir/nodes"
			echo "J$i $(addr_of "J$i")" >> "$dir/nodes"
			echo "M$i $(addr_of "M$i")" >> "$dir/nodes"
			echo "K$i $(addr_of "K$i")" >> "$dir/nodes"
		done
		local ops="$OBX_OPS" idle=2 retry=150
		for ((i = 0; i < 5; i++)); do
			launch_role "$dir" "P$i" --SCENARIO=obxcluster --IDX="$i" --N=5 --TICK="$TICK" --OPS="$ops" --IDLE="$idle" --RETRY="$retry" --DROP=0
			PIDS[P$i]=$LASTPID
		done
		tmo=3000; [ "$TLS" = 1 ] && tmo=30000
		for ((i = 0; i < 5; i++)); do
			waitfile "$dir/P$i.ready" "$tmo" || { bad "$dir" "obxcluster: P$i не готов"; return 1; }
		done
		local tmo; tmo=60000; [ "$TLS" = 1 ] && tmo=150000
		wait_members "$dir" 5 "$tmo" || { bad "$dir" "obxcluster: не все вступили"; return 1; }
		# разрыв сети: P3 заморожен до начала работы
		kill -STOP "${PIDS[P3]}" 2>/dev/null
		touch "$dir/start"
		say "obxcluster: нагрузка пошла, жду три перевода"
		tmo=30000; [ "$TLS" = 1 ] && tmo=120000
		wait_progress "$dir" 3 "$tmo" || { kill -CONT "${PIDS[P3]}" 2>/dev/null; bad "$dir" "obxcluster: работа не началась"; return 1; }
		# жертвы — процессы лидеров журнала и службы состава, кроме P3
		v1=$(leader_of "$dir" J); v2=$(leader_of "$dir" M)
		[ -n "$v1" ] || v1=0
		[ -n "$v2" ] || v2=1
		[ "$v1" = 3 ] && v1=0
		[ "$v2" = 3 ] && v2=2
		if [ "$v2" = "$v1" ]; then
			for c in 0 1 2 4; do
				if [ "$c" != "$v1" ]; then v2=$c; break; fi
			done
		fi
		say "obxcluster: убиваю P$v1 и P$v2, P3 в разрыве"
		for c in "$v1" "$v2"; do
			killpid "${PIDS[P$c]}"; unset "PIDS[P$c]"
			rm -f "$dir/P$c.ready" "$dir/P$c.result" "$dir/P$c.progress"
		done
		sleep 2
		say "obxcluster: возвращаю P3 из разрыва"
		kill -CONT "${PIDS[P3]}" 2>/dev/null
		# перезапуск убитых узлов с тем же хранилищем
		say "obxcluster: поднимаю P$v1 и P$v2"
		for c in "$v1" "$v2"; do
			launch_role "$dir" "P$c" --SCENARIO=obxcluster --IDX="$c" --N=5 --TICK="$TICK" --OPS="$ops" --IDLE="$idle" --RETRY="$retry" --DROP=0
			PIDS[P$c]=$LASTPID
		done
		anycoord=0
		anyloss=0
		for ((i = 0; i < 5; i++)); do
			waitfile "$dir/P$i.result" 6000 || { bad "$dir" "obxcluster: нет итога P$i"; return 1; }
		done
		for ((i = 0; i < 5; i++)); do
			grep -q 'good=1' "$dir/P$i.result" || { bad "$dir" "obxcluster: P$i $(cat "$dir/P$i.result" 2>/dev/null)"; return 1; }
			sum=$(field "$dir/P$i.result" sum); dups=$(field "$dir/P$i.result" dups)
			total=$(field "$dir/P$i.result" total); applied=$(field "$dir/P$i.result" applied)
			expect=$(field "$dir/P$i.result" expect)
			done=$(field "$dir/P$i.result" done); failed=$(field "$dir/P$i.result" failed)
			refunded=$(field "$dir/P$i.result" refunded)
			retries=$(field "$dir/P$i.result" retries)
			rejected=$(field "$dir/P$i.result" rejectedRetries)
			coord=$(field "$dir/P$i.result" coordRetries)
			verified=$(field "$dir/P$i.result" verified)
			verifybad=$(field "$dir/P$i.result" verifyBad)
			conserved=$(field "$dir/P$i.result" conserved)
			losses=$(field "$dir/P$i.result" losses)
			[ "$sum" = "$OBX_SUM" ] \
				|| { bad "$dir" "obxcluster: P$i сумма $sum"; return 1; }
			[ "$dups" = 0 ] || { bad "$dir" "obxcluster: P$i повторов $dups"; return 1; }
			[ "$total" = "$OBX_OPS" ] || { bad "$dir" "obxcluster: P$i переводов $total"; return 1; }
			[ "$applied" = "$expect" ] \
				|| { bad "$dir" "obxcluster: P$i событий $applied, ожидалось $expect"; return 1; }
			[ "$((done + failed + refunded))" = "$total" ] \
				|| { bad "$dir" "obxcluster: P$i учтено $done+$failed+$refunded из $total"; return 1; }
			[ "${refunded:-0}" -ge 1 ] \
				|| { bad "$dir" "obxcluster: P$i нет возвратов — сага не проверила откат"; return 1; }
			[ "${done:-0}" -ge 1 ] \
				|| { bad "$dir" "obxcluster: P$i нет законченных переводов — нечего сверять"; return 1; }
			[ "${failed:-0}" -ge 1 ] \
				|| { bad "$dir" "obxcluster: P$i нет отказов списания — повтор отклонённого шага не проверен"; return 1; }
			[ "${retries:-0}" -ge 1 ] \
				|| { bad "$dir" "obxcluster: P$i нет повторов шага (retries=$retries)"; return 1; }
			[ "${rejected:-0}" -ge 1 ] \
				|| { bad "$dir" "obxcluster: P$i нет повторов отклонённого шага"; return 1; }
			anycoord=$((anycoord + ${coord:-0}))
			anyloss=$((anyloss + ${losses:-0}))
			[ "${verified:-0}" = "$total" ] \
				|| { bad "$dir" "obxcluster: P$i проверено $verified из $total"; return 1; }
			[ "${verifybad:-1}" = 0 ] \
				|| { bad "$dir" "obxcluster: P$i повтор после пассивации дал другой итог ($verifybad)"; return 1; }
			[ "${conserved:-0}" = 1 ] \
				|| { bad "$dir" "obxcluster: P$i остатки не сошлись с итогами переводов"; return 1; }
			MEAS="$MEAS obxcluster-P$i done=$done failed=$failed refunded=$refunded retries=$retries losses=$losses coord=$coord"
		done
		[ "$anycoord" -ge 1 ] \
			|| { bad "$dir" "obxcluster: координатор ни разу не повторил шаг по тайм-ауту"; return 1; }
		[ "$anyloss" -ge 1 ] \
			|| { bad "$dir" "obxcluster: ни один первый ответ шага не потерян — повтор не проверен"; return 1; }
		check_conflict_traps "$dir" || { bad "$dir" "obxcluster: трап"; return 1; }
		MEAS="$MEAS obxcluster-ms=$(( $(date +%s%3N) - start ))"
		say "obxcluster: $(( $(date +%s%3N) - start )) мс"
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
		if want reconnect; then run_reconnect "$d/reconnect" || break; fi
		if want secure; then
			[ "$TLS" = 1 ] || { say "secure: пропуск без --tls"; }
			[ "$TLS" = 1 ] && { run_secure "$d/secure" || break; }
		fi
		if want raft; then
			run_raft "$d/raft3" 3 "$OPS" 0 1 "$KILLAT" || break
			run_raft "$d/raft5" 5 "$OPS" 0 1 "$KILLAT" || break
		fi
		if want saga; then run_saga "$d/saga" || break; fi
		if want grain; then run_grain "$d/grain" || break; fi
		if want membership; then run_membership "$d/membership" || break; fi
		if want lease; then
			[ "$TLS" = 1 ] || { say "lease: пропуск без --tls"; }
			[ "$TLS" = 1 ] && { run_lease "$d/lease" || break; }
		fi
		if want obxcluster; then
			run_obxcluster "$d/obxcluster" || break
		fi
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

mods="ActorCore ActorSup ActorRun ActorJournal ActorCodec ActorPersist CasHash CasStore ActorNet ActorNetTcp ActorNetTls CryptoAosCommStreams CryptoTLS AsyncIO AsyncSelect AsyncChan LinNative LinThreads RaftCore RaftSm RaftStorage RaftKvStorage KvPages KvStore KvFault KvLin KvTree KvDb RaftNode RaftJournal ActorGrain ActorFileJournal RaftMembership RaftGrainMembership ObxCluster Actor_devCluster"
pass=0
while [ "$pass" -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"$work/compile.log" 2>&1; then break; fi
	if [ "$pass" -eq 3 ]; then
		echo "test-cluster: компиляция не удалась (проход $pass):" >&2
		tail -40 "$work/compile.log" >&2; exit 1
	fi
done

# Узлы сценария: по этому списку выдаются сертификаты, вручную список не
# ведётся. Роли, которым сертификат подменяют (F, K, N, F1..F3), в списке
# не нужны: они предъявляют сертификат другой роли.
certs_for() {
	case "$1" in
		ask) echo "E Q";;
		reconnect) echo "S C";;
		secure) echo "E E2 e X M Y";;
		raft) echo "C R0 R1 R2 R3 R4";;
		saga) echo "A B C";;
		grain) echo "G0 G1 G2 R0 R1 R2";;
		membership) echo "S0 S1 S2 S3 S4 R0 R1 R2 G0 G1 G2 G3 K0 K1 K2 K3";;
		lease) echo "S0 S1 S2 S3 S4 R0 R1 R2 G0 G1 G2 K0 K1 K2";;
		obxcluster) echo "G0 G1 G2 G3 G4 J0 J1 J2 J3 J4 M0 M1 M2 M3 M4 K0 K1 K2 K3 K4";;
		bench) echo "C R0 R1 R2";;
	esac
}

CERTDIR=""
if [ "$TLS" = 1 ]; then
	CERTDIR="$work/certs"
	if [ "$ONLY" = all ]; then
		nodes=$(for s in ask reconnect secure raft saga grain membership lease obxcluster bench; do
			certs_for "$s"; done | tr ' ' '\n' | sort -u | tr '\n' ' ')
	else
		nodes=$(certs_for "$ONLY" | tr ' ' '\n' | sort -u | tr '\n' ' ')
	fi
	[ -n "$nodes" ] || { echo "test-cluster: для сценария '$ONLY' нет списка узлов" >&2; exit 2; }
	"$BB/tools/cluster-certs.sh" "$CERTDIR" $nodes >/dev/null || exit 1
fi

export OBX_OPS
echo "cluster: прогонов $RUNS, mark=quick:$QUICK veth:$VETH tls:$TLS"
unshare -rmn bash "$0" --inner "$work" "$INSTALL" "$RUNS" "$VETH" "$OPS" "$KILLAT" "$BENCH_OPS" "$TICK" "$KEEP" "$ONLY" "$TLS" "$CERTDIR"
rc=$?
if [ -f "$work/measurements.txt" ]; then
	echo "cluster: замеры:"
	sed 's/^/  /' "$work/measurements.txt"
	cp "$work/measurements.txt" "$BB/.work/cluster-measurements.txt" 2>/dev/null || true
fi
exit $rc
