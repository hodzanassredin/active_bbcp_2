#!/usr/bin/env python3
"""Прозрачный прокси для проверки прослушки TLS.

    tls-proxy.py СЛУШАТЬ ЦЕЛЬ ЗАХВАТ

Слушает СЛУШАТЬ (host:port), при каждом соединении подключается к ЦЕЛИ и
перекладывает байты в обе стороны, дописывая всё увиденное в ЗАХВАТ.
TLS заканчивается на узлах, поэтому в захвате должен быть только шифртекст.
Прокси не разбирает протокол и не меняет байты.
"""
import socket
import select
import sys


def parse(addr):
    host, _, port = addr.rpartition(":")
    if not host:
        host = "127.0.0.1"
    return host, int(port)


def relay(client, server, out):
    socks = [client, server]
    while True:
        readable, _, _ = select.select(socks, [], [], 1.0)
        if not readable:
            continue
        done = False
        for s in readable:
            data = s.recv(65536)
            if not data:
                done = True
                break
            out.write(data)
            out.flush()
            (server if s is client else client).sendall(data)
        if done:
            return


def main():
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    host, port = parse(sys.argv[1])
    target = parse(sys.argv[2])
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((host, port))
    listener.listen(8)
    with open(sys.argv[3], "ab", buffering=0) as out:
        while True:
            client, _ = listener.accept()
            try:
                server = socket.create_connection(target, timeout=5)
            except OSError:
                client.close()
                continue
            try:
                relay(client, server, out)
            except OSError:
                pass
            finally:
                client.close()
                server.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
