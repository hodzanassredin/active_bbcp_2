# Crypto

`Crypto` — библиотека криптографии, перенесённая из системы ETH Oberon A2. Она даёт хеши,
аутентификацию сообщений, блочные и потоковые шифры, аутентифицированное шифрование,
арифметику больших чисел, Диффи — Хеллмана, RSA, X25519, разбор X.509 и клиент и сервер TLS 1.3
и 1.2. Клиент TLS используется через `CryptoTLSStream`, протокол `CommStreams`: `Paket`
выбирает его по имени `"CryptoTLSStream"` для HTTPS.

Клиент проверяет цепочку сертификата сервера до настроенного доверенного корня, срок
действия и имя сервера (SAN/CN), а в TLS 1.2 — ещё и подпись `ServerKeyExchange`; в TLS 1.3
подпись `CertificateVerify` проверяется по контекстной строке и хешу транскрипта. Сервер
может запросить и проверить клиентский сертификат; на этом держится взаимная проверка
сертификатов узлов кластера. Политика по умолчанию предлагает TLS 1.3, затем TLS 1.2 с
ECDHE и AEAD; сервер выбирает 1.3, если клиент его предложил.

## Модули

Модули поддержки сохраняют имена исходников A2:

| Группа | Модули |
|---|---|
| Совместимость с A2 | `CryptoAosCompat`, `CryptoAosBIT`, `CryptoAosBigNumbers`, `CryptoAosRandom`, `CryptoAosStreams`, `CryptoAosStrings`, `CryptoAosRealConversions` |
| Поддержка хоста | `CryptoAosClock`, `CryptoAosDates`, `CryptoAosFileStreams`, `CryptoAosPipes`, `CryptoAosIP`, `CryptoAosCommStreams`, `CryptoAosTCP`, `CryptoTCPServices` |
| Примитивы | `CryptoUtils`, `CryptoCiphers`, `CryptoAES`, `CryptoDES`, `CryptoDES3`, `CryptoARC4`, `CryptoBlowfish`, `CryptoCAST`, `CryptoIDEA`, `CryptoGcm`, `CryptoChaCha20` |
| Хеши и MAC | `CryptoHashes`, `CryptoMD5`, `CryptoSHA1`, `CryptoSHA256`, `CryptoSHA384`, `CryptoSHA512`, `CryptoKeccakF1600`, `CryptoKeccakSponge`, `CryptoSHA3`, `CryptoHMAC`, `CryptoHKDF`, `CryptoBase64` |
| Открытый ключ | `CryptoAosBigNumbers`, `CryptoPrimes`, `CryptoRSA`, `CryptoPKCS1`, `CryptoDiffieHellman`, `CryptoFieldElement`, `CryptoX25519`, `CryptoEC`, `CryptoECDSA`, `CryptoEd25519`, `CryptoPSS`, `CryptoASN1`, `CryptoX509` |
| Случайные числа | `CryptoOsRandom`, `CryptoFortuna`, `CryptoFortunaRng` |
| TLS | `CryptoTLS`, `CryptoTLSStream` |

## Применение

Шифр создаётся по имени модуля, получает ключ через `InitKey`, вектор инициализации через `SetIV`,
когда он нужен режиму, и затем используется через `Encrypt` и `Decrypt`. Хеш создаётся через
`CryptoHashes.NewHash("CryptoMD5")`, наполняется через `Update` и читается через `GetHash`.
Семейство SHA-2 включает SHA-256, SHA-384 и SHA-512; имя хеша задаёт длину результата.

Вывод ключа выполняет `CryptoHKDF`. `Extract` превращает исходный ключевой материал и соль в
псевдослучайный ключ, `Expand` растягивает его до нужной длины со строкой контекста, `HKDF`
делает оба шага:

```componentpascal
CryptoHKDF.Extract("CryptoSHA256", salt, saltLen, ikm, ikmLen, prk);
ok := CryptoHKDF.Expand("CryptoSHA256", prk, 32, info, infoLen, okm, okmLen)
```

`CryptoHMAC` использует размер блока лежащего в основе хеша: ключ длиннее блока хешируется один
раз, а внутренние и внешние дополнения имеют размер блока хеша.

`CryptoGcm` работает с AES-128 и AES-256: `InitKey(key, 256)` выбирает расписание на 14 раундов.
`CryptoChaCha20` даёт ChaCha20, Poly1305 и общий AEAD `ChaCha20-Poly1305` по RFC 8439. AEAD
принимает ключ 32 байта, nonce 12 байт и дополнительные данные, а возвращает тег 16 байт:

