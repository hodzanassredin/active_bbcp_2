#!/usr/bin/env python3
"""Отрицательные векторы RSASSA-PSS (RFC 8017, EMSA-PSS-VERIFY).

Векторы Wycheproof покрывают изменение m_hash и соль длины 0, но не
проверяют по отдельности структуру EM: разделитель 0x01, нули в PS,
концевой 0xBC и старшие биты maskedDB. Здесь EM собирается намеренно
испорченным и подписывается настоящим ключом (s = EM^d mod n), поэтому
проверяющая сторона получает ровно такой EM после s^e mod n.

Ключ выводится детерминированно из seed=8017 (два простых 512 бит), чтобы
файл pss-negative.txt воспроизводился побайтно.

Формат строки (как в pss-wycheproof.txt):
  n | e | sha | msg (hex) | sig (hex) | valid|invalid | комментарий
"""
import hashlib
import random

E = 65537


def is_probable_prime(n, rnd, rounds=40):
    if n < 2:
        return False
    small = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]
    for p in small:
        if n % p == 0:
            return n == p
    d, s = n - 1, 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for _ in range(rounds):
        a = rnd.randrange(2, n - 1)
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = pow(x, 2, n)
            if x == n - 1:
                break
        else:
            return False
    return True


def gen_prime(rnd, bits=512):
    while True:
        cand = rnd.getrandbits(bits) | (1 << (bits - 1)) | 1
        if cand % E == 1:
            continue
        if is_probable_prime(cand, rnd):
            return cand


def mgf1(seed, length, hashfn=hashlib.sha256):
    out = b""
    c = 0
    while len(out) < length:
        out += hashfn(seed + c.to_bytes(4, "big")).digest()
        c += 1
    return out[:length]


def build(n, d, msg, salt, defect):
    hlen = hashlib.sha256().digest_size
    modbits = n.bit_length()
    emlen = (modbits - 1 + 7) // 8
    zero_bits = 8 * emlen - (modbits - 1)
    mhash = hashlib.sha256(msg).digest()

    if defect == "salt31":
        salt = salt[:-1]
    elif defect == "salt33":
        salt = salt + b"\xA5"
    h = hashlib.sha256(b"\x00" * 8 + mhash + salt).digest()
    dblen = emlen - hlen - 1

    if defect in ("salt31", "salt33"):
        pslen = dblen - len(salt) - 1
        assert pslen >= 0
        db = bytearray(b"\x00" * pslen + b"\x01" + salt)
    else:
        pslen = dblen - hlen - 1
        sep = {"sep00": 0x00, "sep02": 0x02}.get(defect, 0x01)
        db = bytearray(b"\x00" * pslen + bytes([sep]) + salt)
        if defect == "ps":
            db[0] = 0x01

    mask = mgf1(h, dblen)
    masked = bytearray(a ^ b for a, b in zip(db, mask))
    if defect == "topbit":
        masked[0] |= 0x80
    else:
        masked[0] &= (0xFF >> zero_bits) & 0xFF
    em = bytes(masked) + h + (b"\xBB" if defect == "trailer" else b"\xBC")
    assert len(em) == emlen
    return pow(int.from_bytes(em, "big"), d, n).to_bytes((modbits + 7) // 8, "big")


def main():
    rnd = random.Random(8017)
    while True:
        p = gen_prime(rnd)
        q = gen_prime(rnd)
        n = p * q
        if p != q and n.bit_length() == 1024:
            break
    d = pow(E, -1, (p - 1) * (q - 1))
    msg = b"negative PSS"
    salt = hashlib.sha256(b"deterministic salt").digest()
    rows = [
        ("", "valid", "control: correct EMSA-PSS"),
        ("sep00", "invalid", "separator 0x00 instead of 0x01"),
        ("sep02", "invalid", "separator 0x02 instead of 0x01"),
        ("ps", "invalid", "nonzero PS octet"),
        ("trailer", "invalid", "trailer 0xBB instead of 0xBC"),
        ("topbit", "invalid", "leftmost bit of maskedDB set"),
        ("salt31", "invalid", "salt length 31"),
        ("salt33", "invalid", "salt length 33"),
    ]
    print("# n (hex) | e (hex) | sha | msg (hex) | sig (hex) | ожидание | комментарий")
    for defect, expect, comment in rows:
        sig = build(n, d, msg, salt, defect).hex().upper()
        print(f"{n:0256X}| {E:06X} | sha256 | {msg.hex()} | {sig} | {expect} | {comment}")


if __name__ == "__main__":
    main()
