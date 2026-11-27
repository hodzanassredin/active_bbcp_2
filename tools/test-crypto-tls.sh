#!/bin/sh
# Проверка TLS подсистемы Crypto: наш клиент и наш сервер против openssl,
# плюс отрицательные случаи (отказ без падения процесса).
#
#   tools/test-crypto-tls.sh
#
# Создаёт во временном каталоге корневой CA, серверные сертификаты RSA,
# ECDSA P-256/P-384 и Ed25519, просроченный сертификат и сертификат с
# чужим именем. Проверяются TLS 1.3 (наборы AES-128/256-GCM и
# ChaCha20-Poly1305, группы x25519/secp256r1/secp384r1, HRR, KeyUpdate,
# Ed25519 CertificateVerify) и TLS 1.2.
# Положительные случаи:
#   * наш клиент -> openssl s_server -rev (проверка цепочки, имени, подписи
#     ServerKeyExchange), ответ "olleh";
#   * openssl s_client -> наш сервер (проверка сертификата сервера), "olleh";
#   * матрица: ECDHE-RSA/ECDHE-ECDSA × AES-128/256-GCM и ChaCha20-Poly1305
#     × x25519/secp256r1/secp384r1 (для ECDSA-сертификата — его кривая),
#     в обе стороны;
#   * взаимная проверка с RSA и ECDSA сертификатом сервера.
# Отрицательные случаи (клиент обязан отказаться, процесс не падает):
#   * сертификат не из доверенного корня;
#   * просроченный сертификат;
#   * сертификат с чужим именем;
#   * подделанная подпись ServerKeyExchange (RSA PKCS#1, RSA-PSS, ECDSA);
#   * вместо ServerKeyExchange сразу ServerHelloDone (нарушение порядка);
#   * группа, которой клиент не предлагал; точка secp256r1 вне кривой;
#   * испорченный тег AES-GCM и ChaCha20-Poly1305;
#   * sigLen = 65535 (границы до копирования);
#   * длина сертификата больше списка;
#   * невозможная длина расширений ServerHello.
# И отрицательные для сервера: клиент без сертификата отвергается, чужой
# клиентский сертификат отвергается, SSLv2 ClientHello и неизвестный тип
# рукопожатия отвергаются без трапа.
# Для TLS 1.3 к этому добавлены: rsa_pkcs1_* и не предложенная схема в
# CertificateVerify, CCS с неверным значением или длиной, CCS после
# Finished и до первого ClientHello, пустой TLSInnerPlaintext и запись из
# одних нулей, законные пустая запись и нулевое дополнение, испорченный
# тег и внутренний тип записи, укороченный Finished, недопустимый
# KeyUpdate, неверные legacy_version/compression, чужая группа и
# расширения ServerHello, нулевая и чрезмерная доля X25519 key_share.
#
# Запускать через tools/iso.sh, как остальные сборки.
set -u
. "$(dirname "$0")/env.sh"

OPENSSL="${OPENSSL:-/usr/bin/openssl}"
CIPHER='ECDHE-RSA-AES128-GCM-SHA256'

if [ ! -x "$OPENSSL" ]; then
	echo "SKIP test-crypto-tls: нет $OPENSSL"
	exit 0
fi
if [ ! -x "$INTERP" ]; then
	echo "test-crypto-tls: нет $INTERP — соберите tools/build64.sh" >&2
	exit 2
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/crypto-tls.XXXXXX") || exit 2
# Таймауты: наш процесс и openssl не должны жить дольше случая. Зависший
# случай — это FAIL с его именем, а не ожидание до утра (порча
# SuiteCompatible раньше вешала server-auto-suite на 1,5 часа).
BB_TIMEOUT="${CRYPTO_TLS_BB_TIMEOUT:-45}"
OPENSSL_TIMEOUT="${CRYPTO_TLS_OPENSSL_TIMEOUT:-30}"
# Все фоновые процессы прогона запускаются в своей сессии (setsid) и
# запоминаются здесь; cleanup гасит всю группу, поэтому после прогона и
# после Ctrl-C/kill не остаётся ни blackbox-console, ни openssl.
BG_PIDS=""
# Встроенный kill в dash не понимает отрицательный pid (группу), внешний
# /bin/kill понимает: kill -TERM -- -PGID.
KILL=/bin/kill
[ -x "$KILL" ] || KILL=/usr/bin/kill
[ -x "$KILL" ] || { echo "test-crypto-tls: нет внешнего kill" >&2; exit 2; }

# spawn LOG CMD... — запустить CMD в новой сессии, вывод в LOG.
spawn() {
	log=$1; shift
	setsid "$@" >"$log" 2>&1 &
	BG_PIDS="$BG_PIDS $!"
	BGPID=$!
}

# stop_group PID — погасить всю группу процессов PID (TERM, затем KILL).
stop_group() {
	pid=$1
	[ -n "$pid" ] || return 0
	"$KILL" -TERM -- "-$pid" 2>/dev/null
	i=0
	while "$KILL" -0 -- "-$pid" 2>/dev/null && [ $i -lt 30 ]; do
		sleep 0.1; i=$((i + 1))
	done
	if "$KILL" -0 -- "-$pid" 2>/dev/null; then
		"$KILL" -KILL -- "-$pid" 2>/dev/null
	fi
	wait "$pid" 2>/dev/null
}

