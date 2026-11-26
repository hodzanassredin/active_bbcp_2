# Crypto64 — обзор безопасности (2026-11-27)

Объект: `Crypto` (порт bbext/Crypto64). Метод: чтение исходников, эталонные векторы,
отрицательные тесты и прогоны против `openssl` в обе стороны, порча кода под новые
тесты. Обзор дополнен TLS 1.3 (клиент и сервер, RFC 8446/8448). Исходный обзор от 2026-08-19 сохранён по смыслу: ниже по каждой находке указано,
что сделано, каким тестом это покрыто и что осталось.

Векторы лежат файлами в `Crypto_dev/Vectors` с источником и лицензией; независимая
сверка — `Crypto_dev/Vectors/check.py`. Модульные тесты — в `tools/tests.txt`,
межпроцессные — в `tools/test-crypto-tls.sh`.

## СТАТУС И КОММИТЫ

| Находка | Статус | Коммит | Покрытие |
|---|---|---|---|
| 1. Подпись `ServerKeyExchange` | исправлено | `dd52f048` | `test-crypto-tls.sh` (`fake-badsig`, `client-serverhello`), `Crypto_devPKCS1` |
| 2. X.509: подпись, цепочка, сроки, имя | исправлено | `c89a551b` | `Crypto_devX509`, `test-crypto-tls.sh` (`untrusted`, `expired`, `wrong-host`, `server-foreign-client`) |
| 3. Границы `sigLen` до копирования | исправлено | `dd52f048`, `262ba455` | `fake-oversize`, `Crypto_devPKCS1` |
| 4. Разбор сети без `ASSERT` | исправлено | `262ba455`, `dd52f048` | `TestAsn1`, `TestMalformed`, `fake-noske`, `v2hello`, `v2record`, `hstype` |
| 5. Энтропия ГСЧ | исправлено, с остатком | `262ba455` | `TestEntropy`; остаток — в «Что осталось» |
| 6. Наборы шифров по умолчанию | исправлено, современный набор | `dd52f048`, `crypto-fix` | `TestDefaults`, матрица `test-crypto-tls.sh` |
| 7. Постоянное время | частично, с пометкой | `262ba455` | `TestConstantTime`, `EqualBytesCT` |
| 8. TLS: результат `Finished` и состояние | исправлено | `crypto-fix` | `fake-badfinished`, `fake-badccs`, `fake-badtag`, `badcertverify`, `goodcertverify` |
| 9. X.509: ограничения пути и расширения | исправлено | `f4ae77de`, `8b9d6d8c` | `Crypto_devX509` (`TestNoCA`, `TestPathLen`, `TestKeyUsage`, `TestCriticalExtension`, `TestCycle`, `TestAlgMismatch`) |
| SHA-384/512, HMAC-SHA-384/512, HKDF | сделано | `crypto-fix` | `Crypto_devSHA512`, `Crypto_devHMAC`, `Crypto_devHKDF`, `Vectors/check.py` |
| P-256/P-384: ECDH и ECDSA (RFC 6979) | сделано | `crypto-fix` | `Crypto_devEC`, `Crypto_devECDSA`, NIST CAVP, RFC 6979, Wycheproof, `Vectors/check.py` |
| AES-256-GCM, ChaCha20-Poly1305 | сделано | `crypto-fix` | `Crypto_devGcm` (`TestAES256`), `Crypto_devChaCha20`, RFC 8439, `Vectors/check.py` |
| RSA-PSS | сделано | `crypto-fix` | `Crypto_devPSS`, Wycheproof, `Vectors/check.py` |
| Ed25519 (проверка) | сделано | `crypto-fix` | `Crypto_devEd25519`, RFC 8032, Wycheproof, `Vectors/check.py` |
| X.509: ECDSA, RSA-PSS, Ed25519, смешанные цепочки | сделано | `crypto-fix` | `Crypto_devX509Modern`, `Crypto_devX509` |
| TLS 1.2: ECDHE-ECDSA/RSA, AES-128/256-GCM, ChaCha20-Poly1305, x25519/secp256r1/secp384r1, PSS/ECDSA | сделано | `crypto-fix` | `tools/test-crypto-tls.sh` (матрица 30 пар в обе стороны), `Crypto_devTLS`, `Crypto_devTLSServer` |
| TLS 1.3: ключевое расписание HKDF, HRR, EE/Cert/CV/Finished, KeyUpdate, сигнал понижения, три набора | сделано | `crypto-fix` | `Crypto_devTLS13` (RFC 8448 побайтно), `tools/test-crypto-tls.sh` (матрица 1.3 в обе стороны, HRR, KeyUpdate, отказы), `crypto_tls13_fake.py` |
| TLS 1.3: версия и набор по умолчанию (1.3, затем 1.2 ECDHE-AEAD) | сделано | `crypto-fix` | матрица `test-crypto-tls.sh`, ручной прогон хостов |
| Публичные HTTPS-хосты | сделано, 8/8 | `crypto-fix` | `tools/test-crypto-hosts.sh` (ручной), исправления sha384/512 RSA, SameAnchor, SAN, PEM-набора — см. раздел ниже |
| Эталонные векторы | — | `5a6eeefb` | `Crypto_dev/Vectors/check.py`, `Crypto_dev*` |
| 64-битные помощники, Net64 | — | `262ba455` | `Test64`, `TestNet64`, `Crypto_devRSA` |

## КРИТИЧНЫЕ

### 1. TLS: подпись ServerKeyExchange — ИСПРАВЛЕНО

`CryptoTLS.ReceiveServerKeyExchange` проверяет подпись RSA PKCS#1 v1.5/SHA-256 по
`client_random || server_random || ServerECDHParams` открытым ключом листа из
`Certificate`. Добавлены `CryptoPKCS1.SignHash`/`VerifyHash` (EMSA-PKCS1-v1_5,
сравнение EM постоянного времени). Сервер подписывает `ServerKeyExchange` в
`SendServerKeyExchange`.

Тесты: `tools/test-crypto-tls.sh`, случаи `fake-badsig` (клиент обязан отказаться и
не дойти до `ClientKeyExchange`), `client-serverhello` (положительный обмен с
`openssl s_server`), `client-mutual`. В `Crypto_devPKCS1` — подпись/проверка и отказы
при подделке.

### 2. X.509: цепочка, сроки, hostname, CheckSignature — ИСПРАВЛЕНО

- `CryptoX509.ReadCert` сохраняет полную DER-запись, подписанную часть (`tbs`) и
  подпись; внешний и внутренний `AlgorithmIdentifier` обязаны совпадать, иначе
  сертификат отвергается как `ErrMalformed`. `CheckSignature` проверяет RSA
  PKCS#1 v1.5 по `tbs`, а не заглушка.
