#!/usr/bin/env python3
"""Собрать Crypto_dev/Mod/X509Data.cp из chains/*.pem.

Запускать после generate-chains.sh:
    python3 -I Crypto_dev/Vectors/x509/make-data.py
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = HERE / "chains"
OUT = HERE.parent.parent / "Mod" / "X509Data.cp"

NAMES = [
    ("RootPem", "R"),
    ("IntermediatePem", "I"),
    ("ChainLeafPem", "L"),
    ("CnLeafPem", "CNL"),
    ("CnWildPem", "CNW"),
    ("CnSanShadowPem", "CNS"),
    ("BadSignerPem", "S"),
    ("VictimPem", "V"),
    ("NoBcPem", "NB"),
    ("NoBcLeafPem", "LB"),
    ("KuPem", "KU"),
    ("KuLeafPem", "LK"),
    ("Path0Pem", "P0"),
    ("Path0SubPem", "P2"),
    ("Path0LeafPem", "LP"),
    ("Path0DirectPem", "PD"),
    ("CritPem", "CU"),
    ("CritLeafPem", "LC"),
    ("CritOnlyLeafPem", "CL"),
    ("ExpiredRootPem", "XR"),
    ("ExpiredRootLeafPem", "XL"),
    ("ExpiredIntPem", "EXP"),
    ("ExpiredIntLeafPem", "LE"),
    ("ForgedRootPem", "FR"),
    ("ForgedLeafPem", "LF"),
    ("CycleAPem", "CA"),
    ("CycleBPem", "CB"),
    ("CycleLeafPem", "CYC"),
]

HEAD = """MODULE Crypto_devX509Data;
(** Сертификаты для отрицательных проверок цепочки CryptoX509.

	Сгенерированы Crypto_dev/Vectors/x509/generate-chains.sh и
	make-data.py; закрытые ключи удалены, тестам нужны только
	сертификаты. Имена: R — корень, I — промежуточный CA с pathlen:1,
	L — его лист; CNL/CNW/CNS — листы без SAN (CN), с шаблоном в CN и
	с SAN, заслоняющим CN; S — издатель с CA:FALSE и V — подписанный
	им лист; NB — промежуточный без basicConstraints, LB — его лист;
	KU — CA без keyCertSign, LK — его лист; P0 — pathlen:0, P2 — CA
	под ним, LP — лист, PD — лист прямо под P0; CU — CA с неизвестным критичным расширением,
	LC — его лист; EXP — просроченный CA, LE — его лист; FR —
	поддельный корень с тем же именем, что R, LF — его лист; CA/CB —
	цикл, CYC — лист. *)
"""


def main():
    out = [HEAD, "\tCONST"]
    for const, short in NAMES:
        pem = (BASE / (short + ".pem")).read_text().strip().replace("\n", "")
        chunks = [pem[i:i + 120] for i in range(0, len(pem), 120)]
        out.append('\t\t%s* = "%s"%s' % (const, chunks[0], "" if len(chunks) == 1 else " +"))
        for idx, chunk in enumerate(chunks[1:]):
            last = idx == len(chunks) - 2
            out.append('\t\t"%s"%s' % (chunk, ";" if last else " +"))
        out.append("")
    out.append("END Crypto_devX509Data.")
    OUT.write_text("\n".join(out).rstrip() + "\n")
    print("написано", OUT)


if __name__ == "__main__":
    main()