cleanup() {
	for p in $BG_PIDS; do
		stop_group "$p"
	done
	BG_PIDS=""
	[ -n "${spid:-}" ] && stop_group "$spid"
	[ -n "${fpid:-}" ] && stop_group "$fpid"
	# Гонка: TERM мог прийти между setsid и записью PID в BG_PIDS, тогда
	# группа не попала в список. У всех процессов прогона в аргументах
	# есть путь $TMP (сертификаты, ключи, логи), поэтому добиваем их по
	# нему, повторяя короткое время на случай, если процесс ещё не успел
	# выполнить exec.
	if command -v pgrep >/dev/null 2>&1; then
		i=0
		while [ $i -lt 20 ]; do
			pids=$(pgrep -f -- "$TMP" 2>/dev/null)
			[ -z "$pids" ] && break
			for p in $pids; do "$KILL" -TERM "$p" 2>/dev/null; done
			sleep 0.1; i=$((i + 1))
		done
		pids=$(pgrep -f -- "$TMP" 2>/dev/null)
		for p in $pids; do "$KILL" -KILL "$p" 2>/dev/null; done
	fi
	# CRYPTO_TLS_KEEP_TMP=1 оставляет журналы и сертификаты разбора порчи.
	if [ "${CRYPTO_TLS_KEEP_TMP:-0}" = 1 ]; then
		echo "test-crypto-tls: TMP сохранён: $TMP" >&2
		return 0
	fi
	rm -rf "$TMP"
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM HUP

fail() {
	echo "test-crypto-tls: FAIL: $*" >&2
	exit 1
}

# CRYPTO_TLS_ONLY — регулярное выражение (grep -E) по имени случая:
# неподходящие случаи пропускаются. Нужно для перебора порч: полный
# прогон идёт без переменной, то есть проверяет всё.
ONLY="${CRYPTO_TLS_ONLY:-}"
selected() {
	[ -z "$ONLY" ] && return 0
	if printf '%s\n' "$1" | grep -Eq -- "$ONLY"; then
		return 0
	fi
	echo "  skip: $1"
	return 1
}

free_port() {
	python3 -I -c 'import socket
s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()'
}

# --- сертификаты ---------------------------------------------------------

req() {
	"$OPENSSL" req "$@" >>"$TMP/cert.log" 2>&1 || { cat "$TMP/cert.log" >&2; fail "openssl req"; }
}
sign() {
	"$OPENSSL" x509 -req "$@" >>"$TMP/cert.log" 2>&1 || { cat "$TMP/cert.log" >&2; fail "openssl x509"; }
}

req -x509 -newkey rsa:2048 -nodes -keyout "$TMP/ca.key" -out "$TMP/ca.pem" -days 1 \
	-subj "/CN=Test CA" \
	-addext "basicConstraints=critical,CA:TRUE" \
	-addext "keyUsage=critical,keyCertSign,cRLSign"

req -x509 -newkey rsa:2048 -nodes -keyout "$TMP/other-ca.key" -out "$TMP/other-ca.pem" -days 1 \
	-subj "/CN=Other CA" \
	-addext "basicConstraints=critical,CA:TRUE" \
	-addext "keyUsage=critical,keyCertSign,cRLSign"

# Сервер: localhost + 127.0.0.1
req -newkey rsa:2048 -nodes -keyout "$TMP/srv.key" -out "$TMP/srv.csr" -subj "/CN=localhost"
printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n' >"$TMP/srv.ext"
sign -in "$TMP/srv.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/srv.pem" -days 1 -extfile "$TMP/srv.ext"

# Сертификаты, подписанные CA с SHA-384/SHA-512 (DigestInfo PKCS#1).
req -newkey rsa:2048 -nodes -keyout "$TMP/srv384.key" -out "$TMP/srv384.csr" -subj "/CN=localhost"
sign -in "$TMP/srv384.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/srv384.pem" -days 1 -sha384 -extfile "$TMP/srv.ext"
req -newkey rsa:2048 -nodes -keyout "$TMP/srv512.key" -out "$TMP/srv512.csr" -subj "/CN=localhost"
sign -in "$TMP/srv512.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/srv512.pem" -days 1 -sha512 -extfile "$TMP/srv.ext"

# Серверные сертификаты ECDSA P-256 и P-384.
ec_cert() {
	name=$1; curve=$2
	"$OPENSSL" ecparam -name "$curve" -genkey -noout -out "$TMP/$name.key" >>"$TMP/cert.log" 2>&1 \
		|| { cat "$TMP/cert.log" >&2; fail "openssl ecparam $curve"; }
	"$OPENSSL" req -new -key "$TMP/$name.key" -out "$TMP/$name.csr" -subj "/CN=localhost" \
		>>"$TMP/cert.log" 2>&1 || { cat "$TMP/cert.log" >&2; fail "openssl req $curve"; }
	sign -in "$TMP/$name.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
		-out "$TMP/$name.pem" -days 1 -extfile "$TMP/srv.ext"
}
ec_cert ec256 prime256v1
ec_cert ec384 secp384r1

# Серверный сертификат Ed25519: CertificateVerify проверяется, но не
# подписывается нашим сервером.
"$OPENSSL" genpkey -algorithm ED25519 -out "$TMP/ed.key" >>"$TMP/cert.log" 2>&1 \
	|| { cat "$TMP/cert.log" >&2; fail "openssl genpkey ed25519"; }
"$OPENSSL" req -new -key "$TMP/ed.key" -out "$TMP/ed.csr" -subj "/CN=localhost" \
	>>"$TMP/cert.log" 2>&1 || { cat "$TMP/cert.log" >&2; fail "openssl req ed25519"; }
sign -in "$TMP/ed.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/ed.pem" -days 1 -extfile "$TMP/srv.ext"

# Сертификат с длинным списком SAN: совпадающее имя стоит дальше 16-го.
req -newkey rsa:2048 -nodes -keyout "$TMP/many.key" -out "$TMP/many.csr" -subj "/CN=localhost"
{
	printf 'subjectAltName='
	i=0
	while [ $i -lt 20 ]; do
		printf 'DNS:host%s.example,' "$i"
		i=$((i + 1))
	done
	printf 'DNS:localhost,IP:127.0.0.1\n'
	printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n'
} >"$TMP/many.ext"
sign -in "$TMP/many.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/many.pem" -days 1 -extfile "$TMP/many.ext"

# Набор корней: сначала нечитаемый сертификат, затем настоящий CA.
{
	printf -- '-----BEGIN CERTIFICATE-----\n'
	head -c 300 /dev/urandom | base64
	printf -- '\n-----END CERTIFICATE-----\n'
	cat "$TMP/ca.pem"
} >"$TMP/badca.pem"

# Большой набор корней (> 16 КиБ): буфер AddTrustedFile должен быть динамическим.
i=0
while [ $i -lt 100 ]; do
	cat "$TMP/ca.pem" >>"$TMP/bigca.pem"
	i=$((i + 1))
done

# Просроченный (notAfter = notBefore = сейчас).
req -newkey rsa:2048 -nodes -keyout "$TMP/exp.key" -out "$TMP/exp.csr" -subj "/CN=127.0.0.1"
printf 'subjectAltName=IP:127.0.0.1\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n' >"$TMP/exp.ext"
sign -in "$TMP/exp.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/exp.pem" -days 0 -extfile "$TMP/exp.ext"

# Промежуточный CA и сервер за ним: цепочка обязана приниматься.
req -newkey rsa:2048 -nodes -keyout "$TMP/int.key" -out "$TMP/int.csr" -subj "/CN=Test Intermediate CA"
printf 'basicConstraints=critical,CA:TRUE,pathlen:1\nkeyUsage=critical,keyCertSign,cRLSign\n' >"$TMP/int.ext"
sign -in "$TMP/int.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/int.pem" -days 1 -extfile "$TMP/int.ext"
req -newkey rsa:2048 -nodes -keyout "$TMP/srvint.key" -out "$TMP/srvint.csr" -subj "/CN=127.0.0.1"
printf 'subjectAltName=IP:127.0.0.1\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n' >"$TMP/srvint.ext"
sign -in "$TMP/srvint.csr" -CA "$TMP/int.pem" -CAkey "$TMP/int.key" -CAcreateserial \
	-out "$TMP/srvint.pem" -days 1 -extfile "$TMP/srvint.ext"

# Издатель с basicConstraints CA:FALSE, подписавший серверный сертификат:
# клиент обязан отвергнуть такую цепочку.
req -newkey rsa:2048 -nodes -keyout "$TMP/badsigner.key" -out "$TMP/badsigner.csr" -subj "/CN=Bad Signer"
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >"$TMP/badsigner.ext"
sign -in "$TMP/badsigner.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/badsigner.pem" -days 1 -extfile "$TMP/badsigner.ext"
req -newkey rsa:2048 -nodes -keyout "$TMP/noca.key" -out "$TMP/noca.csr" -subj "/CN=127.0.0.1"
printf 'subjectAltName=IP:127.0.0.1\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n' >"$TMP/noca.ext"
sign -in "$TMP/noca.csr" -CA "$TMP/badsigner.pem" -CAkey "$TMP/badsigner.key" -CAcreateserial \
	-out "$TMP/noca.pem" -days 1 -extfile "$TMP/noca.ext"

# Чужое имя.
req -newkey rsa:2048 -nodes -keyout "$TMP/wh.key" -out "$TMP/wh.csr" -subj "/CN=example.com"
printf 'subjectAltName=DNS:example.com\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n' >"$TMP/wh.ext"
sign -in "$TMP/wh.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/wh.pem" -days 1 -extfile "$TMP/wh.ext"

# Клиентский сертификат.
req -newkey rsa:2048 -nodes -keyout "$TMP/cl.key" -out "$TMP/cl.csr" -subj "/CN=client"
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\n' >"$TMP/cl.ext"
sign -in "$TMP/cl.csr" -CA "$TMP/ca.pem" -CAkey "$TMP/ca.key" -CAcreateserial \
	-out "$TMP/cl.pem" -days 1 -extfile "$TMP/cl.ext"

# Клиентский сертификат от чужого CA: сервер обязан его отвергнуть.
req -newkey rsa:2048 -nodes -keyout "$TMP/badcl.key" -out "$TMP/badcl.csr" -subj "/CN=foreign"
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\n' >"$TMP/badcl.ext"
sign -in "$TMP/badcl.csr" -CA "$TMP/other-ca.pem" -CAkey "$TMP/other-ca.key" -CAcreateserial \
	-out "$TMP/badcl.pem" -days 1 -extfile "$TMP/badcl.ext"

# --- сборка тестовых модулей --------------------------------------------

"$BB/tools/go64.sh" Crypto_devTLS Crypto_devTLSServer >"$TMP/compile.log" 2>&1 || {
	tail -20 "$TMP/compile.log" >&2; fail "Crypto_devTLS/Crypto_devTLSServer не собрались"
}

no_trap() {
	if grep -q 'TRAP REPORT' "$1"; then
		cat "$1" >&2
		fail "трап в $2"
	fi
}

# Наш клиент. ca/cert/key могут быть пустыми.
client_case() {
	desc=$1; port=$2; ca=$3; cert=$4; key=$5; expectFail=$6; badapp=${7:-0}
	selected "$desc" || return 0
	log="$TMP/client-$desc.log"
	args="--CRYPTO_TLS_ADDR=127.0.0.1:$port --CRYPTO_TLS_EXPECT_FAIL=$expectFail --CRYPTO_TLS_EXPECT_BAD_APP=$badapp"
	[ -n "$ca" ] && args="$args --CRYPTO_TLS_CA=$ca"
	[ -n "$cert" ] && args="$args --CRYPTO_TLS_CERT=$cert"
	[ -n "$key" ] && args="$args --CRYPTO_TLS_KEY=$key"
	(
		cd "$INSTALL" &&
		printf 'UnitRunner.Run Crypto_devTLS proc quit\n' |
			LD_LIBRARY_PATH="$INSTALL" timeout -s TERM -k 5 "$BB_TIMEOUT" "$INTERP" $args
	) >"$log" 2>&1
	rc=$?
	[ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] && fail "клиент ($desc): таймаут $BB_TIMEOUT с"
	no_trap "$log" "клиенте ($desc)"
	# Итоговая строка «failed 0» печатается и для нуля исполненных
	# тестов (например, модуль не загрузился), поэтому нужен именно PASS.
	if ! grep -q '^PASS Crypto_devTLS.TestProcHandshake' "$log"; then
		cat "$log" >&2; fail "клиент ($desc): тест не исполнился"
	fi
	grep -q 'failed 0,' "$log" || { cat "$log" >&2; fail "клиент ($desc)"; }
	echo "  ok: client $desc"
}

# Наш клиент против openssl s_server.
client_vs_openssl() {
	desc=$1; cert=$2; key=$3; ca=$4; ccert=$5; ckey=$6; expectFail=$7; verify=$8
	selected "$desc" || return 0
	server_extra=${9:-}
	cipher=${10:-$CIPHER}; group=${11:-}
	port=$(free_port)
	extra=""
	gopt=""
	[ "$verify" = "1" ] && extra="-Verify 1 -CAfile $TMP/ca.pem"
	[ -n "$group" ] && gopt="-groups $group"
	spawn "$TMP/server-$desc.log" sh -c \
		"sleep $BB_TIMEOUT | '$OPENSSL' s_server -accept 127.0.0.1:$port -cert '$cert' -key '$key' -tls1_2 -cipher '$cipher' $gopt -rev $extra $server_extra"
	spid=$BGPID
	i=0; ready=0
	while [ $i -lt 100 ]; do
		if python3 -I -c "import socket,sys
s=socket.socket(); s.settimeout(0.1)
try: s.connect(('127.0.0.1',$port))
except OSError: sys.exit(1)
s.close()" 2>/dev/null; then ready=1; break; fi
		sleep 0.05; i=$((i + 1))
	done
	[ $ready -eq 1 ] || fail "s_server ($desc) не поднялся"
	client_case "$desc" "$port" "$ca" "$ccert" "$ckey" "$expectFail"
	stop_group "$spid"; spid=""
}

# Наш сервер против openssl s_client.
server_vs_openssl() {
	desc=$1; ca=$2; require=$3; expectReject=$4; ccert=$5; ckey=$6; want=$7
	selected "$desc" || return 0
	cipher=${8:-$CIPHER}; group=${9:-}
	srv_cert=${10:-$TMP/srv.pem}; srv_key=${11:-$TMP/srv.key}
	port=$(free_port)
	log="$TMP/tlsserver-$desc.log"
	args="--CRYPTO_TLS_ADDR=127.0.0.1:$port --CRYPTO_TLS_CERT=$srv_cert --CRYPTO_TLS_KEY=$srv_key"
	args="$args --CRYPTO_TLS_REQUIRE_CLIENT=$require --CRYPTO_TLS_EXPECT_REJECT=$expectReject"
	[ -n "$ca" ] && args="$args --CRYPTO_TLS_CA=$ca"
	spawn "$log" sh -c "cd '$INSTALL' && printf 'UnitRunner.Run Crypto_devTLSServer proc quit\n' | LD_LIBRARY_PATH='$INSTALL' timeout -s TERM -k 5 $BB_TIMEOUT '$INTERP' $args"
	spid=$BGPID
	sleep 1
	client_extra=""
	gopt=""
	copt=""
	[ -n "$ccert" ] && client_extra="-cert $ccert -key $ckey"
	[ -n "$group" ] && [ "$group" != "-" ] && gopt="-groups $group"
	[ "$cipher" != "-" ] && copt="-cipher $cipher"
	printf 'hello\n' | timeout -s TERM -k 5 "$OPENSSL_TIMEOUT" "$OPENSSL" s_client -connect "127.0.0.1:$port" \
		-tls1_2 $copt $gopt -CAfile "$TMP/ca.pem" -verify_return_error \
		$client_extra -quiet >"$TMP/sclient-$desc.out" 2>"$TMP/sclient-$desc.err"
	wait "$spid" 2>/dev/null; rc=$?; spid=""
	[ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] && fail "сервер ($desc): таймаут $BB_TIMEOUT с"
	no_trap "$log" "сервере ($desc)"
	if ! grep -q '^PASS Crypto_devTLSServer.TestProcServer' "$log"; then
		cat "$log" >&2; fail "сервер ($desc): тест не исполнился"
	fi
	grep -q 'failed 0,' "$log" || { cat "$log" >&2; fail "сервер ($desc)"; }
	if [ "$expectReject" != "1" ]; then
		tr -d '\r' <"$TMP/sclient-$desc.out" | grep -q "$want" ||
			fail "s_client ($desc) не получил $want"
	fi
	echo "  ok: server $desc"
}

# Наш клиент против openssl s_server -tls1_3.
client_vs_openssl13() {
	desc=$1; cert=$2; key=$3; ca=$4; ccert=$5; ckey=$6
	expectFail=$7; cipher=$8; group=$9; verify=${10:-0}
	selected "$desc" || return 0
	port=$(free_port)
	extra=""
	[ "$verify" = "1" ] && extra="-Verify 1 -CAfile $TMP/ca.pem"
	spawn "$TMP/server13-$desc.log" sh -c \
		"sleep $BB_TIMEOUT | '$OPENSSL' s_server -accept 127.0.0.1:$port -cert '$cert' -key '$key' -tls1_3 -ciphersuites '$cipher' -groups '$group' -rev $extra"
	spid=$BGPID
	i=0; ready=0
	while [ $i -lt 100 ]; do
		if python3 -I -c "import socket,sys
s=socket.socket(); s.settimeout(0.1)
try: s.connect(('127.0.0.1',$port))
except OSError: sys.exit(1)
s.close()" 2>/dev/null; then ready=1; break; fi
		sleep 0.05; i=$((i + 1))
	done
	[ $ready -eq 1 ] || fail "s_server 1.3 ($desc) не поднялся"
	client_case "$desc" "$port" "$ca" "$ccert" "$ckey" "$expectFail"
	stop_group "$spid"; spid=""
}

# Наш сервер против openssl s_client -tls1_3.
server_vs_openssl13() {
	desc=$1; ca=$2; cipher=$3; group=$4; srv_cert=$5; srv_key=$6
	require=${7:-0}; ccert=${8:-}; ckey=${9:-}
	selected "$desc" || return 0
	port=$(free_port)
	log="$TMP/tlsserver13-$desc.log"
	args="--CRYPTO_TLS_ADDR=127.0.0.1:$port --CRYPTO_TLS_CERT=$srv_cert --CRYPTO_TLS_KEY=$srv_key"
	args="$args --CRYPTO_TLS_REQUIRE_CLIENT=$require --CRYPTO_TLS_EXPECT_REJECT=0"
	[ -n "$ca" ] && args="$args --CRYPTO_TLS_CA=$ca"
	spawn "$log" sh -c "cd '$INSTALL' && printf 'UnitRunner.Run Crypto_devTLSServer proc quit\n' | LD_LIBRARY_PATH='$INSTALL' timeout -s TERM -k 5 $BB_TIMEOUT '$INTERP' $args"
	spid=$BGPID
	sleep 1
	client_extra=""
	[ -n "$ccert" ] && client_extra="-cert $ccert -key $ckey"
	printf 'hello\n' | timeout -s TERM -k 5 "$OPENSSL_TIMEOUT" "$OPENSSL" s_client -connect "127.0.0.1:$port" \
		-tls1_3 -ciphersuites "$cipher" -groups "$group" -CAfile "$TMP/ca.pem" \
		-verify_return_error $client_extra -quiet >"$TMP/sclient13-$desc.out" 2>"$TMP/sclient13-$desc.err"
	rc=$?
	wait "$spid" 2>/dev/null; srcc=$?; spid=""
	[ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] && fail "s_client ($desc): таймаут"
	no_trap "$log" "сервере ($desc)"
	if ! grep -q '^PASS Crypto_devTLSServer.TestProcServer' "$log"; then
		cat "$log" >&2; cat "$TMP/sclient13-$desc.err" >&2
		fail "сервер 1.3 ($desc): тест не исполнился"
	fi
	grep -q 'failed 0,' "$log" || { cat "$log" >&2; fail "сервер 1.3 ($desc)"; }
	tr -d '\r' <"$TMP/sclient13-$desc.out" | grep -q "olleh" ||
		fail "s_client 1.3 ($desc) не получил olleh"
	echo "  ok: server13 $desc"
}

# Сервер, умеющий 1.3, но согласовавший 1.2 с клиентом без 1.3, обязан
# поставить DOWNGRD\x01 в последние 8 байт ServerHello.random.
server_sentinel_case() {
	selected sentinel || return 0
	port=$(free_port)
	log="$TMP/tlsserver-sentinel.log"
	args="--CRYPTO_TLS_ADDR=127.0.0.1:$port --CRYPTO_TLS_CERT=$TMP/srv.pem"
	args="$args --CRYPTO_TLS_KEY=$TMP/srv.key --CRYPTO_TLS_REQUIRE_CLIENT=0"
	args="$args --CRYPTO_TLS_EXPECT_REJECT=0"
	spawn "$log" sh -c "cd '$INSTALL' && printf 'UnitRunner.Run Crypto_devTLSServer proc quit\n' | LD_LIBRARY_PATH='$INSTALL' timeout -s TERM -k 5 $BB_TIMEOUT '$INTERP' $args"
	spid=$BGPID
	sleep 1
	printf 'hello\n' | timeout -s TERM -k 5 "$OPENSSL_TIMEOUT" "$OPENSSL" s_client \
		-connect "127.0.0.1:$port" -tls1_2 -msg -quiet >"$TMP/sclient-sentinel.log" 2>&1
	rc=$?
	wait "$spid" 2>/dev/null; spid=""
	[ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] && fail "sentinel: таймаут"
	no_trap "$log" "сервере (sentinel)"
	grep -q 'failed 0,' "$log" || { cat "$log" >&2; fail "sentinel: сервер"; }
	tr -d '\r' <"$TMP/sclient-sentinel.log" | grep -q 'olleh' ||
		fail "sentinel: обмен не завершён"
	python3 -I - "$TMP/sclient-sentinel.log" <<'PY' || fail "sentinel: нет DOWNGRD\\x01 в ServerHello.random"
import re
import sys
lines = open(sys.argv[1], errors="replace").read().splitlines()
for i, line in enumerate(lines):
    if re.search(r"Handshake \[length [0-9a-f]+\], ServerHello$", line):
        data = []
        j = i + 1
        while j < len(lines) and re.match(r"^\s+[0-9a-f]{2}( [0-9a-f]{2})*$", lines[j]):
            data.append(lines[j].strip())
            j += 1
        msg = bytes.fromhex(" ".join(data))
        random = msg[4 + 2:4 + 34]
        sys.exit(0 if random[24:] == b"DOWNGRD\x01" else 1)
sys.exit(1)
PY
	echo "  ok: server sentinel"
}

# Поддельный TLS 1.3 сервер (Crypto/tools/crypto_tls13_fake.py).
fake13_case() {
	mode=$1; expect=${2:-0}; badapp=${3:-0}
	selected "fake13-$mode" || return 0
	port=$(free_port)
	spawn "$TMP/fake13-$mode.log" timeout -s TERM -k 5 "$BB_TIMEOUT" \
		python3 -I "$BB/tools/crypto_tls13_fake.py" "$mode" "$port" "$TMP/srv.pem" "$TMP/srv.key"
	fpid=$BGPID
	i=0
	while [ $i -lt 100 ]; do
		grep -q ready "$TMP/fake13-$mode.log" 2>/dev/null && break
		sleep 0.05; i=$((i + 1))
	done
	client_case "fake13-$mode" "$port" "$TMP/ca.pem" "" "" "$expect" "$badapp"
	wait "$fpid" 2>/dev/null; fpid=""
}

# Поддельный сервер для отрицательных проверок клиента.
fake_case() {
	mode=$1; expect=${2:-1}; badapp=${3:-0}
	selected "fake-$mode" || return 0
	fcert=${4:-$TMP/srv.pem}; fkey=${5:-$TMP/srv.key}
	port=$(free_port)
	spawn "$TMP/fake-$mode.log" timeout -s TERM -k 5 "$BB_TIMEOUT" \
		python3 -I "$BB/tools/crypto_tls_fake.py" "$mode" "$port" "$fcert" "$fkey"
	fpid=$BGPID
	i=0
	while [ $i -lt 100 ]; do
		grep -q ready "$TMP/fake-$mode.log" 2>/dev/null && break
		sleep 0.05; i=$((i + 1))
	done
	client_case "fake-$mode" "$port" "$TMP/ca.pem" "" "" "$expect" "$badapp"
	wait "$fpid" 2>/dev/null; fpid=""
	case "$mode" in
		goodfinished|badfinished|badccs|badtag|badtagchacha|shortrec|oldversion|longfinished|longccs) ;;	# CKE здесь обязателен
		*)
			if grep -q saw_cke "$TMP/fake-$mode.log"; then
				fail "клиент дошёл до ClientKeyExchange при плохом SKE ($mode)"
			fi
			;;
	esac
}

