#!/usr/bin/env python3
"""Поддельный TLS 1.2 сервер для отрицательных проверок клиента Crypto.

Сценарии:
  badsig   — корректная цепочка, но подпись ServerKeyExchange испорчена;
  oversize — sigLen = 65535 (проверка границ до копирования);
  badcert  — в Certificate длина сертификата больше списка;
  garbage  — ServerHello с невозможной длиной, затем закрытие;
  noske    — вместо ServerKeyExchange сразу ServerHelloDone: клиент ждёт
             ServerKeyExchange при ECDHE и обязан отказаться без трапа.
  badcurve — SKE с группой, которой клиент не предлагал;
  badpoint — SKE с точкой secp256r1 вне кривой;
  badpss   — SKE с подписью RSA-PSS, испорчен последний байт;
  badecdsa — SKE с подписью ECDSA P-256, испорчен последний байт;
  badtagchacha — ChaCha20-Poly1305, затем запись с испорченным тегом.

Клиент обязан отказаться и не упасть (без TRAP). Запускается из
tools/test-crypto-tls.sh; порт и путь к PEM сертификата — аргументы.
"""
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import crypto_tls_lib as L

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, padding, utils


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


def record(typ, body):
    return bytes([typ]) + b"\x03\x03" + len(body).to_bytes(2, "big") + body


def handshake(hs_type, body):
    return bytes([hs_type]) + len(body).to_bytes(3, "big") + body


def load_cert_der(path):
    # PEM -> DER без внешних зависимостей
    data = open(path, "rb").read()
    start = data.index(b"-----BEGIN CERTIFICATE-----") + len(b"-----BEGIN CERTIFICATE-----")
    end = data.index(b"-----END CERTIFICATE-----", start)
    import base64
    return base64.b64decode(b"".join(data[start:end].split()))


