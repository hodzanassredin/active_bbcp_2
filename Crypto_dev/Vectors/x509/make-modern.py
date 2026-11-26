#!/usr/bin/env python3
"""Сгенерировать сертификаты с ECDSA, RSA-PSS и Ed25519 и модуль данных.

Закрытые ключи создаются в памяти и на диск не пишутся; в
Crypto_dev/Vectors/x509/modern сохраняются только сертификаты, по ним
make-modern-data.py собирает Crypto_dev/Mod/X509ModernData.cp.
"""
from datetime import datetime, timedelta, timezone
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519, padding, rsa
from cryptography.x509.oid import NameOID

HERE = Path(__file__).resolve().parent
OUT = HERE / "modern"
OUT.mkdir(exist_ok=True)

NB = datetime(2026, 1, 1, tzinfo=timezone.utc)
NA = datetime(2036, 1, 1, tzinfo=timezone.utc)


def name(cn):
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])


def ca_cert(subject, issuer_name, key, issuer_key, alg, rsa_pad=None):
    b = (x509.CertificateBuilder()
         .subject_name(name(subject)).issuer_name(name(issuer_name))
         .public_key(key.public_key()).serial_number(x509.random_serial_number())
         .not_valid_before(NB).not_valid_after(NA)
         .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
         .add_extension(x509.KeyUsage(digital_signature=False, content_commitment=False,
                                      key_encipherment=False, data_encipherment=False,
                                      key_agreement=False, key_cert_sign=True, crl_sign=True,
                                      encipher_only=False, decipher_only=False), critical=True))
    if rsa_pad is None:
        return b.sign(issuer_key, alg)
    return b.sign(issuer_key, alg, rsa_padding=rsa_pad)


def leaf_cert(subject, issuer_name, key, issuer_key, alg, san, rsa_pad=None):
    b = (x509.CertificateBuilder()
         .subject_name(name(subject)).issuer_name(name(issuer_name))
         .public_key(key.public_key()).serial_number(x509.random_serial_number())
         .not_valid_before(NB).not_valid_after(NA)
         .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
         .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                                      key_encipherment=False, data_encipherment=False,
                                      key_agreement=False, key_cert_sign=False, crl_sign=False,
                                      encipher_only=False, decipher_only=False), critical=True)
         .add_extension(x509.SubjectAlternativeName([x509.DNSName(san)]), critical=False))
    if rsa_pad is None:
        return b.sign(issuer_key, alg)
    return b.sign(issuer_key, alg, rsa_padding=rsa_pad)


def pem(cert):
    return cert.public_bytes(serialization.Encoding.PEM)


def main():
    pss = padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32)
    rsa_root = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    r = ca_cert("Modern RSA Root", "Modern RSA Root", rsa_root, rsa_root,
                hashes.SHA256(), pss)
    (OUT / "RsaRoot.pem").write_bytes(pem(r))
    p = leaf_cert("pss.example", "Modern RSA Root",
                  rsa.generate_private_key(public_exponent=65537, key_size=2048),
                  rsa_root, hashes.SHA256(), "pss.example", pss)
    (OUT / "RsaPssLeaf.pem").write_bytes(pem(p))
    pt = bytearray(p.public_bytes(serialization.Encoding.DER))
    pos = pt.find(b"pss.example")
    pt[pos] = ord("f")   # pfs.example: DER цел, подпись не сходится
    (OUT / "RsaPssLeafTampered.pem").write_bytes(
        x509.load_der_x509_certificate(bytes(pt)).public_bytes(serialization.Encoding.PEM))

    ec_root = ec.generate_private_key(ec.SECP256R1())
    er = ca_cert("Modern EC Root", "Modern EC Root", ec_root, ec_root, hashes.SHA256())
    (OUT / "EcRoot.pem").write_bytes(pem(er))
    ec_leaf = leaf_cert("ec.example", "Modern EC Root",
                        ec.generate_private_key(ec.SECP256R1()), ec_root,
                        hashes.SHA256(), "ec.example")
    (OUT / "EcLeaf.pem").write_bytes(pem(ec_leaf))
    tampered = bytearray(ec_leaf.public_bytes(serialization.Encoding.DER))
    pos = tampered.find(b"ec.example")
    tampered[pos] = ord("f")   # ef.example: DER цел, подпись не сходится
    (OUT / "EcLeafTampered.pem").write_bytes(
        x509.load_der_x509_certificate(bytes(tampered)).public_bytes(serialization.Encoding.PEM))

    ec384_root = ec.generate_private_key(ec.SECP384R1())
    er3 = ca_cert("Modern EC384 Root", "Modern EC384 Root", ec384_root, ec384_root,
                  hashes.SHA384())
    (OUT / "Ec384Root.pem").write_bytes(pem(er3))
    el3 = leaf_cert("ec384.example", "Modern EC384 Root",
                    ec.generate_private_key(ec.SECP384R1()), ec384_root,
                    hashes.SHA384(), "ec384.example")
    (OUT / "Ec384Leaf.pem").write_bytes(pem(el3))

    ed_root = ed25519.Ed25519PrivateKey.generate()
    edr = ca_cert("Modern Ed Root", "Modern Ed Root", ed_root, ed_root, None)
    (OUT / "EdRoot.pem").write_bytes(pem(edr))
    edl = leaf_cert("ed.example", "Modern Ed Root", ed25519.Ed25519PrivateKey.generate(),
                    ed_root, None, "ed.example")
    (OUT / "EdLeaf.pem").write_bytes(pem(edl))
    et = bytearray(edl.public_bytes(serialization.Encoding.DER))
    pos = et.find(b"ed.example")
    et[pos] = ord("f")   # fd.example
    (OUT / "EdLeafTampered.pem").write_bytes(
        x509.load_der_x509_certificate(bytes(et)).public_bytes(serialization.Encoding.PEM))

    # Смешанная цепочка: RSA-PSS корень -> EC P-256 промежуточный -> Ed25519 лист.
    mixed_int = ec.generate_private_key(ec.SECP256R1())
    mi = ca_cert("Modern Mixed Intermediate", "Modern RSA Root", mixed_int, rsa_root,
                 hashes.SHA256(), pss)
    (OUT / "MixedInt.pem").write_bytes(pem(mi))
    ml = leaf_cert("mixed.example", "Modern Mixed Intermediate",
                   ed25519.Ed25519PrivateKey.generate(), mixed_int, hashes.SHA256(), "mixed.example")
    (OUT / "MixedLeaf.pem").write_bytes(pem(ml))
    print("written", OUT)


if __name__ == "__main__":
    main()