# Некорректный клиент против нашего сервера: отказ без трапа.
evil_server_case() {
	desc=$1; mode=$2
	selected "$desc" || return 0
	port=$(free_port)
	log="$TMP/tlsserver-$desc.log"
	args="--CRYPTO_TLS_ADDR=127.0.0.1:$port --CRYPTO_TLS_CERT=$TMP/srv.pem --CRYPTO_TLS_KEY=$TMP/srv.key"
	args="$args --CRYPTO_TLS_REQUIRE_CLIENT=0 --CRYPTO_TLS_EXPECT_REJECT=1"
	spawn "$log" sh -c "cd '$INSTALL' && printf 'UnitRunner.Run Crypto_devTLSServer proc quit\n' | LD_LIBRARY_PATH='$INSTALL' timeout -s TERM -k 5 $BB_TIMEOUT '$INTERP' $args"
	spid=$BGPID
	sleep 1
	timeout -s TERM -k 5 "$OPENSSL_TIMEOUT" python3 -I "$BB/tools/tls_evil_client.py" "$mode" 127.0.0.1 "$port" \
		>"$TMP/evil-$desc.out" 2>&1 || true
	wait "$spid" 2>/dev/null; rc=$?; spid=""
	[ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] && fail "сервер ($desc): таймаут $BB_TIMEOUT с"
	no_trap "$log" "сервере ($desc)"
	if ! grep -q '^PASS Crypto_devTLSServer.TestProcServer' "$log"; then
		cat "$log" >&2; fail "сервер ($desc): тест не исполнился"
	fi
	grep -q 'failed 0,' "$log" || { cat "$log" >&2; fail "сервер ($desc)"; }
	echo "  ok: server $desc"
}

