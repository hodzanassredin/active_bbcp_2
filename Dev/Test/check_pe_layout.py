#!/usr/bin/env python3
"""Validate PE section order and image/file bounds required by the Windows loader."""
import struct
import sys
from pathlib import Path


def check(path):
	b=Path(path).read_bytes()
	def u16(at): return struct.unpack_from('<H',b,at)[0]
	def u32(at): return struct.unpack_from('<I',b,at)[0]
	def align(value,boundary): return (value+boundary-1)//boundary*boundary
	assert b[:2]==b'MZ'
	pe=u32(60); assert b[pe:pe+4]==b'PE\0\0'
	n=u16(pe+6); size=u16(pe+20); opt=pe+24; magic=u16(opt)
	assert magic in (0x10b,0x20b) and size==(240 if magic==0x20b else 224)
	assert u16(pe+4)==(0x8664 if magic==0x20b else 0x14c)
	if magic==0x20b: assert u16(pe+22)&0x20, "PE64 must be LARGE_ADDRESS_AWARE for the high-address heap"
	section_align=u32(opt+32); file_align=u32(opt+36)
	assert section_align>=file_align and section_align&(section_align-1)==0
	assert 512<=file_align<=65536 and file_align&(file_align-1)==0
	headers=u32(opt+60); assert headers%file_align==0 and opt+size+40*n<=headers<=len(b)
	next_va=align(headers,section_align); raw=[]; sections=[]
	for i in range(n):
		p=opt+size+40*i; name=b[p:p+8].rstrip(b'\0').decode('ascii'); vs,va,rs,ro=struct.unpack_from('<4I',b,p+8)
		assert va==next_va, f'{name}: RVA {va:#x}, expected {next_va:#x}; section table is not in ascending contiguous order'
		assert va%section_align==0
		next_va=align(va+max(vs,rs),section_align)
		if rs:
			assert ro>=headers and ro%file_align==0 and rs%file_align==0 and ro+rs<=len(b),(name,'invalid raw data bounds')
			raw.append((ro,ro+rs,name))
		else: assert ro==0,(name,'nonempty pointer for BSS')
		sections.append((va,va+max(vs,rs),u32(p+36)))
	assert u32(opt+56)==next_va,'SizeOfImage does not match sections'
	for left,right in zip(sorted(raw),sorted(raw)[1:]): assert left[1]<=right[0],'overlapping raw sections'
	entry=u32(opt+16); assert any(a<=entry<z and flags&0x20000000 for a,z,flags in sections)
	dirs=opt+(112 if magic==0x20b else 96)
	for i in range(u32(dirs-4)):
		rva,length=struct.unpack_from('<II',b,dirs+8*i)
		if not rva or not length: continue
		if i==4: assert rva+length<=len(b)
		else: assert any(a<=rva and rva+length<=z for a,z,_ in sections),('invalid data directory',i)
	print(f'{path}: PE section order, alignment and image/file bounds valid')


if __name__=='__main__':
	for path in sys.argv[1:]: check(path)
