#!/usr/bin/env python3
"""Link a native Linux AMD64 executable and validate ELF, startup, GC and finalizers."""
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'build/elf64-regression'
OUT.mkdir(parents=True,exist_ok=True)
exe = OUT/'test'
commands = f'''DevLibrarian.EnableDevLib('{OUT}', 'Mod64');
DevCompiler64.CompileThis Kernel LinLibc LinLibW LinDl LinRt Utf LinKernel Math CheckMath64 CheckElf64State CheckElf64
DevLinker1.LinkElfExe64 '{exe}' := Kernel+ LinKernel CheckElf64$ ~
'''
r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
	text=True,capture_output=True,timeout=120)
log = r.stdout+r.stderr; (OUT/'build.log').write_text(log)
assert not r.returncode and f'{exe} written' in log and 'errors detected' not in log, log
blob = exe.read_bytes()
h = struct.unpack_from('<16sHHIQQQIHHHHHH',blob)
assert h[0][:7] == b'\x7fELF\x02\x01\x01' and h[1:4] == (2,62,1)
segments = [struct.unpack_from('<IIQQQQQQ',blob,h[5]+i*h[9]) for i in range(h[10])]
for p in segments:
	if p[0] == 1:
		assert p[5] <= p[6] and p[2] % p[7] == p[3] % p[7]
		assert p[1] & 3 != 3, 'writable executable segment'
interp = next(p for p in segments if p[0]==3)
assert blob[interp[2]:interp[2]+interp[5]] == b'/lib64/ld-linux-x86-64.so.2\0'
assert next(p for p in segments if p[0]==0x6474e551)[1] == 6
r = subprocess.run(['readelf','-aW',str(exe)],capture_output=True,text=True,timeout=10)
(OUT/'readelf.log').write_text(r.stdout+r.stderr)
assert not r.returncode and not r.stderr, r.stderr
assert 'R_X86_64_GLOB_DAT' in r.stdout and '__libc_start_main' in r.stdout
# The executable contains all code and starts from a directory without OCF files.
r = subprocess.run([str(exe),'argument'],cwd='/tmp',env=dict(os.environ,ELF64_TEST_VALUE='present'),
	capture_output=True,text=True,timeout=30)
log = r.stdout+r.stderr; (OUT/'run.log').write_text(log)
assert not r.returncode and 'DevLinker1 ELF64 native Kernel OK' in log and 'ELF64 terminators OK' in log and log.count('TRAP REPORT - BEGIN') == 1 and '77 (postcondition violated)' in log, log
assert not (OUT/'System/Code/Kernel64.ocf').exists()
print(log,flush=True)
print('ELF64 structure, dynamic imports, argc/argv, environment, heap/GC, protected trap recovery, Math and terminators passed')