# Наш сервер против Python-клиента: проверка CertificateVerify.
python_client_case() {
	desc=$1; mode=$2; expectReject=$3
	selected "$desc" || return 0
	port=$(free_port)
	log="$TMP/tlsserver-$desc.log"
	args="--CRYPTO_TLS_ADDR=127.0.0.1:$port --CRYPTO_TLS_CERT=$TMP/srv.pem --CRYPTO_TLS_KEY=$TMP/srv.key"
	args="$args --CRYPTO_TLS_REQUIRE_CLIENT=1 --CRYPTO_TLS_EXPECT_REJECT=$expectReject --CRYPTO_TLS_CA=$TMP/ca.pem"
	spawn "$log" sh -c "cd '$INSTALL' && printf 'UnitRunner.Run Crypto_devTLSServer proc quit\n' | LD_LIBRARY_PATH='$INSTALL' timeout -s TERM -k 5 $BB_TIMEOUT '$INTERP' $args"
	spid=$BGPID
	sleep 1
	timeout -s TERM -k 5 "$OPENSSL_TIMEOUT" python3 -I "$BB/tools/crypto_tls_client.py" "$mode" 127.0.0.1 "$port" \
		"$TMP/cl.pem" "$TMP/cl.key" >"$TMP/pyclient-$desc.log" 2>&1
	pyrc=$?
	wait "$spid" 2>/dev/null; rc=$?; spid=""
	[ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] && fail "сервер ($desc): таймаут $BB_TIMEOUT с"
	no_trap "$log" "сервере ($desc)"
	if ! grep -q '^PASS Crypto_devTLSServer.TestProcServer' "$log"; then
		cat "$log" >&2; cat "$TMP/pyclient-$desc.log" >&2
		fail "сервер ($desc): тест не исполнился"
	fi
	grep -q 'failed 0,' "$log" || { cat "$log" >&2; fail "сервер ($desc)"; }
	[ "$pyrc" -eq 0 ] || { cat "$TMP/pyclient-$desc.log" >&2; fail "python-клиент ($desc)"; }
	echo "  ok: server $desc"
}

