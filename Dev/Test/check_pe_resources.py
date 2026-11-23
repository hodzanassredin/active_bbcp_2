#!/usr/bin/env python3
"""Build PE64 resources, inspect their complete directory tree and load them with WinAPI."""
import os
import argparse
import shutil
from pathlib import Path
import struct
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--native-linux-dir', help='also run DevLinker1 inside an existing native Linux x64 tree')
args=parser.parse_args()
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'build/pe-resources64'
OUT.mkdir(parents=True, exist_ok=True)
def align(b): return b + b'\0' * (-len(b)%4)
def key(x): return struct.pack('<HH',65535,x) if isinstance(x,int) else x.encode('utf-16le')+b'\0\0'
def record(typ,name,lang,payload):
	h = align(key(typ)+key(name)) + struct.pack('<IHHII',0,0x30,lang,0,0)
	return align(struct.pack('<II',len(payload),8+len(h))+h+payload)
null = record(0,0,0,b'')
container = null+record(10,100,1033,b'ENGLISH')+record(10,100,1049,b'RUSSIAN')+record('ТЕСТ','ДАННЫЕ',1033,b'NAMED!')
(OUT/'multiple.res').write_bytes(container)
(OUT/'sample.tlb').write_bytes(b'TYPELIB!')
(OUT/'app.manifest').write_text('<?xml version="1.0" encoding="UTF-8" standalone="yes"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><assemblyIdentity version="1.0.0.0" name="ResourceTest" type="win32" processorArchitecture="amd64"/></assembly>')
dib = struct.pack('<IiiHHIIiiII',40,1,1,1,24,0,4,0,0,0,0)+b'\x00\x00\xff\x00'
(OUT/'sample.bmp').write_bytes(struct.pack('<HIHHI',0x4d42,14+len(dib),0,0,54)+dib)
image = struct.pack('<IiiHHIIiiII',40,32,64,1,32,0,32*32*4,0,0,0,0)+b'\x00\x00\xff\xff'*(32*32)+b'\0'*128
(OUT/'sample.cur').write_bytes(struct.pack('<HHHBBBBHHII',0,2,1,32,32,0,0,2,3,len(image),22)+image)
exe = OUT/'test.exe'
resources = f"1 Applogo.ico 2 '{OUT}/sample.cur' 3 '{OUT}/sample.bmp' 4 BlackBox.res 5 '{OUT}/app.manifest' 6 '{OUT}/sample.tlb' 0 '{OUT}/multiple.res'"
commands = f"""DevLibrarian.EnableDevLib('{OUT}', 'Mod64');
DevCompiler64.CompileThis CheckPeResourceApi64 CheckPeResources64
DevLinker1.LinkPeExe64 '{exe}' := CheckPeResources64$ {resources} ~
"""
r = subprocess.run([str(ROOT/'dev0Linux')],input=commands,cwd=ROOT,text=True,capture_output=True,timeout=90)
log = r.stdout+r.stderr; (OUT/'build.log').write_text(log)
assert not r.returncode and f'{exe} written' in log and 'errors detected' not in log,log
def extract(blob):
	pe=struct.unpack_from('<I',blob,60)[0]; optional=pe+24
	magic=struct.unpack_from('<H',blob,optional)[0]; assert magic in (0x10b,0x20b)
	size_optional=struct.unpack_from('<H',blob,pe+20)[0]
	resource_rva,resource_size=struct.unpack_from('<II',blob,optional+(112 if magic==0x20b else 96)+16)
	sections=[]
	for i in range(struct.unpack_from('<H',blob,pe+6)[0]):
		p=optional+size_optional+40*i; name=blob[p:p+8].rstrip(b'\0'); vs,va,rs,ro=struct.unpack_from('<IIII',blob,p+8)
		sections.append((name,va,vs,ro,rs,struct.unpack_from('<I',blob,p+36)[0]))
	def offset(rva):
		for _,va,vs,ro,rs,_ in sections:
			if va<=rva<va+max(vs,rs): return ro+rva-va
		raise AssertionError(hex(rva))
	base=offset(resource_rva); leaves={}
	def read_key(n):
		if n&0x80000000:
			p=base+(n&0x7fffffff); size=struct.unpack_from('<H',blob,p)[0]
			return blob[p+2:p+2+2*size].decode('utf-16le')
		return n
	def tree(at,path=()):
		named,numbered=struct.unpack_from('<HH',blob,base+at+12); entries=[]
		for i in range(named+numbered):
			k,v=struct.unpack_from('<II',blob,base+at+16+8*i); entries.append(read_key(k)); p=path+(read_key(k),)
			if v&0x80000000: tree(v&0x7fffffff,p)
			else:
				rva,size,codepage,reserved=struct.unpack_from('<IIII',blob,base+v)
				assert resource_rva<=rva and rva+size<=resource_rva+resource_size
				assert codepage==reserved==0 and len(p)==3
				leaves[p]=blob[offset(rva):offset(rva)+size]
		assert entries==sorted(entries,key=lambda x:(isinstance(x,int),x))
	tree(0)
	assert not next(s for s in sections if s[0]==b'.rsrc')[5]&0x80000000
	return leaves
subprocess.run(['python3',str(ROOT/'Dev/Test/check_pe_layout.py'),str(exe)],check=True)
leaves=extract(exe.read_bytes())
assert leaves[2,3,1033]==dib
assert leaves[10,100,1033]==b'ENGLISH' and leaves[10,100,1049]==b'RUSSIAN'
assert leaves['ТЕСТ','ДАННЫЕ',1033]==b'NAMED!'
assert leaves['TYPELIB',6,1033]==b'TYPELIB!'
assert leaves[24,5,1033]==(OUT/'app.manifest').read_bytes()
assert leaves[1,1,1033]==struct.pack('<HH',2,3)+image
ico=(ROOT/'Win/Rsrc/Applogo.ico').read_bytes(); n=struct.unpack_from('<H',ico,4)[0]; group=leaves[14,1,1033]
assert len(group)==6+14*n
for i in range(n):
	size,at=struct.unpack_from('<II',ico,6+16*i+8); icon_id=struct.unpack_from('<H',group,6+14*i+12)[0]
	assert leaves[3,icon_id,1033]==ico[at:at+size]