- `VerifyChain` строит путь лист → издатели → доверенный корень. Проверяются:
  подпись каждого звена; `notBefore`/`notAfter` листа, каждого промежуточного и
  доверенного корня; `basicConstraints CA:TRUE` у издателя; `keyUsage`
  (если расширение есть, бит `keyCertSign` обязателен); `pathLenConstraint`
  (считаются только несамоподписанные промежуточные, RFC 5280 §6.1.4);
  неизвестное критичное расширение у любого сертификата пути — отказ
  `ErrUnsupported` (RFC 5280 §4.2). Издатель ищется только в предъявленной
  цепочке и в доверенном наборе; сертификат с тем же именем, что корень, но
  другим ключом не принимается. Цикл обрывается пределом `MaxChainDepth`.
  Незавершённый путь отвергается.
- `MatchHost` проверяет SAN `dNSName`/`iPAddress`, CN — только при отсутствии
  SAN (RFC 6125 §6.4.4). IP-адрес в SAN не сравнивается с DNS-именем и
  наоборот; регистр ASCII не важен. Шаблон `*` допустим только целой левой
  меткой, совпадает ровно с одной меткой и не может стоять над доменом
  верхнего уровня (`*.com` отвергается, RFC 6125 §6.4.3). Шаблон внутри
  A-label (`xn--*`) не совпадает ни с чем.
