#!/usr/bin/env python3
"""Build and test the WinKernel x64 from the repository root."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TESTS = [('test.exe', 'CheckWinKernel64', 0),
		('recovery.exe', 'CheckWinKernel64Recovery', 0),
		('recursive.exe', 'CheckWinKernel64Recursive', 127)]


def check_imports(path):
	data = path.read_bytes()
	def u16(p): return struct.unpack_from('<H', data, p)[0]
	def u32(p): return struct.unpack_from('<I', data, p)[0]
	pe = u32(60)
	assert data[pe:pe+4] == b'PE\0\0' and u16(pe+4) == 0x8664
	opt = pe+24
	assert u16(opt) == 0x20b
	sections = []
	for i in range(u16(pe+6)):
		pos = opt+u16(pe+20)+40*i
		size, addr, rawsize, raw = struct.unpack_from('<4I', data, pos+8)
		sections.append((addr, min(size, rawsize), raw))
	def offset(rva):
		for addr, size, raw in sections:
			if addr <= rva < addr+size:
				return raw+rva-addr
		raise AssertionError(f'invalid RVA {rva:x}')
	rva, size = struct.unpack_from('<2I', data, opt+112+8)
	assert size % 20 == 0
	imports = set()
	for i in range(size//20-1):
		p = offset(u32(offset(rva+20*i)+12))
		imports.add(data[p:data.index(0, p)].decode('ascii').lower())
	assert imports == {'kernel32.dll', 'ntdll.dll', 'ole32.dll'}, imports
	print(f'{path.name}: only Windows system DLL imports', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/winkernel64')
	parser.add_argument('--build-only', action='store_true')
	parser.add_argument('--run-only', action='store_true')
	parser.add_argument('--wine', default='wine64')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	if not args.run_only:
		commands = [
			'DevCompiler64.InitLogMarks',
			f"DevLibrarian.EnableDevLib('{out}', 'Mod64');",
			'DevCompiler64.CompileThis Kernel WinApi WinOle '
			'WinNtdll WinTrace WinKernel '
			'CheckWinKernel64 CheckWinKernel64Recovery CheckWinKernel64Recursive',
		]
		for name, module, _ in TESTS:
			commands.append(f"DevLinker1.LinkPeExe64 '{out/name}' := Kernel+ WinKernel {module}$ ~")
		result = subprocess.run([str(ROOT/'dev0Linux')], input='\n'.join(commands)+'\n',
								cwd=ROOT, text=True, capture_output=True, timeout=60)
		log = result.stdout+result.stderr
		print(log, end='', flush=True)
		assert result.returncode == 0, result.returncode
		assert not any(marker in log.lower() for marker in
					('errors detected', 'command error', 'not found', 'failed')), log
		for name, _, _ in TESTS:
			assert f'{out/name} written' in log, f'missing successful link of {name}'
	for name, _, _ in TESTS:
		check_imports(out/name)
	subprocess.run([sys.executable, str(ROOT/'Dev/Test/check_unwind64.py'),
					*(str(out/name) for name, _, _ in TESTS)], check=True, cwd=ROOT)
	if args.build_only:
		return
	env = dict(os.environ, WINEDEBUG='-all')
	for name, _, expected in TESTS:
		result = subprocess.run([args.wine, str(out/name)], cwd=out, env=env,
								text=True, capture_output=True, timeout=45)
		print(result.stdout+result.stderr, end='', flush=True)
		assert result.returncode == expected, (name, result.returncode, expected)
		print(f'\n{name}: expected exit {expected}', flush=True)
	print('WinKernel x64: all checks passed', flush=True)


if __name__ == '__main__':
	main()