```componentpascal
CryptoChaCha20.Encrypt(key, nonce, aad, aadLen, plain, plainLen, cipher, tag);
ok := CryptoChaCha20.Decrypt(key, nonce, aad, aadLen, cipher, plainLen, tag, plain)
```

Ключи на эллиптических кривых обслуживает `CryptoEC` для P-256 и P-384. Точка читается из
несжатой формы SEC1 (`0x04 || X || Y`), проверяется и только потом используется; ECDH
возвращает x-координату общей точки:

```componentpascal
c := CryptoEC.P256();
ok := CryptoEC.PointFromBytes(c, peer, 65, qx, qy);
ok := CryptoEC.ECDH(c, privateKey, qx, qy, sharedX)
```

`CryptoECDSA` подписывает и проверяет хешем своей кривой (SHA-256 для P-256, SHA-384 для P-384).
Одноразовое число детерминировано (RFC 6979): те же ключ и хеш дают те же `r` и `s`; `r` и `s` —
значения фиксированной длины в порядке байтов от старшего.

```componentpascal
ok := CryptoECDSA.Sign(c, privateKey, hash, hashLen, r, s);
ok := CryptoECDSA.Verify(c, qx, qy, hash, hashLen, r, s)
```

`CryptoPSS` подписывает и проверяет RSASSA-PSS с солью длины хеша и MGF1 по тому же хешу.
Подпись — строка байтов длины модуля; хеш передаётся уже посчитанным:

```componentpascal
CryptoPSS.SignHash("CryptoSHA256", digest, 32, privateKey, sig, 0, sigLen);
ok := CryptoPSS.VerifyHash("CryptoSHA256", digest, 32, publicKey, sig, 0, sigLen)
```

`CryptoEd25519.Verify` проверяет подпись Ed25519 (открытый ключ 32 байта, подпись 64 байта) по
сообщению. Создание подписи не реализовано; для сертификатов и TLS нужна именно проверка:

```componentpascal
ok := CryptoEd25519.Verify(publicKey, message, messageLen, signature)
```

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
`X509.ReadPemPrivateKey` разбирают PEM без файловой системы. `X509.ReadPemEcPrivateKey`
читает закрытый ключ ECDSA из SEC1 (`EC PRIVATE KEY`) или PKCS#8 (`PRIVATE KEY`) и возвращает
кривую и скаляр; им пользуются `Context.LoadEcPrivateKeyFile` и
`LoadClientEcPrivateKeyFile` для наборов ECDHE-ECDSA.

## Безопасность

- Клиент и сервер TLS 1.3 (RFC 8446) работают с наборами `TLS_AES_128_GCM_SHA256`,
  `TLS_AES_256_GCM_SHA384` и `TLS_CHACHA20_POLY1305_SHA256`. Ключевое расписание целиком на
  HKDF: `HKDF-Extract` и `HKDF-Expand-Label` с метками `derived`, `c hs traffic`,
  `s hs traffic`, `c ap traffic`, `s ap traffic`, `finished`, `key`, `iv` и `traffic upd`.
  Хеш расписания — SHA-256 или SHA-384 по набору. Клиент шлёт `key_share` для X25519, а после
  `HelloRetryRequest` — для secp256r1 или secp384r1; сервер принимает долю для любой из трёх
  групп. `supported_versions` содержит 1.3 и 1.2.
- В TLS 1.3 `CertificateVerify` подписывает 64 пробела, строку
  `TLS 1.3, server CertificateVerify` (или `client`), нулевой байт и хеш транскрипта без
  самого `CertificateVerify`. Принимаются `rsa_pss_rsae_sha256/384`,
  `ecdsa_secp256r1_sha256`, `ecdsa_secp384r1_sha384` и `ed25519`; Ed25519 проверяется, но
  не подписывается. RSA PKCS#1 v1.5 отвергается, даже когда `rsa_pkcs1_sha256` назван в
  `signature_algorithms`: RFC 8446 §4.4.3 определяет его только для подписей сертификатов.
  Схема, которую вторая сторона не предлагала, тоже отвергается. `Finished` — HMAC с ключом
  `finished` по хешу транскрипта; клиентский `Finished` считается по транскрипту вместе с
  серверным, application-секреты — тоже.