r=subprocess.run(['wine64',str(exe)],cwd=ROOT,env=dict(os.environ,WINEDEBUG='-all'),capture_output=True,text=True,timeout=45)
(OUT/'run.log').write_text(r.stdout+r.stderr)
assert not r.returncode,(r.returncode,r.stdout+r.stderr)
subprocess.run(['python3',str(ROOT/'Dev/Test/check_unwind64.py'),str(exe)],check=True)
print(f'PE64 resources OK: {len(leaves)} payloads, icon/cursor/bitmap/version/manifest/TYPELIB, names and languages; WinAPI and unwind checks passed')

# The same resource writer must also work for the existing i386 commands.
exe32=OUT/'test32.exe'
commands=f"""DevLibrarian.DisableDevLib
DevCompiler.CompileThis CheckPeResources32
DevLinker1.LinkPeExe '{exe32}' := CheckPeResources32 {resources} ~
"""
r=subprocess.run([str(ROOT/'dev0Linux')],input=commands,cwd=ROOT,text=True,capture_output=True,timeout=60)
assert not r.returncode and f'{exe32} written' in r.stdout+r.stderr,r.stdout+r.stderr
assert extract(exe32.read_bytes())==leaves
subprocess.run(['python3',str(ROOT/'Dev/Test/check_pe_layout.py'),str(exe32)],check=True)
# Errors must not publish partially written executables; state resets per command.
(OUT/'broken.res').write_bytes(container[:-3])
(OUT/'broken.ico').write_bytes(b'\0\0\1\0\1\0')
invalid=["1[65536] Applogo.ico", "1 missing.ico", "1", f"1 '{OUT}/broken.res'", f"1 '{OUT}/broken.ico'", f"6 '{OUT}/sample.tlb' 6 '{OUT}/sample.tlb'"]
commands=f"DevLibrarian.EnableDevLib('{OUT}', 'Mod64');\n"
for i,resource_args in enumerate(invalid):
	commands+=f"DevLinker1.LinkPeExe64 '{OUT}/invalid-{i}.exe' := CheckPeResources64$ {resource_args} ~\n"
commands+=f"DevLinker1.LinkElfExe64 '{OUT}/invalid-elf' := CheckPeResources64$ 1 Applogo.ico ~\n"
commands+=f"DevLinker1.LinkPeExe64 '{OUT}/empty.exe' := CheckPeResources64$ ~\n"
r=subprocess.run([str(ROOT/'dev0Linux')],input=commands,cwd=ROOT,text=True,capture_output=True,timeout=60)
(OUT/'negative.log').write_text(r.stdout+r.stderr)
assert not r.returncode and not list(OUT.glob('invalid-*')),r.stdout+r.stderr
blob=(OUT/'empty.exe').read_bytes(); pe=struct.unpack_from('<I',blob,60)[0]
assert struct.unpack_from('<II',blob,pe+24+112+16)==(0,0)
print('PE32 resources match PE64; missing/truncated/duplicate resources, bad language and ELF rejection checked; resource state resets correctly')

if args.native_linux_dir:
	native_dir=(ROOT/args.native_linux_dir).resolve()
	assert (native_dir/'blackbox-console').exists()
	commands=f"DevLibrarian.EnableDevLib('{native_dir}', 'Mod64');\nDevCompiler64.CompileThis DevLnkResources DevLnkWritePe DevLinker1\n"
	r=subprocess.run([str(ROOT/'dev0Linux')],input=commands,cwd=ROOT,text=True,capture_output=True,timeout=60)
	assert not r.returncode and 'errors detected' not in r.stdout+r.stderr,r.stdout+r.stderr
	for name in ('PeResources64','PeResourceApi64'):
		shutil.copy2(OUT/'Check/Code'/f'{name}.ocf',native_dir/'Check/Code'/f'{name}.ocf')
	native_exe=OUT/'native.exe'
	native_resources=resources.replace('Applogo.ico',f"'{ROOT}/Win/Rsrc/Applogo.ico'").replace('BlackBox.res',f"'{ROOT}/Win/Rsrc/BlackBox.res'")
	command=f"DevLibrarian.EnableDevLib('{native_dir}', 'Mod64');\nDevLinker1.LinkPeExe64 '{native_exe}' := CheckPeResources64$ {native_resources} ~\n"
	r=subprocess.run([str(native_dir/'blackbox-console')],input=command,cwd=native_dir,text=True,capture_output=True,timeout=60)
	(OUT/'native-link.log').write_text(r.stdout+r.stderr)
	assert not r.returncode and f'{native_exe} written' in r.stdout+r.stderr,r.stdout+r.stderr
	assert extract(native_exe.read_bytes())==leaves
	subprocess.run(['python3',str(ROOT/'Dev/Test/check_pe_layout.py'),str(native_exe)],check=True)
	r=subprocess.run(['wine64',str(native_exe)],cwd=ROOT,env=dict(os.environ,WINEDEBUG='-all'),text=True,capture_output=True,timeout=45)
	assert not r.returncode,r.stdout+r.stderr
	subprocess.run(['python3',str(ROOT/'Dev/Test/check_unwind64.py'),str(native_exe)],check=True)
	print('Native Linux x64 linker: identical PE resources, valid PE/unwind and WinAPI checks passed')