def serve_handshake(conn, mode, cert_path, key_path):
    """Полное рукопожатие с корректной подписью SKE.

    goodfinished — верный Finished (положительный контроль);
    badfinished  — испорченные 12 байт verify_data;
    badccs       — ChangeCipherSpec со значением 2 и верный Finished;
    badtag       — верное рукопожатие, затем запись с испорченным GCM-тегом;
    shortrec     — верное рукопожатие, затем GCM-запись короче nonce и тега;
    oldversion   — ServerHello с версией TLS 1.1;
    longfinished — Finished из 13 байт вместо 12;
    longccs      — ChangeCipherSpec из 2 байт с верным первым байтом;
    zeropub      — SKE с нулевым открытым ключом X25519 (общий секрет 0);
    badcurve     — SKE с группой, которой клиент не предлагал;
    badpoint     — SKE с точкой secp256r1 вне кривой;
    badpss       — SKE с подписью RSA-PSS, испорчен последний байт;
    badecdsa     — SKE с подписью ECDSA P-256, испорчен последний байт;
    badtagchacha — ChaCha20-Poly1305, затем запись с испорченным тегом.
    """
    rec = read_record(conn)
    if rec is None:
        return
    _, _, _, body = rec
    msgs = L.split_handshake(body)
    if not msgs or msgs[0][0] != 1:
        return
    client_hs = msgs[0][1]
    transcript = client_hs
    _, client_random, _ = L.parse_client_hello(client_hs[4:])

    server_random = os.urandom(32)
    if mode == "downgrade":
        # Сигнал понижения RFC 8446, 4.1.3: сервер умеет 1.3, но ответил 1.2.
        server_random = os.urandom(24) + b"DOWNGRD\x01"
    version = b"\x03\x02" if mode == "oldversion" else L.VERSION
    is_chacha = mode == "badtagchacha"
    suite = L.SUITE_CHACHA if is_chacha else L.SUITE
    sh = version + server_random + b"\x00" + suite + b"\x00" + b"\x00\x00"
    msg = L.handshake(2, sh)
    transcript += msg
    conn.sendall(L.plain_record(22, msg))

    der = load_cert_der(cert_path)
    msg = L.handshake(11, (3 + len(der)).to_bytes(3, "big") + len(der).to_bytes(3, "big") + der)
    transcript += msg
    conn.sendall(L.plain_record(22, msg))

    priv, pub = L.new_x25519()
    curve = b"\x00\x1d"
    pub_len = 32
    scheme = b"\x04\x01"
    if mode == "zeropub":
        pub = b"\x00" * 32
    elif mode == "badcurve":
        # Группа, которой клиент не предлагал.
        curve = b"\x99\x99"
        pub = os.urandom(32)
    elif mode == "badpoint":
        # secp256r1, но точка заведомо не лежит на кривой.
        curve = b"\x00\x17"
        pub = b"\x04" + b"\x01" * 64
        pub_len = 65
    params = b"\x03" + curve + bytes([pub_len]) + pub
    signed = client_random + server_random + params
    if mode == "badecdsa":
        # ECDSA-подпись SKE с испорченным последним байтом.
        ec_key = L.load_ec_key(key_path)
        sig = ec_key.sign(L.sha256(signed),
                          ec.ECDSA(utils.Prehashed(hashes.SHA256())))
        sig = sig[:-1] + bytes([sig[-1] ^ 1])
        scheme = b"\x04\x03"
    elif mode == "badpss":
        # RSA-PSS с испорченным последним байтом.
        sig = key.sign(L.sha256(signed),
                       padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                                   salt_length=32),
                       utils.Prehashed(hashes.SHA256()))
        sig = sig[:-1] + bytes([sig[-1] ^ 1])
        scheme = b"\x08\x04"
    else:
        key = L.load_rsa_key(key_path)
        sig = L.sign_transcript(key, L.sha256(signed))
    ske = params + scheme + len(sig).to_bytes(2, "big") + sig
    msg = L.handshake(12, ske)
    transcript += msg
    conn.sendall(L.plain_record(22, msg))

    msg = L.handshake(14, b"")
    transcript += msg
    conn.sendall(L.plain_record(22, msg))

    # ClientKeyExchange (клиентский сертификат мы не запрашивали).
    client_pub = None
    while client_pub is None:
        rec = read_record(conn)
        if rec is None:
            return
        typ, _, _, body = rec
        if typ != 22:
            return
        for mt, raw in L.split_handshake(body):
            transcript += raw
            if mt == 16:
                client_pub = L.parse_cke(raw[4:])
    if mode == "zeropub":
        shared = b"\x00" * 32
    else:
        shared = priv.exchange(L.x25519.X25519PublicKey.from_public_bytes(client_pub))
    master = L.prf(shared, b"master secret", client_random + server_random, 48)
    if is_chacha:
        keys = L.derive_keys_chacha(master, client_random, server_random)
    else:
        keys = L.derive_keys(master, client_random, server_random)
    rec_open = L.chacha_open if is_chacha else L.aead_open
    rec_seal = L.chacha_record if is_chacha else L.aead_record

    # CCS клиента — открытый, Finished — первый шифрованный, seq=0.
    seen_ccs = False
    while True:
        rec = read_record(conn)
        if rec is None:
            return
        typ, _, _, body = rec
        if typ == 20:
            seen_ccs = True
        elif typ == 22 and seen_ccs:
            plain = rec_open(body, keys["client_key"], keys["client_iv"], 0, typ)
            if plain is None:
                return
            transcript += plain
            break
        elif typ == 21:
            return

    verify = L.prf(master, b"server finished", L.sha256(transcript), 12)
    if mode == "badfinished":
        verify = bytes([verify[0] ^ 1]) + verify[1:]
    if mode == "longfinished":
        verify = verify + b"\x00"
    ccs_value = 2 if mode == "badccs" else 1
    ccs_body = b"\x01\x00" if mode == "longccs" else bytes([ccs_value])
    conn.sendall(L.plain_record(20, ccs_body))
    conn.sendall(rec_seal(22, L.handshake(20, verify),
                          keys["server_key"], keys["server_iv"], 0))

    if mode not in ("goodfinished", "badtag", "badtagchacha", "shortrec", "zeropub"):
        # Клиент обязан ответить алертом и закрыться; читаем до закрытия.
        conn.settimeout(2)
        try:
            while conn.recv(1024):
                pass
        except (socket.timeout, ConnectionResetError, BrokenPipeError):
            pass
        return

    # Приложение: вернуть перевёрнутую строку, как openssl s_server -rev.
    seq = 1
    conn.settimeout(5)
    while True:
        rec = read_record(conn)
        if rec is None:
            return
        typ, _, _, body = rec
        if typ == 23:
            plain = rec_open(body, keys["client_key"], keys["client_iv"], seq, typ)
            if plain is None:
                return
            if mode == "shortrec":
                conn.sendall(L.plain_record(23, b"\x00" * 8))
                return
            seq += 1
            reply = plain[::-1]
            if plain.endswith(b"\n"):
                reply = plain[:-1][::-1] + b"\n"
            rec = rec_seal(23, reply,
                           keys["server_key"], keys["server_iv"], 1)
            if mode in ("badtag", "badtagchacha"):
                rec = rec[:-1] + bytes([rec[-1] ^ 1])
            conn.sendall(rec)
            return
        if typ == 21:
            return