- `HelloRetryRequest` распознаётся по случайному значению `SHA-256("HelloRetryRequest")`;
  клиент повторяет `ClientHello` с той же случайной строкой и session id, но с `key_share`
  запрошенной группы, второй HRR отвергается. Сервер при выборе группы сначала ищет
  присланный `key_share`, иначе отвечает HRR, а на второй `ClientHello` проверяет, что набор
  и группа не изменились.
- Сигнал понижения RFC 8446 §4.1.3: сервер, умеющий 1.3, но согласовавший 1.2, ставит в
  последние 8 байт `ServerHello.random` `DOWNGRD\x01` (`\x00` для 1.1 и ниже); клиент,
  предлагавший 1.3, при виде этой метки прерывает рукопожатие с `illegal_parameter`.
  Отсутствие метки — нормальный ответ сервера, который умеет только 1.2.
- `KeyUpdate` принимается после рукопожатия: секрет `traffic upd` обновляет ключи чтения, а
  на `request_update=1` клиент или сервер сначала отправляет свой `KeyUpdate` под старыми
  ключами и затем обновляет ключи записи. Номер записи сбрасывается на каждой эпохе.
- Записи TLS 1.3 идут во внешнем `application_data`: открытый текст — данные || внутренний
  тип || нулевое дополнение, nonce — `iv XOR seq`, дополнительные данные — внешний тип,
  версия `0303` и длина шифртекста с тегом. Внутренний тип — последний ненулевой байт;
  запись из одних нулей типа не содержит и отвергается. Запись короче тега AEAD плюс байт
  типа — ошибка декодирования; пустая запись приложения законна и не несёт байтов. Тег GCM
  и ChaCha20-Poly1305 сравнивается постоянного времени.
- 0-RTT и PSK не реализованы: `pre_shared_key` и `early_data` не отправляются, сервер их не
  принимает, `NewSessionTicket` игнорируется (сессия не возобновляется).
- Клиент и сервер TLS 1.2 используют ECDHE на X25519, secp256r1 или secp384r1 и AEAD-записи.
  Политика по умолчанию предлагает ECDHE-ECDSA и ECDHE-RSA с AES-128-GCM-SHA256,
  AES-256-GCM-SHA384 и ChaCha20-Poly1305-SHA256 (шесть наборов). CBC, 3DES, RC4, NULL и RSA
  key transport остаются в коде и включаются явно через `Policy.SetCipherSuites`; по умолчанию
  они не предлагаются.
- Клиент шлёт `signature_algorithms` с ECDSA P-256/P-384, RSA-PSS с SHA-256/384, RSA PKCS#1
  v1.5 SHA-256 и Ed25519 (только проверка), `supported_groups` с тремя кривыми,
  `ec_point_formats` (uncompressed) и пустой `renegotiation_info`. Сервер выбирает набор,
  совместимый со своим сертификатом, схему подписи, предложенную клиентом, и для
  ECDSA-сертификата — группу ECDHE, совпадающую с кривой сертификата (OpenSSL в TLS 1.2
  требует того же).
- X.509 поддерживает подписи RSA PKCS#1 v1.5, RSA-PSS, ECDSA на P-256/P-384 и Ed25519, а в
  SPKI — ключи RSA, EC (P-256/P-384) и Ed25519. Цепочка может смешивать эти алгоритмы; каждое
  звено проверяется ключом того типа, которым оно подписано.
- Клиент проверяет цепочку до доверенного корня, срок действия каждого звена (включая
  корень), `basicConstraints CA`, `keyUsage keyCertSign` (если расширение есть),
  `pathLenConstraint` и неизвестные критичные расширения. Сертификат с тем же именем, что
  доверенный корень, но другим ключом, незавершённый путь и цепочка длиннее
  `MaxChainDepth` отвергаются. Имя сверяется с SAN `dNSName`/`iPAddress`; CN используется
  только при отсутствии SAN. IP-адрес не совпадает с DNS-именем и наоборот; шаблон `*`
  совпадает ровно с одной меткой и не допускается над доменом верхнего уровня (`*.com`).
- `ServerKeyExchange` подписан согласованной схемой (RSA PKCS#1 v1.5, RSA-PSS или ECDSA;
  Ed25519 можно проверить, но не подписать); клиент пересчитывает подпись по
  `client_random || server_random || ECDHParams`. Подпись ECDSA — DER `ECDSA-Sig-Value`; хеш
  короче или длиннее порядка кривой приводится к целому так, как требует ECDSA. Сертификат без
  доверенного корня, просроченный сертификат, чужое имя или подделанная подпись прерывают
  рукопожатие алертом.