# Наш сервер против Python-клиента TLS 1.3 (crypto_tls13_client.py).
python_client13_case() {
	desc=$1; mode=$2; expectReject=$3
	badapp=${4:-0}
	selected "$desc" || return 0
	port=$(free_port)
	log="$TMP/tlsserver-$desc.log"
	args="--CRYPTO_TLS_ADDR=127.0.0.1:$port --CRYPTO_TLS_CERT=$TMP/srv.pem --CRYPTO_TLS_KEY=$TMP/srv.key"
	args="$args --CRYPTO_TLS_REQUIRE_CLIENT=1 --CRYPTO_TLS_EXPECT_REJECT=$expectReject"
	args="$args --CRYPTO_TLS_EXPECT_BAD_APP=$badapp --CRYPTO_TLS_CA=$TMP/ca.pem"
	spawn "$log" sh -c "cd '$INSTALL' && printf 'UnitRunner.Run Crypto_devTLSServer proc quit\n' | LD_LIBRARY_PATH='$INSTALL' timeout -s TERM -k 5 $BB_TIMEOUT '$INTERP' $args"
	spid=$BGPID
	sleep 1
	timeout -s TERM -k 5 "$OPENSSL_TIMEOUT" python3 -I "$BB/tools/crypto_tls13_client.py" "$mode" 127.0.0.1 "$port" \
		"$TMP/cl.pem" "$TMP/cl.key" >"$TMP/pyclient13-$desc.log" 2>&1
	pyrc=$?
	wait "$spid" 2>/dev/null; rc=$?; spid=""
	[ "$rc" -eq 124 ] || [ "$rc" -eq 137 ] && fail "сервер ($desc): таймаут $BB_TIMEOUT с"
	no_trap "$log" "сервере ($desc)"
	if ! grep -q '^PASS Crypto_devTLSServer.TestProcServer' "$log"; then
		cat "$log" >&2; cat "$TMP/pyclient13-$desc.log" >&2
		fail "сервер 1.3 ($desc): тест не исполнился"
	fi
	grep -q 'failed 0,' "$log" || { cat "$log" >&2; fail "сервер 1.3 ($desc)"; }
	if [ "$expectReject" != "1" ] && [ "$badapp" != "1" ]; then
		[ "$pyrc" -eq 0 ] || { cat "$TMP/pyclient13-$desc.log" >&2; fail "python-клиент 1.3 ($desc)"; }
	fi
	echo "  ok: server13-python $desc"
}

