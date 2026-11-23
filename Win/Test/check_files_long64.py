#!/usr/bin/env python3
"""Exercise Files64 on Windows AMD64, including sparse offsets above 4 GiB."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/winfiles-long64')
	parser.add_argument('--wine', default='wine64')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	data = Path(tempfile.mkdtemp(prefix='FilesCase-', dir=out))
	(data/'locked.bin').write_bytes(b'LOCKED_ORIGINAL')
	(data/'readonly.bin').write_bytes(b'READONLY_ORIGINAL')
	(data/'temp').mkdir()
	with (data/'max.bin').open('wb') as f:
		f.truncate(2**31-1)
		f.seek(2**31-2)
		f.write(b'2')
	with (data/'oversize.bin').open('wb') as f:
		f.truncate(2**31)
	with (data/'huge.bin').open('wb') as f: f.truncate(2**32+8192)
	windows_temp = 'Z:'+str(data/'temp').replace('/', '\\')
	commands = f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64');
DevCompiler64.CompileThis Kernel WinApi WinOle WinNtdll WinTrace WinKernel Files WinFiles Files64 WinFiles64 CheckWinFilesLong64
DevLinker1.LinkPeExe64 '{out}/files.exe' := Kernel+ WinKernel Files WinFiles Files64 WinFiles64 CheckWinFilesLong64$ ~
'''
	result = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		text=True, capture_output=True, timeout=90)
	log = result.stdout+result.stderr
	print(log, end='', flush=True)
	assert result.returncode == 0, result.returncode
	assert not re.search(r'error:|errors? detected|command error|not found|failed', log, re.I), log
	assert f'{out}/files.exe written' in log
	result = subprocess.run([args.wine, str(out/'files.exe')], cwd=ROOT,
		env=dict(os.environ, WINEDEBUG='-all', BB_FILES_TEST_DIR=data.name, TMP=windows_temp, TEMP=windows_temp),
		capture_output=True, text=True, timeout=180)
	print(result.stdout+result.stderr, end='', flush=True)
	assert result.returncode == 0, result.returncode
	assert 'WinFiles64 x64 disk write OK' in result.stdout+result.stderr
	assert (data/'persist.bin').read_bytes() == bytes(i % 127 for i in range(4096))*300
	assert (data/'данные.bin').read_bytes() == b'*'
	assert (data/'locked.bin').read_bytes() == b'\t'
	assert (data/'readonly.bin').read_bytes() == b'\x0b'
	assert (data/'update.bin').read_bytes() == bytes([14, 7])
	pattern = bytes(i % 127 for i in range(4096))
	assert (data/'large.bin').stat().st_size == 70*1024*1024
	with (data/'large.bin').open('rb') as f:
		for _ in range(70*1024*1024//len(pattern)):
			assert f.read(len(pattern)) == pattern
		assert not f.read(1)
	with (data/'max.bin').open('rb') as f:
		f.seek(2**31-2)
		assert f.read() == b'3'
	assert not list((data/'temp').glob('.bbtmp-*')), 'temporary backing file leaked'
	assert not list(data.glob('.bbtmp-*'))
	assert not (data/'missing').exists() and not (data/'renamed.bin').exists()
	with (data/'huge.bin').open('rb') as f:
		assert (data/'huge.bin').stat().st_size == 2**32+8200
		for at in [2**31-4,2**32-4,2**32+8192]:
			f.seek(at); assert f.read(8) == b'ABCDEFGH'
	print(f'\nWinFiles64 Windows x64: all disk checks passed ({data})')


if __name__ == '__main__':
	main()
