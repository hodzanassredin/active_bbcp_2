#!/usr/bin/env python3
"""Check the actual PE32+ output of the CheckPe64Main integration fixture.
Usage: python3 Dev/Test/check_pe64.py /tmp/dev-pe64-test/hello.exe
No third-party packages or Windows SDK are required.
"""
import struct
import sys
from pathlib import Path


def check(path):
    data = Path(path).read_bytes()
    def u16(p): return struct.unpack_from('<H', data, p)[0]
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    def u64(p): return struct.unpack_from('<Q', data, p)[0]
    assert data[:2] == b'MZ'
    pe = u32(60)
    assert data[pe:pe+4] == b'PE\0\0'
    assert u16(pe+4) == 0x8664, 'not AMD64'
    count, optional_size = u16(pe+6), u16(pe+20)
    assert optional_size == 240
    opt = pe+24
    assert u16(opt) == 0x20b
    base, image_size = u64(opt+24), u32(opt+56)
    assert base + image_size < 2**31, 'image exceeds supported low-address range'
    sections = []
    table = opt+optional_size
    for i in range(count):
        p = table+40*i
        name = data[p:p+8].rstrip(b'\0')
        size, rva, raw_size, raw = struct.unpack_from('<4I', data, p+8)
        flags = u32(p+36)
        assert rva % 4096 == 0
        assert raw_size % 512 == 0
        if raw_size:
            assert raw % 512 == 0 and raw+raw_size <= len(data)
        if name == b'.var':
            assert raw == raw_size == 0 and size > 0, 'incorrect BSS section'
        sections.append((name, size, rva, raw_size, raw, flags))
    assert len({s[0] for s in sections}) == count, 'duplicate section headers'
    assert table+40*count <= u32(opt+60), 'section table exceeds SizeOfHeaders'
    def offset(rva, size=1):
        for _, _, addr, length, raw, _ in sections:
            if addr <= rva and rva+size <= addr+length:
                return raw+rva-addr
        raise AssertionError(f'RVA {rva:x} is not file-backed')
    def name_at(rva):
        p = offset(rva)
        return data[p:data.index(0, p)].decode('ascii')
    def directory(n):
        return struct.unpack_from('<2I', data, opt+112+8*n)
    entry = u32(opt+16)
    assert any(s[2] <= entry < s[2]+s[1] and s[5] & 0x20000000 for s in sections)
    imp, imp_size = directory(1)
    assert imp_size % 20 == 0
    imported = {}
    for i in range(imp_size//20-1):
        lookup, timestamp, forward, name, iat = struct.unpack_from('<5I', data, offset(imp+20*i, 20))
        assert lookup % 8 == iat % 8 == 0
        dll = name_at(name)
        names = []
        for n in range(10000):
            value = u64(offset(lookup+8*n, 8))
            assert u64(offset(iat+8*n, 8)) == value
            if not value: break
            assert value < image_size, 'incorrect 64-bit import thunk'
            names.append(name_at(value+2))
        else: raise AssertionError('unterminated import lookup table')
        imported[dll] = names
    assert {'ExitProcess', 'GetStdHandle', 'WriteFile'} <= set(imported['kernel32.dll'])
    exp, _ = directory(0)
    p = offset(exp, 40)
    funcs, names = u32(p+20), u32(p+24)
    assert funcs == names and names > 0
    ptrs, ords = u32(p+32), u32(p+36)
    exports = [name_at(u32(offset(ptrs+4*i, 4))) for i in range(names)]
    assert exports == sorted(exports) and len(set(exports)) == len(exports)
    assert 'CheckPe64Dep_Twice' in exports and 'Kernel64_NewRec' in exports
    for i in range(names): assert u16(offset(ords+2*i, 2)) < funcs
    rel, rel_size = directory(5)
    cursor, relocations = 0, set()
    while cursor < rel_size:
        page, size = struct.unpack_from('<2I', data, offset(rel+cursor, 8))
        assert size >= 8 and size % 4 == 0 and cursor+size <= rel_size
        for i in range((size-8)//2):
            v = u16(offset(rel+cursor+8+2*i, 2))
            kind, addr = v >> 12, page+(v & 4095)
            if not kind: continue
            assert kind == 10, 'PE64 needs DIR64, not HIGHLOW'
            assert addr not in relocations, 'duplicate base relocation'
            relocations.add(addr)
            value = u64(offset(addr, 8))
            assert base <= value < base+image_size, 'bad absolute pointer'
        cursor += size
    assert relocations
    print(f'{path}: PE64 valid; {count} sections, {names} sorted exports, {len(relocations)} DIR64 relocations')


if __name__ == '__main__':
    check(sys.argv[1])