echo "test-crypto-tls: положительные случаи"
client_vs_openssl "client-serverhello" "$TMP/srv.pem" "$TMP/srv.key" "$TMP/ca.pem" "" "" 0 0
client_vs_openssl "client-intermediate" "$TMP/srvint.pem" "$TMP/srvint.key" "$TMP/ca.pem" \
	"" "" 0 0 "-cert_chain $TMP/int.pem"
server_vs_openssl "server" "$TMP/ca.pem" 0 0 "" "" "olleh"
server_vs_openssl "server-mutual" "$TMP/ca.pem" 1 0 "$TMP/cl.pem" "$TMP/cl.key" "olleh"
python_client_case goodcertverify goodcertverify 0
client_vs_openssl "client-mutual" "$TMP/srv.pem" "$TMP/srv.key" "$TMP/ca.pem" \
	"$TMP/cl.pem" "$TMP/cl.key" 0 1

echo "test-crypto-tls: матрица наборов, групп и типов ключей"
SUITES_RSA="ECDHE-RSA-AES128-GCM-SHA256 ECDHE-RSA-AES256-GCM-SHA384 ECDHE-RSA-CHACHA20-POLY1305"
SUITES_EC="ECDHE-ECDSA-AES128-GCM-SHA256 ECDHE-ECDSA-AES256-GCM-SHA384 ECDHE-ECDSA-CHACHA20-POLY1305"
for s in $SUITES_RSA; do
	for g in x25519 secp256r1 secp384r1; do
		client_vs_openssl "rsa-$s-$g" "$TMP/srv.pem" "$TMP/srv.key" "$TMP/ca.pem" \
			"" "" 0 0 "" "$s" "$g"
		server_vs_openssl "rsa-$s-$g" "$TMP/ca.pem" 0 0 "" "" "olleh" "$s" "$g"
	done
