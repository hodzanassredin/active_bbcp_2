#!/usr/bin/env python3
"""Exercise real Windows OLE drag loops against a native test-only peer."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/drag64')
	parser.add_argument('--wine', default='wine64')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	(out/'drag.txt').write_text('Dropped file', encoding='ascii')
	subprocess.run([sys.executable, str(ROOT/'Win/Test/build_blackbox64.py'), '--out', str(out)], check=True)
	subprocess.run(['x86_64-w64-mingw32-gcc', '-shared', '-O2', '-Wall', str(ROOT/'Win/Test/drag64_probe.c'), '-o', str(out/'drag64probe.dll'), '-lole32', '-luuid', '-lshell32', '-luser32', '-lgdi32'], check=True)
	subprocess.run(['x86_64-w64-mingw32-gcc', '-municode', '-O2', '-Wall', str(ROOT/'Win/Test/drag64_probe.c'), '-o', str(out/'drag64peer.exe'), '-lole32', '-luuid', '-lshell32', '-luser32', '-lgdi32'], check=True)
	commands = f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64');
DevCompiler64.CompileThis CheckWinDragProbe64 CheckWinDragConfig64 CheckWinDrag64
DevLinker1.LinkPeExe64 '{out}/drag.exe' := Kernel+ WinKernel Files WinFiles StdLoader CheckWinDragConfig64 WinLoader$ ~
'''
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT, capture_output=True, text=True, timeout=120)
	log = r.stdout+r.stderr
	print(log, end='', flush=True)
	assert not r.returncode and not re.search(r'error:|errors? detected|not found|failed', log, re.I), log
	r = subprocess.run([args.wine,str(out/'drag.exe')], cwd=out, env=dict(os.environ,WINEDEBUG='-all', BB_DRAG_FILE='Z:'+str(out/'drag.txt').replace('/', '\\')), capture_output=True, text=True, timeout=120)
	print(r.stdout+r.stderr, end='', flush=True)
	assert r.returncode == 0, r.returncode
	log = r.stdout+r.stderr
	assert 'WinDrag x64 OLE drag/drop OK' in log and 'TRAP REPORT' not in log
	print('Windows x64 drag/drop: all checks passed')


if __name__ == '__main__':
	main()
