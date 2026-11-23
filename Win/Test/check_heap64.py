#!/usr/bin/env python3
"""Check high-address GC objects; --large also retains more than 4 GiB of live data."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/heap64')
	parser.add_argument('--wine', default='wine64')
	parser.add_argument('--large', action='store_true', help='allocate and touch a 4.25 GiB live heap')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	subprocess.run([sys.executable,str(ROOT/'Win/Test/build_blackbox64.py'),'--out',str(out)],check=True)
	commands = f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64');
DevCompiler64.CompileThis CheckWinHeapConfig64 CheckWinHeap64
DevLinker1.LinkPeExe64 '{out}/heap.exe' := Kernel+ WinKernel Files WinFiles StdLoader CheckWinHeapConfig64 WinLoader$ ~
'''
	r = subprocess.run([str(ROOT/'dev0Linux')],input=commands,cwd=ROOT,capture_output=True,text=True,timeout=120)
	log = r.stdout+r.stderr
	print(log,end='',flush=True)
	assert not r.returncode and not re.search(r'error:|errors? detected|not found|failed',log,re.I),log
	r = subprocess.run([args.wine,str(out/'heap.exe')],cwd=out,
		env=dict(os.environ,WINEDEBUG='-all',BB_HEAP_LARGE='1' if args.large else '0'),
		capture_output=True,text=True,timeout=300 if args.large else 90)
	log = r.stdout+r.stderr
	print(log,end='',flush=True)
	assert not r.returncode and 'WinHeap x64 OK' in log and 'TRAP REPORT' not in log,r.returncode
	if args.large:
		assert '4.25 GiB live heap, collection and reuse OK' in log
	print('\nWindows x64 high-address heap: all checks passed')


if __name__ == '__main__':
	main()
