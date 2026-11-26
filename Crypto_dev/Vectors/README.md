# Эталонные векторы Crypto

Значения взяты из опубликованных источников; файлы нужны, чтобы
происхождение проверок было видно и проверяемо независимо от кода
Component Pascal. Те же значения зашиты в модулях `Crypto_dev*` и
исполняются `tools/test.sh`; `Crypto_dev/Vectors/check.py` сверяет
файлы с независимой реализацией на Python.

| Файл | Источник | Лицензия |
|---|---|---|
| `aes.txt` | FIPS 197, NIST SP 800-38A | public domain (US Government work) |
| `gcm.txt` | NIST SP 800-38D, тестовые векторы GCM | public domain (US Government work) |
| `sha.txt` | FIPS 180-4 (SHA-1, SHA-256), FIPS 202 (SHA3-256) | public domain (US Government work) |
| `hmac.txt` | RFC 2104, RFC 4231 | IETF Trust, BSD-подобная лицензия RFC |
| `x25519.txt` | RFC 7748, раздел 6.1 | IETF Trust |
| `prf.txt` | RFC 5246, P_SHA256; значения посчитаны независимо на Python/hmac | IETF Trust |
| `x509/` | сертификаты сгенерированы для этого репозитория (CA, лист, просроченный); ключи удалены | CC0 / public domain |

`check.py` требует `cryptography` для AES и X25519; без него эти
группы пропускаются, хеши/HMAC/PRF проверяются всегда.
