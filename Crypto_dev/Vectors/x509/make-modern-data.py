#!/usr/bin/env python3
"""Собрать Crypto_dev/Mod/X509ModernData.cp из modern/*.pem."""
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = HERE / "modern"
OUT = HERE.parent.parent / "Mod" / "X509ModernData.cp"

NAMES = [
    ("RsaRootPem", "RsaRoot"),
    ("RsaPssLeafPem", "RsaPssLeaf"),
    ("RsaPssLeafTamperedPem", "RsaPssLeafTampered"),
    ("EcRootPem", "EcRoot"),
    ("EcLeafPem", "EcLeaf"),
    ("EcLeafTamperedPem", "EcLeafTampered"),
    ("Ec384RootPem", "Ec384Root"),
    ("Ec384LeafPem", "Ec384Leaf"),
    ("EdRootPem", "EdRoot"),
    ("EdLeafPem", "EdLeaf"),
    ("EdLeafTamperedPem", "EdLeafTampered"),
    ("MixedIntPem", "MixedInt"),
    ("MixedLeafPem", "MixedLeaf"),
]

HEAD = """MODULE Crypto_devX509ModernData;
(** Сертификаты с ECDSA, RSA-PSS и Ed25519 для Crypto_devX509Modern.

	Сгенерированы Crypto_dev/Vectors/x509/make-modern.py и
	make-modern-data.py; закрытые ключи удалены. RsaRoot — RSA-2048,
	подписывает RSA-PSS/SHA-256; RsaPssLeaf — лист pss.example.
	EcRoot/EcLeaf — P-256, ECDSA/SHA-256, ec.example; EcLeafTampered —
	тот же лист с испорченным именем в TBS. Ec384Root/Ec384Leaf —
	P-384, ECDSA/SHA-384. EdRoot/EdLeaf — Ed25519, ed.example.
	RsaPssLeafTampered и EdLeafTampered — те же листы с испорченным именем в TBS.
	MixedInt — P-256 промежуточный CA под RsaRoot (RSA-PSS), MixedLeaf —
	Ed25519 лист под MixedInt (ECDSA), mixed.example. *)
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
    out.append("END Crypto_devX509ModernData.")
    OUT.write_text("\n".join(out).rstrip() + "\n")
    print("написано", OUT)


if __name__ == "__main__":
    main()
