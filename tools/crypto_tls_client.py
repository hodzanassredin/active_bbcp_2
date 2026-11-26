#!/usr/bin/env python3
"""Клиент TLS 1.2 для проверки CertificateVerify на стороне CryptoTLS.

Режимы:
  goodcertverify — корректная подпись CertificateVerify и Finished;
  badcertverify  — цепочка верная, подпись CertificateVerify испорчена;
  badfinished    — верная цепочка и CertificateVerify, испорчен Finished;
  nocertcv       — Certificate не послан, CertificateVerify послан;
  oldversion     — ClientHello с версией TLS 1.0.

Сервер (Crypto_devTLSServer) в режиме REQUIRE_CLIENT=1 обязан принять
первый и отвергнуть второй. Нужен, чтобы порча «проверка подписи
CertificateVerify пропущена» краснила тест. Запускается из
tools/test-crypto-tls.sh после tools/crypto_tls_client.py.
"""
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import crypto_tls_lib as L

MODE, HOST, PORT, CERT, KEY = sys.argv[1:6]


def send(sock, msg):
    sock.sendall(L.plain_record(22, msg))


def main():
    cert_der = L.load_cert_der(CERT)
    key = L.load_rsa_key(KEY)
    client_random = os.urandom(32)
    suite = L.SUITE
    hello_version = b"\x03\x01" if MODE == "oldversion" else L.VERSION
    hello = hello_version + client_random + b"\x00" + (2).to_bytes(2, "big") + suite + b"\x01\x00"
    msg = L.handshake(1, hello)
    transcript = msg

    sock = socket.create_connection((HOST, PORT), timeout=10)
    sock.settimeout(10)
    try:
        send(sock, msg)

        server_random = None
        server_pub = None
        requested = False
        done = False
        while not done:
            rec = L.read_record(sock)
            if rec is None:
                return 2
            typ, _, _, body = rec
            if typ == 21:
                return 0
            if typ != 22:
                continue
            for mt, raw in L.split_handshake(body):
                transcript += raw
                if mt == 2:
                    _, server_random, _, _ = L.parse_server_hello(raw[4:])
                elif mt == 11:
                    certs = L.parse_certificate(raw[4:])
                    if not certs:
                        return 2
                elif mt == 12:
                    params, _ = L.parse_ske(raw[4:])
                    server_pub = params[4:36]
                elif mt == 13:
                    requested = True
                elif mt == 14:
                    done = True
        if server_random is None or server_pub is None:
            return 2

        priv, pub = L.new_x25519()
        shared = priv.exchange(L.x25519.X25519PublicKey.from_public_bytes(server_pub))
        master = L.prf(shared, b"master secret", client_random + server_random, 48)
        keys = L.derive_keys(master, client_random, server_random)
        if requested and MODE != "nocertcv":
            msg = L.handshake(11, (3 + len(cert_der)).to_bytes(3, "big")
                              + len(cert_der).to_bytes(3, "big") + cert_der)
            transcript += msg
            send(sock, msg)

        msg = L.handshake(16, b"\x20" + pub)
        transcript += msg
        send(sock, msg)

        if requested:
            digest = L.sha256(transcript)
            if MODE in ("badcertverify", "nocertcv"):
                sig = os.urandom(256)
            else:
                sig = L.sign_transcript(key, digest)
            msg = L.handshake(15, b"\x04\x01" + len(sig).to_bytes(2, "big") + sig)
            transcript += msg
            send(sock, msg)
        sock.sendall(L.plain_record(20, b"\x01"))
        verify = L.prf(master, b"client finished", L.sha256(transcript), 12)
        if MODE == "badfinished":
            verify = bytes([verify[0] ^ 1]) + verify[1:]
        sock.sendall(L.aead_record(22, L.handshake(20, verify),
                                   keys["client_key"], keys["client_iv"], 0))

        # Сервер ответит CCS и Finished; при ошибке — алертом.
        seen_ccs = False
        while True:
            rec = L.read_record(sock)
            if rec is None:
                return 0 if MODE in ("badcertverify", "badfinished", "nocertcv", "oldversion") else 2
            typ, _, _, body = rec
            if typ == 21:
                return 0
            if typ == 20:
                seen_ccs = True
            elif typ == 22 and seen_ccs:
                plain = L.aead_open(body, keys["server_key"], keys["server_iv"], 0, typ)
                if plain is None:
                    return 2
                if MODE in ("badcertverify", "badfinished", "nocertcv", "oldversion"):
                    return 2
                break

        # Положительный контроль: обмен строкой, как s_server -rev.
        sock.sendall(L.aead_record(23, b"hello\n", keys["client_key"], keys["client_iv"], 1))
        while True:
            rec = L.read_record(sock)
            if rec is None:
                return 2
            typ, _, _, body = rec
            if typ == 21:
                return 0
            if typ == 23:
                plain = L.aead_open(body, keys["server_key"], keys["server_iv"], 1, typ)
                if plain in (b"olleh\n", b"\nolleh"):
                    return 0
                return 2
    finally:
        L.close_quiet(sock)


if __name__ == "__main__":
    if len(sys.argv) != 6:
        sys.stderr.write("usage: crypto_tls_client.py MODE HOST PORT CERT KEY\n")
        sys.exit(2)
    sys.exit(main())
