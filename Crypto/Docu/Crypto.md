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
| Primitives | `CryptoUtils`, `CryptoCiphers`, `CryptoAES`, `CryptoDES`, `CryptoDES3`, `CryptoARC4`, `CryptoBlowfish`, `CryptoCAST`, `CryptoIDEA`, `CryptoGcm`, `CryptoChaCha20` |
| Hashes and MAC | `CryptoHashes`, `CryptoMD5`, `CryptoSHA1`, `CryptoSHA256`, `CryptoSHA384`, `CryptoSHA512`, `CryptoKeccakF1600`, `CryptoKeccakSponge`, `CryptoSHA3`, `CryptoHMAC`, `CryptoHKDF`, `CryptoBase64` |
| Public key | `CryptoAosBigNumbers`, `CryptoPrimes`, `CryptoRSA`, `CryptoPKCS1`, `CryptoDiffieHellman`, `CryptoFieldElement`, `CryptoX25519`, `CryptoEC`, `CryptoECDSA`, `CryptoEd25519`, `CryptoPSS`, `CryptoASN1`, `CryptoX509` |
| Random | `CryptoOsRandom`, `CryptoFortuna`, `CryptoFortunaRng` |
| TLS | `CryptoTLS`, `CryptoTLSStream` |

## Use

A cipher is created by its module name, keyed with `InitKey`, given an IV with `SetIV` when the mode
needs one, and then used with `Encrypt` and `Decrypt`. A hash is created with
`CryptoHashes.NewHash("CryptoMD5")`, fed with `Update` and read with `GetHash`. The SHA-2 family
covers SHA-256, SHA-384 and SHA-512; the name of the hash selects the output length.

Key derivation uses `CryptoHKDF`. `Extract` turns input key material and a salt into a
pseudorandom key, `Expand` stretches it to the requested length with an info string, and `HKDF`
does both:

```componentpascal
CryptoHKDF.Extract("CryptoSHA256", salt, saltLen, ikm, ikmLen, prk);
ok := CryptoHKDF.Expand("CryptoSHA256", prk, 32, info, infoLen, okm, okmLen)
```

`CryptoHMAC` uses the block size of the underlying hash, so a key longer than the block is hashed
once and the inner and outer pads have the size of the hash block.

`CryptoGcm` works with AES-128 and AES-256: `InitKey(key, 256)` selects the 14-round key schedule.
`CryptoChaCha20` provides ChaCha20, Poly1305 and the combined `ChaCha20-Poly1305` AEAD named in
RFC 8439. The AEAD takes a 32-byte key, a 12-byte nonce and additional data, and returns a
16-byte tag:

```componentpascal
CryptoChaCha20.Encrypt(key, nonce, aad, aadLen, plain, plainLen, cipher, tag);
ok := CryptoChaCha20.Decrypt(key, nonce, aad, aadLen, cipher, plainLen, tag, plain)
```

Elliptic-curve keys use `CryptoEC` for P-256 and P-384. A point is decoded from the uncompressed
SEC1 form (`0x04 || X || Y`), decoded and validated before use, and ECDH returns the x-coordinate
of the shared point:

```componentpascal
c := CryptoEC.P256();
ok := CryptoEC.PointFromBytes(c, peer, 65, qx, qy);
ok := CryptoEC.ECDH(c, privateKey, qx, qy, sharedX)
```

`CryptoECDSA` signs and verifies with the hash of the curve (SHA-256 for P-256, SHA-384 for
P-384). The nonce is deterministic (RFC 6979), so the same key and hash give the same `r` and `s`;
`r` and `s` are fixed-length big-endian values of the order size.

```componentpascal
ok := CryptoECDSA.Sign(c, privateKey, hash, hashLen, r, s);
ok := CryptoECDSA.Verify(c, qx, qy, hash, hashLen, r, s)
```

`CryptoPSS` signs and verifies RSASSA-PSS with a salt as long as the hash and MGF1 over the same
hash. The signature is the modulus-sized byte string; the hash is passed already computed:

```componentpascal
CryptoPSS.SignHash("CryptoSHA256", digest, 32, privateKey, sig, 0, sigLen);
ok := CryptoPSS.VerifyHash("CryptoSHA256", digest, 32, publicKey, sig, 0, sigLen)
```

