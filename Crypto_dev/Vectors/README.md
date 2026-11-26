# Эталонные векторы Crypto

Значения взяты из опубликованных источников; файлы нужны, чтобы
происхождение проверок было видно и проверяемо независимо от кода
Component Pascal. Те же значения зашиты в модулях `Crypto_dev*` и
исполняются `tools/test.sh`; `Crypto_dev/Vectors/check.py` сверяет
файлы с независимой реализацией на Python.

| Файл | Источник | Лицензия |
|---|---|---|
| `aes.txt` | FIPS 197, NIST SP 800-38A | public domain (US Government work) |
| `gcm.txt` | NIST SP 800-38D, тестовые векторы GCM (AES-128 и AES-256) | public domain (US Government work) |
| `sha.txt` | FIPS 180-4 (SHA-1, SHA-256, SHA-384, SHA-512), FIPS 202 (SHA3-256) | public domain (US Government work) |
| `hmac.txt` | RFC 2104, RFC 4231 (MD5, SHA-1, SHA-256, SHA-384, SHA-512) | IETF Trust, BSD-подобная лицензия RFC |
| `hkdf.txt` | RFC 5869, случаи 1–3; SHA-512 — независимый расчёт по RFC 5869 | IETF Trust |
| `x25519.txt` | RFC 7748, раздел 6.1 | IETF Trust |
| `ecdsa-wycheproof.txt` | Wycheproof `ecdsa_secp256r1_sha256_test.json`, выбранные случаи; плюс векторы NIST CAVP и RFC 6979 в `Crypto_devECDSA` | Apache License 2.0 |
| `ecdh-wycheproof.txt` | Wycheproof `ecdh_secp256r1_test.json`, выбранные случаи; плюс независимо посчитанные векторы P-256/P-384 в `Crypto_devEC` | Apache License 2.0 |
| `prf.txt` | RFC 5246, P_SHA256; значения посчитаны независимо на Python/hmac | IETF Trust |
| `chacha.txt` | RFC 8439: блок ChaCha20, поток, Poly1305, AEAD | IETF Trust |
| `pss-wycheproof.txt` | Wycheproof `rsa_pss_2048_sha256_mgf1_32_test.json`, выбранные случаи | Apache License 2.0 |
| `ed25519-wycheproof.txt` | Wycheproof `ed25519_test.json`, выбранные случаи; векторы RFC 8032 в `Crypto_devEd25519` | Apache License 2.0 |
| `x509/modern/` | сертификаты ECDSA P-256/P-384, RSA-PSS, Ed25519 и смешанная цепочка; `make-modern.py` и `make-modern-data.py` пересоздают `Crypto_devX509ModernData`; ключи удалены | CC0 / public domain |
| `x509/` | сертификаты сгенерированы для этого репозитория (CA, лист, просроченный); ключи удалены | CC0 / public domain |
| `x509/chains/` | цепочки для проверки пути: промежуточные CA, `CA:FALSE`-издатель, `pathLen`, `keyUsage`, критичное расширение, цикл; `generate-chains.sh` пересоздаёт их, `make-data.py` собирает `Crypto_devX509Data` | CC0 / public domain |

`check.py` требует `cryptography` для AES, GCM, X25519, ChaCha20, Poly1305, ECDSA/ECDH,
RSA-PSS и Ed25519;
без него эти группы пропускаются, хеши/HMAC/HKDF/PRF проверяются всегда.
