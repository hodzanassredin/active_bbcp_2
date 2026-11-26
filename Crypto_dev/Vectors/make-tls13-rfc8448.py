#!/usr/bin/env python3
"""Извлекает сообщения простого рукопожатия RFC 8448 в Crypto_devTLS13Data.

    python3 Crypto_dev/Vectors/make-tls13-rfc8448.py rfc8448.txt

Запускать из корня репозитория: пути к TLS13Data.cp и tls13-rfc8448.txt
относительны.

Печатает Crypto_dev/Mod/TLS13Data.cp и Crypto_dev/Vectors/tls13-rfc8448.txt.
Текст RFC — вход, а не исполняемый файл; генератор читает только строки
вида "Label (N octets):  hex ...".
"""
import re
import sys


LABELS = [
    ("ClientHello", "ClientHello"),
    ("ServerHello", "ServerHello"),
    ("EncryptedExtensions", "EncryptedExtensions"),
    ("Certificate", "Certificate"),
    ("CertificateVerify", "CertificateVerify"),
    ("Finished", "Finished"),
]

# Значения расписания из раздела 3 RFC 8448.
VALUES = [
    ("ClientRandom", "cb 34 ec b1 e7 81 63 ba 1c 38 c6 da cb 19 6a 6d "
                     "ff a2 1a 8d 99 12 ec 18 a2 ef 62 83 02 4d ec e7"),
    ("ServerRandom", "a6 af 06 a4 12 18 60 dc 5e 6e 60 24 9c d3 4c 95 "
                     "93 0c 8a c5 cb 14 34 da c1 55 77 2e d3 e2 69 28"),
    ("SharedSecret", "8b d4 05 4f b5 5b 9d 63 fd fb ac f9 f0 4b 9f 0d "
                     "35 e6 d6 3f 53 75 63 ef d4 62 72 90 0f 89 49 2d"),
    ("EarlySecret", "33 ad 0a 1c 60 7e c0 3b 09 e6 cd 98 93 68 0c e2 "
                    "10 ad f3 00 aa 1f 26 60 e1 b2 2e 10 f1 70 f9 2a"),
    ("DerivedSecret", "6f 26 15 a1 08 c7 02 c5 67 8f 54 fc 9d ba b6 97 "
                      "16 c0 76 18 9c 48 25 0c eb ea c3 57 6c 36 11 ba"),
    ("HandshakeSecret", "1d c8 26 e9 36 06 aa 6f dc 0a ad c1 2f 74 1b 01 "
                        "04 6a a6 b9 9f 69 1e d2 21 a9 f0 ca 04 3f be ac"),
    ("ClientHsSecret", "b3 ed db 12 6e 06 7f 35 a7 80 b3 ab f4 5e 2d 8f "
                       "3b 1a 95 07 38 f5 2e 96 00 74 6a 0e 27 a5 5a 21"),
    ("ServerHsSecret", "b6 7b 7d 69 0c c1 6c 4e 75 e5 42 13 cb 2d 37 b4 "
                       "e9 c9 12 bc de d9 10 5d 42 be fd 59 d3 91 ad 38"),
    ("DerivedSecret2", "43 de 77 e0 c7 77 13 85 9a 94 4d b9 db 25 90 b5 "
                       "31 90 a6 5b 3e e2 e4 f1 2d d7 a0 bb 7c e2 54 b4"),
    ("MasterSecret", "18 df 06 84 3d 13 a0 8b f2 a4 49 84 4c 5f 8a 47 "
                     "80 01 bc 4d 4c 62 79 84 d5 a4 1d a8 d0 40 29 19"),
    ("ClientAppSecret", "9e 40 64 6c e7 9a 7f 9d c0 5a f8 88 9b ce 65 52 "
                        "87 5a fa 0b 06 df 00 87 f7 92 eb b7 c1 75 04 a5"),
    ("ServerAppSecret", "a1 1a f9 f0 55 31 f8 56 ad 47 11 6b 45 a9 50 32 "
                        "82 04 b4 f4 4b fb 6b 3a 4b 4f 1f 3f cb 63 16 43"),
    ("ServerVerifyData", "9b 9b 14 1d 90 63 37 fb d2 cb dc e7 1d f4 de da "
                         "4a b4 2c 30 95 72 cb 7f ff ee 54 54 b7 8f 07 18"),
    ("ClientVerifyData", "a8 ec 43 6d 67 76 34 ae 52 5a c1 fc eb e1 1a 03 "
                         "9e c1 76 94 fa c6 e9 85 27 b6 42 f2 ed d5 ce 61"),
    ("ServerWriteKey", "3f ce 51 60 09 c2 17 27 d0 f2 e4 e8 6e e4 03 bc"),
    ("ServerWriteIv", "5d 31 3e b2 67 12 76 ee 13 00 0b 30"),
    ("ServerAppKey", "9f 02 28 3b 6c 9c 07 ef c2 6b b9 f2 ac 92 e3 56"),
    ("ServerAppIv", "cf 78 2b 88 dd 83 54 9a ad f1 e9 84"),
    ("ClientAppKey", "17 42 2d da 59 6e d5 d9 ac d8 90 e3 c6 3f 50 51"),
    ("ClientAppIv", "5b 78 92 3d ee 08 57 90 33 e5 23 d9"),
    ("ExporterSecret", "fe 22 f8 81 17 6e da 18 eb 8f 44 52 9e 67 92 c5 "
                       "0c 9a 3f 89 45 2f 68 d8 ae 31 1b 43 09 d3 cf 50"),
]

