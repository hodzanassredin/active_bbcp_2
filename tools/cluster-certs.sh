#!/bin/sh
# Сертификаты узлов кластера для tools/test-cluster.sh --tls.
#
#   tools/cluster-certs.sh КАТАЛОГ УЗЕЛ...
#
# Создаёт в КАТАЛОГЕ:
#   ca.pem, ca.key              — корень кластера (ECDSA P-256, CA:TRUE);
#   <узел>.pem, <узел>.key      — сертификат и ключ узла (ECDSA P-256),
#                                 имя узла — в SAN (DNS), EKU serverAuth/clientAuth;
#   <узел>.fp                   — SHA-256 сертификата (hex, 64 знака);
#   roster                      — строки "<узел> <hex-отпечаток>" для всех узлов;
#   foreign-ca.pem, foreign.key — чужой корень (для отрицательного случая);
#   foreign.pem, foreign.key    — узел, подписанный чужим корнем;
#   M-second.pem, M-second.key  — второй ключ с именем M (отпечаток не тот).
#
# Отпечаток — SHA-256 DER сертификата; его же транспорт сверяет с
# сертификатом соединения. openssl нужен только для выдачи, не для работы.
set -eu

if [ $# -lt 2 ]; then
	echo "cluster-certs: нужен каталог и хотя бы один узел" >&2
	exit 2
fi

DIR=$1
shift
OPENSSL=${OPENSSL:-openssl}
mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)

make_root() {
	key=$1 pem=$2 cn=$3
	"$OPENSSL" ecparam -name prime256v1 -genkey -noout -out "$key"
	"$OPENSSL" req -x509 -new -key "$key" -sha256 -days 2 -subj "/CN=$cn" \
		-addext "basicConstraints=critical,CA:TRUE" \
		-addext "keyUsage=critical,keyCertSign,cRLSign" \
		-out "$pem"
}

make_node_san() {
	# make_node_san SAN key pem ca cakey — имя в SAN задаётся отдельно
	san=$1 key=$2 pem=$3 ca=$4 cakey=$5
	ext=$DIR/.ext.$$
	"$OPENSSL" ecparam -name prime256v1 -genkey -noout -out "$key"
	"$OPENSSL" req -new -key "$key" -subj "/CN=$san" -out "$DIR/.csr.$$"
	{
		echo "basicConstraints=critical,CA:FALSE"
		echo "keyUsage=critical,digitalSignature"
		echo "extendedKeyUsage=serverAuth,clientAuth"
		echo "subjectAltName=DNS:$san"
	} > "$ext"
	"$OPENSSL" x509 -req -in "$DIR/.csr.$$" -CA "$ca" -CAkey "$cakey" -CAcreateserial \
		-days 2 -sha256 -extfile "$ext" -out "$pem"
	rm -f "$ext" "$DIR/.csr.$$"
}

make_node() {
	make_node_san "$1" "$2" "$3" "$4" "$5"
}

make_root "$DIR/ca.key" "$DIR/ca.pem" "BlackBox Cluster Root"
make_root "$DIR/foreign-ca.key" "$DIR/foreign-ca.pem" "Foreign Root"
make_node foreign "$DIR/foreign.key" "$DIR/foreign.pem" "$DIR/foreign-ca.pem" "$DIR/foreign-ca.key"
"$OPENSSL" x509 -in "$DIR/foreign.pem" -outform DER | "$OPENSSL" dgst -sha256 -hex | awk '{print $NF}' > "$DIR/foreign.fp"
# M-second — второй ключ с именем M в SAN и тем же корнем: отпечаток не тот.
# Имя файла не совпадает ни с одним узлом: прежнее M2 затирал узел M2
# сценария obxcluster, и в полном прогоне --tls подделка шла с настоящим M2.
make_node_san M "$DIR/M-second.key" "$DIR/M-second.pem" "$DIR/ca.pem" "$DIR/ca.key"

: > "$DIR/roster"
for node in "$@"; do
	case "$node" in
		*[!A-Za-z0-9_.-]*|'')
			echo "cluster-certs: недопустимое имя узла '$node'" >&2
			exit 2;;
	esac
	make_node "$node" "$DIR/$node.key" "$DIR/$node.pem" "$DIR/ca.pem" "$DIR/ca.key"
	fp=$("$OPENSSL" x509 -in "$DIR/$node.pem" -outform DER | "$OPENSSL" dgst -sha256 -hex | awk '{print $NF}')
	printf '%s\n' "$fp" > "$DIR/$node.fp"
	printf '%s %s\n' "$node" "$fp" >> "$DIR/roster"
done

echo "cluster-certs: $DIR, узлов $(wc -l < "$DIR/roster")"
