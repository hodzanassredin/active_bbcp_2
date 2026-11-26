#!/usr/bin/env python3
"""Минимальный TLS 1.2 peer для отрицательных проверок Crypto.

Только ECDHE-RSA-AES128-GCM-SHA256 (0xC02F): тот же набор, что по
умолчанию у CryptoTLS. Нужен, чтобы поддельный сервер мог довести
рукопожатие до Finished, а поддельный клиент — до CertificateVerify.
Библиотека не претендует на полноту: разбирается ровно то, что шлют
CryptoTLS и openssl в тестах.

Используется tools/crypto_tls_fake.py и tools/crypto_tls_client.py.
"""
import hashlib
import hmac

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, utils, x25519
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

VERSION = b"\x03\x03"
SUITE = b"\xc0\x2f"

HANDSHAKE, ALERT, CCS, APPLICATION = 22, 21, 20, 23


def prf(secret, label, seed, length):
    seed = label + seed
    out = b""
    a = seed
    while len(out) < length:
        a = hmac.new(secret, a, hashlib.sha256).digest()
        out += hmac.new(secret, a + seed, hashlib.sha256).digest()
    return out[:length]


def handshake(msg_type, body):
    return bytes([msg_type]) + len(body).to_bytes(3, "big") + body


def split_handshake(data):
    out = []
    i = 0
    while i + 4 <= len(data):
        typ = data[i]
        ln = int.from_bytes(data[i + 1:i + 4], "big")
        if i + 4 + ln > len(data):
            break
        out.append((typ, data[i:i + 4 + ln]))
        i += 4 + ln
    return out


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def read_record(sock):
    hdr = recv_exact(sock, 5)
    if hdr is None:
        return None
    typ, major, minor = hdr[0], hdr[1], hdr[2]
    ln = int.from_bytes(hdr[3:5], "big")
    body = recv_exact(sock, ln)
    if body is None:
        return None
    return typ, major, minor, body


def plain_record(typ, body, version=VERSION):
    return bytes([typ]) + version + len(body).to_bytes(2, "big") + body


def aead_record(typ, body, key, implicit_iv, seq):
    seqb = seq.to_bytes(8, "big")
    nonce = implicit_iv + seqb
    aad = seqb + bytes([typ]) + VERSION + len(body).to_bytes(2, "big")
    ct = AESGCM(key).encrypt(nonce, body, aad)
    return plain_record(typ, seqb + ct)


def aead_open(body, key, implicit_iv, seq, typ):
    if len(body) < 8 + 16:
        return None
    seqb = body[:8]
    expected = seq.to_bytes(8, "big")
    if seqb != expected:
        return None
    nonce = implicit_iv + seqb
    plain_len = len(body) - 8 - 16
    aad = seqb + bytes([typ]) + VERSION + plain_len.to_bytes(2, "big")
    try:
        return AESGCM(key).decrypt(nonce, body[8:], aad)
    except Exception:
        return None


def load_rsa_key(path):
    data = open(path, "rb").read()
    if b"BEGIN RSA PRIVATE KEY" in data:
        return serialization.load_pem_private_key(data, password=None)
    if b"BEGIN PRIVATE KEY" in data:
        return serialization.load_pem_private_key(data, password=None)
    raise ValueError("no private key in %s" % path)


def load_cert_der(path):
    data = open(path, "rb").read()
    start = data.index(b"-----BEGIN CERTIFICATE-----")
    start += len(b"-----BEGIN CERTIFICATE-----")
    end = data.index(b"-----END CERTIFICATE-----", start)
    import base64
    return base64.b64decode(b"".join(data[start:end].split()))


def sign_transcript(key, digest):
    return key.sign(digest, padding.PKCS1v15(), utils.Prehashed(hashes.SHA256()))


def sha256(data):
    return hashlib.sha256(data).digest()


def derive_keys(master, client_random, server_random):
    block = prf(master, b"key expansion", server_random + client_random, 40)
    return {
        "client_key": block[0:16],
        "server_key": block[16:32],
        "client_iv": block[32:36],
        "server_iv": block[36:40],
    }


def new_x25519():
    key = x25519.X25519PrivateKey.generate()
    return key, key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def parse_client_hello(body):
    if len(body) < 2 + 32 + 1:
        raise ValueError("short ClientHello")
    version = body[:2]
    client_random = body[2:34]
    sid_len = body[34]
    pos = 35 + sid_len
    suites_len = int.from_bytes(body[pos:pos + 2], "big")
    pos += 2 + suites_len
    comp_len = body[pos]
    pos += 1 + comp_len
    return version, client_random, body[pos:]


def parse_server_hello(body):
    if len(body) < 2 + 32 + 1:
        raise ValueError("short ServerHello")
    server_random = body[2:34]
    sid_len = body[34]
    pos = 35 + sid_len
    suite = body[pos:pos + 2]
    pos += 2
    return body[:2], server_random, suite, body[pos + 1:]


def parse_certificate(body):
    if len(body) < 3:
        raise ValueError("short Certificate")
    list_len = int.from_bytes(body[:3], "big")
    pos = 3
    end = pos + list_len
    certs = []
    while pos < end:
        ln = int.from_bytes(body[pos:pos + 3], "big")
        pos += 3
        certs.append(body[pos:pos + ln])
        pos += ln
    return certs


def parse_ske(body):
    if len(body) < 40:
        raise ValueError("short SKE")
    params = body[:36]
    sig_len = int.from_bytes(body[36:38], "big")
    return params, body[38:38 + sig_len]


def parse_cke(body):
    if len(body) < 1 or body[0] != 32:
        raise ValueError("bad CKE")
    return body[1:33]


def close_quiet(sock):
    try:
        sock.close()
    except OSError:
        pass
