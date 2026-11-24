#!/bin/sh
# Тест этапа 8 в отдельном сетевом и mount-пространстве без root.
#
#   tools/test-resolver.sh
#
# `unshare -rmn` даёт user/mount/net-пространства; в нём поднимается
# dummy-интерфейс, а resolv.conf указывает на адрес из его подсети.
# Пакеты наружу dummy не отдаёт, поэтому getaddrinfo ждёт тайм-аут
# резолвера (timeout/attempts по умолчанию), не получая ответа.
# ResolveJob исполняется в рабочей нити, а тест считает шаги главного
# цикла за это время.
#
# Нет unshare/dummy/mount — тест пропускается с причиной (код 0),
# чтобы обычный прогон tools/test.sh не краснел на чужой машине.
set -u
. "$(dirname "$0")/env.sh"

MOD=Comm_devResolver
MARKS=proc
TMO=180
DEPS="LinThreads LinNative AsyncWork AsyncWorkLin AsyncIO AsyncSelect AsyncChan AsyncOnce"

skip() {
	echo "SKIP $MOD: $*"
	exit 0
}

# 1. Права на пространства и обязательные утилиты.
probe=$(unshare -rmn true 2>&1) || skip "unshare -rmn недоступен: $probe"
command -v ip >/dev/null 2>&1 || skip "нет команды ip"
command -v mount >/dev/null 2>&1 || skip "нет команды mount"
probe=$(unshare -rmn sh -c 'ip link set lo up && ip link add dummy0 type dummy && ip link set dummy0 up' 2>&1) \
	|| skip "dummy-интерфейс в пространстве не создаётся: $probe"

[ -x "$INTERP" ] || { echo "test-resolver: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }

# 2. Компиляция (сеть не нужна, идёт снаружи пространства).
work=$(mktemp -d "${TMPDIR:-/tmp}/nsres.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT INT TERM
mods="UnitAssert UnitRunner Unit_devProbe Unit_devMain $DEPS"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"$work/compile.log" 2>&1 \
			&& "$BB/tools/go64.sh" "$MOD" >>"$work/compile.log" 2>&1; then
		break
	fi
	if [ $pass -eq 3 ]; then
		echo "test-resolver: компиляция не удалась (проход $pass):" >&2
		tail -40 "$work/compile.log" >&2; exit 1
	fi
done

# 3. Свои resolv.conf/nsswitch.conf: только files+dns и один недоступный
#    сервер. Строки options нет — тайм-аут и число попыток по умолчанию.
printf 'nameserver 10.99.99.2\n' >"$work/resolv.conf"
printf 'passwd: files\ngroup: files\nhosts: files dns\nnetworks: files\n' >"$work/nsswitch.conf"

# 4. Прогон в пространстве. Каталог установки и интерпретатор — снаружи,
#    поэтому пути абсолютные и в кавычках.
printf 'UnitRunner.Run %s %s\n' "$MOD" "$MARKS" >"$work/cmd"
unshare -rmn sh -c "
	set -e
	ip link set lo up
	ip link add dummy0 type dummy
	ip addr add 10.99.99.1/24 dev dummy0
	ip link set dummy0 up
	mount --bind '$work/resolv.conf' /etc/resolv.conf
	mount --bind '$work/nsswitch.conf' /etc/nsswitch.conf
	cd '$INSTALL'
	LD_LIBRARY_PATH='$INSTALL' timeout -k 5 $TMO '$INTERP' <'$work/cmd'
" >"$work/out" 2>&1
rc=$?

grep -E '^(PASS|FAIL|EMPTY|SKIP|== Unit|Comm_devResolver:)' "$work/out" || true
if [ $rc -eq 124 ]; then
	echo "test-resolver: $MOD — таймаут ${TMO}s" >&2
	tail -20 "$work/out" >&2; exit 1
fi
if [ $rc -ne 0 ]; then
	echo "test-resolver: $MOD — процесс завершился с кодом $rc" >&2
	tail -20 "$work/out" >&2; exit 1
fi
if grep -q '^== Unit: ' "$work/out" && grep -q 'failed 0,' "$work/out"; then
	exit 0
fi
echo "test-resolver: $MOD — есть провалы или нет итоговой строки" >&2
tail -20 "$work/out" >&2; exit 1
