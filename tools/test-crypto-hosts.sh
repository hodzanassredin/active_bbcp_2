#!/bin/sh
# Ручной прогон TLS-клиента по публичным HTTPS-хостам (нужна сеть).
#
#   tools/iso.sh tools/test-crypto-hosts.sh
#   CRYPTO_HOSTS="example.com,example.org:443" tools/iso.sh tools/test-crypto-hosts.sh
#
# Печатает для каждого хоста версию, набор, группу ECDHE, схему подписи и
# первую строку ответа HTTP. Не входит в tools/test.sh.
set -u
. "$(dirname "$0")/env.sh"

HOSTS="${CRYPTO_HOSTS:-google.com,cloudflare.com,github.com,letsencrypt.org,wikipedia.org,microsoft.com,apple.com,amazon.com}"
CA="${CRYPTO_CA:-/etc/ssl/certs/ca-certificates.crt}"

if [ ! -x "$INTERP" ]; then
	echo "test-crypto-hosts: нет $INTERP — соберите tools/build64.sh" >&2
	exit 2
fi

"$BB/tools/go64.sh" Crypto_devHosts >/tmp/crypto-hosts-build.log 2>&1 || {
	tail -20 /tmp/crypto-hosts-build.log >&2
	echo "test-crypto-hosts: Crypto_devHosts не собрался" >&2
	exit 2
}

cd "$INSTALL" || exit 2
printf 'Crypto_devHosts.Do\n' |
	LD_LIBRARY_PATH="$INSTALL" timeout 300 "$INTERP" \
		--CRYPTO_HOSTS="$HOSTS" --CRYPTO_CA="$CA"
