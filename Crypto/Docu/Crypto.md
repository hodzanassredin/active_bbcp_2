# Crypto

`Crypto` is the cryptographic library ported from the ETH Oberon A2 System. It provides hashes,
message authentication, block and stream ciphers, authenticated encryption, big-number arithmetic,
Diffie-Hellman, RSA, X25519, X.509 parsing and a TLS 1.2 client. The TLS client is used through
`CryptoTLSStream`, a `CommStreams` protocol: `Paket` selects it by the name `"CryptoTLSStream"` for
HTTPS.

The subsystem has both the TLS 1.2 client and the TLS 1.2 server. The client verifies the server
certificate chain up to a configured trusted root, the validity period and the server name
(SAN/CN), and checks the `ServerKeyExchange` signature. The server can request and verify a client
certificate; this is the basis of the mutual authentication used by cluster nodes.

## Modules

The support modules keep the names of the A2 sources:

| Group | Modules |
|---|---|
| A2 compatibility | `CryptoAosCompat`, `CryptoAosBIT`, `CryptoAosBigNumbers`, `CryptoAosRandom`, `CryptoAosStreams`, `CryptoAosStrings`, `CryptoAosRealConversions` |
| Host support | `CryptoAosClock`, `CryptoAosDates`, `CryptoAosFileStreams`, `CryptoAosPipes`, `CryptoAosIP`, `CryptoAosCommStreams`, `CryptoAosTCP`, `CryptoTCPServices` |
| Primitives | `CryptoUtils`, `CryptoCiphers`, `CryptoAES`, `CryptoDES`, `CryptoDES3`, `CryptoARC4`, `CryptoBlowfish`, `CryptoCAST`, `CryptoIDEA`, `CryptoGcm` |
| Hashes and MAC | `CryptoHashes`, `CryptoMD5`, `CryptoSHA1`, `CryptoSHA256`, `CryptoKeccakF1600`, `CryptoKeccakSponge`, `CryptoSHA3`, `CryptoHMAC`, `CryptoBase64` |
| Public key | `CryptoAosBigNumbers`, `CryptoPrimes`, `CryptoRSA`, `CryptoPKCS1`, `CryptoDiffieHellman`, `CryptoFieldElement`, `CryptoX25519`, `CryptoASN1`, `CryptoX509` |
| Random | `CryptoOsRandom`, `CryptoFortuna`, `CryptoFortunaRng` |
| TLS | `CryptoTLS`, `CryptoTLSStream` |

## Use

A cipher is created by its module name, keyed with `InitKey`, given an IV with `SetIV` when the mode
needs one, and then used with `Encrypt` and `Decrypt`. A hash is created with
`CryptoHashes.NewHash("CryptoMD5")`, fed with `Update` and read with `GetHash`.

TLS is reached through the stream interface, as described for the TCP protocol in the
`CommStreams` documentation. Instead of the name `CommTCP`, pass the name `CryptoTLSStream` to
`CommStreams.NewStream`. Direct use of `CryptoTLS.Connection` is not recommended.

The direct API gives control over the trusted roots and the client certificate:

```componentpascal
ctx := CryptoTLS.NewContext();
ctx.AddTrustedFile("ca.pem");                 (* root of the cluster *)
ctx.LoadClientCertificateFile("node.pem");    (* optional, for mutual auth *)
ctx.LoadClientPrivateKeyFile("node.key");
con := CryptoTLS.GetConnection();
con.SetContext(ctx);
con.Open("0.0.0.0:0", "host:443", res)
```

A server loads its own certificate and key and, when mutual authentication is required, the
trusted root used to verify client certificates:

```componentpascal
ctx := CryptoTLS.NewContext();
ctx.LoadCertificateFile("server.pem");
ctx.LoadPrivateKeyFile("server.key");
ctx.AddTrustedFile("ca.pem");
ctx.RequireClientCertificate(TRUE);
srv := CryptoTLS.GetConnection();
srv.SetContext(ctx);
srv.Open("0.0.0.0:443", "", res);             (* listens *)
srv.Accept(client, res);                       (* completes the handshake *)
```

`CryptoTLS.ReadFile` reads a PEM file by an absolute path; `X509.ReadPemChain` and
`X509.ReadPemPrivateKey` parse PEM without a file system.

## Security

- The default cipher suite list contains only `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256`. CBC,
  3DES, RC4, NULL and RSA key transport remain in the code and can be enabled explicitly with
  `Policy.SetCipherSuites`; they are not offered by default.