HEADER = re.compile(r"^\s{3,}([A-Za-z]+) \((\d+) octets\):(.*)$")


def extract_messages(text):
    """Возвращает список (label, hex) в порядке появления."""
    out = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        m = HEADER.match(lines[i])
        if not m:
            i += 1
            continue
        label = m.group(1)
        if label not in [l for l, _ in LABELS]:
            i += 1
            continue
        chunk = m.group(3).strip()
        parts = [chunk] if chunk else []

        def is_hex(line):
            return re.match(r"^\s+[0-9a-f]{2}( [0-9a-f]{2})*$", line) is not None

        def is_page(line):
            s = line.strip()
            return (s == "" or s.startswith("Thomson")
                    or s.startswith("RFC 8448") or s == "\f")

        j = i + 1
        while j < len(lines):
            line = lines[j]
            if is_hex(line):
                parts.append(line.strip())
                j += 1
                continue
            if is_page(line):
                # Разрыв страницы: hex может продолжаться после колонтитула.
                k = j
                while k < len(lines) and is_page(lines[k]):
                    k += 1
                if k < len(lines) and is_hex(lines[k]):
                    j = k
                    continue
                break
            break
        data = " ".join(parts)
        if data and re.match(r"^[0-9a-f ]+$", data):
            out.append((label, data))
        i = j
    return out


def cp_string(hexstr):
    chunks = [hexstr[i:i + 56] for i in range(0, len(hexstr), 56)]
    return " +\n\t\t".join('"%s"' % c for c in chunks)


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: make-tls13-rfc8448.py RFC8448.txt\n")
        return 2
    text = open(sys.argv[1], encoding="utf-8").read()
    msgs = extract_messages(text)
    # Первые пять сообщений уникальны; Finished встречается дважды.
    wanted = {}
    for label, data in msgs:
        key = label
        if label == "Finished":
            key = "ServerFinished" if "ServerFinished" not in wanted else "ClientFinished"
        if key not in wanted:
            wanted[key] = data.replace(" ", "").upper()
    for name, _ in LABELS:
        if name == "Finished":
            continue
        if name not in wanted:
            sys.stderr.write("missing %s\n" % name)
            return 1
    if "ServerFinished" not in wanted or "ClientFinished" not in wanted:
        sys.stderr.write("missing Finished messages\n")
        return 1

    names = [name for name, _ in LABELS if name != "Finished"] + [
        "ServerFinished", "ClientFinished"]
    cp = ["MODULE Crypto_devTLS13Data;",
          "(** Сообщения простого рукопожатия RFC 8448, раздел 3.",
          "",
          "\tСгенерировано Crypto_dev/Vectors/make-tls13-rfc8448.py из",
          "\tтекста RFC 8448; каждое значение — hex байтов сообщения",
          "\tвместе с 4-байтовым заголовком handshake. *)",
          "",
          "\tCONST"]
    for name in names:
        cp.append("\t\t%s* = %s;" % (name, cp_string(wanted[name])))
    for name, value in VALUES:
        cp.append("\t\t%s* = \"%s\";" % (name, value.replace(" ", "").upper()))
    cp.append("")
    cp.append("END Crypto_devTLS13Data.")
    open("Crypto_dev/Mod/TLS13Data.cp", "w", encoding="utf-8").write("\n".join(cp) + "\n")

    with open("Crypto_dev/Vectors/tls13-rfc8448.txt", "w", encoding="utf-8") as f:
        f.write("# RFC 8448, раздел 3 (Simple 1-RTT Handshake), hex.\n")
        f.write("# Сгенерировано make-tls13-rfc8448.py.\n\n")
        for name in names:
            f.write("%s = %s\n\n" % (name, wanted[name]))
        for name, value in VALUES:
            f.write("%s = %s\n\n" % (name, value.replace(" ", "").upper()))
    for name in names:
        sys.stdout.write("%s %d bytes\n" % (name, len(wanted[name]) // 2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
