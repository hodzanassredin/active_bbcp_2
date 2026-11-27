#!/usr/bin/env python3
"""Поддельный TLS 1.3 сервер для проверок клиента Crypto.

    crypto_tls13_fake.py MODE PORT CERT.pem KEY.pem

Режимы:
  good        — полное рукопожатие, затем перевёрнутая строка (контроль);
  keyupdate   — после рукопожатия KeyUpdate(request_update=1), затем обмен;
  badcv       — CertificateVerify подписан с контекстом client вместо server;
  badpkcs1    — CertificateVerify подписан rsa_pkcs1_sha256 (схема
                предложена клиентом, но в 1.3 запрещена, RFC 8446 §4.4.3);
  badpkcs1sha384 — то же с rsa_pkcs1_sha384 (клиент её не предлагал);
  badfinished — испорчен server Finished;
  badkeys     — ключи handshake выведены из испорченного транскрипта;
  hrr         — HelloRetryRequest с другой группой, затем полное рукопожатие;
  hrrtwice    — два HelloRetryRequest подряд (клиент обязан отказаться);
  hrrsv       — HRR без supported_versions (отказ);
  hrrks       — HRR с ключом в key_share вместо одной группы (отказ);
  ccsok       — законный CCS {0x01} до Finished (обмен продолжается);
  ccsbad      — CCS с байтом 0x02 до Finished (отказ);
  ccslong     — CCS длиной 2 до Finished (отказ);
  ccsafter    — CCS после Finished (отказ в обмене);
  emptyrec    — запись с пустым TLSInnerPlaintext (нет байта типа);
  zeropad     — запись из одних нулей (типа нет);
  emptyapp    — законная пустая запись application_data (обмен идёт);
  paddedapp   — законная пустая запись с нулевым дополнением (обмен идёт);
  shversion   — ServerHello с legacy_version 0304 (отказ);
  shcomp      — ServerHello с legacy_compression_method 1 (отказ);
  badext13    — длина блока расширений ServerHello больше записи (отказ);
  badee       — EncryptedExtensions с невозможной длиной (отказ);
  badcert13   — Certificate с невозможной длиной списка (отказ);
  shortfinished — Finished короче хеша набора (отказ);
  zeroshare   — key_share сервера из одних нулей (отказ);
  badtag13    — испорченный тег AEAD записи 1.3;
  badinnertype — внутренний тип вне 20..23;
  badkeyupdate — KeyUpdate с кодом 2.

Клиент обязан либо завершить обмен (good/keyupdate/hrr/ccsok/emptyapp/
paddedapp), либо отказаться без трапа (остальные). Запускается из
tools/test-crypto-tls.sh.
"""
import hashlib
import hmac
import os
import socket
import sys
import base64

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, padding, x25519
from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305

SUITES = {
    0x1301: ("sha256", 16, "aes"),
    0x1302: ("sha384", 32, "aes"),
    0x1303: ("sha256", 32, "chacha"),
}
GROUP_X25519 = 29
GROUP_P256 = 23
HRR_RANDOM = bytes.fromhex(
    "CF21AD74E59A6111BE1D8C021E65B891C2A211167ABB8C5E079E09E2C8A8339C")


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
    typ, major, minor, ln = hdr[0], hdr[1], hdr[2], int.from_bytes(hdr[3:5], "big")
    body = recv_exact(sock, ln)
    if body is None:
        return None
    return typ, major, minor, body


def plain_record(typ, body, version=b"\x03\x03"):
    return bytes([typ]) + version + len(body).to_bytes(2, "big") + body


def hs(msg_type, body):
    return bytes([msg_type]) + len(body).to_bytes(3, "big") + body


def split_handshake(data):
    out = []
    pos = 0
    while pos + 4 <= len(data):
        t = data[pos]
        n = int.from_bytes(data[pos + 1:pos + 4], "big")
        if pos + 4 + n > len(data):
            break
        out.append((t, data[pos:pos + 4 + n]))
        pos += 4 + n
    return out


