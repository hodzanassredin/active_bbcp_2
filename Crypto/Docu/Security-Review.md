# Crypto64 — обзор безопасности (2026-11-26)

Объект: `Crypto` (порт bbext/Crypto64). Метод: чтение исходников, эталонные векторы,
отрицательные тесты и прогоны против `openssl` в обе стороны, порча кода под новые
тесты. Исходный обзор от 2026-08-19 сохранён по смыслу: ниже по каждой находке указано,
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
| 6. Наборы шифров по умолчанию | исправлено | `dd52f048` | `TestDefaults` |
| 7. Постоянное время | частично, с пометкой | `262ba455` | `TestConstantTime`, `EqualBytesCT` |
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
  подпись; `CheckSignature` проверяет RSA PKCS#1 v1.5 по `tbs`, а не заглушка.
- `VerifyChain` строит путь лист → издатели → доверенный корень, проверяет подпись
  каждого звена, сроки и `basicConstraints CA:TRUE`; незавершённый путь отвергается.
- `MatchHost` проверяет SAN `dNSName`/`iPAddress`, CN — только при отсутствии SAN;
  шаблон `*` совпадает ровно с одной меткой.
- `Context.AddTrustedPem/AddTrustedFile` задают доверенные корни; `LoadCertificateFile`
  и `LoadPrivateKeyFile` читают PEM сертификата и ключа (PKCS#1 и PKCS#8).
- `Bypass` больше не обходит проверку: старый `Verify` проверяет только сроки, а
  `VerifyChain` — полный путь.

Тесты: `Crypto_devX509` (разбор, цепочка, IP/DNS SAN, чужое имя, просроченный лист,
подделанная подпись, обрезанный и мусорный DER); `tools/test-crypto-tls.sh`
(`untrusted`, `expired`, `wrong-host`, `server-foreign-client`).

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

## ПРОВЕРКА ПОРЧЕЙ

Каждый механизм проверен порчей кода: тест обязан покраснеть. Ниже — порчи и
поймавший их тест; испорченный исходник каждый раз возвращался и пересобирался.
Всего 34 порчи, 33 пойманы; одна (возврат старой формы `ULssRaw`) не меняет
поведения и объяснена в конце таблицы.

| Механизм | Порчи | Поймал |
|---|---|---|
| Энтропия ОС | `entropy-zero`, `entropy-bounds`, `entropy-false` | `Crypto_devPRF.TestEntropy` |
| Сравнение постоянного времени | `ct-always-true`, `ct-always-false`, `ct-first-n-bytes` | `Crypto_devPRF.TestConstantTime` |
| Наборы по умолчанию | `default-3des`, `default-cbc`, `default-null` | `Crypto_devPRF.TestDefaults` |
| X.509 | `x509-signature-ignored`, `x509-root-ignored`, `x509-hostname-ignored`, `x509-expiry-ignored` | `Crypto_devX509.TestTampered`, `TestChain`, `TestParse`, `TestExpired` |
| ASN.1 | `asn1-indefinite`, `asn1-parent-overrun`, `asn1-zero-bitstring` | `Crypto_devX509.TestAsn1` |
| 64-битные помощники, Net64 | `net64-old-encoding`, `uadd64-bad-carry`, `ulsh-bad-shift`, `uls-reversed`, `ugtr-false` | `Crypto_devBigNumbers.TestNet64`, `Test64`, `Test1` |
| PKCS#1 v1.5 | `pkcs1-verify-true`, `pkcs1-verify-false`, `pkcs1-wrong-digest-len` | `Crypto_devPKCS1.TestSlowSign` |
| GCM-тег | `gcm-tag-ignored`, `gcm-tag-always-fail`, `gcm-tag-one-byte` | `Crypto_devGcm.TestTampered`, `TestVector2` |
| TLS SKE и сертификаты | `tls-ske-skip`, `tls-ske-wrong-data`, `tls-client-cert-skip`, `tls-server-cert-skip` | `tools/test-crypto-tls.sh` (`fake-badsig`, `client-serverhello`, `untrusted`, `server-foreign-client`) |
| Состояния и границы TLS | `tls-noske-assert`, `tls-v2-unchecked` | `tools/test-crypto-tls.sh` (`fake-noske`, `v2hello` — `TRAP`) |

Порча `uls-signed-compare` (возврат сравнения через XOR старшего бита) тест не
краснит: на этом компиляторе эта форма эквивалентна явным ветвям, поэтому замена
— вопрос читаемости, а не поведения. Порчи `uls-reversed` и `ugtr-false` роняют
`Test1` трапом или таймаутом (деление в `DivMod` перестаёт завершаться); это
засчитано как красный результат.

## ЧТО ОСТАЛОСЬ

- TLS 1.3 не реализован (в этапе 3 он был «если укладывается»).
- `CryptoAosBigNumbers.NewRand` не сообщает об отказе `/dev/urandom`: у `NewPrime`
  и `NewRandRange` нет пути ошибки. На Linux источник есть всегда; чтобы убрать
  остаток, нужно менять сигнатуры `CryptoPrimes`/`CryptoRSA` и проверять `NIL`
  у всех вызывающих.
- CRL/OCSP/certificate transparency не проверяются; доверие задаётся набором корней.
- Фрагментация handshake-сообщений по нескольким записям не поддержана: `ReceiveHandshake`
  отвергает сообщение, не влезшее в запись. У `Accept` нет таймаута рукопожатия:
  молчащий клиент занимает сервер.
- Только RSA-сертификаты и X25519; ECDSA, AES-256-GCM и другие группы не добавлены.
- Возобновление сессий и пересогласование — в объёме исходного кода A2, без тикетов
  TLS 1.3 и без отдельного аудита.
- Постоянное время bignum/RSA — см. п. 7.

## Приоритет исправлений (исходный, закрыт)

1. bounds-check `sigLen` — исправлено (п. 3);
2. подпись `ServerKeyExchange` — исправлено (п. 1);
3. X.509: hostname + цепочка — исправлено (п. 2);
4. энтропия — исправлено (п. 5);
5. политика suites — исправлено (п. 6).
