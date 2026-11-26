#!/usr/bin/env python3
"""Поддельный TLS 1.2 сервер для отрицательных проверок клиента Crypto.

Сценарии:
  badsig   — корректная цепочка, но подпись ServerKeyExchange испорчена;
  oversize — sigLen = 65535 (проверка границ до копирования);
  badcert  — в Certificate длина сертификата больше списка;
  garbage  — ServerHello с невозможной длиной, затем закрытие;
  noske    — вместо ServerKeyExchange сразу ServerHelloDone: клиент ждёт
             ServerKeyExchange при ECDHE и обязан отказаться без трапа.

Клиент обязан отказаться и не упасть (без TRAP). Запускается из
tools/test-crypto-tls.sh; порт и путь к PEM сертификата — аргументы.
"""
import os
import socket
import sys
import time


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


def serve(mode, port, cert_path):
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
    if len(sys.argv) != 4:
        sys.stderr.write("usage: crypto_tls_fake.py MODE PORT CERT.pem\n")
        sys.exit(2)
    sys.exit(serve(sys.argv[1], int(sys.argv[2]), sys.argv[3]))