- Сервер, требующий клиентский сертификат, шлёт `CertificateRequest` и проверяет цепочку и
  `CertificateVerify` клиента до приёма его `Finished`.
- Отвергнутые `Finished`, `ChangeCipherSpec` с неверным значением и запись с плохим тегом
  GCM или ChaCha20-Poly1305 закрывают соединение: `ReceiveFinished` возвращает результат, и
  состояние рукопожатия не переводится в `Established`.
- Секреты сравниваются через `CryptoUtils.EqualBytesCT`: MAC записи, тег GCM и
  ChaCha20-Poly1305, `Finished` и
  PKCS#1-блок не сравниваются с ранним выходом. Poly1305 использует 26-битные разряды и
  64-битные накопители; окончательное приведение выбирает из двух кандидатов маской. `CryptoHKDF` строится на `CryptoHMAC`, поэтому
  HMAC-SHA-256, HMAC-SHA-384 и HMAC-SHA-512 используют блок 128 байт у SHA-384 и SHA-512 и блок
  64 байта у SHA-256.
- P-256 и P-384 используют полные проективные формулы сложения Ренеса, Костелло и Батины. У них
  нет исключительных случаев, поэтому удвоение, сложение точки с собой и сложение
  противоположных точек идут одной последовательностью. Умножение на скаляр — окно 4 бита:
  число сложений зависит только от размера кривой, элемент таблицы выбирается маскированным
  перебором всех 16, а условное вычитание в операциях поля идёт через маски, не через ветвления.
  `InRange` и `IsZeroN` не выходят раньше времени. `CryptoEC.Validate` проверяет, что обе
  координаты меньше модуля поля и что точка лежит на кривой; ECDH, проверка ECDSA и разбор
  точки отвергают точку вне кривой, точку (0,0) и пустое представление.
- ECDSA следует RFC 6979: одноразовое число — выход HMAC-DRBG по закрытому ключу и приведённому
  хешу, поэтому повтор подписи невозможен без повтора сообщения. Проверка отвергает `r` или `s`,
  равные нулю или не меньшие порядка, и сравнивает пересчитанный `r` постоянного времени.
- `CryptoEC` работает на 32-битных разрядах с умножением Монтгомери. Отрицание, обращение и
  возведение в степень в поле используют открытые модуль и показатель; закрытый скаляр входит
  только через маскированный выбор окна.
- RSA-PSS следует RFC 8017: `emBits = modBits - 1`, соль длины хеша, MGF1 по тому же хешу.
  Итоговый хеш сравнивается через `EqualBytesCT`; в восстановленном EM проверяются нулевые
  старшие биты `maskedDB`, ведущие нулевые байты, байт `0xbc`, разделитель `0x01` и соль.
  `Crypto_devPSS` отвергает испорченные EM из `Vectors/pss-negative.txt` (разделитель
  `0x00`/`0x02`, ненулевой PS, концевой `0xbb`, выставленный старший бит, соль длины 31 или
  33), каждый подписан настоящим ключом.
- Ed25519 следует RFC 8032. `Decode` отвергает `y >= p`, неквадрат `x` и `x = 0` при знаковом
  бите; `Verify` отвергает `S >= L`, что снимает малеабельность подписи `S + L`. Формула
  сложения в расширенных координатах полная для этой кривой, поэтому удвоение и нейтральный
  элемент идут той же последовательностью.
- X.509 принимает RSA-PSS только с явным хешем, MGF1 по тому же хешу и солью длины хеша;
  другие параметры дают `ErrUnsupported`. Подпись ECDSA разбирается из DER как два
  положительных числа (ведущий ноль допустим, отрицательное значение отвергается) и
  проверяется кривой ключа издателя; хеш, длина которого не совпадает с кривой, отвергается.
  Для Ed25519 проверяются сами байты TBS.
- Сертификат с неизвестным или неподдерживаемым `AlgorithmIdentifier` подписи отвергается при
  чтении. Внутренний и внешний алгоритмы подписи обязаны совпадать, включая длину соли PSS.
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

