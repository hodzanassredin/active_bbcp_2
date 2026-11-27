#!/usr/bin/env python3
"""Некорректный TLS-клиент для отрицательных проверок сервера Crypto.

Сценарии:
  v2hello  — запись Handshake с типом 30 (SSLv2 ClientHello), но длиной 0;
             следом байты, задающие challengeLen = 65535 и допустимый набор
             шифров. Старый разбор SSLv2 читал их без проверки длины и
             уходил за границу буфера; сервер обязан отказать без трапа.
  v2record — запись с типом 128 (SSLv2 ClientHello): сервер обязан отказать.
  hstype   — запись Handshake с неизвестным типом: сервер обязан отказать.
  ccsfirst — ChangeCipherSpec {0x01} до первого ClientHello: по RFC 8446
             §5 это unexpected_message; сервер обязан отказать.
  ccsfirstbad — то же с байтом 0x02: отказ.
  ccsfirstlong — CCS длиной 2 до ClientHello: отказ.

Запускается из tools/test-crypto-tls.sh; адрес — аргумент host:port.
Клиент ничего не проверяет: он только посылает данные и закрывается.
"""
import socket
import sys
import time


def record(typ, body):
    return bytes([typ, 3, 3]) + len(body).to_bytes(2, "big") + body


def handshake(hs_type, body):
    return bytes([hs_type]) + len(body).to_bytes(3, "big") + body


# Тело: SSLv2 ClientHello, но handshake-длина 0. Байты после заголовка
# задают nofSuites=0, idLen=0, challengeLen=0xFFFF, набор 00 C0 2F.
V2_HELLO = bytes([30, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0x00, 0xC0, 0x2F])


def serve(mode, host, port):
    s = socket.create_connection((host, port), timeout=10)
    s.settimeout(3)
    try:
        if mode == "v2hello":
            s.sendall(record(22, V2_HELLO))
        elif mode == "v2record":
            s.sendall(bytes([128, 3, 3, 0, 0]))
        elif mode == "hstype":
            s.sendall(record(22, handshake(63, b"\x00")))
        elif mode == "ccsfirst":
            s.sendall(record(20, b"\x01"))
        elif mode == "ccsfirstbad":
            s.sendall(record(20, b"\x02"))
        elif mode == "ccsfirstlong":
            s.sendall(record(20, b"\x01\x01"))
        else:
            sys.stderr.write("unknown mode %s\n" % mode)
            return 2
        # Дочитать ответ (алерт) и закрыться.
        try:
            s.recv(1024)
        except (socket.timeout, ConnectionResetError, BrokenPipeError):
            pass
        time.sleep(0.1)
    finally:
        s.close()
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.stderr.write("usage: tls_evil_client.py MODE HOST PORT\n")
        sys.exit(2)
    sys.exit(serve(sys.argv[1], sys.argv[2], int(sys.argv[3])))
