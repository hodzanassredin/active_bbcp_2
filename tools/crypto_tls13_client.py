#!/usr/bin/env python3
"""Клиент TLS 1.3 для проверки сервера Crypto.

    crypto_tls13_client.py MODE HOST PORT CERT.pem KEY.pem

Режимы:
  good       — полное взаимное рукопожатие и обмен строкой (контроль);
  goodccs    — законный CCS {0x01} после ClientHello, затем обмен;
  ccsbad     — CCS с байтом 0x02 во время рукопожатия (сервер обязан
               отказать, RFC 8446 §5);
  ccslong    — CCS длиной 2 во время рукопожатия (отказ);
  ccsafter   — CCS после Finished (сервер обязан отказать в обмене);
  emptyrec   — после Finished запись с пустым TLSInnerPlaintext (нет типа);
  zeropad    — после Finished запись из одних нулей (типа нет);
  badpkcs1   — CertificateVerify подписан rsa_pkcs1_sha256: подпись
               проверяема, но в CertificateVerify 1.3 разрешён только
               RSASSA-PSS (RFC 8446 §4.4.3), сервер обязан отказать;
  badsig13   — схема верная, подпись испорчена;
  bigshare   — key_share объявляет 65535 байт при коротком теле;
  badextch   — длина блока расширений ClientHello больше тела;
  zeroshare  — key_share из одних нулей (Shared обязан отвергнуть).

Сертификат клиента — RSA, подписанный доверенным корнем сервера.
Запускается из tools/test-crypto-tls.sh.
"""
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import crypto_tls13_fake as F

GROUP_X25519 = 29
SUITE = 0x1301


def build_client_hello(client_random, session_id, share_pub, sig_schemes,
                       share_len=None, ext_len=None):
    if share_len is None:
        share_len = len(share_pub)
    if ext_len is None:
        ext_len = None
    exts = b""
    groups = GROUP_X25519.to_bytes(2, "big")
    exts += b"\x00\x0a" + (2 + len(groups)).to_bytes(2, "big") \
        + len(groups).to_bytes(2, "big") + groups
    algs = b"".join(s.to_bytes(2, "big") for s in sig_schemes)
    exts += b"\x00\x0d" + (2 + len(algs)).to_bytes(2, "big") \
        + len(algs).to_bytes(2, "big") + algs
    exts += b"\x00\x2b\x00\x03\x02\x03\x04"
    entry = GROUP_X25519.to_bytes(2, "big") + share_len.to_bytes(2, "big") + share_pub
    exts += b"\x00\x33" + (2 + len(entry)).to_bytes(2, "big") \
        + len(entry).to_bytes(2, "big") + entry
    body = (b"\x03\x03" + client_random + bytes([len(session_id)]) + session_id
            + (2).to_bytes(2, "big") + SUITE.to_bytes(2, "big") + b"\x00"
            + (ext_len if ext_len is not None else len(exts)).to_bytes(2, "big")
            + exts)
    return F.hs(1, body)


def parse_server_hello(body):
    pos = 0
    version = body[pos:pos + 2]
    pos += 2
    random = body[pos:pos + 32]
    pos += 32
    sid_len = body[pos]
    pos += 1
    session_id = body[pos:pos + sid_len]
    pos += sid_len
    suite = int.from_bytes(body[pos:pos + 2], "big")
    pos += 2
    pos += 1  # compression
    end = pos + 2 + int.from_bytes(body[pos:pos + 2], "big")
    pos += 2
    group = None
    share = None
    while pos + 4 <= end:
        etype = int.from_bytes(body[pos:pos + 2], "big")
        elen = int.from_bytes(body[pos + 2:pos + 4], "big")
        data = body[pos + 4:pos + 4 + elen]
        pos += 4 + elen
        if etype == 51 and len(data) >= 4:
            group = int.from_bytes(data[0:2], "big")
            klen = int.from_bytes(data[2:4], "big")
            share = data[4:4 + klen]
    return version, random, session_id, suite, group, share


def send_hs(sock, suite, key, iv, seq, msg):
    sock.sendall(F.seal(suite, key, iv, seq, 22, msg))


