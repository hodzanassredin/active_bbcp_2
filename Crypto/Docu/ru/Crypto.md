# Crypto

`Crypto` — библиотека криптографии, перенесённая из системы ETH Oberon A2. Она даёт хеши,
аутентификацию сообщений, блочные и потоковые шифры, аутентифицированное шифрование,
арифметику больших чисел, Диффи — Хеллмана, RSA, X25519, разбор X.509 и клиент TLS 1.2. Клиент TLS
используется через `CryptoTLSStream`, протокол `CommStreams`: `Paket` выбирает его по имени
`"CryptoTLSStream"` для HTTPS.

В подсистеме есть и клиент, и сервер TLS 1.2. Клиент проверяет цепочку сертификата
сервера до настроенного доверенного корня, срок действия и имя сервера (SAN/CN), а также
подпись `ServerKeyExchange`. Сервер может запросить и проверить клиентский сертификат; на
этом держится взаимная проверка сертификатов узлов кластера.

## Модули

Модули поддержки сохраняют имена исходников A2:

| Группа | Модули |
|---|---|
| Совместимость с A2 | `CryptoAosCompat`, `CryptoAosBIT`, `CryptoAosBigNumbers`, `CryptoAosRandom`, `CryptoAosStreams`, `CryptoAosStrings`, `CryptoAosRealConversions` |
| Поддержка хоста | `CryptoAosClock`, `CryptoAosDates`, `CryptoAosFileStreams`, `CryptoAosPipes`, `CryptoAosIP`, `CryptoAosCommStreams`, `CryptoAosTCP`, `CryptoTCPServices` |
| Примитивы | `CryptoUtils`, `CryptoCiphers`, `CryptoAES`, `CryptoDES`, `CryptoDES3`, `CryptoARC4`, `CryptoBlowfish`, `CryptoCAST`, `CryptoIDEA`, `CryptoGcm` |
| Хеши и MAC | `CryptoHashes`, `CryptoMD5`, `CryptoSHA1`, `CryptoSHA256`, `CryptoKeccakF1600`, `CryptoKeccakSponge`, `CryptoSHA3`, `CryptoHMAC`, `CryptoBase64` |
| Открытый ключ | `CryptoAosBigNumbers`, `CryptoPrimes`, `CryptoRSA`, `CryptoPKCS1`, `CryptoDiffieHellman`, `CryptoFieldElement`, `CryptoX25519`, `CryptoASN1`, `CryptoX509` |
| Случайные числа | `CryptoOsRandom`, `CryptoFortuna`, `CryptoFortunaRng` |
| TLS | `CryptoTLS`, `CryptoTLSStream` |

## Применение

Шифр создаётся по имени модуля, получает ключ через `InitKey`, вектор инициализации через `SetIV`,
когда он нужен режиму, и затем используется через `Encrypt` и `Decrypt`. Хеш создаётся через
`CryptoHashes.NewHash("CryptoMD5")`, наполняется через `Update` и читается через `GetHash`.

TLS доступен через интерфейс потоков так, как описано для протокола TCP в документации
`CommStreams`. Вместо имени `CommTCP` в `CommStreams.NewStream` передаётся имя `CryptoTLSStream`.
Прямое использование `CryptoTLS.Connection` не рекомендуется.

Прямой интерфейс даёт управление доверенными корнями и клиентским сертификатом:

```componentpascal
ctx := CryptoTLS.NewContext();
ctx.AddTrustedFile("ca.pem");                 (* корень кластера *)
ctx.LoadClientCertificateFile("node.pem");    (* необязательно, для mutual auth *)
ctx.LoadClientPrivateKeyFile("node.key");
con := CryptoTLS.GetConnection();
con.SetContext(ctx);
con.Open("0.0.0.0:0", "host:443", res)
```

Сервер загружает свой сертификат и ключ, а при взаимной проверке — и корень, которым
проверяются клиентские сертификаты:

```componentpascal
ctx := CryptoTLS.NewContext();
ctx.LoadCertificateFile("server.pem");
ctx.LoadPrivateKeyFile("server.key");
ctx.AddTrustedFile("ca.pem");
ctx.RequireClientCertificate(TRUE);
srv := CryptoTLS.GetConnection();
srv.SetContext(ctx);
srv.Open("0.0.0.0:443", "", res);             (* слушает *)
srv.Accept(client, res);                       (* завершает рукопожатие *)
```

`CryptoTLS.ReadFile` читает PEM по абсолютному пути; `X509.ReadPemChain` и
`X509.ReadPemPrivateKey` разбирают PEM без файловой системы.

## Безопасность

- В наборе шифров по умолчанию только `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256`. CBC, 3DES,
  RC4, NULL и RSA key transport остаются в коде и включаются явно через
  `Policy.SetCipherSuites`; по умолчанию они не предлагаются.
- Клиент проверяет цепочку до доверенного корня, срок действия каждого звена (включая
  корень), `basicConstraints CA`, `keyUsage keyCertSign` (если расширение есть),
  `pathLenConstraint` и неизвестные критичные расширения. Сертификат с тем же именем, что
  доверенный корень, но другим ключом, незавершённый путь и цепочка длиннее
  `MaxChainDepth` отвергаются. Имя сверяется с SAN `dNSName`/`iPAddress`; CN используется
  только при отсутствии SAN. IP-адрес не совпадает с DNS-именем и наоборот; шаблон `*`
  совпадает ровно с одной меткой и не допускается над доменом верхнего уровня (`*.com`).
- `ServerKeyExchange` подписан RSA PKCS#1 v1.5/SHA-256; клиент пересчитывает подпись по
  `client_random || server_random || ECDHParams`. Чужой корень, просроченный сертификат,
  неверное имя или подделанная подпись прерывают рукопожатие алертом.
