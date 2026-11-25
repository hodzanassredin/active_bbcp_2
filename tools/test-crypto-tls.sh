#!/bin/sh
# Рукопожатие TLS 1.2 с локальным openssl s_server.
#
#   tools/test-crypto-tls.sh
#
# Создаёт самоподписанный сертификат во временном каталоге, поднимает
# s_server на 127.0.0.1 со случайным свободным портом и гоняет
# Crypto_devTLS: клиент CryptoTLSStream шлёт "hello", сервер с -rev
# возвращает "olleh". Сервер снимается по PID. В сеть наружу сценарий
# не ходит. Запускать через tools/iso.sh, как остальные сборки.
set -u
. "$(dirname "$0")/env.sh"

OPENSSL="${OPENSSL:-/usr/bin/openssl}"

if [ ! -x "$OPENSSL" ]; then
	echo "SKIP Crypto_devTLS: нет $OPENSSL"
	exit 0
fi
if [ ! -x "$INTERP" ]; then
	echo "test-crypto-tls: нет $INTERP — соберите tools/build64.sh" >&2
	exit 2
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/crypto-tls.XXXXXX") || exit 2
pid=""
cleanup() {
	if [ -n "$pid" ]; then
		kill "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
	fi
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

if ! "$OPENSSL" req -x509 -newkey rsa:2048 -keyout "$TMP/key.pem" -out "$TMP/cert.pem" \
		-days 1 -nodes -subj "/CN=127.0.0.1" \
		-addext "subjectAltName=IP:127.0.0.1" >"$TMP/req.log" 2>&1; then
	echo "test-crypto-tls: не удалось создать сертификат:" >&2
	cat "$TMP/req.log" >&2
	exit 1
fi

# Свободный порт: ядро выдаёт его и сразу освобождает; окно гонки мало.
port=$(python3 -I -c 'import socket
s=socket.socket()
s.bind(("127.0.0.1",0))
print(s.getsockname()[1])
s.close()') || { echo "test-crypto-tls: нет python3 для выбора порта" >&2; exit 2; }

"$OPENSSL" s_server -accept "127.0.0.1:$port" -cert "$TMP/cert.pem" -key "$TMP/key.pem" \
	-tls1_2 -cipher 'ECDHE-RSA-AES128-GCM-SHA256' -rev >"$TMP/server.log" 2>&1 &
pid=$!

ready=0
i=0
while [ $i -lt 100 ]; do
	if python3 -I -c "import socket,sys
s=socket.socket()
s.settimeout(0.2)
try:
	s.connect(('127.0.0.1',$port))
except OSError:
	sys.exit(1)
s.close()"; then
		ready=1
		break
	fi
	if ! kill -0 "$pid" 2>/dev/null; then break; fi
	i=$((i + 1))
	sleep 0.1
done
if [ $ready -ne 1 ]; then
	echo "test-crypto-tls: s_server не поднялся:" >&2
	cat "$TMP/server.log" >&2
	exit 1
fi

echo "test-crypto-tls: сервер на 127.0.0.1:$port"
if ! "$BB/tools/go64.sh" Crypto_devTLS >"$TMP/compile.log" 2>&1; then
	echo "test-crypto-tls: Crypto_devTLS не скомпилировался:" >&2
	tail -20 "$TMP/compile.log" >&2
	exit 1
fi

client_cmd='cd "$1" && LD_LIBRARY_PATH="$1${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec "$2" "$3"'

if command -v /usr/bin/time >/dev/null 2>&1; then
	printf 'UnitRunner.Run Crypto_devTLS proc quit\n' \
		| /usr/bin/time -f 'client cpu: user=%U sys=%S wall=%e s' -o "$TMP/time.txt" \
			sh -c "$client_cmd" sh "$INSTALL" "$INTERP" "--CRYPTO_TLS_ADDR=127.0.0.1:$port" \
			>"$TMP/client.log" 2>&1
else
	printf 'UnitRunner.Run Crypto_devTLS proc quit\n' \
		| sh -c "$client_cmd" sh "$INSTALL" "$INTERP" "--CRYPTO_TLS_ADDR=127.0.0.1:$port" \
			>"$TMP/client.log" 2>&1
fi
rc=$?
grep -E '^(PASS|FAIL|EMPTY|== Unit)' "$TMP/client.log" || true
[ -f "$TMP/time.txt" ] && cat "$TMP/time.txt"
grep -m1 'CIPHER is' "$TMP/server.log" || true

if [ $rc -ne 0 ] || ! grep -q 'failed 0,' "$TMP/client.log"; then
	echo "test-crypto-tls: FAIL" >&2
	tail -20 "$TMP/client.log" >&2
	exit 1
fi
echo "test-crypto-tls: PASS (TLS 1.2 handshake and echo)"
exit 0