def main():
    if len(sys.argv) != 6:
        sys.stderr.write("usage: crypto_tls13_client.py MODE HOST PORT CERT KEY\n")
        return 2
    mode, host, port, cert_path, key_path = sys.argv[1:6]
    port = int(port)

    der = F.load_cert_der(cert_path)
    priv = F.load_private_key(key_path)

    client_random = os.urandom(32)
    session_id = os.urandom(32)
    share_priv, share_pub = F.make_key(GROUP_X25519)
    share_len = None
    ext_len = None
    if mode == "badextch":
        # Длина блока расширений больше тела CH.
        ext_len = 0xFFFF
    if mode == "bigshare":
        # key_share объявляет 65535 байт, а несёт 4: сервер обязан
        # отвергнуть до копирования.
        share_pub = b"\x00" * 4
        share_len = 0xFFFF
    elif mode == "zeroshare":
        # Точка из одних нулей: Shared обязан её отвергнуть.
        share_pub = bytes(32)
    sig_schemes = [0x0804]
    if mode == "badpkcs1":
        sig_schemes.append(0x0401)
    ch = build_client_hello(client_random, session_id, share_pub, sig_schemes,
                            share_len, ext_len)
    transcript = ch

    sock = socket.create_connection((host, port), timeout=10)
    sock.settimeout(10)
    try:
        sock.sendall(F.plain_record(22, ch))
        if mode in ("ccsbad", "ccslong", "goodccs"):
            # CCS сразу после ClientHello, до Finished: законен только
            # один байт 0x01. Рукопожатие продолжаем: если сервер
            # пропустил плохой CCS, он доведёт его до конца и примет
            # клиента (тест expectReject покраснеет).
            if mode == "ccslong":
                data = b"\x01\x01"
            elif mode == "ccsbad":
                data = b"\x02"
            else:
                data = b"\x01"
            sock.sendall(F.plain_record(20, data))

        # ServerHello — единственная незашифрованная запись сервера.
        rec = F.read_record(sock)
        if rec is None:
            return 2
        typ, _, _, body = rec
        if typ != 22:
            return 2
        msgs = F.split_handshake(body)
        if not msgs or msgs[0][0] != 2:
            return 2
        sh = msgs[0][1]
        transcript += sh
        version, srv_random, sid, suite, group, peer_pub = parse_server_hello(sh[4:])
        if version != b"\x03\x03" or group != GROUP_X25519 or peer_pub is None:
            return 2
        if sid != session_id:
            return 2
        name, key_len, kind = F.SUITES[suite]
        if mode == "zeroshare":
            shared = bytes(32)
        else:
            shared = share_priv.exchange(
                F.x25519.X25519PublicKey.from_public_bytes(peer_pub))

        zeros = bytes(F.hash_len(name))
        empty_hash = F.hash_name(name)(b"").digest()
        early = F.hkdf_extract(name, zeros, zeros)
        derived = F.expand_label(name, early, b"derived", empty_hash, F.hash_len(name))
        handshake = F.hkdf_extract(name, derived, shared)
        c_hs = F.derive_secret(name, handshake, b"c hs traffic", transcript)
        s_hs = F.derive_secret(name, handshake, b"s hs traffic", transcript)
        derived2 = F.expand_label(name, handshake, b"derived", empty_hash, F.hash_len(name))
        master = F.hkdf_extract(name, derived2, zeros)
        c_key = F.expand_label(name, c_hs, b"key", b"", key_len)
        c_iv = F.expand_label(name, c_hs, b"iv", b"", 12)
        s_key = F.expand_label(name, s_hs, b"key", b"", key_len)
        s_iv = F.expand_label(name, s_hs, b"iv", b"", 12)

        # Полёт сервера: EE, необязательный CertificateRequest,
        # Certificate, CertificateVerify, Finished.
        sseq = 0
        hsbuf = b""
        got_finished = False
        cert_requested = False
        while not got_finished:
            rec = F.read_record(sock)
            if rec is None:
                return 2
            typ, _, _, body = rec
            if typ == 21:
                return 0
            if typ == 20:
                continue
            if typ != 23:
                return 2
            plain = F.open_record(suite, s_key, s_iv, sseq, body)
            sseq += 1
            if plain is None or not plain:
                return 2
            if plain[-1] == 21:
                return 0
            if plain[-1] != 22:
                return 2
            hsbuf += plain[:-1]
            while True:
                msgs = F.split_handshake(hsbuf)
                if not msgs:
                    break
                mt, raw = msgs[0]
                hsbuf = hsbuf[len(raw):]
                transcript += raw
                if mt == 13:
                    cert_requested = True
                elif mt == 20:
                    got_finished = True

        c_app = F.derive_secret(name, master, b"c ap traffic", transcript)
        s_app = F.derive_secret(name, master, b"s ap traffic", transcript)
        c_app_key = F.expand_label(name, c_app, b"key", b"", key_len)
        c_app_iv = F.expand_label(name, c_app, b"iv", b"", 12)
        s_app_key = F.expand_label(name, s_app, b"key", b"", key_len)
        s_app_iv = F.expand_label(name, s_app, b"iv", b"", 12)

        # Полёт клиента: Certificate, CertificateVerify, Finished.
        cseq = 0
        if cert_requested:
            cert_msg = F.hs(11, b"\x00"
                            + (3 + len(der) + 2).to_bytes(3, "big")
                            + len(der).to_bytes(3, "big") + der + b"\x00\x00")
            send_hs(sock, suite, c_key, c_iv, cseq, cert_msg)
            cseq += 1
            transcript += cert_msg
            digest = F.hash_name(name)(transcript).digest()
            scheme = 0x0804
            if mode == "badpkcs1":
                scheme = 0x0401
                sig = F.sign_cert_verify_pkcs1(
                    priv, scheme, digest, b"TLS 1.3, client CertificateVerify")
            else:
                sig = F.sign_cert_verify(
                    priv, scheme, digest, b"TLS 1.3, client CertificateVerify")
            if mode == "badsig13":
                # Подпись испорчена, схема верная.
                sig = bytes([sig[0] ^ 1]) + sig[1:]
            cv = F.hs(15, scheme.to_bytes(2, "big") + len(sig).to_bytes(2, "big") + sig)
            send_hs(sock, suite, c_key, c_iv, cseq, cv)
            cseq += 1
            transcript += cv

        fin = F.hs(20, F.finished_verify(name, c_hs, transcript))
        send_hs(sock, suite, c_key, c_iv, cseq, fin)

        if mode in ("ccsbad", "ccslong"):
            while F.read_record(sock) is not None:
                pass
            return 0
        if mode == "ccsafter":
            # CCS после Finished, затем законный запрос: сервер обязан
            # отвергнуть CCS, не прочитав запрос (иначе счётчик got
            # покажет, что запись дошла до приложения).
            sock.sendall(F.plain_record(20, b"\x01"))
            sock.sendall(F.seal(suite, c_app_key, c_app_iv, 0, 23, b"hello\n"))
            while F.read_record(sock) is not None:
                pass
            return 0
        if mode in ("emptyrec", "zeropad"):
            bad = b"" if mode == "emptyrec" else bytes(32)
            sock.sendall(F.seal_raw(suite, c_app_key, c_app_iv, 0, bad))
            sock.sendall(F.seal(suite, c_app_key, c_app_iv, 1, 23, b"hello\n"))
            while F.read_record(sock) is not None:
                pass
            return 0

        # Обмен строкой: сервер возвращает её перевёрнутой.
        sock.sendall(F.seal(suite, c_app_key, c_app_iv, 0, 23, b"hello\n"))
        sseq = 0
        while True:
            rec = F.read_record(sock)
            if rec is None:
                return 2
            typ, _, _, body = rec
            if typ == 21:
                return 0
            if typ != 23:
                continue
            plain = F.open_record(suite, s_app_key, s_app_iv, sseq, body)
            sseq += 1
            if plain is None or not plain:
                return 2
            if plain[-1] != 23:
                return 2
            return 0 if plain[:-1] in (b"olleh\n", b"\nolleh") else 2
    except (BrokenPipeError, ConnectionResetError, socket.timeout):
        # Для отрицательных режимов разрыв и есть ожидаемый исход;
        # положительный обмен обязан дойти до конца.
        return 0 if mode not in ("good", "goodccs") else 2
    finally:
        try:
            sock.close()
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