- Рукопожатие TLS 1.2 и 1.3 поддерживает несжатые точки на X25519, secp256r1 и secp384r1;
  P-521, сжатые точки и остальные named groups не реализованы. Extended master secret,
  тикеты сессий, безопасное пересогласование, 0-RTT и PSK не реализованы; в 1.2
  отправляется пустой `renegotiation_info`, любое пересогласование отвергается, в 1.3
  `NewSessionTicket` игнорируется.
- В TLS 1.3 нет 0-RTT, PSK, post-handshake authentication и ранних данных; клиентский
  сертификат запрашивается только в основном рукопожатии. Сервер 1.3 обслуживает
  RSA- и ECDSA-сертификаты.
- Подпись Ed25519 не реализована (проверка есть). RSA-PSS использует фиксированную длину соли,
  равную длине хеша; другие длины соли не принимаются. Встроенный сервер подписывает
  RSA-PSS или ECDSA по своему сертификату и не обслуживает Ed25519-сертификат.
- CRL, OCSP и certificate transparency не проверяются. Решение о доверии — настроенный
  набор корней.
- Арифметика больших чисел (закрытые операции RSA) и X25519 не постоянного времени на
  уровне машины. В лестнице X25519 условная перестановка без ветвлений, но возведение в
  степень по модулю в `CryptoAosBigNumbers` зависит от данных. Это отмечено в обзоре
  безопасности и приемлемо для модели угроз кластера; для общего сервера, доступного
  локальному атакующему с высоким разрешением таймингов, — нет.
- Возобновление сессий и пересогласование в TLS 1.2 поддержаны лишь в объёме исходного кода
  A2; в TLS 1.3 возобновления нет — серверный `NewSessionTicket` не сохраняется.
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

Модульные тесты лежат в `Crypto_dev`: `Crypto_devHashes`, `Crypto_devSHA512`, `Crypto_devHMAC`,
`Crypto_devHKDF`, `Crypto_devCiphers`, `Crypto_devGcm`, `Crypto_devChaCha20`, `Crypto_devBigNumbers`, `Crypto_devDH`,
`Crypto_devRSA`, `Crypto_devX25519`, `Crypto_devEC`, `Crypto_devECDSA`, `Crypto_devEd25519`, `Crypto_devPSS`, `Crypto_devX509`,
`Crypto_devX509Modern`,
`Crypto_devPKCS1`, `Crypto_devPRF`, `Crypto_devTLS13`. Их
запускает `tools/test.sh` по списку `tools/tests.txt`. Эталонные значения также лежат файлами с
указанием источника и лицензии в `Crypto_dev/Vectors`; `Crypto_dev/Vectors/check.py` сверяет их
независимой реализацией на Python.

`Crypto_devTLS13` сверяет ключевое расписание TLS 1.3 с простым рукопожатием RFC 8448
побайтно: early/derived/handshake/master, client/server handshake и application traffic
secrets, ключи и iv записи, `Finished` сервера и клиента. Сообщения RFC 8448 лежат в
`Crypto_devTLS13Data` (генератор `Crypto_dev/Vectors/make-tls13-rfc8448.py`, исходные
значения — `Vectors/tls13-rfc8448.txt`). Отдельно проверяются SHA-384, связывание секретов
и `Finished` с транскриптом и разделение меток `key`/`iv`.

