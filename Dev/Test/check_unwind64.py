#!/usr/bin/env python3
"""Validate PE64 runtime function ranges and the UW64 v1 code subset."""
import struct
import sys
from pathlib import Path


def check(path):
    data = Path(path).read_bytes()
    def u16(p): return struct.unpack_from('<H', data, p)[0]
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    pe = u32(60)
    assert data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0'
    assert u16(pe+4) == 0x8664 and u16(pe+24) == 0x20b
    opt = pe+24
    sections = []
    for i in range(u16(pe+6)):
        p = opt+u16(pe+20)+i*40
        name = data[p:p+8].rstrip(b'\0')
        size, addr, rawsize, raw = struct.unpack_from('<4I', data, p+8)
        sections.append((name, addr, size, raw, rawsize, u32(p+36)))
    def locate(rva, n):
        for name, addr, size, raw, rawsize, flags in sections:
            if addr <= rva and rva+n <= addr+min(size, rawsize):
                assert raw+rva-addr+n <= len(data)
                return raw+rva-addr, name, flags
        raise AssertionError(f'RVA {rva:x} outside file-backed section')
    table, size = struct.unpack_from('<II', data, opt+112+3*8)
    assert size and size % 12 == 0
    pos, name, flags = locate(table, size)
    assert name == b'.pdata' and not flags & 0x80000000
    previous = 0
    for i in range(size//12):
        beg, end, info = struct.unpack_from('<III', data, pos+12*i)
        assert previous <= beg < end
        previous = end
        _, _, flags = locate(beg, end-beg)
        assert flags & 0x20000000
        assert info % 4 == 0
        p, name, flags = locate(info, 4)
        assert name == b'.xdata' and not flags & 0x80000000
        version, prolog, count, frame = data[p:p+4]
        assert version == 1 and prolog <= end-beg
        assert frame & 15 in (0, 5)
        locate(info, (4+count*2+3)//4*4)
        j, prior, fp = 0, prolog, 0
        while j < count:
            offset, opcode = data[p+4+2*j:p+6+2*j]
            op, arg = opcode & 15, opcode >> 4
            assert offset <= prior
            prior = offset
            if op == 0:
                assert arg in (3, 5, 6, 7, 12, 13, 14, 15)
                slots = 1
            elif op == 1:
                assert arg == 0
                slots = 2
            elif op == 3:
                assert arg == 0 and frame & 15 == 5
                fp += 1
                slots = 1
            elif op == 8:
                assert 6 <= arg <= 15
                slots = 2
            else:
                raise AssertionError(f'unsupported unwind opcode {op}')
            j += slots
            assert j <= count
        assert fp == int(bool(frame & 15))
    print(f'{path}: {size//12} sorted runtime functions; unwind data valid')


if __name__ == '__main__':
    for path in sys.argv[1:]:
        check(path)