`CryptoEd25519.Verify` checks an Ed25519 signature (public key 32 bytes, signature 64 bytes) over
a message. Signature creation is not implemented; verification is what certificates and TLS need:

```componentpascal
ok := CryptoEd25519.Verify(publicKey, message, messageLen, signature)
```

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
`X509.ReadPemPrivateKey` parse PEM without a file system. `X509.ReadPemEcPrivateKey` reads an
ECDSA private key from SEC1 (`EC PRIVATE KEY`) or PKCS#8 (`PRIVATE KEY`) and returns the curve
and the scalar; `Context.LoadEcPrivateKeyFile` and `LoadClientEcPrivateKeyFile` use it for
ECDHE-ECDSA suites.

## Security

- The TLS 1.2 client and server use ECDHE on X25519, secp256r1 or secp384r1 and AEAD records.
  The default policy offers ECDHE-ECDSA and ECDHE-RSA with AES-128-GCM-SHA256,
  AES-256-GCM-SHA384 and ChaCha20-Poly1305-SHA256 (six suites). CBC, 3DES, RC4, NULL and RSA
  key transport remain in the code and can be enabled explicitly with `Policy.SetCipherSuites`;
  they are not offered by default.
- The client sends `signature_algorithms` with ECDSA P-256/P-384, RSA-PSS with SHA-256/384,
  RSA PKCS#1 v1.5 SHA-256 and Ed25519 (verification only), `supported_groups` with the three
  curves, `ec_point_formats` (uncompressed) and an empty `renegotiation_info`. The server picks a
  suite compatible with its certificate, a signature scheme offered by the client and, for an
  ECDSA certificate, the ECDHE group matching the certificate curve (OpenSSL enforces the same
  rule in TLS 1.2).
- X.509 supports RSA PKCS#1 v1.5, RSA-PSS, ECDSA with P-256/P-384 and Ed25519 signatures, and
  RSA, EC (P-256/P-384) and Ed25519 public keys in SPKI. A chain may mix these algorithms; each
  link is checked with the key type of its issuer.
- The client verifies the certificate chain to a trusted root, the validity period of every
  link (including the root), `basicConstraints CA`, `keyUsage keyCertSign` when the extension is
  present, `pathLenConstraint` and unknown critical extensions. A certificate with the same name
  as a trusted root but a different key, an unfinished path and a chain longer than
  `MaxChainDepth` are rejected. The server name is matched against SAN `dNSName`/`iPAddress`;
  CN is used only when SAN is absent. An IP address never matches a DNS name and vice versa; a
  wildcard matches exactly one label and is not allowed over a top-level domain (`*.com`).
- `ServerKeyExchange` is signed by the negotiated scheme (RSA PKCS#1 v1.5, RSA-PSS or ECDSA;
  Ed25519 can be verified but not signed); the client recomputes the signature
  over `client_random || server_random || ECDHParams`. ECDSA signatures are DER
  `ECDSA-Sig-Value`; a hash shorter or longer than the curve order is converted to an integer
  as ECDSA requires. A server certificate without a trusted root,
  an expired certificate, a wrong name or a forged signature aborts the handshake with an alert.
- A server that requires a client certificate sends `CertificateRequest` and verifies the client
  chain and `CertificateVerify` before accepting the client's `Finished`.
- A rejected `Finished`, a `ChangeCipherSpec` with a wrong value and a record with a bad GCM tag
  close the connection; `ReceiveFinished` returns the result and the handshake state is not
  advanced to `Established`.
- Secret comparisons use `CryptoUtils.EqualBytesCT`: the record MAC, the GCM tag, the
  ChaCha20-Poly1305 tag, `Finished` and the PKCS#1 encoded message are not compared with an early
  exit. Poly1305 uses 26-bit limbs and 64-bit accumulators; the final reduction selects between
  the two candidates with a mask. `CryptoHKDF` builds on
  `CryptoHMAC`, so HMAC-SHA-256, HMAC-SHA-384 and HMAC-SHA-512 use the 128-byte block of SHA-384
  and SHA-512 and the 64-byte block of SHA-256.