Рукопожатию TLS нужен второй процесс, поэтому у него отдельный сценарий:
`tools/test-crypto-tls.sh` создаёт во временном каталоге CA, промежуточный CA, издателя с
`CA:FALSE`, серверные сертификаты RSA, ECDSA P-256/P-384 и Ed25519, клиентский, просроченные
сертификаты и сертификат с чужим именем. Он гоняет клиент против `openssl s_server -rev`,
сервер против `openssl s_client`, проверяет матрицы TLS 1.3 (три набора × три группы клиента
с HRR и сервера × RSA/ECDSA-сертификаты, Ed25519 `CertificateVerify`) и TLS 1.2
(ECDHE-RSA/ECDHE-ECDSA × AES-128/256-GCM и ChaCha20-Poly1305 ×
x25519/secp256r1/secp384r1) в обе стороны, взаимную аутентификацию с RSA- и
ECDSA-сертификатом сервера и требует отказа (без трапа процесса) для чужого CA, просроченного
сертификата, промежуточного или корня, неверного имени, издателя с `CA:FALSE`, подделанной
подписи `ServerKeyExchange` (RSA PKCS#1, RSA-PSS и ECDSA), плохого `Finished`, неверного
`ChangeCipherSpec`, испорченного тега AES-GCM или ChaCha20-Poly1305, группы, которой клиент не
предлагал, точки secp256r1 вне кривой, чрезмерной длины подписи, неверной длины сертификата,
невозможной длины расширений, отсутствующего `ServerKeyExchange`, нулевого открытого ключа
X25519, SSLv2 `ClientHello` и неизвестного типа рукопожатия. Для TLS 1.3 к этому добавлены
`KeyUpdate`, `HelloRetryRequest` (и повторный HRR), `CertificateVerify` с чужим контекстом,
испорченный `Finished`, ключи из испорченного транскрипта и метка понижения `DOWNGRD` в
ответе 1.2. `tools/crypto_tls_lib.py`, `crypto_tls_fake.py`, `crypto_tls13_fake.py` и
`crypto_tls_client.py` — Python-партнёры для случаев, где нужен корректный транскрипт
рукопожатия: плохой `Finished`, плохой `ChangeCipherSpec`, испорченный тег записи, нулевая
точка X25519, плохая или хорошая подпись `CertificateVerify`, `KeyUpdate` и HRR.
Модулей `Crypto_devTLS` и `Crypto_devTLSServer` нет в `tools/tests.txt`, потому что без
второго процесса они не проходят.


### Публичные хосты (ручной прогон, нужна сеть)

`tools/test-crypto-hosts.sh` (не входит в `tools/test.sh`) собирает `Crypto_devHosts`, загружает
системный набор CA (`/etc/ssl/certs/ca-certificates.crt` по умолчанию), соединяется со списком
публичных HTTPS-хостов и посылает `GET / HTTP/1.0`. Для каждого хоста печатаются версия, набор,
группа ECDHE, схема подписи рукопожатия и первая строка ответа. Прогон 27.11.2026 соединился со всеми восемью хостами по TLS 1.3; microsoft.com запросил
secp256r1 через `HelloRetryRequest`, потому что первая доля клиента — x25519:

| Хост | Версия | Набор | Группа | Подпись |
|---|---|---|---|---|
| google.com | TLS 1.3 | TLS_AES_256_GCM_SHA384 | x25519 | ecdsa_secp256r1_sha256 |
| cloudflare.com | TLS 1.3 | TLS_AES_256_GCM_SHA384 | x25519 | ecdsa_secp256r1_sha256 |
| github.com | TLS 1.3 | TLS_AES_128_GCM_SHA256 | x25519 | ecdsa_secp256r1_sha256 |
| letsencrypt.org | TLS 1.3 | TLS_AES_128_GCM_SHA256 | x25519 | ecdsa_secp256r1_sha256 |
| wikipedia.org | TLS 1.3 | TLS_AES_128_GCM_SHA256 | x25519 | ecdsa_secp256r1_sha256 |
| microsoft.com | TLS 1.3 | TLS_AES_256_GCM_SHA384 | secp256r1 | rsa_pss_rsae_sha256 |
| apple.com | TLS 1.3 | TLS_AES_256_GCM_SHA384 | x25519 | ecdsa_secp256r1_sha256 |
| amazon.com | TLS 1.3 | TLS_AES_256_GCM_SHA384 | x25519 | rsa_pss_rsae_sha256 |

Прогон выявил и привёл к исправлению: отвергались подписи сертификатов
`sha384WithRSAEncryption`/`sha512WithRSAEncryption`; DigestInfo в PKCS#1 знал только
MD5/SHA-1/SHA-256, поэтому цепочки SHA-384/512 не проверялись; cross-signed корень (тот же
субъект и ключ, что у доверенного корня, другой издатель) не сопоставлялся; список SAN
обрезался на 16 записях; один нечитаемый сертификат отбрасывал весь набор корней, а у
`AddTrustedFile` был буфер 16 КиБ. Всё это покрыто `tools/test-crypto-tls.sh` (цепочки
SHA-384/SHA-512, список из 22 SAN, набор с мусорным сертификатом, набор 113 КиБ); случай
cross-signed корня покрыт ручным прогоном и таблицей порч в обзоре безопасности.



Подсистема распространяется по лицензиям BSD ETH Zurich и Hirbod Behnam; полный текст лежит в
`Crypto/LICENSE`. Обзор безопасности со статусом каждой находки и покрывающим её тестом — в
`Crypto/Docu/Security-Review.md`.
