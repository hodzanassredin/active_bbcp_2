#!/bin/sh
# Сертификаты для проверки цепочки X.509 (Crypto_devX509).
#
#   Crypto_dev/Vectors/x509/generate-chains.sh [каталог]
#
# Каталог по умолчанию — chains рядом со скриптом. Скрипт пересоздаёт
# файлы; после этого значения нужно перенести в Crypto_dev/Mod/X509Data.cp
# (PEM целиком, без изменений). Ключи остаются в каталоге и в репозиторий
# не входят: тестам нужны только сертификаты.
#
# Набор:
#   R    — доверенный корень;
#   I, L — промежуточный CA (pathlen:1) и лист;
#   S, V — издатель с CA:FALSE и подписанный им лист для чужого имени;
#   NB, LB — промежуточный без basicConstraints и его лист;
#   KU, LK — CA:TRUE без keyCertSign и его лист;
#   P0, P2, LP — pathlen:0, под ним ещё один CA и лист;
#   CU, LC — CA с неизвестным критичным расширением и его лист;
#   EXP, LE — просроченный промежуточный CA и его лист;
#   FR, LF — поддельный корень с тем же именем и его лист;
#   CA, CB, CYC — цикл A <- B <- A и лист.
set -eu

OPENSSL="${OPENSSL:-openssl}"
OUT="${1:-$(dirname "$0")/chains}"
mkdir -p "$OUT"
cd "$OUT"
rm -f ./*.pem ./*.csr ./*.ext ./*.srl

genkey() { "$OPENSSL" genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$1" 2>/dev/null; }
csr() { "$OPENSSL" req -new -key "$1" -out "$2" -subj "$3" 2>/dev/null; }
selfsign() {
	"$OPENSSL" req -x509 -key "$1" -out "$2" -days 3650 -subj "$3" \
		-addext "basicConstraints=critical,CA:TRUE" \
		-addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
}
sign() { # key csr cacert cakey out days extfile
	"$OPENSSL" x509 -req -in "$2" -CA "$3" -CAkey "$4" -CAcreateserial \
		-out "$5" -days "$6" -extfile "$7" 2>/dev/null
}

genkey R.key
selfsign R.key R.pem "/CN=Crypto Chain Root"

genkey I.key; csr I.key I.csr "/CN=Crypto Chain Intermediate"
printf 'basicConstraints=critical,CA:TRUE,pathlen:1\nkeyUsage=critical,keyCertSign,cRLSign\n1.3.6.1.4.1.99999.2=DER:05:00\n' >I.ext
sign I.key I.csr R.pem R.key I.pem 3650 I.ext
genkey L.key; csr L.key L.csr "/CN=chain.example"
printf 'subjectAltName=DNS:chain.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n' >L.ext
sign L.key L.csr I.pem I.key L.pem 3650 L.ext

genkey CNL.key; csr CNL.key CNL.csr "/CN=cn.example"
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >CNL.ext
sign CNL.key CNL.csr R.pem R.key CNL.pem 3650 CNL.ext
genkey CNW.key; csr CNW.key CNW.csr "/CN=*.example.org"
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >CNW.ext
sign CNW.key CNW.csr R.pem R.key CNW.pem 3650 CNW.ext
genkey CNS.key; csr CNS.key CNS.csr "/CN=cn.example"
printf 'subjectAltName=DNS:other.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >CNS.ext
sign CNS.key CNS.csr R.pem R.key CNS.pem 3650 CNS.ext

genkey S.key; csr S.key S.csr "/CN=Crypto Bad Signer"
printf 'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >S.ext
sign S.key S.csr R.pem R.key S.pem 3650 S.ext
genkey V.key; csr V.key V.csr "/CN=victim.example"
printf 'subjectAltName=DNS:victim.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n' >V.ext
sign V.key V.csr S.pem S.key V.pem 3650 V.ext

genkey NB.key; csr NB.key NB.csr "/CN=Crypto No BC"
printf 'keyUsage=critical,keyCertSign,cRLSign\n' >NB.ext
sign NB.key NB.csr R.pem R.key NB.pem 3650 NB.ext
genkey LB.key; csr LB.key LB.csr "/CN=nobc.example"
printf 'subjectAltName=DNS:nobc.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >LB.ext
sign LB.key LB.csr NB.pem NB.key LB.pem 3650 LB.ext

genkey KU.key; csr KU.key KU.csr "/CN=Crypto KU No CertSign"
printf 'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature\n' >KU.ext
sign KU.key KU.csr R.pem R.key KU.pem 3650 KU.ext
genkey LK.key; csr LK.key LK.csr "/CN=kuno.example"
printf 'subjectAltName=DNS:kuno.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >LK.ext
sign LK.key LK.csr KU.pem KU.key LK.pem 3650 LK.ext

genkey P0.key; csr P0.key P0.csr "/CN=Crypto PathLen0"
printf 'basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n' >P0.ext
sign P0.key P0.csr R.pem R.key P0.pem 3650 P0.ext
genkey P2.key; csr P2.key P2.csr "/CN=Crypto SubCA"
printf 'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n' >P2.ext
sign P2.key P2.csr P0.pem P0.key P2.pem 3650 P2.ext
genkey LP.key; csr LP.key LP.csr "/CN=pathlen.example"
printf 'subjectAltName=DNS:pathlen.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >LP.ext
sign LP.key LP.csr P2.pem P2.key LP.pem 3650 LP.ext
genkey PD.key; csr PD.key PD.csr "/CN=pathlen-direct.example"
printf 'subjectAltName=DNS:pathlen-direct.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >PD.ext
sign PD.key PD.csr P0.pem P0.key PD.pem 3650 PD.ext

genkey CU.key; csr CU.key CU.csr "/CN=Crypto Critical Unknown"
printf 'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n1.3.6.1.4.1.99999.1=critical,DER:05:00\n' >CU.ext
sign CU.key CU.csr R.pem R.key CU.pem 3650 CU.ext
genkey LC.key; csr LC.key LC.csr "/CN=crit.example"
printf 'subjectAltName=DNS:crit.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >LC.ext
sign LC.key LC.csr CU.pem CU.key LC.pem 3650 LC.ext
# Лист с неизвестным критичным расширением: проверка нужна и на листе.
genkey CL.key; csr CL.key CL.csr "/CN=critleaf.example"
printf 'subjectAltName=DNS:critleaf.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n1.3.6.1.4.1.99999.3=critical,DER:05:00\n' >CL.ext
sign CL.key CL.csr R.pem R.key CL.pem 3650 CL.ext

# Просроченный доверенный корень: сроки проверяются и у якоря.
genkey XR.key; csr XR.key XR.csr "/CN=Crypto Expired Root"
printf 'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n' >XR.ext
"$OPENSSL" x509 -req -in XR.csr -signkey XR.key -out XR.pem -days 0 -extfile XR.ext 2>/dev/null
genkey XL.key; csr XL.key XL.csr "/CN=expiredroot.example"
printf 'subjectAltName=DNS:expiredroot.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >XL.ext
sign XL.key XL.csr XR.pem XR.key XL.pem 3650 XL.ext

genkey EXP.key; csr EXP.key EXP.csr "/CN=Crypto Expired Intermediate"
printf 'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n' >EXP.ext
sign EXP.key EXP.csr R.pem R.key EXP.pem 0 EXP.ext
genkey LE.key; csr LE.key LE.csr "/CN=expiredint.example"
printf 'subjectAltName=DNS:expiredint.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >LE.ext
sign LE.key LE.csr EXP.pem EXP.key LE.pem 3650 LE.ext

genkey FR.key
selfsign FR.key FR.pem "/CN=Crypto Chain Root"
genkey LF.key; csr LF.key LF.csr "/CN=forged.example"
printf 'subjectAltName=DNS:forged.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >LF.ext
sign LF.key LF.csr FR.pem FR.key LF.pem 3650 LF.ext

genkey CA.key; csr CA.key CA.csr "/CN=Cycle A"
genkey CB.key; csr CB.key CB.csr "/CN=Cycle B"
# B сначала самоподписан тем же ключом, которым потом подпишет A:
# так A и B ссылаются друг на друга и путь замыкается.
selfsign CB.key BT.pem "/CN=Cycle B"
printf 'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n' >CA.ext
sign CA.key CA.csr BT.pem CB.key CA.pem 3650 CA.ext
printf 'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n' >CB.ext
sign CB.key CB.csr CA.pem CA.key CB.pem 3650 CB.ext
genkey CYC.key; csr CYC.key CYC.csr "/CN=cycle.example"
printf 'subjectAltName=DNS:cycle.example\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n' >CYC.ext
sign CYC.key CYC.csr CA.pem CA.key CYC.pem 3650 CYC.ext

rm -f ./*.key ./*.csr ./*.ext ./*.srl BT.pem
echo "сертификаты в $OUT"