def serve(mode, port, cert_path, key_path=None):
    der = load_cert_der(cert_path)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)
    srv.settimeout(30)
    sys.stdout.write("ready\n")
    sys.stdout.flush()
    conn, _ = srv.accept()
    conn.settimeout(15)
    try:
        if mode in ("goodfinished", "badfinished", "badccs", "badtag", "shortrec", "oldversion", "longfinished", "longccs", "zeropub", "badcurve", "badpoint", "badpss", "badecdsa", "badpss", "badtagchacha", "downgrade"):
            serve_handshake(conn, mode, cert_path, key_path)
            return 0
        # ClientHello
        rec = read_record(conn)
        if rec is None:
            return 0
        # ServerHello
        random = os.urandom(32)
        sh = b"\x03\x03" + random + b"\x00" + b"\xc0\x2f" + b"\x00" + b"\x00\x00"
        if mode == "hugerecord":
            # Объявленная длина записи больше любого допустимого буфера.
            conn.sendall(bytes([23, 3, 3, 0xFF, 0xFF]))
            time.sleep(1)
            return 0
        if mode == "garbage":
            body = sh[:-2] + b"\xff\xff"  # длина расширений больше записи
            conn.sendall(record(22, handshake(2, body)))
            time.sleep(1)
            return 0
        conn.sendall(record(22, handshake(2, sh)))
        # Certificate
        if mode == "badlist":
            body = (0xFFFFFF).to_bytes(3, "big") + (0x010000).to_bytes(3, "big") + b"A" * 32
            conn.sendall(record(22, handshake(11, body)))
            time.sleep(1)
            return 0
        if mode == "badcert":
            body = (3 + len(der) + 7).to_bytes(3, "big") + (len(der) + 7).to_bytes(3, "big") + der
            conn.sendall(record(22, handshake(11, body)))
            time.sleep(1)
            return 0
        body = (3 + len(der)).to_bytes(3, "big") + len(der).to_bytes(3, "big") + der
        conn.sendall(record(22, handshake(11, body)))
        # ServerKeyExchange
        if mode == "noske":
            # Порядок нарушен: клиент ждёт SKE, а получает
            # ServerHelloDone. Отказ, не трап.
            conn.sendall(record(22, handshake(14, b"")))
        else:
            params = b"\x03\x00\x1d\x20" + os.urandom(32)
            if mode == "oversize":
                sig_len = 0xFFFF
                sig = b""
            else:
                sig_len = 256
                sig = os.urandom(sig_len)  # заведомо неверная подпись
            ske = params + b"\x04\x01" + sig_len.to_bytes(2, "big") + sig
            conn.sendall(record(22, handshake(12, ske)))
            conn.sendall(record(22, handshake(14, b"")))
        # Ждём реакцию: при верной проверке SKE клиент шлёт alert и не
        # доходит до ClientKeyExchange. Иначе фиксируем CKE.
        conn.settimeout(2)
        try:
            while True:
                rec = read_record(conn)
                if rec is None:
                    break
                typ, _, _, body = rec
                if typ == 22 and len(body) >= 1 and body[0] == 16:
                    sys.stdout.write("saw_cke\n")
                    sys.stdout.flush()
                    break
        except (socket.timeout, ConnectionResetError, BrokenPipeError):
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
    if len(sys.argv) not in (4, 5):
        sys.stderr.write("usage: crypto_tls_fake.py MODE PORT CERT.pem [KEY.pem]\n")
        sys.exit(2)
    key = sys.argv[4] if len(sys.argv) == 5 else None
    sys.exit(serve(sys.argv[1], int(sys.argv[2]), sys.argv[3], key))