- `Context.AddTrustedPem/AddTrustedFile` задают доверенные корни; `LoadCertificateFile`
  и `LoadPrivateKeyFile` читают PEM сертификата и ключа (PKCS#1 и PKCS#8).
- `Bypass` больше не обходит проверку: старый `Verify` проверяет только сроки
  (совместимый вход, не проверка пути), а `VerifyChain` — полный путь.
- Политики сертификатов, EKU, name constraints, CRL/OCSP/CT не проверяются;
  это перечислено в «Что осталось».

Тесты: `Crypto_devX509` (разбор, промежуточная цепочка, IP/DNS SAN, CN,
шаблоны и их границы, чужое имя, просроченный лист, просроченный
промежуточный и корень, подделанная подпись, расхождение внешнего и
внутреннего `AlgorithmIdentifier`, `CA:FALSE`-издатель, отсутствие
`basicConstraints`, `keyCertSign`, `pathLen=0`, критичное расширение у
промежуточного и листа, цикл, чужой корень с тем же именем, `AllowOutdated`,
обрезанный и мусорный DER); `tools/test-crypto-tls.sh` (`untrusted`,
`expired`, `wrong-host`, `noca`, `client-intermediate`,
`server-foreign-client`).

### 3. Переполнение буфера в ReceiveServerKeyExchange — ИСПРАВЛЕНО

`sigLen` проверяется до копирования: `sigLen = 0`, `sigLen > MaxSignatureLength`,
`36 + 4 + sigLen > len`. Копирования в фиксированный `signature[512]` больше нет —
подпись проверяется прямо в буфере записи. `PKCS1.VerifyHash` дополнительно
проверяет `sigOfs + sigLen` и `sigLen <= k`.

Тесты: `fake-oversize` (sigLen = 65535) — отказ без трапа; порча проверки ловится
`test-crypto-tls.sh`.

### 4. Парсинг сетевых данных через ASSERT — ИСПРАВЛЕНО

`ReceiveClientHello`, `ReceiveServerHello`, `ReceiveCertificate`,
`ReceiveServerKeyExchange`, `ReceiveHandshake`, `ReceiveRecord`, `ReceiveAlert`,
`ChangeCipherSpec` проверяют длины и возвращают алерт/ошибку. SSLv2 `ClientHello` обеих
форм (запись типа 128 и handshake-сообщение типа 30) отвергается, небезопасный разбор
`ReceiveV2ClientHello` удалён. В состояниях, где последовательность зависит от выбора второй стороны,
тип handshake-сообщения сверяется с состоянием: неизвестный тип даёт `UnexpectedMessage`,
прежний `ASSERT(subAction=0CH)` в `AwaitServerKeyExchange` убран.
`CryptoASN1.Decode` не доверяет длинам: обрезанный/бесконечный DER поднимает `ok := FALSE`,
разбор не уходит за буфер; недопустимая нулевая длина BIT STRING отвергается.

Тесты: `fake-noske` (вместо `ServerKeyExchange` сразу `ServerHelloDone`),
`v2hello`/`v2record`/`hstype` (некорректный клиент против нашего сервера),
`fake-badcert`, `fake-badlist`, `fake-garbage`, `fake-hugerecord` — отказ без
`TRAP`; `Crypto_devX509.TestAsn1` (бесконечная длина, пять байт длины, выход ребёнка за
родителя, нулевой BIT STRING, обрезка) и `TestMalformed` (огромная длина, обрезка, пустой
ввод).

### 8. TLS: результат `Finished` и состояние рукопожатия — ИСПРАВЛЕНО

`ReceiveFinished` возвращал `BOOLEAN`, но `Transition` игнорировал результат:
после проверки клиент и сервер безусловно выполняли `SetState(TCP.Established)`
(и сервер переходил к своему `Finished`). Поэтому запись с неверным
`verify_data` закрывала соединение алертом, но состояние тут же перезаписывалось
на `Established`; `Open` возвращал успех, а сервер принимал клиента с чужим
`Finished`. Теперь `ReceiveFinished` возвращает `FALSE` при любой ошибке, а все
три места (`клиент AwaitFinished`, сервер `AwaitFinished` и
`AwaitFinishedWithResumption`) проверяют результат и не меняют состояние.

Тесты: `fake-badfinished` (испорченные 12 байт), `fake-longfinished`
(13 байт), `fake-badccs` (`ChangeCipherSpec` со значением 2),
`fake-longccs` (два байта), `fake-badtag` (испорченный тег GCM
приложения), `fake-shortrec` (запись короче nonce и тега),
`fake-zeropub` (нулевой общий секрет X25519), `fake-oldversion`
(ServerHello 1.1), `badcertverify` (верная цепочка, чужая подпись
`CertificateVerify`), `nocertcv` (`CertificateVerify` без сертификата),
`badfinished` (сервер против клиента с чужим `Finished`), `oldversion`
(ClientHello 1.0), положительные `goodfinished`, `goodcertverify` и
обмен с промежуточным CA.
Тест `client_case` принимает успех только по строке
`PASS Crypto_devTLS...`: итог «failed 0» печатается и при нуле
исполненных тестов, когда модуль не загрузился.

## ВЫСОКИЕ

### 5. Слабая энтропия ГСЧ — ИСПРАВЛЕНО

Новый модуль `CryptoOsRandom` читает `/dev/urandom`. Им пользуются
`CryptoUtils.RandomBytesOk` (PKCS#1 padding и TLS pre-master), `CryptoAosBigNumbers`
(простые числа RSA/DH) и `CryptoFortunaRng` (сид и подмешивание на каждый `Generate`).
PKCS#1 и TLS при недоступном источнике отказываются от операции, а не подставляют
предсказуемый LCG. Генерация простых чисел `CryptoAosBigNumbers.NewRand` читает
`/dev/urandom`, но не пробрасывает отказ через сигнатуру и при отказе пользуется
внутренним генератором; это записано в «Что осталось».

Тесты: `Crypto_devPRF.TestEntropy` (два блока различаются, не все нули, проверка
границ); порчи `entropy-zero`, `entropy-bounds`, `entropy-false`.

## СРЕДНИЕ

### 6. Наборы шифров — ИСПРАВЛЕНО

По умолчанию только `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256`. CBC, 3DES, RC4, NULL и
RSA key transport остаются в коде для совместимости и включаются только явным
`Policy.SetCipherSuites`. Сервер и клиент проверяют, что выбранный набор был предложен.

Тесты: `Crypto_devPRF.TestDefaults` (ровно один набор, GCM); порчи
`default-add-3des`, `default-cbc`, `default-null`.

### 7. Постоянное время — ЧАСТИЧНО, с явной пометкой

Сделано:
- `CryptoUtils.EqualBytesCT` — сравнение MAC записи, тега GCM, `Finished` и
  PKCS#1-блока (EM) без раннего выхода.
- Лестница X25519 использует условную перестановку без ветвлений по битам скаляра;
  общий секрет проверяется на ноль (`AgreementChecked`, RFC 7748 §6.1).
- `PKCS1.VerifyHash` сравнивает восстановленный EM с ожидаемым постоянного времени.

Не сделано и оставлено осознанно: `CryptoAosBigNumbers.ModExp`/`Div2` и RSA CRT
зависят от данных. Переписывание bignum в постоянное время — отдельная большая работа;
для модели угроз кластера (узлы в доверенной сети, без локального атакующего с
высоким разрешением таймингов) риск принят. Для публичного сервера с недоверенными
локальными процессами это ограничение нужно снять до использования.

Тесты: `Crypto_devPRF.TestConstantTime` (функциональная проверка сравнения, не
таймингов); порчи `ct-always-true`, `ct-always-false`, `ct-first-n-bytes`.

## ЗАМЕТКИ / INFO

- X25519: clamping корректен; добавлен эталонный вектор RFC 7748 §6.1
  (`Crypto_devX25519.TestRFC7748`, `Crypto_dev/Vectors/x25519.txt`).
- Хеши/HMAC/шифры/GCM: векторы расширены (AES-128 FIPS-197, HMAC-SHA256 RFC 4231),
  файлы — в `Crypto_dev/Vectors`.
- GCM: тег сравнивается постоянного времени; nonce в записи — `salt || seq`, явная
  часть уникальна для ключа.
- Клиентская и серверная стороны TLS 1.2; взаимная проверка сертификатов
  (`CertificateRequest`, проверка цепочки клиента, `CertificateVerify`).
- `CryptoAosStreams.Net64`/`UNet64` делили значение на 1e8 и теряли старшие биты;
  теперь две 32-битные половины, есть `Crypto_devBigNumbers.TestNet64`.
- 64-битные помощники `CryptoAosCompat` переписаны явно: беззнаковые сравнения,
  перенос и заём через половины, логический сдвиг через половины, частный случай
  деления на 2^32, аппаратное умножение младших 64 бит. Прежние формы на этом
  компиляторе давали тот же результат (сравнение через XOR старшего бита
  эквивалентно беззнаковому), но RSA-тест с 1024-битным ключом шёл 133 с; после
  переписывания — 2.2 с. Проверяется `Crypto_devBigNumbers` (`Test64`:
  `ULss32`/`ULss64`/`UGtr32`/`UGtr64`, перенос, заём, сдвиги на 32 и 40,
  деление на 2^32; `TestNet64`), `Crypto_devRSA`, `Crypto_devPKCS1`,
  `Crypto_devX509` и TLS-сценарием с `openssl`.
- `CryptoPKCS1.Decrypt` проверяет дополнение с границами (не было `padd` при `NEW`,
  циклы могли уйти за буфер).

## TLS 1.2: СОВРЕМЕННЫЕ НАБОРЫ (ЭТАП 3б, П. 2)

Клиент и сервер TLS 1.2 переведены на ECDHE с x25519/secp256r1/secp384r1 и AEAD:
ECDHE-ECDSA и ECDHE-RSA с AES-128-GCM-SHA256, AES-256-GCM-SHA384 и
ChaCha20-Poly1305-SHA256. PRF и хеш транскрипта выбираются по набору (SHA-256
или SHA-384); запись AES-GCM использует nonce 4+8, ChaCha20-Poly1305 — соль
12 байт и номер записи (RFC 7905). Подпись `ServerKeyExchange` и
`CertificateVerify` проверяется и создаётся по согласованной схеме:
RSA PKCS#1 v1.5 SHA-256/384, RSA-PSS rsae SHA-256/384, ECDSA P-256/P-384;
Ed25519 только проверяется. Для ECDSA хеш приводится к целому по правилам
ECDSA (усечение/дополнение до порядка кривой), поэтому P-384-сертификат
работает и с `ecdsa_secp256r1_sha256`, как это делает OpenSSL в TLS 1.2.

Сервер разбирает `supported_groups` и `signature_algorithms` клиента, выбирает
набор, совместимый с сертификатом, схему из предложенных и для
ECDSA-сертификата — группу, равную кривой сертификата (это же правило
проверяет OpenSSL). Ключ ECDSA читается из PEM SEC1 «EC PRIVATE KEY» и PKCS#8
(`X509.ReadPemEcPrivateKey`). Найдено и исправлено переполнение буферов
`CryptoPSS.Encode` на ключе 2048 бит: `db`/`dbMask` были размером
`2*MaxHash+8` (136 байт), а для 2048-битного модуля `dbLen = 223`; подпись
`ServerKeyExchange` RSA-PSS падала трапом. Добавлен `TestSign2048`.

Порчи (каждая — отдельный прогон; «поймана» — соответствующий сценарий
`test-crypto-tls.sh` или `Crypto_devPSS` красный):

| Место | Порча | Результат |
|---|---|---|
| `ServerGroup`: выбор группы ECDSA по кривой | всегда `GroupX25519` | поймана: сервер P-256 + клиент secp256r1 (`wrong curve`) |
| `NewGroup`: `secp384r1` | `EC.P256()` | поймана: клиент P-384 не принимает SKE |
| `EcGroup.Shared`: `secretLen := n` | `n - 1` | поймана: клиент P-256, неверный pre-master |
| ChaCha `Seal`: XOR номера записи | убран | поймана: клиент ChaCha/x25519 |
| ChaCha `saltLen` | 12 → 4 | поймана: сервер ChaCha/x25519 |
| ChaCha `Open`: результат `Decrypt` | игнорируется | поймана: `fake-badtagchacha` (усиленная проверка «соединение закрыто») |
| `PrepareSecurityParameters`: PRF SHA-384 | SHA-256 | поймана: клиент AES-256-GCM-SHA384 |
| `PrfDigestLen` | 48 → 32 | поймана: клиент AES-256-GCM-SHA384 |
| `TranscriptDigestAt` | всегда SHA-256 | поймана: клиент AES-256-GCM-SHA384 |
| `TLSVerifyDigest`: RSA-PSS | `RETURN TRUE` | поймана: `fake-badpss` |
| `TLSVerifyDigest`: ECDSA P-256 | `RETURN TRUE` | поймана: `fake-badecdsa` |
| `SuiteCompatible` | `RETURN TRUE` | поймана: `server-auto-suite` (клиент openssl предлагает оба типа) |
| `ReceiveClientHello`: граница `supported_groups` | `epos+6` → `epos+4` | поймана: ECDSA-сервер не видит группу |
| `TLSNewHash`: `ecdsa_secp384r1_sha384` | нет SHA-384 | поймана: сервер P-384 не подписывает SKE |
| `PrepareSecurityParameters`: ключ AES-256/ChaCha | 32 → 16 | поймана: клиент AES-256-GCM-SHA384 |
| `X509.ReadPemEcPrivateKey`: OID P-384 (SEC1) | `EC.P256()` | поймана: сервер P-384 |
| `X509.ReadPemEcPrivateKey`: SEC1-заголовок | не принимается | поймана: сервер ECDSA (openssl пишет SEC1) |
| `X509.ReadPemEcPrivateKey`: длина скаляра | `4n` → `4n-1` | поймана: сервер ECDSA, ключ не загружается |
| `X509.ReadPemEcPrivateKey`: выравнивание скаляра | слева → справа | эквивалентна: openssl пишет скаляр ровно `4n` байт, ветка дополнения на этих векторах не срабатывает |
| `CryptoPSS.Encode`: буферы `db`/`dbMask` | `MaxModulusBytes` → `2*MaxHash+8` | поймана: `TestSign2048` трапом |

## TLS 1.3: КЛИЕНТ И СЕРВЕР (ЭТАП 3б, П. 7)

Клиент и сервер TLS 1.3 работают с `TLS_AES_128_GCM_SHA256`,
`TLS_AES_256_GCM_SHA384` и `TLS_CHACHA20_POLY1305_SHA256`; ключевое расписание
построено только на HKDF (`Extract`, `Expand-Label`, `Derive-Secret`). Клиент
шлёт `key_share` для x25519, а после `HelloRetryRequest` — для secp256r1 или
secp384r1; сервер выбирает группу из присланного `key_share`, иначе отвечает
`HelloRetryRequest`. Клиент повторяет
`ClientHello` с тем же random и legacy_session_id; по RFC 8446 §4.4.1 первый
`ClientHello` заменяется в транскрипте синтетическим `message_hash`, иначе
`Finished` не сходится с OpenSSL. `EncryptedExtensions`, `Certificate`,
`CertificateVerify` и `Finished` идут под handshake-ключами; application-секреты
снимаются с транскрипта вместе с серверным `Finished`. `CertificateVerify`
подписывает 64 пробела, контекстную строку `TLS 1.3, server CertificateVerify`
(или `client`), ноль и хеш транскрипта без самого сообщения. Принимаются
`rsa_pss_rsae_sha256/384`, `ecdsa_secp256r1_sha256`,
`ecdsa_secp384r1_sha384` и `ed25519` (только проверка). `KeyUpdate` обновляет
секрет `traffic upd` и номер записи; на `request_update=1` ответ уходит под
старыми ключами. Сервер, умеющий 1.3, но согласовавший 1.2, ставит `DOWNGRD\x01`
в конец `ServerHello.random`; клиент, предлагавший 1.3, отвергает такую метку,
но принимает её отсутствие (сервер, умеющий только 1.2). 0-RTT и PSK не
реализованы; `NewSessionTicket` игнорируется.

Порчи (каждая — отдельный прогон; проверка — сценарий
`tools/test-crypto-tls.sh` или `Crypto_devTLS13`):

| Место | Порча | Результат |
|---|---|---|
| `HKDFExpandLabel`: префикс `"tls13 "` | первый байт `74` → `00` | поймана: `Crypto_devTLS13` (5 проверок красных) |
| `HKDFExpandLabel`: длина label в info | `6 + labelLen` → `labelLen` | поймана: `Crypto_devTLS13` |
| `DeriveHandshakeSecrets13`: контекст `derived` | `SHA(empty)` → пустая строка | поймана: клиент 1.3 с openssl |
| `TLS13HashName`: SHA-256 | `'2'` → `'3'` (`CryptoSHA356`) | поймана: клиент с `TLS_AES_128_GCM_SHA256` |
| `ReplaceClientHelloWithHash13`: тип сообщения | `0xFE` → `0x01` | поймана: HRR против openssl (`-groups secp256r1`) |
| `ReceiveCertificateVerify13`: контекст | `server` → `client` | поймана: клиент 1.3 с openssl |
| `ReceiveCertificateVerify13`: проверка подписи | `IF ~TLSVerify(...)` → `IF FALSE` | поймана: `fake13-badcv` |
| `ReceiveFinished` (1.3): сравнение `verify_data` | `IF ~EqualBytesCT(...)` → `IF FALSE` | поймана: `fake13-badfinished` |
| `ReceiveFinished` (1.3): базовый секрет | `serverHsSecret` → `clientHsSecret` | поймана: клиент 1.3 с openssl |
| `ReceiveServerHello`: группа из HRR | `ksGroup` → `GroupX25519` | поймана: HRR против openssl |
| `SendClientHello`: `key_share` после HRR | всегда `GroupX25519` | поймана: HRR против openssl |
| `HasDowngradeSentinel` | `RETURN FALSE` | поймана: `fake-downgrade` |
| `ReceiveServerHello`: проверка метки | отвергать любой 1.2 | поймана: клиент 1.2 против openssl |
| `SendServerHello`: последний байт метки | `01` → `00` | поймана: `server sentinel` (разбор `ServerHello.random`) |
| `ReceiveKeyUpdate`: обновление read-секрета | вызов убран | поймана: `fake13-keyupdate` |
| `ReceiveKeyUpdate`: порядок | ключи записи обновляются до ответа | поймана: `fake13-keyupdate` (ответ под старыми ключами) |
| `BuildNonce13`: XOR с номером записи | убран | поймана: клиент 1.3 (первая же запись) |
| `BuildAAD13`: длина | ciphertext+tag → plaintext | поймана: клиент 1.3 |
| `ReceiveHandshake`: транскрипт | пропуск `Certificate` | поймана: клиент 1.3 (CertificateVerify) |
| `TranscriptHash13`: длина | `transcriptLen` → `transcriptLen - 1` | поймана: клиент 1.3 |
| `TLS13FinishedFromHash`: вход HMAC | `digestLen` → `digestLen - 1` | поймана: клиент 1.3 |
| `SendCertificateVerify13`: контекст сервера | `server` → `client` | поймана: сервер 1.3 (openssl отвергает подпись) |

Механизмы: ключевое расписание — 4 порчи, транскрипт — 3, `CertificateVerify`
(контекстная строка и проверка) — 3, `Finished` — 3, HRR — 3, сигнал понижения
(клиент и сервер) — 3, записи — 2, `KeyUpdate` — 2. Все 22 красные на целевых
прогонах: `Crypto_devTLS13`, `Crypto_devTLS`/`Crypto_devTLSServer` против openssl
и `tools/crypto_tls13_fake.py`.

## ПУБЛИЧНЫЕ ХОСТЫ (ЭТАП 3б, П. 6)

`tools/test-crypto-hosts.sh` (ручной, нужна сеть) соединяется нашим клиентом с восемью
публичными HTTPS-хостами, грузит системный набор CA и печатает версию, набор, группу и
подпись. Прогон 27.11.2026: 8/8 по TLS 1.3 (google, cloudflare, github, letsencrypt,
wikipedia, microsoft, apple, amazon). Наборы TLS_AES_256_GCM_SHA384 и
TLS_AES_128_GCM_SHA256, группы x25519 и secp256r1; microsoft.com запросил secp256r1
через `HelloRetryRequest` (первая доля клиента — x25519). Подписи —
ecdsa_secp256r1_sha256 и rsa_pss_rsae_sha256. Прогон 26.11.2026 по TLS 1.2 нашёл шесть
дефектов X.509/PKCS#1, все исправлены:

- не поддерживались OID `sha384WithRSAEncryption` и `sha512WithRSAEncryption` в
  `AlgorithmIdentifier` сертификата;
- `PKCS1.BuildEM` знал DigestInfo только для MD5/SHA-1/SHA-256, поэтому подпись цепочки
  SHA-384/SHA-512 не проверялась;
- доверенный корень сопоставлялся только по точному DER; cross-signed корень (тот же субъект
  и ключ, другой издатель) не принимался — добавлено сопоставление по субъекту и открытому
  ключу (`SameAnchor`);
- `MaxSans = 16` обрезал длинные списки SAN;
- `X509.ReadPemChain` прекращал разбор на первом нечитаемом сертификате, отбрасывая остальные
  корни набора;
- `Context.AddTrustedFile` читал файл в буфер 16 КиБ, а системный набор — 180 КиБ.

Порчи (проверка — соответствующий сценарий `tools/test-crypto-tls.sh` или ручной прогон
хостов):

| Место | Порча | Результат |
|---|---|---|
| `PKCS1`: DigestInfo SHA-384 | первый байт `30` → `31` | поймана: клиент против CA-подписи SHA-384 |
| `PKCS1`: DigestInfo SHA-512 | первый байт `30` → `31` | поймана: клиент против CA-подписи SHA-512 |
| X509 `ParseSigAlg`: `sha384WithRSA` | ветка отключена | поймана: клиент, сертификат SHA-384 |
| X509 `ParseSigAlg`: `sha512WithRSA` | ветка отключена | поймана: клиент, сертификат SHA-512 |
| X509 `MaxSans` | 64 → 16 | поймана: `client-many-san` (совпадение дальше 16-го) |
| X509 `ReadPemChain` | возврат на первом нечитаемом | поймана: `client-badca-skip` |
| `AddTrustedFile`: буфер | динамический → 16 КиБ | поймана: `client-bigca` (113 КиБ) |
| X509 `SameAnchor` | `RETURN FALSE` (только точный DER) | поймана ручным прогоном: google/cloudflare `certError=4` |

## ПРОВЕРКА ПОРЧЕЙ

Порчи вносились по одной в рабочую копию, модуль пересобирался
(`tools/go64.sh`), затем шёл прогон: `tools/test.sh Crypto_devX509` для
X.509 и `tools/test-crypto-tls.sh` для TLS. Исходник возвращался.
«Поймана» — прогон красный; «эквивалентна» — проверка не влияет на приём
чужого сертификата или на исход рукопожатия, и это объяснено. Порчи A и B
приёмки стоят первыми в таблице X.509. Всего 78 новых порч: X.509 — 35
(30 красных, включая таймаут цикла, и 5 эквивалентных), TLS — 43
(25 красных и 18 эквивалентных).

Механизмы SHA-384/512, HKDF, P-256/P-384, ChaCha20-Poly1305, RSA-PSS,
Ed25519 и новые подписи X.509 проверены 67 порчами: 63 красных,
4 перечислены как эквивалентные на выбранных векторах с причиной.

TLS 1.3 проверен 22 порчами (таблица выше): все 22 красные на целевых
прогонах — `Crypto_devTLS13` для расписания, `Crypto_devTLS`/
`Crypto_devTLSServer` против openssl для рукопожатия и HRR,
`crypto_tls13_fake.py` для `KeyUpdate`, чужого контекста
`CertificateVerify`, испорченного `Finished` и ключей. Первый проход
нашёл три выжившие: цель `hash-name-256` шла с набором SHA-384 (добавлен
явный `-ciphersuites TLS_AES_128_GCM_SHA256`), порча `downgrade-always`
меняла недостижимую ветку (перенесена на условие вызова), а
`keyupdate-order` не проверялся, потому что фейк не читал ответный
`KeyUpdate` (фейк усилен: ответ обязан прийти под старыми ключами).
После этого красные все 22.

### Приёмка пунктов 1–5: структура EMSA-PSS

Порча `Crypto/Mod/PSS.cp`, убирающая проверку разделителя `db[psLen] # 1X`,
раньше выживала: в `Crypto_devPSS` не было вектора с испорченным
разделителем. Добавлены отрицательные векторы
`Crypto_dev/Vectors/pss-negative.txt` (генератор `make-pss-negative.py`):
EM собирается с одним дефектом и подписывается настоящим ключом
(`s = EM^d mod n`), поэтому проверяющая сторона после `s^e mod n`
получает ровно этот EM. Вектор с ненулевым старшим битом maskedDB
обнаружил настоящий пропуск: шаг 4 из RFC 8017, 9.1.2 (старшие
`8*emLen - emBits` бита maskedDB нулевые) не проверялся, а маска `db[0]`
ниже скрывала эти биты. Проверка добавлена в `CryptoPSS.VerifyHash`;
контрольный вектор подтверждает, что годная подпись проходит.

| Место | Порча | Результат |
|---|---|---|
| `VerifyHash`: `IF db[psLen] # 1X` | `IF FALSE` (порча приёмки) | поймана `TestNegativeEncoding` (разделитель 0x00 и 0x02) |
| `VerifyHash`: `IF ORD(em[emLen-1]) # 0BCH` | проверка удалена | поймана `TestNegativeEncoding` (концевой 0xBB) |
| `VerifyHash`: проверка старших бит maskedDB | `IF FALSE` | поймана `TestNegativeEncoding` (старший бит maskedDB) |
| `VerifyHash`: `IF db[i] # 0X` | `IF FALSE` | поймана `TestNegativeEncoding` (ненулевой PS) |
| `VerifyHash`: `saltLen := hLen` | `hLen - 1` | поймана `TestNegativeEncoding` (соль 31) и годные векторы |
| `MGF1`: `INC(c)` | `INC(c, 0)` | поймана контрольным и годными векторами |
| `VerifyHash`: `EqualBytesCT(h, hp)` | `ok := TRUE` | поймана `TestWycheproof` (изменённый m_hash) |
| `VerifyHash`: `db[i] := em[i] XOR dbMask[i]` | `db[i] := em[i]` | поймана годными векторами |
| `Encode`: `em[emLen-1] := 0BCH` | `0BBH` | поймана `TestSign` (своя подпись не проверяется) |
| `VerifyHash`: обнуление старших бит `db[0]` | строка удалена | поймана годными векторами (маска делает PS ненулевым) |

До этой приёмки порча `pss-zerobits` считалась эквивалентной; после
добавления проверки старших бит и векторов она краснеет, как и остальные
девять.

### X.509

| Место | Порча | Результат |
|---|---|---|
| `CheckIssuer`: `~issuer.isCA` | `IF FALSE` (порча A приёмки) | поймана `TestNoCA` |
| `CheckIssuer`: `~issuer.isCA` | инверсия (`issuer.isCA`) | поймана `TestIntermediate` (отвергает настоящий CA) |
| `CheckIssuer`: `keyUsagePresent & ~keyCertSign` | `IF FALSE` | поймана `TestKeyUsage` |
| `CheckIssuer`: `~keyCertSign` | инверсия | поймана `TestIntermediate` |
| `CheckIssuer`: `hasPathLen & pathLen < walk` | `IF FALSE` | поймана `TestPathLen` |
| `CheckIssuer`: `pathLen < walk` | `<` → `<=` | поймана `TestPathLenAccept` (pathLen=0, лист прямо под CA) |
| `CheckIssuer`: `hasPathLen &` | убрать `hasPathLen` | поймана `TestIntermediate` (у корня нет `pathLen`) |
| `CheckIssuer`: `issuer.critUnsupported` | `IF FALSE` | эквивалентна: принятый издатель на следующем витке становится `current` и проверяется там; доверенный якорь доверен извне |
| `VerifyChain`: `current.critUnsupported` | `IF FALSE` | поймана `TestCriticalLeaf` |
| `VerifyChain`: `CheckDate(current)` | `err := Ok` | поймана `TestExpired` |
| `CheckIssuer`: `CheckDate(issuer)` | `RETURN TRUE` | поймана `TestExpiredRoot` (срок якоря) |
| `CheckDate`: `cmpBefore > 0` | `>= 0` | эквивалентна: разница только при `now = notBefore` ровно в секунду, и порча отвергает (fail-closed) |
| `CheckDate`: `cmpAfter > 0` | `>= 0` | эквивалентна: только `now = notAfter`, тоже fail-closed |
| `CheckDate`: `AllowOutdated IN flags` | `IF FALSE` | поймана `TestLegacyVerify` |
| `VerifyChain`: `CheckSignature` | `IF FALSE` | поймана `TestTampered` |
| `VerifyChain`: `HostOk` (оба места) | `IF TRUE` | поймана `TestChain` (чужое имя) |
| `VerifyChain`: `InList(issuer, roots)` | `IF FALSE` | поймана `TestChain` (годная цепочка отвергается) |
| `VerifyChain`: `InList(current, roots)` | `IF FALSE` | эквивалентна fail-closed: прямой доверенный лист отвергается; обычный путь принимается на следующем витке через `InList(issuer)` |
| `VerifyChain`: `depth > MaxChainDepth` | `IF FALSE` | поймана `TestCycle` таймаутом (цикл не завершается) |
| `MatchHost`: `sanCount > 0` | `IF FALSE` | поймана `TestCnFallback` (SAN заслоняет CN) |
| `MatchHost`: `commonName # NIL` | `IF FALSE` | поймана `TestCnFallback` (CN без SAN) |
| `MatchHost`: `isIP & sanLen = 4` | инверсия `isIP` | поймана `TestHostIp` |
| `MatchHost`: `~isIP & MatchDNS` | убрать `~isIP` | поймана `TestHostIp` (DNS SAN совпал с IP) |
| `MatchDNS`: поиск точки | точка, после которой длины совпадают (порча B приёмки) | поймана `TestWildcard`, `TestCnFallback` (`a.b.example.com`) |
| `MatchDNS`: правило TLD `~hasDot` | `IF FALSE` | поймана `TestWildcard` (`*.com`) |
| `MatchDNS`: `CAP` при сравнении | без `CAP` | поймана `TestHostIp`, `TestWildcard` (регистр) |
| `ParseExtensions`: неизвестное критичное | `ELSIF FALSE` | поймана `TestCriticalExtension` |
| `ParseExtensions`: бит `keyCertSign` | `DIV 4` → `DIV 8` | поймана `TestIntermediate` |
| `ParseExtensions`: `isCA := bvalue` | `:= FALSE` | поймана `TestIntermediate` |
| `ParseExtensions`: `hasPathLen := TRUE` | `:= FALSE` | поймана `TestPathLen` |
| `ParseExtensions`: тег dNSName | `2` → `4` | поймана `TestParse` |
| `ReadCert`: внешний/внутренний `AlgorithmIdentifier` | убрать сравнение | поймана `TestAlgMismatch` |
| `ReadCert`: `innerHash # c.hashName` | инверсия (`=`) | поймана `TestParse` (годный сертификат отвергнут) |
| `ReadCert`: разбор внутреннего алгоритма | писать в `c.hashName` (старое поведение) | поймана `TestAlgMismatch` |
| `Verify`: `CheckDate(chain)` | `RETURN TRUE` | поймана `TestLegacyVerify` |

### TLS

| Место | Порча | Результат |
|---|---|---|
| `ReceiveServerKeyExchange`: `len < 40` | `IF FALSE` | эквивалентна: сервер уже аутентифицирован; `PKCS1.VerifyHash` сам проверяет `sigOfs+sigLen ≤ LEN(sig)` и `sigLen ≤ k`; подпись по устаревшим байтам не сойдётся |
| `ReceiveServerKeyExchange`: `len < 40` | `<` → `<=` | эквивалентна: при `len = 40` `sigLen` обязан быть 0, это отвергает следующая проверка |
| `ReceiveServerKeyExchange`: curve_type/named_curve/point_format | `IF FALSE` | эквивалентна: реализация всегда считает X25519 по 32 байтам, метки группы не участвуют в ключе и подписи |
| `ReceiveServerKeyExchange`: hash/sig id | `IF FALSE` | эквивалентна: проверка всегда SHA-256/RSA, метки — только объявление |
| `ReceiveServerKeyExchange`: `sigLen = 0/Max/len` | `IF FALSE` | эквивалентна: те же границы повторяет `PKCS1.VerifyHash` |
| `ReceiveServerKeyExchange`: `sigLen` | `>` → `>=` | поймана `client-serverhello` (SKE занимает ровно запись) |
| `ReceiveServerKeyExchange`: `CheckSignature` | `IF FALSE` | поймана `fake-badsig` |
| `ReceiveServerKeyExchange`: `X25519.AgreementChecked` | `ok := TRUE` | поймана `fake-zeropub` (общий секрет 0) |
| `ReceiveServerKeyExchange`: `signedLen := 100` | `99` | поймана `client-serverhello` |
| `ReceiveServerKeyExchange`: `serverCert = NIL` | `IF FALSE` | эквивалентна: состояние `AwaitServerKeyExchange` достижимо только после `ReceiveCertificate`; иначе SKE даёт `UnexpectedMessage` |
| `ReceiveFinished`: `len # 12` | `IF FALSE` | эквивалентна: `verify_data` — секрет из `master_secret`; длина не помогает подделке |
| `ReceiveFinished`: `len # 12` | `#` → `<` | поймана `fake-longfinished` (13 байт) |
| `ReceiveFinished`: `EqualBytesCT` | `IF FALSE` | поймана `fake-badfinished` |
| `Transition`: результат `ReceiveFinished` (клиент) | `IF FALSE` | поймана `fake-badfinished` |
| `Transition`: результат `ReceiveFinished` (сервер) | `IF FALSE` | поймана `badfinished` (клиент с чужим `Finished`) |
| `ReceiveFinished`: метка PRF | `"server finished"` → `"client finished"` | поймана `client-serverhello` |
| `ReceiveCertificateVerify`: `len < 4` | `IF FALSE` | эквивалентна: подпись проверяет `PKCS1.VerifyHash`; клиент с валидной подписью уже аутентифицирован сертификатом |
| `ReceiveCertificateVerify`: hash/sig id | `IF FALSE` | эквивалентна: проверка всегда SHA-256/RSA, метки не участвуют |
| `ReceiveCertificateVerify`: `sigLen = 0/Max/len` | `IF FALSE` | эквивалентна: границы повторяет `PKCS1.VerifyHash` |
| `ReceiveCertificateVerify`: `sigLen` | `>` → `>=` | поймана `goodcertverify` (CV занимает ровно сообщение) |
| `ReceiveCertificateVerify`: `clientCert = NIL` | `IF FALSE` | поймана `nocertcv` (трап при разыменовании) |
| `ReceiveCertificateVerify`: `VerifyHash` | `IF FALSE` | поймана `badcertverify` |
| `ReceiveChangeCipherSpec`: значение 1 | `IF FALSE` | поймана `fake-badccs` |
| `ReceiveRecord`: `len # 1` у CCS | убрать проверку | поймана `fake-longccs` (2 байта) |
| `ReceiveRecord`: `len > MaxCiphertextLength` | `IF FALSE` | поймана `fake-hugerecord` |
| `ReceiveRecord`: `len > MaxCiphertextLength` | `>` → `>=` | эквивалентна: записей ровно максимальной длины нет; отказ fail-closed |
| `ReceiveRecord`: `length # len` | `IF FALSE` | эквивалентна: недобор закрывает соединение при следующем чтении; MAC/тег и разбор handshake отвергают |
| `ReceiveRecord`: версия записи | `IF FALSE` | эквивалентна: версия записи в TLS 1.2 справочная, версии ClientHello/ServerHello проверяются отдельно |
| `ReceiveRecord`: `type < 20` / `> 23` | `IF FALSE` | эквивалентна: неизвестный тип не обрабатывается, `CASE ELSE` даёт алерт |
| `ReceiveRecord`: `type < 20` | `<` → `<=` | поймана положительными: CCS (тип 20) перестаёт приниматься |
| `ReceiveRecord`: `len < nonce+tag` | `IF FALSE` | поймана `fake-shortrec` |
| `ReceiveRecord`: `len < nonce+tag` | `<` → `<=` | эквивалентна: записей с нулевым открытым текстом нет; отказ fail-closed |
| `ReceiveRecord`: тег GCM | `IF FALSE` | поймана `fake-badtag` |
| `ReceiveHandshake`: `ptr + 4 + msgLen > len` | `IF FALSE` | эквивалентна по аутентификации: чтение остаётся в `inbuf`, подпись и `Finished` проверяются по фактическим байтам |
| `ReceiveHandshake`: `ptr + 4 + msgLen > len` | `>` → `>=` | поймана положительными: сообщение занимает ровно запись |
| `ReceiveHandshake`: `ptr + 4 > len` | `IF FALSE` | эквивалентна: следующая проверка `msgLen` отвергает обрезанный заголовок |
| `ReceiveHandshake`: `ptr + 4 > len` | `>` → `>=` | поймана положительными: пустой `ServerHelloDone` занимает ровно заголовок |
| `ReceiveCertificate`: `listLen + 3 > len` | `>` → `>=` | поймана положительными: тело сертификата занимает ровно сообщение |
| `ReceiveClientHello`: версия | `IF FALSE` | поймана `oldversion` (ClientHello 1.0) |
| `ReceiveServerHello`: версия | `IF FALSE` | поймана `fake-oldversion` (ServerHello 1.1) |
| `Transition`: гейт клиентского сертификата после CKE | `IF FALSE` | поймана `server-no-client-cert` |
| `Transition`: гейт клиентского сертификата в `AwaitFinished` | `IF FALSE` | эквивалентна: гейт после CKE не пускает непроверенного клиента в `AwaitChangeSpec` |
| `Transition`: оба гейта | `IF FALSE` | поймана `server-no-client-cert` |

### Прочие механизмы

| Механизм | Порчи | Поймал |
|---|---|---|
| Энтропия ОС | `entropy-zero`, `entropy-bounds`, `entropy-false` | `Crypto_devPRF.TestEntropy` |
| Сравнение постоянного времени | `ct-always-true`, `ct-always-false`, `ct-first-n-bytes` | `Crypto_devPRF.TestConstantTime` |
| Наборы по умолчанию | `default-3des`, `default-cbc`, `default-null` | `Crypto_devPRF.TestDefaults` |
| ASN.1 | `asn1-indefinite`, `asn1-parent-overrun`, `asn1-zero-bitstring` | `Crypto_devX509.TestAsn1` |
| 64-битные помощники, Net64 | `net64-old-encoding`, `uadd64-bad-carry`, `ulsh-bad-shift`, `uls-reversed`, `ugtr-false` | `Crypto_devBigNumbers.TestNet64`, `Test64`, `Test1` |
| PKCS#1 v1.5 | `pkcs1-verify-true`, `pkcs1-verify-false`, `pkcs1-wrong-digest-len` | `Crypto_devPKCS1.TestSlowSign` |
| GCM-тег | `gcm-tag-ignored`, `gcm-tag-always-fail`, `gcm-tag-one-byte` | `Crypto_devGcm.TestTampered`, `TestVector2` |
| TLS SKE и сертификаты | `tls-ske-skip`, `tls-ske-wrong-data`, `tls-client-cert-skip`, `tls-server-cert-skip` | `tools/test-crypto-tls.sh` (`fake-badsig`, `client-serverhello`, `untrusted`, `server-foreign-client`) |
| Состояния и границы TLS | `tls-noske-assert`, `tls-v2-unchecked` | `tools/test-crypto-tls.sh` (`fake-noske`, `v2hello` — `TRAP`) |
| SHA-384/512 | `sha512-rotr`, `sha512-iv`, `sha512-len`, `sha512-schedule`, `sha384-size` | `Crypto_devSHA512` (все пять красные) |
| HMAC-SHA-384/512 | `hmac-block`, `hmac-ipad`, `hmac-trunc` | `Crypto_devHMAC` (все три красные) |
| HKDF | `hkdf-counter`, `hkdf-prk`, `hkdf-salt` | `Crypto_devHKDF` (все три красные) |
| P-256/P-384: поле и точка | `ec-mont-sub`, `ec-formula`, `ec-digit`, `ec-oncurve`, `ec-window`, `ec-inrange`, `ec-mont-zero`, `ec384-a`, `ec384-nbits` | `Crypto_devEC` (все красные; `ec384-nbits` покраснел после того, как векторы P-384 стали полной длины — с короткими скалярами старшие 128 бит не проверялись) |
| ECDH | `ecdh-novalidate`, `ecdh-noprivcheck`, `ecdh-wrongpoint`, `ecdh-zero` | три красные; `ecdh-zero` эквивалентна: на проверенной точке простого порядка общий секрет ноль только у бесконечности, а её отвергает `Validate` |
| ECDSA RFC 6979 | `ecdsa-sep`, `ecdsa-swap`, `ecdsa-sformula`, `ecdsa-u1`, `ecdsa-compare`, `ecdsa-range` | пять красные; `ecdsa-range` на выбранных векторах эквивалентна (r=0 и s=n отвергаются арифметикой, r=p не совпадает), но проверка диапазона оставлена по RFC 6979/ECDSA |
| AES-256-GCM | `aes256` | `Crypto_devGcm.TestAES256` (красная) |
| ChaCha20 | `chacha-qr`, `chacha-sigma`, `chacha-counter`, `chacha-inc` | `Crypto_devChaCha20` (все четыре красные) |
| Poly1305 | `poly-clamp`, `poly-fullbit`, `poly-mul`, `poly-addmask`, `poly-final5` | четыре красные; `poly-final5` (+5 → +4 в окончательном приведении) на выбранных векторах не меняет тег и остаётся эквивалентной без случая на границе приведения |
| ChaCha20-Poly1305 AEAD | `aead-tag`, `aead-pad`, `aead-lens` | все три красные |
| RSA-PSS | `pss-separator`, `pss-trailer`, `pss-topbits`, `pss-ps`, `pss-saltlen`, `pss-mgf`, `pss-hcompare`, `pss-dbmask`, `pss-sign`, `pss-zerobits` | все десять красные (приёмка п. 0); `pss-topbits` — новая проверка старших бит maskedDB по RFC 8017, 9.1.2; `pss-zerobits` после неё уже не эквивалентна |
| Ed25519 | `ed-lessL`, `ed-sign`, `ed-nibble`, `ed-pointeq`, `ed-addD`, `ed-hashR` | `ed-pointeq` по одной оси не краснеет (X уже различается); при возврате `TRUE` из всего сравнения — красная. Остальные пять красные |
| X.509: алгоритмы подписи и ключи | `x509-ecdsa-skip`, `x509-pss-skip`, `x509-ed-skip`, `x509-pss-type`, `x509-ec-curve`, `x509-ed-oid`, `x509-ecdsa-hash`, `x509-int-zero` | все восемь красные (`Crypto_devX509Modern`) |

Порча `uls-signed-compare` (возврат сравнения через XOR старшего бита) тест не
краснит: на этом компиляторе эта форма эквивалентна явным ветвям, поэтому замена
— вопрос читаемости, а не поведения. Порчи `uls-reversed` и `ugtr-false` роняют
`Test1` трапом или таймаутом (деление в `DivMod` перестаёт завершаться); это
засчитано как красный результат.

## ЧТО ОСТАЛОСЬ

- TLS 1.3: нет 0-RTT, PSK, post-handshake authentication и ранних данных;
  `NewSessionTicket` игнорируется, возобновления нет. Сервер 1.3 подписывает
  RSA-PSS и ECDSA, Ed25519 только проверяется (исходящей подписи нет).
- `CryptoAosBigNumbers.NewRand` не сообщает об отказе `/dev/urandom`: у `NewPrime`
  и `NewRandRange` нет пути ошибки. На Linux источник есть всегда; чтобы убрать
  остаток, нужно менять сигнатуры `CryptoPrimes`/`CryptoRSA` и проверять `NIL`
  у всех вызывающих.
- CRL/OCSP/certificate transparency не проверяются; доверие задаётся набором корней.
- Политики сертификатов (`certificatePolicies`, `policyMappings`, `inhibitAnyPolicy`),
  `extendedKeyUsage` (`serverAuth`/`clientAuth`) и `nameConstraints` не проверяются:
  модуль не строит дерево политик RFC 5280 §6.1 и не ограничивает имена потомков.
  Для модели «доверенный набор корней, сертификаты выданы своим CA» это принято;
  для публичного доверия нужно добавить.
- Списка публичных суффиксов (PSL) нет. По RFC 6125 отвергается только шаблон над
  одной меткой верхнего уровня (`*.com`); `*.co.uk` синтаксически допустим и
  совпадает с `a.co.uk` (тест это фиксирует). Браузеры дополнительно сверяются с
  PSL; здесь политика остаётся за вызывающим.
- IDNA не реализована: имя хоста должно быть уже в A-label (`xn--…`), U-label
  побайтово не совпадёт. Шаблон внутри A-label (`xn--*.example.com`) не считается
  шаблоном и ни с чем не совпадает.
- Абсолютное имя с точкой на конце (`example.com.`) не нормализуется и не
  совпадает с шаблоном без точки.
- IPv6-литерал не поддерживается: `MatchHost` разбирает только IPv4, а
  `iPAddress` длиной 16 байт разбирается, но ни с чем не совпадает
  (fail-closed).
- Границы `notBefore`/`notAfter` сравниваются с точностью до секунды; сдвиг
  сравнения на равенство меняет только отказ в ровно ту секунду (fail-closed),
  поэтому в таблице порч помечен эквивалентным по безопасности.
- Старый `X509.Verify` — совместимый вход, который проверяет только сроки
  (и ничего не знает о пути). Для аутентификации использовать `VerifyChain`.
- Фрагментация handshake-сообщений по нескольким записям не поддержана: `ReceiveHandshake`
  отвергает сообщение, не влезшее в запись. У `Accept` нет таймаута рукопожатия:
  молчащий клиент занимает сервер.
- TLS 1.2 использует x25519/secp256r1/secp384r1 (только несжатые точки) и подписи
  RSA PKCS#1, RSA-PSS, ECDSA и проверку Ed25519. Подпись Ed25519 не реализована; сервер
  с Ed25519-сертификатом не обслуживается. P-521, сжатые точки и остальные named groups нет.
- Возобновление сессий и пересогласование — в объёме исходного кода A2, без тикетов
  TLS 1.3 и без отдельного аудита. Сервер разбирает расширения `ClientHello`
  (`supported_groups`, `signature_algorithms`) при выборе параметров, но не сверяет
  `renegotiated_connection` (RFC 5746) и отвергает пересогласование; безопасное
  пересогласование не реализовано. Extended master secret и session tickets не поддержаны.
  Для модели кластера пересогласование не используется.
- Постоянное время bignum/RSA — см. п. 7.

## Приоритет исправлений (исходный, закрыт)

1. bounds-check `sigLen` — исправлено (п. 3);
2. подпись `ServerKeyExchange` — исправлено (п. 1);
3. X.509: hostname + цепочка — исправлено (п. 2);
4. энтропия — исправлено (п. 5);
5. политика suites — исправлено (п. 6).
