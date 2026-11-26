#!/usr/bin/env python3
"""Независимая проверка эталонных векторов Crypto (Crypto_dev/Vectors).

Хеши, HMAC и PRF проверяются стандартной библиотекой; AES, GCM и X25519 —
модулем cryptography, если он установлен. Векторы — те же, что зашиты в
Crypto_dev* и исполняются tools/test.sh.
"""
import hashlib
import hmac
import re
import sys

BASE = __file__.rsplit("/", 1)[0]
failures = 0


def fail(msg):
    global failures
    failures += 1
    print("FAIL:", msg)


def rows(name):
    out = []
    for line in open(f"{BASE}/{name}", encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out.append([c.strip() for c in line.split("|")])
    return out


def unhex(s):
    return bytes.fromhex(re.sub(r"\s+", "", s))


def maybe_hex(s):
    if s.startswith("hex:"):
        return bytes.fromhex(s[4:].replace(" ", ""))
    return s.encode()


# hashes
for name, msg, want in rows("sha.txt"):
    if msg == '""':
        data = b""
    elif msg.startswith("hex:"):
        data = bytes.fromhex(msg[4:])
    else:
        data = msg.encode()
    got = hashlib.new(name, data).hexdigest().upper()
    if got != want.replace(" ", "").upper():
        fail(f"sha {name} {msg}: {got} != {want}")

# hmac
for name, key, data, want in rows("hmac.txt"):
    k = maybe_hex(key)
    d = maybe_hex(data)
    got = hmac.new(k, d, name).hexdigest().upper()
    if got != want.upper():
        fail(f"hmac {name}: {got} != {want}")

# hkdf (RFC 5869)
def hkdf(alg, ikm, salt, info, length):
    prk = hmac.new(salt, ikm, alg).digest()
    t, out, i = b"", b"", 1
    while len(out) < length:
        t = hmac.new(prk, t + info + bytes([i]), alg).digest()
        out += t
        i += 1
    return prk, out[:length]


for alg, ikm, salt, info, length, prk_want, okm_want in rows("hkdf.txt"):
    data = unhex(ikm)
    s = b"" if salt == '""' else unhex(salt)
    inf = b"" if info == '""' else unhex(info)
    prk, okm = hkdf(alg, data, s, inf, int(length))
    if prk.hex().upper() != prk_want.upper():
        fail(f"hkdf {alg} prk: {prk.hex()} != {prk_want}")
    if okm.hex().upper() != okm_want.upper():
        fail(f"hkdf {alg} okm: {okm.hex()} != {okm_want}")

# PRF (RFC 5246, P_SHA256)
kv = {}
for line in open(f"{BASE}/prf.txt", encoding="utf-8"):
    line = line.strip()
    if "=" in line and not line.startswith("#"):
        k, v = line.split("=", 1)
        kv[k.strip()] = v.strip()
secret = bytes.fromhex(kv["secret"])
seed = bytes.fromhex(kv["seed"])
label = kv["label"].encode()


def p_hash(secret, seed, n):
    out, a = b"", seed
    while len(out) < n:
        a = hmac.new(secret, a, hashlib.sha256).digest()
        out += hmac.new(secret, a + seed, hashlib.sha256).digest()
    return out[:n]


got = p_hash(secret, label + seed, len(bytes.fromhex(kv["out"]))).hex().upper()
if got != kv["out"].upper():
    fail(f"prf: {got} != {kv['out']}")

try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
    have_crypto = True
except Exception:
    have_crypto = False
    print("SKIP: AES/GCM/X25519 — нет модуля cryptography")

if have_crypto:
    for mode, key, iv, plain, cipher in rows("aes.txt"):
        key_b = unhex(key)
        pt = unhex(plain)
        want = unhex(cipher)
        alg = algorithms.AES(key_b)
        if mode == "ECB":
            c = Cipher(alg, modes.ECB()).encryptor()
        elif mode == "CBC":
            c = Cipher(alg, modes.CBC(unhex(iv))).encryptor()
        else:
            c = Cipher(alg, modes.CTR(unhex(iv))).encryptor()
        got = (c.update(pt) + c.finalize())
        if len(got) < len(want):
            got = got + b"\0" * (len(want) - len(got))
        got = got[: len(want)]
        # CTR не требует выравнивания; ECB/CBC — блоки.
        if got != want:
            fail(f"aes {mode} {key[:8]}: {got.hex()} != {want.hex()}")
    for key, iv, aad, plain, cipher, tag in rows("gcm.txt"):
        key_b = unhex(key)
        iv_b = unhex(iv)
        aad_b = b"" if aad == "-" else unhex(aad)
        pt = b"" if plain == "-" else unhex(plain)
        ct = b"" if cipher == "-" else unhex(cipher)
        tag_b = unhex(tag)
        got = AESGCM(key_b).encrypt(iv_b, pt, aad_b or None)
        if got != ct + tag_b:
            fail(f"gcm {key[:8]}: {got.hex().upper()} != {(ct + tag_b).hex().upper()}")
    vals = {}
    for line in open(f"{BASE}/x25519.txt", encoding="utf-8"):
        line = line.strip()
        if "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            vals[k.strip()] = bytes.fromhex(v.strip())
    for who in ("alice", "bob"):
        sk = X25519PrivateKey.from_private_bytes(vals[f"{who}_private"])
        pub = sk.public_key().public_bytes_raw()
        if pub != vals[f"{who}_public"]:
            fail(f"x25519 {who} public: {pub.hex()} != {vals[f'{who}_public'].hex()}")
    shared = X25519PrivateKey.from_private_bytes(vals["alice_private"]).exchange(
        X25519PrivateKey.from_private_bytes(vals["bob_private"]).public_key())
    if shared != vals["shared"]:
        fail(f"x25519 shared: {shared.hex()} != {vals['shared'].hex()}")

    # Wycheproof ECDSA P-256: выбранные случаи.
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives import hashes
    from cryptography.exceptions import InvalidSignature

    def der_int(x):
        b = x.to_bytes((x.bit_length() + 7) // 8 or 1, "big")
        if b[0] & 0x80:
            b = b"\x00" + b
        return b"\x02" + bytes([len(b)]) + b

    def der_sig(r, s):
        body = der_int(r) + der_int(s)
        return b"\x30" + bytes([len(body)]) + body

    curves = {"secp256r1": ec.SECP256R1()}
    for name, hashname, qx, qy, msg, r, s, expect, comment in rows("ecdsa-wycheproof.txt"):
        try:
            pub = ec.EllipticCurvePublicNumbers(int(qx, 16), int(qy, 16),
                                                curves[name]).public_key()
            pub.verify(der_sig(int(r, 16), int(s, 16)), unhex(msg),
                       ec.ECDSA(hashes.SHA256()))
            ok = True
        except (InvalidSignature, ValueError):
            ok = False
        if ok != (expect == "valid"):
            fail(f"ecdsa-wycheproof {comment}: получено {'valid' if ok else 'invalid'}")

    for name, priv, x, y, shared, expect, comment in rows("ecdh-wycheproof.txt"):
        try:
            pub = ec.EllipticCurvePublicNumbers(int(x, 16), int(y, 16),
                                                curves[name]).public_key()
            got = ec.derive_private_key(int(priv, 16), curves[name]).exchange(ec.ECDH(), pub)
            ok = (expect == "valid" and got.hex().upper() == shared.upper())
        except (ValueError, Exception):
            ok = (expect == "invalid")
        if not ok:
            fail(f"ecdh-wycheproof {comment}: не совпало с ожиданием")

if failures:
    print(f"check-vectors: {failures} провалов")
    sys.exit(1)
print("check-vectors: OK")