done
for s in $SUITES_EC; do
	client_vs_openssl "ec256-$s" "$TMP/ec256.pem" "$TMP/ec256.key" "$TMP/ca.pem" \
		"" "" 0 0 "" "$s" secp256r1
	server_vs_openssl "ec256-$s" "$TMP/ca.pem" 0 0 "" "" "olleh" "$s" secp256r1 \
		"$TMP/ec256.pem" "$TMP/ec256.key"
	client_vs_openssl "ec384-$s" "$TMP/ec384.pem" "$TMP/ec384.key" "$TMP/ca.pem" \
		"" "" 0 0 "" "$s" secp384r1
	server_vs_openssl "ec384-$s" "$TMP/ca.pem" 0 0 "" "" "olleh" "$s" secp384r1 \
		"$TMP/ec384.pem" "$TMP/ec384.key"
done
# Взаимная проверка с ECDSA-сертификатом сервера.
client_vs_openssl "client-mutual-ecdsa" "$TMP/ec256.pem" "$TMP/ec256.key" "$TMP/ca.pem" \
	"$TMP/cl.pem" "$TMP/cl.key" 0 1 "" "ECDHE-ECDSA-AES128-GCM-SHA256" secp256r1
server_vs_openssl "server-mutual-ecdsa" "$TMP/ca.pem" 1 0 "$TMP/cl.pem" "$TMP/cl.key" "olleh" \
	"ECDHE-ECDSA-AES128-GCM-SHA256" secp256r1 "$TMP/ec256.pem" "$TMP/ec256.key"
# Клиент openssl предлагает и ECDSA-, и RSA-наборы; сервер с RSA-ключом
# обязан выбрать совместимый (проверка SuiteCompatible).
server_vs_openssl "server-auto-suite" "$TMP/ca.pem" 0 0 "" "" "olleh" "-" "-"
# Цепочка, подписанная CA хешем SHA-384/SHA-512.
client_vs_openssl "client-rsa-sha384" "$TMP/srv384.pem" "$TMP/srv384.key" "$TMP/ca.pem" \
	"" "" 0 0 "" "ECDHE-RSA-AES128-GCM-SHA256" x25519
server_vs_openssl "server-rsa-sha384" "$TMP/ca.pem" 0 0 "" "" "olleh" \
	"ECDHE-RSA-AES128-GCM-SHA256" x25519 "$TMP/srv384.pem" "$TMP/srv384.key"
client_vs_openssl "client-rsa-sha512" "$TMP/srv512.pem" "$TMP/srv512.key" "$TMP/ca.pem" \
	"" "" 0 0 "" "ECDHE-RSA-AES128-GCM-SHA256" x25519
server_vs_openssl "server-rsa-sha512" "$TMP/ca.pem" 0 0 "" "" "olleh" \
	"ECDHE-RSA-AES128-GCM-SHA256" x25519 "$TMP/srv512.pem" "$TMP/srv512.key"
client_vs_openssl "client-many-san" "$TMP/many.pem" "$TMP/many.key" "$TMP/ca.pem" \
	"" "" 0 0 "" "ECDHE-RSA-AES128-GCM-SHA256" x25519
client_vs_openssl "client-badca-skip" "$TMP/srv.pem" "$TMP/srv.key" "$TMP/badca.pem" \
	"" "" 0 0 "" "ECDHE-RSA-AES128-GCM-SHA256" x25519
client_vs_openssl "client-bigca" "$TMP/srv.pem" "$TMP/srv.key" "$TMP/bigca.pem" \
	"" "" 0 0 "" "ECDHE-RSA-AES128-GCM-SHA256" x25519

