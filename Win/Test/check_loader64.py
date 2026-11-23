#!/usr/bin/env python3
"""Build and exercise StdLoader on Windows AMD64 using Wine."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/winloader64')
	parser.add_argument('--wine', default='wine64')
	parser.add_argument('--build-only', action='store_true')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	commands = f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64');
DevCompiler64.CompileThis Kernel WinApi WinOle WinNtdll WinTrace WinKernel Files WinFiles StdLoader CheckLoaderState CheckLoaderDep CheckLoaderPlugin CheckLoaderBroken CheckLoaderLegacy CheckWinLoader64
DevLinker1.LinkPeExe64 '{out}/loader.exe' := Kernel+ WinKernel Files WinFiles StdLoader CheckLoaderState CheckWinLoader64$ ~
'''
	result = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		text=True, capture_output=True, timeout=60)
	log = result.stdout+result.stderr
	print(log, end='', flush=True)
	assert result.returncode == 0, result.returncode
	assert not any(s in log.lower() for s in ('errors detected', 'command error', 'not found', 'failed')), log
	assert f'{out}/loader.exe written' in log
	# Mutations only affect generated fixtures in the isolated output tree.
	code = out/'Check/Code'
	broken = bytearray((code/'LoaderBroken.ocf').read_bytes())
	uw = broken.rfind(b'UW64')
	assert uw >= 0
	arch = bytearray(broken)
	struct.pack_into('<I', arch, 4, 10)
	(code/'LoaderArch.ocf').write_bytes(arch)
	(code/'LoaderTruncated.ocf').write_bytes(broken[:12])
	struct.pack_into('<I', broken, uw+4, 99)
	(code/'LoaderBroken.ocf').write_bytes(broken)
	legacy = (code/'LoaderLegacy.ocf').read_bytes()
	uw = legacy.rfind(b'UW64')
	assert uw >= 0
	(code/'LoaderLegacy.ocf').write_bytes(legacy[:uw])
	# The two dynamically loaded modules must not be linked into the executable.
	exe = (out/'loader.exe').read_bytes()
	assert b'CheckLoaderPlugin\0' not in exe and b'CheckLoaderDep\0' not in exe
	subprocess.run([sys.executable, str(ROOT/'Dev/Test/check_unwind64.py'), str(out/'loader.exe')],
		check=True, cwd=ROOT)
	if not args.build_only:
		# Run from an unrelated CWD: OCF lookup must use the executable directory.
		result = subprocess.run([args.wine, str(out/'loader.exe')], cwd=ROOT,
			env=dict(os.environ, WINEDEBUG='-all'), capture_output=True, text=True, timeout=45)
		print(result.stdout+result.stderr, end='', flush=True)
		assert result.returncode == 0, result.returncode
		assert 'WinLoader x64 OK' in result.stdout+result.stderr
		print('\nStdLoader Windows x64: all checks passed', flush=True)


if __name__ == '__main__':
	main()
