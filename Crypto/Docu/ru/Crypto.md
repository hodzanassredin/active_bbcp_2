# Crypto

`Crypto` — библиотека криптографии, перенесённая из системы ETH Oberon A2. Она даёт хеши,
аутентификацию сообщений, блочные и потоковые шифры, аутентифицированное шифрование,
арифметику больших чисел, Диффи — Хеллмана, RSA, X25519, разбор X.509 и клиент TLS 1.2. Клиент TLS
используется через `CryptoTLSStream`, протокол `CommStreams`: `Paket` выбирает его по имени
`"CryptoTLSStream"` для HTTPS.

Реализация TLS поддерживает клиентскую сторону и версию TLS 1.2. К коду A2 она добавляет обмен
ключами Диффи — Хеллмана на эллиптических кривых, расширение SNI и режим шифра GCM.

## Модули

Модули поддержки сохраняют имена исходников A2:

| Группа | Модули |
|---|---|
| Совместимость с A2 | `CryptoAosCompat`, `CryptoAosBIT`, `CryptoAosBigNumbers`, `CryptoAosRandom`, `CryptoAosStreams`, `CryptoAosStrings`, `CryptoAosRealConversions` |
| Поддержка хоста | `CryptoAosClock`, `CryptoAosDates`, `CryptoAosFileStreams`, `CryptoAosPipes`, `CryptoAosIP`, `CryptoAosCommStreams`, `CryptoAosTCP`, `CryptoTCPServices` |
| Примитивы | `CryptoUtils`, `CryptoCiphers`, `CryptoAES`, `CryptoDES`, `CryptoDES3`, `CryptoARC4`, `CryptoBlowfish`, `CryptoCAST`, `CryptoIDEA`, `CryptoGcm` |
| Хеши и MAC | `CryptoHashes`, `CryptoMD5`, `CryptoSHA1`, `CryptoSHA256`, `CryptoKeccakF1600`, `CryptoKeccakSponge`, `CryptoSHA3`, `CryptoHMAC`, `CryptoBase64` |
| Открытый ключ | `CryptoAosBigNumbers`, `CryptoPrimes`, `CryptoRSA`, `CryptoPKCS1`, `CryptoDiffieHellman`, `CryptoFieldElement`, `CryptoX25519`, `CryptoASN1`, `CryptoX509` |
| Случайные числа | `CryptoFortuna`, `CryptoFortunaRng` |
| TLS | `CryptoTLS`, `CryptoTLSStream` |

## Применение

Шифр создаётся по имени модуля, получает ключ через `InitKey`, вектор инициализации через `SetIV`,
когда он нужен режиму, и затем используется через `Encrypt` и `Decrypt`. Хеш создаётся через
`CryptoHashes.NewHash("CryptoMD5")`, наполняется через `Update` и читается через `GetHash`.

TLS доступен через интерфейс потоков так, как описано для протокола TCP в документации
`CommStreams`. Вместо имени `CommTCP` в `CommStreams.NewStream` передаётся имя `CryptoTLSStream`.
Прямое использование `CryptoTLS.Connection` не рекомендуется.

## Сборка

Подсистема входит в мир AMD64, который собирает `tools/build64.sh`. Каталога `Mod64` у неё нет:
64-битные помощники `CryptoAosCompat` написаны на Component Pascal, и один набор исходников
обслуживает цель AMD64. Компилятор i386 не собирает `CryptoAosCompat`: на вложенном делении
`LONGINT` в `AosCompat.cp` он останавливается с «not enough floating-point registers», поэтому
`Crypto` нет в `tools/build32.sh` и `tools/test32.sh`.

## Тесты

Модульные тесты лежат в `Crypto_dev`: `Crypto_devHashes`, `Crypto_devHMAC`, `Crypto_devCiphers`,
`Crypto_devBigNumbers`, `Crypto_devDH`, `Crypto_devRSA`, `Crypto_devX25519`. Их запускает
`tools/test.sh` по списку `tools/tests.txt`.

Рукопожатию TLS нужен второй процесс, поэтому у него отдельный сценарий:
`tools/test-crypto-tls.sh` создаёт самоподписанный сертификат во временном каталоге, поднимает
`openssl s_server` на `127.0.0.1`, прогоняет тест `Crypto_devTLS` и снимает сервер по PID. Модуля
`Crypto_devTLS` нет в `tools/tests.txt`, потому что без сервера он не может пройти. Клиент шлёт
`hello`, читает перевёрнутую строку от `s_server -rev` и требует успеха рукопожатия и обмена.

## Лицензия и известные ограничения

Подсистема распространяется по лицензиям BSD ETH Zurich и Hirbod Behnam; полный текст лежит в
`Crypto/LICENSE`. Обзор кода, включая отсутствие проверки сертификата сервера и подписи
`ServerKeyExchange`, находится в `Crypto/Docu/Security-Review.md`.