- P-256 and P-384 use the complete projective addition formulas of Renes, Costello and Batina.
  They have no exceptional cases, so doubling, addition of a point to itself and addition of
  opposite points use the same sequence. Scalar multiplication is a fixed 4-bit window: the number
  of additions depends only on the curve size, the table entry is chosen by a masked scan of all
  16 entries, and the conditional subtraction in field operations uses masks rather than
  branches. `InRange` and `IsZeroN` do not exit early. `CryptoEC.Validate` checks that both
  coordinates are below the field modulus and that the point satisfies the curve equation; ECDH,
  ECDSA verification and point decoding reject a point outside the curve, the point (0,0) and an
  empty encoding.
- ECDSA follows RFC 6979: the nonce is HMAC-DRBG output over the private key and the reduced hash,
  so a repeated signature is impossible without a repeated message. Verification rejects `r` or
  `s` equal to zero or at least the order, and compares the recomputed `r` in constant time.
- `CryptoEC` works on fixed 32-bit limbs with Montgomery multiplication. Field negation,
  inversion and exponentiation use the public modulus and the public exponent; the private scalar
  only enters through the masked window selection.
- RSA-PSS follows RFC 8017 with `emBits = modBits - 1`, a salt of the hash length and MGF1 over
  the same hash. The final hash is compared with `EqualBytesCT`; the recovered encoded message is
  checked for zero leading bits of `maskedDB`, the leading zero octets, the `0xbc` trailer, the
  `0x01` separator and the salt. `Crypto_devPSS` rejects malformed encoded messages from
  `Vectors/pss-negative.txt` (separator `0x00`/`0x02`, nonzero PS, trailer `0xbb`, a set leading
  bit, salt length 31 or 33), each signed with the real key.
- Ed25519 follows RFC 8032. `Decode` rejects `y >= p`, a non-square `x`, and `x = 0` with the sign
  bit set; `Verify` rejects `S >= L`, which removes the signature malleability of `S + L`. The
  extended-coordinate addition formula is complete for the curve, so the same sequence handles
  doubling and the neutral element.
- X.509 accepts RSA-PSS only with an explicit hash, MGF1 over the same hash and a salt equal to
  the hash length; other parameters give `ErrUnsupported`. An ECDSA signature is parsed from DER
  as two positive integers (a leading zero is allowed, a negative value is rejected) and checked
  against the curve of the issuer key; a hash whose length does not match the curve is refused.
  For Ed25519 the TBS bytes are verified directly.
- A certificate whose `AlgorithmIdentifier` names an unsupported or unknown signature algorithm is
  rejected on read. The inner and outer signature algorithms of the certificate must match,
  including the PSS salt length.
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
- The TLS 1.2 handshake supports uncompressed points on X25519, secp256r1 and secp384r1; P-521,
  compressed points and the other named curves are not implemented. The extended master secret,
  session tickets, secure renegotiation and 0-RTT/PSK are not implemented; an empty
  `renegotiation_info` is sent and any renegotiation is refused.
- Ed25519 signing is not implemented (verification is). RSA-PSS uses a fixed salt length equal to
  the hash length; other salt lengths are not accepted. The built-in server signs with RSA-PSS or
  ECDSA according to its certificate and does not serve an Ed25519 certificate.
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

Unit tests live in `Crypto_dev`: `Crypto_devHashes`, `Crypto_devSHA512`, `Crypto_devHMAC`,
`Crypto_devHKDF`, `Crypto_devCiphers`, `Crypto_devGcm`, `Crypto_devChaCha20`, `Crypto_devBigNumbers`, `Crypto_devDH`,
`Crypto_devRSA`, `Crypto_devX25519`, `Crypto_devEC`, `Crypto_devECDSA`, `Crypto_devEd25519`, `Crypto_devPSS`, `Crypto_devX509`,
`Crypto_devX509Modern`,
`Crypto_devPKCS1` and `Crypto_devPRF`.
`tools/test.sh` runs them from `tools/tests.txt`. The reference values are also stored as files
with their sources and licences in `Crypto_dev/Vectors`; `Crypto_dev/Vectors/check.py` verifies
them with an independent Python implementation.