echo "test-crypto-tls: TLS 1.3 — наборы, группы, типы ключей"
SUITES13="TLS_AES_128_GCM_SHA256 TLS_AES_256_GCM_SHA384 TLS_CHACHA20_POLY1305_SHA256"
for s in $SUITES13; do
	for g in x25519 secp256r1 secp384r1; do
		client_vs_openssl13 "rsa13-$s-$g" "$TMP/srv.pem" "$TMP/srv.key" "$TMP/ca.pem" \
			"" "" 0 "$s" "$g"
		server_vs_openssl13 "rsa13-$s-$g" "$TMP/ca.pem" "$s" "$g" \
			"$TMP/srv.pem" "$TMP/srv.key"
	done
done
for s in $SUITES13; do
	for g in x25519 secp256r1; do
		client_vs_openssl13 "ec256-13-$s-$g" "$TMP/ec256.pem" "$TMP/ec256.key" \
			"$TMP/ca.pem" "" "" 0 "$s" "$g"
		server_vs_openssl13 "ec256-13-$s-$g" "$TMP/ca.pem" "$s" "$g" \
			"$TMP/ec256.pem" "$TMP/ec256.key"
	done
	for g in x25519 secp384r1; do
		client_vs_openssl13 "ec384-13-$s-$g" "$TMP/ec384.pem" "$TMP/ec384.key" \
			"$TMP/ca.pem" "" "" 0 "$s" "$g"
		server_vs_openssl13 "ec384-13-$s-$g" "$TMP/ca.pem" "$s" "$g" \
			"$TMP/ec384.pem" "$TMP/ec384.key"
	done
done
# Ed25519: CertificateVerify проверяется (подпись делает openssl).
for s in $SUITES13; do
	client_vs_openssl13 "ed25519-13-$s" "$TMP/ed.pem" "$TMP/ed.key" "$TMP/ca.pem" \
		"" "" 0 "$s" x25519
done
# Взаимная проверка TLS 1.3.
client_vs_openssl13 "client13-mutual" "$TMP/srv.pem" "$TMP/srv.key" "$TMP/ca.pem" \
	"$TMP/cl.pem" "$TMP/cl.key" 0 TLS_AES_128_GCM_SHA256 x25519 1
client_vs_openssl13 "client13-mutual-ecdsa" "$TMP/ec256.pem" "$TMP/ec256.key" \
	"$TMP/ca.pem" "$TMP/cl.pem" "$TMP/cl.key" 0 TLS_AES_128_GCM_SHA256 secp256r1 1
server_vs_openssl13 "server13-mutual" "$TMP/ca.pem" TLS_AES_128_GCM_SHA256 x25519 \
	"$TMP/srv.pem" "$TMP/srv.key" 1 "$TMP/cl.pem" "$TMP/cl.key"
server_vs_openssl13 "server13-mutual-ecdsa" "$TMP/ca.pem" TLS_AES_256_GCM_SHA384 secp256r1 \
	"$TMP/ec256.pem" "$TMP/ec256.key" 1 "$TMP/cl.pem" "$TMP/cl.key"
server_sentinel_case

echo "test-crypto-tls: отрицательные случаи"
client_vs_openssl "untrusted" "$TMP/srv.pem" "$TMP/srv.key" "" "" "" 1 0
client_vs_openssl "expired" "$TMP/exp.pem" "$TMP/exp.key" "$TMP/ca.pem" "" "" 1 0
client_vs_openssl "wrong-host" "$TMP/wh.pem" "$TMP/wh.key" "$TMP/ca.pem" "" "" 1 0
client_vs_openssl "noca" "$TMP/noca.pem" "$TMP/noca.key" "$TMP/ca.pem" "" "" 1 0 \
	"-cert_chain $TMP/badsigner.pem"
fake_case badsig
fake_case goodfinished 0
fake_case badfinished
fake_case badccs
fake_case badtag 0 1
fake_case badtagchacha 0 1
fake_case shortrec 0 1
fake_case oldversion
fake_case longfinished
fake_case longccs
fake_case zeropub
fake_case badcurve
fake_case badpoint
fake_case badpss
fake_case badecdsa 1 0 "$TMP/ec256.pem" "$TMP/ec256.key"
fake_case noske
fake_case oversize
fake_case badcert
fake_case garbage
fake_case hugerecord
fake_case badlist
# Понижение версии: сервер, умеющий 1.3, ответил 1.2 с меткой DOWNGRD.
fake_case downgrade

echo "test-crypto-tls: TLS 1.3 — KeyUpdate, HRR и отказы"
fake13_case good 0
fake13_case keyupdate 0
fake13_case hrr 0
fake13_case badcv 1
fake13_case badfinished 1
fake13_case badkeys 1
fake13_case hrrtwice 1

server_vs_openssl "server-no-client-cert" "$TMP/ca.pem" 1 1 "" "" ""
server_vs_openssl "server-foreign-client" "$TMP/ca.pem" 1 1 "$TMP/badcl.pem" "$TMP/badcl.key" ""
python_client_case badcertverify badcertverify 1
python_client_case badfinished badfinished 1
python_client_case nocertcv nocertcv 1
python_client_case oldversion oldversion 1
evil_server_case v2hello v2hello
evil_server_case v2record v2record
evil_server_case hstype hstype

echo "test-crypto-tls: TLS 1.3 — схема CertificateVerify"
# RFC 8446 §4.4.3: в CertificateVerify 1.3 RSA только PSS; схема
# rsa_pkcs1_sha256 предложена клиентом, rsa_pkcs1_sha384 — нет.
fake13_case badpkcs1 1
fake13_case badpkcs1sha384 1

# Python-клиент TLS 1.3: положительный контроль, схема и подпись.
python_client13_case good13 good 0
python_client13_case badcv13pkcs1 badpkcs1 1
python_client13_case badsig13 badsig13 1

echo "test-crypto-tls: PASS (TLS 1.3 и 1.2 в обе стороны, взаимные сертификаты, отказы)"
exit 0