- The client verifies the certificate chain to a trusted root, the validity period and the
  server name (SAN, then CN only when SAN is absent). A wildcard matches exactly one label.
- `ServerKeyExchange` is signed with RSA PKCS#1 v1.5/SHA-256; the client recomputes the signature
  over `client_random || server_random || ECDHParams`. A server certificate without a trusted root,
  an expired certificate, a wrong name or a forged signature aborts the handshake with an alert.
- A server that requires a client certificate sends `CertificateRequest` and verifies the client
  chain and `CertificateVerify` before accepting the client's `Finished`.
- Secret comparisons use `CryptoUtils.EqualBytesCT`: the record MAC, the GCM tag, `Finished` and
  the PKCS#1 encoded message are not compared with an early exit.
- Keys and nonces come from `CryptoOsRandom`, which reads `/dev/urandom`; the Fortuna generator
  mixes fresh OS entropy on every request. PKCS#1 padding and the TLS pre-master secret abort if
  the source is unavailable instead of using predictable bytes. Prime generation
  (`CryptoAosBigNumbers.NewRand`) reads the same source but cannot report a failure through its
  signature; the review records this as a remaining limitation.
- Network lengths are checked before any copy; malformed handshake data produces an alert and a
  closed connection, not an `ASSERT`. Where the sequence depends on the peer's choice, the
  handshake type is checked against the state; an unexpected type is an alert. SSLv2 `ClientHello`
  (both the record type 128 and handshake type 30) is rejected.

## Limitations

- TLS 1.3 is not implemented.
- Certificates must use RSA keys; the key exchange is X25519 only. No other named groups.
- CRL, OCSP and certificate transparency are not checked. The trust decision is the configured
  root set.
- The big-number arithmetic (RSA private operations) and X25519 are not constant-time at the
  machine level. The X25519 ladder uses a branch-free conditional swap, but modular
  exponentiation in `CryptoAosBigNumbers` is data-dependent. This is documented in the security
  review and is acceptable for the cluster threat model; it is not suitable for a shared server
  exposed to a local high-resolution timing attacker.
- Session resumption and renegotiation are supported only as far as the original A2 code did;
  the server does not implement TLS 1.3 tickets.
- A handshake message must fit in one TLS record; fragmentation across records is rejected. The
  server has no handshake timeout, so a silent client holds `Accept`.
- There is no system CA store. `CryptoTLSStream` uses the default context and therefore rejects
  every certificate until the caller configures trusted roots through the direct
  `CryptoTLS.NewContext` API.

## Build

The subsystem is part of the AMD64 world built by `tools/build64.sh`. It has no `Mod64` directory:
the 64-bit helpers of `CryptoAosCompat` are written in Component Pascal and the same sources serve
the AMD64 target. The i386 compiler does not build `CryptoAosCompat`; it stops with "not enough
floating-point registers" at the nested `LONGINT` division in `AosCompat.cp`, so `Crypto` is absent
from `tools/build32.sh` and `tools/test32.sh`.

## Tests

Unit tests live in `Crypto_dev`: `Crypto_devHashes`, `Crypto_devHMAC`, `Crypto_devCiphers`,
`Crypto_devGcm`, `Crypto_devBigNumbers`, `Crypto_devDH`, `Crypto_devRSA`, `Crypto_devX25519`,
`Crypto_devX509`, `Crypto_devPKCS1` and `Crypto_devPRF`. `tools/test.sh` runs them from
`tools/tests.txt`. The reference values are also stored as files with their sources and licences in
`Crypto_dev/Vectors`; `Crypto_dev/Vectors/check.py` verifies them with an independent Python
implementation.

The TLS handshake needs a peer process, so it has a separate scenario:
`tools/test-crypto-tls.sh` creates a CA, server, client, expired and wrong-host certificates in a
temporary directory, runs the client against `openssl s_server -rev` and the server against
`openssl s_client`, checks mutual authentication in both directions, and requires refusal (without a
process trap) for an untrusted CA, an expired certificate, a wrong name, a forged
`ServerKeyExchange` signature, an oversized signature length, a bad certificate length, an
impossible extension length, a missing `ServerKeyExchange`, an SSLv2 `ClientHello` and an unknown
handshake type. `Crypto_devTLS` and `Crypto_devTLSServer` are not in `tools/tests.txt`,
because without a peer they cannot run.


## Licence and known limitations

The subsystem is distributed under the BSD licences of ETH Zurich and Hirbod Behnam; the full text
is in `Crypto/LICENSE`. The security review, with the status of every finding and the test that
covers it, is in `Crypto/Docu/Security-Review.md`.