- Сервер, требующий клиентский сертификат, шлёт `CertificateRequest` и проверяет цепочку и
  `CertificateVerify` клиента до приёма его `Finished`.
- Отвергнутые `Finished`, `ChangeCipherSpec` с неверным значением и запись с плохим тегом
  GCM закрывают соединение: `ReceiveFinished` возвращает результат, и состояние
  рукопожатия не переводится в `Established`.
- Секреты сравниваются через `CryptoUtils.EqualBytesCT`: MAC записи, тег GCM, `Finished` и
  PKCS#1-блок не сравниваются с ранним выходом.
- Ключи и nonce берутся из `CryptoOsRandom` (`/dev/urandom`); генератор Fortuna подмешивает
  свежую энтропию ОС на каждый запрос. PKCS#1-дополнение и TLS pre-master при недоступном
  источнике завершаются ошибкой, а не предсказуемыми байтами. Генерация простых чисел
  (`CryptoAosBigNumbers.NewRand`) читает тот же источник, но не умеет сообщить об ошибке
  через свою сигнатуру; это отмечено в обзоре как оставшееся ограничение.
- Длины из сети проверяются до копирования; некорректные данные рукопожатия дают алерт и
  закрытое соединение, а не `ASSERT`. Там, где последовательность зависит от выбора второй стороны, тип
  handshake-сообщения сверяется с состоянием; неизвестный тип даёт алерт. SSLv2 `ClientHello`
  (и запись типа 128, и сообщение типа 30) отвергается.

## Ограничения

- TLS 1.3 не реализован.
- Сертификаты только с RSA-ключами; обмен ключами только X25519, других named groups нет.
- CRL, OCSP и certificate transparency не проверяются. Решение о доверии — настроенный
  набор корней.
- Арифметика больших чисел (закрытые операции RSA) и X25519 не постоянного времени на
  уровне машины. В лестнице X25519 условная перестановка без ветвлений, но возведение в
  степень по модулю в `CryptoAosBigNumbers` зависит от данных. Это отмечено в обзоре
  безопасности и приемлемо для модели угроз кластера; для общего сервера, доступного
  локальному атакующему с высоким разрешением таймингов, — нет.
- Возобновление сессий и пересогласование поддержаны лишь в объёме исходного кода A2;
  серверных тикетов TLS 1.3 нет.
- Сообщение рукопожатия должно помещаться в одну запись TLS; фрагментация по нескольким
  записям отвергается. У сервера нет таймаута рукопожатия: молчащий клиент занимает `Accept`.
- Системного хранилища корневых CA нет. `CryptoTLSStream` использует контекст по умолчанию и
  потому отвергает любой сертификат, пока вызывающий не настроит доверенные корни через прямой
  интерфейс `CryptoTLS.NewContext`.

## Сборка

Подсистема входит в мир AMD64, который собирает `tools/build64.sh`. Каталога `Mod64` у неё нет:
64-битные помощники `CryptoAosCompat` написаны на Component Pascal, и один набор исходников
обслуживает цель AMD64. Компилятор i386 не собирает `CryptoAosCompat`: на вложенном делении
`LONGINT` в `AosCompat.cp` он останавливается с «not enough floating-point registers», поэтому
`Crypto` нет в `tools/build32.sh` и `tools/test32.sh`.

## Тесты

Модульные тесты лежат в `Crypto_dev`: `Crypto_devHashes`, `Crypto_devHMAC`, `Crypto_devCiphers`,
`Crypto_devGcm`, `Crypto_devBigNumbers`, `Crypto_devDH`, `Crypto_devRSA`, `Crypto_devX25519`,
`Crypto_devX509`, `Crypto_devPKCS1`, `Crypto_devPRF`. Их запускает `tools/test.sh` по списку
`tools/tests.txt`. Эталонные значения также лежат файлами с указанием источника и лицензии в
`Crypto_dev/Vectors`; `Crypto_dev/Vectors/check.py` сверяет их независимой реализацией на Python.

Рукопожатию TLS нужен второй процесс, поэтому у него отдельный сценарий:
`tools/test-crypto-tls.sh` создаёт во временном каталоге CA, промежуточный CA, издателя с
`CA:FALSE`, серверный, клиентский, просроченные сертификаты и сертификат с чужим именем,
гоняет клиент против `openssl s_server -rev`, сервер против `openssl s_client`, проверяет
взаимную аутентификацию в обе стороны и требует отказа (без трапа процесса) для чужого CA,
просроченного сертификата, промежуточного или корня, неверного имени, издателя с `CA:FALSE`,
подделанной подписи `ServerKeyExchange`, плохого `Finished`, неверного `ChangeCipherSpec`,
испорченного тега GCM, чрезмерной длины подписи, неверной длины сертификата, невозможной
длины расширений, отсутствующего `ServerKeyExchange`, нулевого открытого ключа X25519,
SSLv2 `ClientHello` и неизвестного типа рукопожатия. `tools/crypto_tls_lib.py`,
`crypto_tls_fake.py` и `crypto_tls_client.py` — Python-партнёры для случаев, где нужен
корректный транскрипт рукопожатия: плохой `Finished`, плохой `ChangeCipherSpec`, испорченный
тег записи, нулевая точка X25519 и плохая или хорошая подпись `CertificateVerify`.
Модулей `Crypto_devTLS` и `Crypto_devTLSServer` нет в `tools/tests.txt`, потому что без
второго процесса они не проходят.


## Лицензия и известные ограничения

Подсистема распространяется по лицензиям BSD ETH Zurich и Hirbod Behnam; полный текст лежит в
`Crypto/LICENSE`. Обзор безопасности со статусом каждой находки и покрывающим её тестом — в
`Crypto/Docu/Security-Review.md`.