def hash_name(name):
    return hashlib.sha256 if name == "sha256" else hashlib.sha384


def hash_len(name):
    return 32 if name == "sha256" else 48


def hkdf_extract(name, salt, ikm):
    return hmac.new(salt, ikm, hash_name(name)).digest()


def expand_label(name, secret, label, context, length):
    info = (length.to_bytes(2, "big") + bytes([6 + len(label)])
            + b"tls13 " + label + bytes([len(context)]) + context)
    out = b""
    t = b""
    i = 1
    while len(out) < length:
        t = hmac.new(secret, t + info + bytes([i]), hash_name(name)).digest()
        out += t
        i += 1
    return out[:length]


def derive_secret(name, secret, label, transcript):
    return expand_label(name, secret, label, hash_name(name)(transcript).digest(),
                        hash_len(name))


def finished_key(name, secret):
    return expand_label(name, secret, b"finished", b"", hash_len(name))


def finished_verify(name, secret, transcript):
    return hmac.new(finished_key(name, secret),
                    hash_name(name)(transcript).digest(),
                    hash_name(name)).digest()


def parse_client_hello(body):
    pos = 0
    version = body[pos:pos + 2]
    pos += 2
    random = body[pos:pos + 32]
    pos += 32
    sid_len = body[pos]
    pos += 1
    session_id = body[pos:pos + sid_len]
    pos += sid_len
    n = int.from_bytes(body[pos:pos + 2], "big")
    pos += 2
    suites = [int.from_bytes(body[pos + 2 * i:pos + 2 * i + 2], "big")
              for i in range(n // 2)]
    pos += n
    comp_len = body[pos]
    pos += 1 + comp_len
    groups = []
    shares = {}
    ext_end = pos + 2 + int.from_bytes(body[pos:pos + 2], "big")
    pos += 2
    while pos + 4 <= ext_end:
        etype = int.from_bytes(body[pos:pos + 2], "big")
        elen = int.from_bytes(body[pos + 2:pos + 4], "big")
        data = body[pos + 4:pos + 4 + elen]
        pos += 4 + elen
        if etype == 10 and len(data) >= 2:
            n = int.from_bytes(data[0:2], "big")
            groups = [int.from_bytes(data[2 + 2 * i:4 + 2 * i], "big")
                      for i in range(n // 2)]
        elif etype == 51 and len(data) >= 2:
            n = int.from_bytes(data[0:2], "big")
            p = 2
            while p + 4 <= 2 + n:
                g = int.from_bytes(data[p:p + 2], "big")
                klen = int.from_bytes(data[p + 2:p + 4], "big")
                shares[g] = data[p + 4:p + 4 + klen]
                p += 4 + klen
    return {
        "version": version,
        "random": random,
        "session_id": session_id,
        "suites": suites,
        "groups": groups,
        "shares": shares,
    }


def make_key(group):
    if group == GROUP_X25519:
        priv = x25519.X25519PrivateKey.generate()
        pub = priv.public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    else:
        curve = ec.SECP256R1() if group == GROUP_P256 else ec.SECP384R1()
        priv = ec.generate_private_key(curve)
        pub = priv.public_key().public_bytes(
            serialization.Encoding.X962,
            serialization.PublicFormat.UncompressedPoint)
    return priv, pub


def shared_secret(priv, group, peer):
    if group == GROUP_X25519:
        return priv.exchange(x25519.X25519PublicKey.from_public_bytes(peer))
    pub = ec.EllipticCurvePublicKey.from_encoded_point(
        ec.SECP256R1() if group == GROUP_P256 else ec.SECP384R1(), peer)
    return priv.exchange(ec.ECDH(), pub)


def load_cert_der(path):
    data = open(path, "rb").read()
    start = data.index(b"-----BEGIN CERTIFICATE-----") + len(
        b"-----BEGIN CERTIFICATE-----")
    end = data.index(b"-----END CERTIFICATE-----", start)
    return base64.b64decode(b"".join(data[start:end].split()))


def load_private_key(path):
    data = open(path, "rb").read()
    return serialization.load_pem_private_key(data, password=None)


def make_aead(suite):
    name, key_len, kind = SUITES[suite]
    return name, key_len, kind


def seal_raw(suite, key, iv, seq, plaintext):
    """Записать произвольный открытый текст (без добавления типа).

    Нужен отрицательным случаям: пустой TLSInnerPlaintext и запись из
    одних нулей заполнения, а также законное дополнение нулями.
    """
    name, _, kind = make_aead(suite)
    total = len(plaintext) + 16
    aad = b"\x17\x03\x03" + total.to_bytes(2, "big")
    nonce = bytes(a ^ b for a, b in zip(iv, seq.to_bytes(12, "big")))
    if kind == "aes":
        ct = AESGCM(key).encrypt(nonce, plaintext, aad)
    else:
        ct = ChaCha20Poly1305(key).encrypt(nonce, plaintext, aad)
    return b"\x17\x03\x03" + len(ct).to_bytes(2, "big") + ct


def seal(suite, key, iv, seq, inner_type, payload):
    return seal_raw(suite, key, iv, seq, payload + bytes([inner_type]))


def open_record(suite, key, iv, seq, body):
    name, _, kind = make_aead(suite)
    ct, tag = body[:-16], body[-16:]
    aad = b"\x17\x03\x03" + len(body).to_bytes(2, "big")
    nonce = bytes(a ^ b for a, b in zip(iv, seq.to_bytes(12, "big")))
    try:
        if kind == "aes":
            plain = AESGCM(key).decrypt(nonce, ct + tag, aad)
        else:
            plain = ChaCha20Poly1305(key).decrypt(nonce, ct + tag, aad)
    except Exception:
        return None
    return plain


def cert_verify_scheme(priv, suite_name):
    if isinstance(priv, ec.EllipticCurvePrivateKey):
        if isinstance(priv.curve, ec.SECP256R1):
            return 0x0403, hashes.SHA256()
        return 0x0503, hashes.SHA384()
    if suite_name == "sha384":
        return 0x0805, hashes.SHA384()
    return 0x0804, hashes.SHA256()


def cert_verify_content(digest, context):
    return b" " * 64 + context + b"\x00" + digest


def sign_cert_verify(priv, scheme, digest, context):
    content = cert_verify_content(digest, context)
    if isinstance(priv, ec.EllipticCurvePrivateKey):
        return priv.sign(content, ec.ECDSA(hashes.SHA256() if scheme == 0x0403
                                         else hashes.SHA384()))
    hash_alg = hashes.SHA256() if scheme == 0x0804 else hashes.SHA384()
    return priv.sign(content, padding.PSS(mgf=padding.MGF1(hash_alg),
                                          salt_length=hash_alg.digest_size),
                     hash_alg)


def sign_cert_verify_pkcs1(priv, scheme, digest, context):
    """RFC 8446, 4.4.3: rsa_pkcs1_* не годится для CertificateVerify 1.3.

    Подпись корректна по PKCS#1 v1.5 и проверяема, поэтому без явной
    проверки схемы рукопожатие прошло бы.
    """
    content = cert_verify_content(digest, context)
    hash_alg = hashes.SHA256() if scheme == 0x0401 else hashes.SHA384()
    return priv.sign(content, padding.PKCS1v15(), hash_alg)


def certificate_message(der):
    entry = len(der).to_bytes(3, "big") + der + b"\x00\x00"
    return hs(11, b"\x00" + len(entry).to_bytes(3, "big") + entry)


def server_hello(session_id, suite, group, pub, random=None, version=b"\x03\x03",
                 compression=0):
    if random is None:
        random = os.urandom(32)
    exts = b"\x00\x2b\x00\x02\x03\x04"
    exts += b"\x00\x33" + (4 + len(pub)).to_bytes(2, "big")
    exts += group.to_bytes(2, "big") + len(pub).to_bytes(2, "big") + pub
    body = (version + random + bytes([len(session_id)]) + session_id
            + suite.to_bytes(2, "big") + bytes([compression])
            + len(exts).to_bytes(2, "big") + exts)
    return hs(2, body)


def bad_extensions_hello(session_id, suite, group, pub):
    """ServerHello, где длина блока расширений больше тела записи."""
    exts = b"\x00\x2b\x00\x02\x03\x04"
    exts += b"\x00\x33" + (4 + len(pub)).to_bytes(2, "big")
    exts += group.to_bytes(2, "big") + len(pub).to_bytes(2, "big") + pub
    body = (b"\x03\x03" + os.urandom(32) + bytes([len(session_id)]) + session_id
            + suite.to_bytes(2, "big") + b"\x00"
            + b"\xff\xff" + exts)
    return hs(2, body)


def hello_retry_request(session_id, suite, group, with_sv=True, with_key=False):
    exts = b""
    if with_sv:
        exts += b"\x00\x2b\x00\x02\x03\x04"
    if with_key:
        # В HRR key_share несёт только группу; здесь намеренно добавлен
        # ключ, чтобы проверить, что клиент это отвергает.
        exts += b"\x00\x33\x00\x04" + group.to_bytes(2, "big") + b"\x00\x20"
    else:
        exts += b"\x00\x33\x00\x02" + group.to_bytes(2, "big")
    body = (b"\x03\x03" + HRR_RANDOM + bytes([len(session_id)]) + session_id
            + suite.to_bytes(2, "big") + b"\x00"
            + len(exts).to_bytes(2, "big") + exts)
    return hs(2, body)


def read_client_hello(conn):
    rec = read_record(conn)
    if rec is None:
        return None
    typ, _, _, body = rec
    if typ != 22:
        return None
    msgs = split_handshake(body)
    if not msgs or msgs[0][0] != 1:
        return None
    return msgs[0][1]


def choose_suite(ch, preferred):
    for s in preferred:
        if s in ch["suites"] and s in SUITES:
            return s
    for s in ch["suites"]:
        if s in SUITES:
            return s
    return None


def choose_group(ch, preferred):
    for g in preferred:
        if g in ch["shares"]:
            return g
    return None


def serve(mode, port, cert_path, key_path):
    der = load_cert_der(cert_path)
    priv = load_private_key(key_path)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)
    srv.settimeout(30)
    sys.stdout.write("ready\n")
    sys.stdout.flush()
    conn, _ = srv.accept()
    conn.settimeout(15)
    def log(x):
        sys.stdout.write(x + "\n"); sys.stdout.flush()
    try:
        ch_msg = read_client_hello(conn)
        if ch_msg is None:
            return 0
        ch = parse_client_hello(ch_msg[4:])
        transcript = ch_msg

        # HelloRetryRequest: клиент прислал x25519, просим P-256.
        retried = False
        if mode in ("hrr", "hrrtwice", "hrrsv", "hrrks"):
            group = GROUP_P256
            suite = choose_suite(ch, [0x1301, 0x1302, 0x1303])
            if suite is None:
                return 0
            hrr = hello_retry_request(ch["session_id"], suite, group,
                                      with_sv=(mode != "hrrsv"),
                                      with_key=(mode == "hrrks"))
            name0, _, _ = SUITES[suite]
            h1 = hash_name(name0)(ch_msg).digest()
            transcript = b"\xfe" + len(h1).to_bytes(3, "big") + h1 + hrr
            conn.sendall(plain_record(22, hrr))
            ch_msg = read_client_hello(conn)
            if ch_msg is None:
                return 0
            ch = parse_client_hello(ch_msg[4:])
            transcript += ch_msg
            retried = True
            if mode == "hrrtwice":
                hrr2 = hello_retry_request(ch["session_id"], suite, GROUP_X25519)
                transcript += hrr2
                conn.sendall(plain_record(22, hrr2))
                # Клиент обязан отказаться; дочитываем до закрытия.
                while read_record(conn) is not None:
                    pass
                return 0

        suite = choose_suite(ch, [0x1301, 0x1302, 0x1303])
        if suite is None:
            return 0
        name, key_len, kind = SUITES[suite]
        if retried:
            group = GROUP_P256
        else:
            group = choose_group(ch, [GROUP_X25519, GROUP_P256])
        if group is None or group not in ch["shares"]:
            return 0

        if mode == "zeroshare":
            # Точка X25519 из одних нулей: Shared обязан её отвергнуть.
            srv_pub = bytes(32)
            shared = bytes(32)
        else:
            srv_priv, srv_pub = make_key(group)
            shared = shared_secret(srv_priv, group, ch["shares"][group])

        # legacy_version в ServerHello 1.3 обязан быть 0303, а
        # legacy_compression_method — 0.
        sh_version = b"\x03\x04" if mode == "shversion" else b"\x03\x03"
        if mode == "badext13":
            sh = bad_extensions_hello(ch["session_id"], suite, group, srv_pub)
        else:
            sh = server_hello(ch["session_id"], suite, group, srv_pub,
                              version=sh_version,
                              compression=1 if mode == "shcomp" else 0)
        transcript += sh
        log("SH")
        conn.sendall(plain_record(22, sh))
        if mode in ("ccsok", "ccsbad", "ccslong"):
            # CCS до Finished: законный — ровно {0x01}, иначе отказ
            # (RFC 8446, 5). Эти записи не шифруются.
            if mode == "ccsbad":
                conn.sendall(plain_record(20, b"\x02"))
            elif mode == "ccslong":
                conn.sendall(plain_record(20, b"\x01\x01"))
            else:
                conn.sendall(plain_record(20, b"\x01"))

        zeros = bytes(hash_len(name))
        empty_hash = hash_name(name)(b"").digest()
        early = hkdf_extract(name, zeros, zeros)
        derived = expand_label(name, early, b"derived", empty_hash, hash_len(name))
        handshake = hkdf_extract(name, derived, shared)
        key_transcript = transcript
        if mode == "badkeys":
            key_transcript = transcript[:-1] + bytes([transcript[-1] ^ 1])
        c_hs = derive_secret(name, handshake, b"c hs traffic", key_transcript)
        s_hs = derive_secret(name, handshake, b"s hs traffic", key_transcript)
        derived2 = expand_label(name, handshake, b"derived", empty_hash, hash_len(name))
        master = hkdf_extract(name, derived2, zeros)

        c_key = expand_label(name, c_hs, b"key", b"", key_len)
        c_iv = expand_label(name, c_hs, b"iv", b"", 12)
        s_key = expand_label(name, s_hs, b"key", b"", key_len)
        s_iv = expand_label(name, s_hs, b"iv", b"", 12)

        # EncryptedExtensions: длина расширений заведомо больше тела.
        ee = hs(8, b"\x00\x05") if mode == "badee" else hs(8, b"\x00\x00")
        if mode == "badcert13":
            cert = hs(11, b"\x00\x00\x00\x0a")
        else:
            cert = certificate_message(der)
        transcript += ee + cert
        digest = hash_name(name)(transcript).digest()
        scheme, _ = cert_verify_scheme(priv, name)
        if mode in ("badpkcs1", "badpkcs1sha384"):
            # Схема есть в signature_algorithms (badpkcs1) или нет
            # (badpkcs1sha384), но для CertificateVerify 1.3 обе
            # запрещены: RSA-подпись обязана быть PSS.
            scheme = 0x0401 if mode == "badpkcs1" else 0x0501
        context = (b"TLS 1.3, client CertificateVerify" if mode == "badcv"
                   else b"TLS 1.3, server CertificateVerify")
        if mode in ("badpkcs1", "badpkcs1sha384"):
            sig = sign_cert_verify_pkcs1(priv, scheme, digest, context)
        else:
            sig = sign_cert_verify(priv, scheme, digest, context)
        cv = hs(15, scheme.to_bytes(2, "big") + len(sig).to_bytes(2, "big") + sig)
        transcript += cv

        fin_transcript = transcript
        if mode == "badtranscript":
            fin_transcript = transcript[:-1] + bytes([transcript[-1] ^ 1])
        verify = finished_verify(name, s_hs, fin_transcript)
        fin = hs(20, verify)
        if mode == "badfinished":
            verify = bytes([verify[0] ^ 1]) + verify[1:]
            fin = hs(20, verify)
        elif mode == "shortfinished":
            # Длина verify_data меньше хеша набора: без проверки длины
            # сравнение первых len байт сойдётся.
            fin = hs(20, verify[:-1])
        transcript += fin

        log("flight")
        conn.sendall(seal(suite, s_key, s_iv, 0, 22, ee))
        conn.sendall(seal(suite, s_key, s_iv, 1, 22, cert))
        conn.sendall(seal(suite, s_key, s_iv, 2, 22, cv))
        conn.sendall(seal(suite, s_key, s_iv, 3, 22, fin))

        c_app = derive_secret(name, master, b"c ap traffic", transcript)
        s_app = derive_secret(name, master, b"s ap traffic", transcript)
        c_app_key = expand_label(name, c_app, b"key", b"", key_len)
        c_app_iv = expand_label(name, c_app, b"iv", b"", 12)
        s_app_key = expand_label(name, s_app, b"key", b"", key_len)
        s_app_iv = expand_label(name, s_app, b"iv", b"", 12)

        # Client Finished (или алерт при отказе). Ключи клиента — c_hs, seq 0.
        rec = read_record(conn)
        if rec is None:
            log("eof before client finished")
            return 0
        typ, _, _, body = rec
        log("client record type %d len %d" % (typ, len(body)))
        if typ == 21:
            return 0
        plain = None
        if typ == 23:
            plain = open_record(suite, c_key, c_iv, 0, body)
        if plain is None:
            log("client record decrypt failed")
            return 0
        log("client inner %d" % plain[-1])
        if plain[-1] == 21:
            log("alert %d %d" % (plain[0], plain[1]))
            return 0
        if plain[-1] != 22:
            return 0
        transcript += plain[:-1]
        if mode not in ("good", "keyupdate", "hrr", "ccsok",
                        "ccsafter", "emptyrec", "zeropad",
                        "emptyapp", "paddedapp", "badkeyupdate",
                        "badtag13", "badinnertype"):
            # Клиент должен был отказаться; если он всё же прислал Finished,
            # считаем, что проверка не сработала.
            return 0

        # Записи после Finished — уже под application-ключами сервера.
        # После плохой записи сервер всё равно отвечает: если клиент её
        # пропустил, он получит ответ, и badApp-проверка покраснеет.
        if mode == "ccsafter":
            # CCS после Finished: RFC 8446, 5 требует unexpected_message.
            conn.sendall(plain_record(20, b"\x01"))
        elif mode in ("emptyrec", "zeropad", "emptyapp", "paddedapp"):
            if mode == "emptyrec":
                bad = b""
            elif mode == "zeropad":
                bad = bytes(32)
            elif mode == "emptyapp":
                bad = b"\x17"
            else:
                bad = b"\x17" + bytes(16)
            conn.sendall(seal_raw(suite, s_app_key, s_app_iv, 0, bad))
        elif mode == "badinnertype":
            # Внутренний тип вне 20..23: CASE ELSE обязан отвергнуть.
            conn.sendall(seal(suite, s_app_key, s_app_iv, 0, 0x99, b""))
        elif mode == "badtag13":
            # Открытый текст верный, тег AEAD испорчен.
            bad = bytearray(seal(suite, s_app_key, s_app_iv, 0, 23, b"olleh\n"))
            bad[-1] ^= 1
            conn.sendall(bytes(bad))
        elif mode == "badkeyupdate":
            # KeyUpdate с кодом 2: проверка обязана отвергнуть; без неё
            # клиент обновит ключи чтения и примет ответ под новыми.
            conn.sendall(seal(suite, s_app_key, s_app_iv, 0, 22, hs(24, b"\x02")))
            s_app = expand_label(name, s_app, b"traffic upd", b"", hash_len(name))
            s_app_key = expand_label(name, s_app, b"key", b"", key_len)
            s_app_iv = expand_label(name, s_app, b"iv", b"", 12)

        # Данные приложения. При keyupdate сервер сначала шлёт KeyUpdate
        # и переключает ключи записи; клиент может прислать данные под
        # старыми ключами (до чтения) или сначала ответный KeyUpdate.
        cseq = 0
        sseq = 0 if mode in ("ccsafter", "badkeyupdate") else (
            1 if mode in ("emptyrec", "zeropad", "emptyapp", "paddedapp",
                          "badinnertype", "badtag13") else 0)
        data = None
        got_keyupdate = False
        if mode == "keyupdate":
            conn.sendall(seal(suite, s_app_key, s_app_iv, 0, 22, hs(24, b"\x01")))
            s_app = expand_label(name, s_app, b"traffic upd", b"", hash_len(name))
            s_app_key = expand_label(name, s_app, b"key", b"", key_len)
            s_app_iv = expand_label(name, s_app, b"iv", b"", 12)
        while data is None:
            rec = read_record(conn)
            if rec is None:
                log("eof before app data")
                return 0
            typ, _, _, body = rec
            log("post rec typ %d len %d seq %d" % (typ, len(body), cseq))
            if typ == 21:
                return 0
            plain = open_record(suite, c_app_key, c_app_iv, cseq, body)
            if plain is None:
                log("app data decrypt failed")
                return 0
            inner = plain[-1]
            cseq += 1
            if inner == 22 and plain[:-1][:1] == bytes([24]):
                c_app = expand_label(name, c_app, b"traffic upd", b"", hash_len(name))
                c_app_key = expand_label(name, c_app, b"key", b"", key_len)
                c_app_iv = expand_label(name, c_app, b"iv", b"", 12)
                cseq = 0
                got_keyupdate = True
                log("client keyupdate")
                continue
            if inner != 23:
                log("unexpected inner %d" % inner)
                return 0
            data = plain[:-1]
        if mode == "keyupdate" and not got_keyupdate:
            # Клиент прочитал KeyUpdate после отправки данных: его
            # ответ обязан прийти под старыми ключами записи.
            rec = read_record(conn)
            if rec is None:
                log("no client keyupdate")
                return 0
            typ, _, _, body = rec
            plain = open_record(suite, c_app_key, c_app_iv, cseq, body) if typ == 23 else None
            if plain is None or plain[-1] != 22 or plain[0] != 24:
                log("client keyupdate not under old keys")
                return 0
            c_app = expand_label(name, c_app, b"traffic upd", b"", hash_len(name))
            c_app_key = expand_label(name, c_app, b"key", b"", key_len)
            c_app_iv = expand_label(name, c_app, b"iv", b"", 12)
            cseq = 0
            got_keyupdate = True
            log("client keyupdate")
        reply = data[::-1]
        if data.endswith(b"\n"):
            reply = data[:-1][::-1] + b"\n"
        log("reply")
        conn.sendall(seal(suite, s_app_key, s_app_iv, sseq, 23, reply))
        # Не закрывать раньше клиента: его close_notify (или алерт после
        # плохой записи) иначе попадёт в сброшенное соединение и трапует
        # запись в CryptoAosCommStreams. Дочитываем до EOF.
        log("drain")
        while read_record(conn) is not None:
            pass
    except (BrokenPipeError, ConnectionResetError, socket.timeout):
        pass
    finally:
        try:
            conn.close()
        finally:
            srv.close()
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.stderr.write(
            "usage: crypto_tls13_fake.py MODE PORT CERT.pem KEY.pem\n")
        sys.exit(2)
    sys.exit(serve(sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]))
