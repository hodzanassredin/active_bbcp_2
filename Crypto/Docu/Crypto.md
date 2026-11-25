# Crypto

`Crypto` is the cryptographic library ported from the ETH Oberon A2 System. It provides hashes,
message authentication, block and stream ciphers, authenticated encryption, big-number arithmetic,
Diffie-Hellman, RSA, X25519, X.509 parsing and a TLS 1.2 client. The TLS client is used through
`CryptoTLSStream`, a `CommStreams` protocol: `Paket` selects it by the name `"CryptoTLSStream"` for
HTTPS.

The TLS implementation supports the client side and TLS version 1.2. It adds elliptic-curve
Diffie-Hellman key exchange, the SNI extension and the GCM cipher mode to the A2 code.

## Modules

The support modules keep the names of the A2 sources:

| Group | Modules |
|---|---|
| A2 compatibility | `CryptoAosCompat`, `CryptoAosBIT`, `CryptoAosBigNumbers`, `CryptoAosRandom`, `CryptoAosStreams`, `CryptoAosStrings`, `CryptoAosRealConversions` |
| Host support | `CryptoAosClock`, `CryptoAosDates`, `CryptoAosFileStreams`, `CryptoAosPipes`, `CryptoAosIP`, `CryptoAosCommStreams`, `CryptoAosTCP`, `CryptoTCPServices` |
| Primitives | `CryptoUtils`, `CryptoCiphers`, `CryptoAES`, `CryptoDES`, `CryptoDES3`, `CryptoARC4`, `CryptoBlowfish`, `CryptoCAST`, `CryptoIDEA`, `CryptoGcm` |
| Hashes and MAC | `CryptoHashes`, `CryptoMD5`, `CryptoSHA1`, `CryptoSHA256`, `CryptoKeccakF1600`, `CryptoKeccakSponge`, `CryptoSHA3`, `CryptoHMAC`, `CryptoBase64` |
| Public key | `CryptoAosBigNumbers`, `CryptoPrimes`, `CryptoRSA`, `CryptoPKCS1`, `CryptoDiffieHellman`, `CryptoFieldElement`, `CryptoX25519`, `CryptoASN1`, `CryptoX509` |
| Random | `CryptoFortuna`, `CryptoFortunaRng` |
| TLS | `CryptoTLS`, `CryptoTLSStream` |

## Use

A cipher is created by its module name, keyed with `InitKey`, given an IV with `SetIV` when the mode
needs one, and then used with `Encrypt` and `Decrypt`. A hash is created with
`CryptoHashes.NewHash("CryptoMD5")`, fed with `Update` and read with `GetHash`.

TLS is reached through the stream interface, as described for the TCP protocol in the
`CommStreams` documentation. Instead of the name `CommTCP`, pass the name `CryptoTLSStream` to
`CommStreams.NewStream`. Direct use of `CryptoTLS.Connection` is not recommended.

## Build

The subsystem is part of the AMD64 world built by `tools/build64.sh`. It has no `Mod64` directory:
the 64-bit helpers of `CryptoAosCompat` are written in Component Pascal and the same sources serve
the AMD64 target. The i386 compiler does not build `CryptoAosCompat`; it stops with "not enough
floating-point registers" at the nested `LONGINT` division in `AosCompat.cp`, so `Crypto` is absent
from `tools/build32.sh` and `tools/test32.sh`.

## Tests

Unit tests live in `Crypto_dev`: `Crypto_devHashes`, `Crypto_devHMAC`, `Crypto_devCiphers`,
`Crypto_devBigNumbers`, `Crypto_devDH`, `Crypto_devRSA`, `Crypto_devX25519`. `tools/test.sh` runs
them from `tools/tests.txt`.

The TLS handshake needs a peer process, so it has a separate scenario:
`tools/test-crypto-tls.sh` creates a self-signed certificate in a temporary directory, starts
`openssl s_server` on `127.0.0.1`, runs the `Crypto_devTLS` test against it and stops the server by
PID. The module `Crypto_devTLS` is not in `tools/tests.txt`, because without a server it cannot run.
The client sends `hello`, reads the reversed line from `s_server -rev` and requires the handshake
and the exchange to succeed.

## Licence and known limitations

The subsystem is distributed under the BSD licences of ETH Zurich and Hirbod Behnam; the full text
is in `Crypto/LICENSE`. The review of the code, including the missing verification of the server
certificate and of the `ServerKeyExchange` signature, is in `Crypto/Docu/Security-Review.md`.