The TLS handshake needs a peer process, so it has a separate scenario:
`tools/test-crypto-tls.sh` creates a CA, an intermediate CA, a `CA:FALSE` issuer, RSA, ECDSA
P-256 and ECDSA P-384 server certificates, client, expired, wrong-host and expired-root
certificates in a temporary directory, runs the client against `openssl s_server -rev` and the
server against `openssl s_client`, checks the matrix of ECDHE-RSA/ECDHE-ECDSA × AES-128/256-GCM
and ChaCha20-Poly1305 × x25519/secp256r1/secp384r1 in both directions, checks mutual
authentication with RSA and ECDSA server certificates, and requires refusal (without a process
trap) for an untrusted CA, an expired certificate or intermediate root, a wrong name, a
`CA:FALSE` issuer, a forged `ServerKeyExchange` signature (RSA PKCS#1, RSA-PSS and ECDSA), a bad
`Finished`, a wrong `ChangeCipherSpec`, a corrupted AES-GCM or ChaCha20-Poly1305 tag, a group the
client did not offer, a secp256r1 point outside the curve, an oversized signature length, a bad
certificate length, an impossible extension length, a missing `ServerKeyExchange`, a zero X25519
public key, an SSLv2 `ClientHello` and an unknown handshake type. `tools/crypto_tls_lib.py`,
`crypto_tls_fake.py` and `crypto_tls_client.py` are the Python peers used for the cases that need
a correct handshake transcript: bad `Finished`, bad `ChangeCipherSpec`, corrupted record tag,
zero X25519 point and a bad or good `CertificateVerify`. `Crypto_devTLS` and
`Crypto_devTLSServer` are not in `tools/tests.txt`, because without a peer they cannot run.


### Public hosts (manual, network)

`tools/test-crypto-hosts.sh` (not in `tools/test.sh`) compiles `Crypto_devHosts`, loads the system
CA bundle (`/etc/ssl/certs/ca-certificates.crt` by default), connects to a list of public HTTPS
hosts and sends `GET / HTTP/1.0`. For each host it prints the negotiated version, cipher suite,
ECDHE group, handshake signature scheme and the first response line. The run of 2026-11-26
connected to all eight hosts over TLS 1.2:

| Host | Suite | Group | Signature |
|---|---|---|---|
| google.com | ECDHE-ECDSA-CHACHA20-POLY1305-SHA256 | x25519 | ecdsa_secp256r1_sha256 |
| cloudflare.com | ECDHE-ECDSA-CHACHA20-POLY1305-SHA256 | x25519 | ecdsa_secp256r1_sha256 |
| github.com | ECDHE-ECDSA-AES128-GCM-SHA256 | x25519 | ecdsa_secp256r1_sha256 |
| letsencrypt.org | ECDHE-ECDSA-AES128-GCM-SHA256 | x25519 | ecdsa_secp256r1_sha256 |
| wikipedia.org | ECDHE-ECDSA-AES256-GCM-SHA384 | x25519 | ecdsa_secp256r1_sha256 |
| microsoft.com | ECDHE-RSA-AES256-GCM-SHA384 | secp256r1 | rsa_pss_rsae_sha256 |
| apple.com | ECDHE-ECDSA-AES128-GCM-SHA256 | x25519 | ecdsa_secp256r1_sha256 |
| amazon.com | ECDHE-RSA-AES128-GCM-SHA256 | x25519 | rsa_pss_rsae_sha256 |

The run exposed and fixed the following: `sha384WithRSAEncryption`/`sha512WithRSAEncryption`
certificate signatures were rejected; PKCS#1 DigestInfo knew only MD5/SHA-1/SHA-256, so SHA-384
and SHA-512 chains failed; a cross-signed root (same subject and public key as a trusted root,
different issuer) was not matched; SAN lists were truncated at 16 entries; one unreadable
certificate aborted the whole trust bundle, and `AddTrustedFile` had a 16 KiB buffer. All of
these are covered by `tools/test-crypto-tls.sh` (SHA-384/SHA-512 chains, a 22-entry SAN list, a
bundle with a garbage certificate, a 113 KiB bundle); the cross-signed-root case is covered by
the manual hosts run and by the mutation table in the security review.



The subsystem is distributed under the BSD licences of ETH Zurich and Hirbod Behnam; the full text
is in `Crypto/LICENSE`. The security review, with the status of every finding and the test that
covers it, is in `Crypto/Docu/Security-Review.md`.
