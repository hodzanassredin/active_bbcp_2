#!/usr/bin/env python3
"""Check Windows AMD64 profiling after normal GUI initialization."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/win-profiler64')
	parser.add_argument('--wine', default='wine64')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	subprocess.run([sys.executable, str(ROOT/'Win/Test/build_blackbox64.py'), '--out', str(out)], cwd=ROOT, check=True)
	subprocess.run(['x86_64-w64-mingw32-gcc', '-c', str(ROOT/'Win/Test/check_subsystem_layout.c'), '-o', str(out/'layout.o')], check=True)
	exe = out/'check-profiler.exe'
	commands = f"""DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64')
DevCompiler64.CompileThis WinMM DevProfiler DevProfiler__Win CheckWinProfiler64 CheckWinProfilerAuto64
DevLinker1.LinkPeExe64 '{exe}' := Kernel+ WinKernel Files WinFiles StdLoader Services CheckWinProfilerAuto64 WinLoader$ ~
"""
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT, text=True, capture_output=True, timeout=120)
	log = r.stdout+r.stderr
	(out/'profiler-build.log').write_text(log)
	assert r.returncode == 0 and not re.search(r'errors? detected|error:|not found|failed', log, re.I), log
	assert f'{exe} written' in log, log
	r = subprocess.run([args.wine, str(exe)], cwd=ROOT, env=dict(os.environ, WINEDEBUG='-all'),
		text=True, capture_output=True, timeout=30)
	log = r.stdout+r.stderr
	print(log)
	assert r.returncode == 0 and 'Win profiler x64 CONTEXT and timer sampling OK' in log, (r.returncode, log)


if __name__ == '__main__':
	main()
