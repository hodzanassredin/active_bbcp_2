#!/usr/bin/env python3
"""Compile every Win module for AMD64 and check native layouts and API calls."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/win-subsystem64')
	parser.add_argument('--wine', default='wine64')
	parser.add_argument('--cc', default='x86_64-w64-mingw32-gcc')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	# Supply common framework symbols before testing the complete Win subsystem.
	subprocess.run([sys.executable, str(ROOT/'Win/Test/build_blackbox64.py'),
		'--out', str(out)], cwd=ROOT, check=True)
	subprocess.run([args.cc, '-c', str(ROOT/'Win/Test/check_subsystem_layout.c'),
		'-o', str(out/'subsystem-layout.o')], check=True)
	exe = out/'subsystem.exe'
	commands = f"""DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64')
DevCompiler64.CompileThis ConsFonts ConsWindows ConsLog SqlDrivers StdTabViews
DevCompiler64.CompileSubs @Win Win
DevCompiler64.CompileThis CheckWinSubsystem64
DevLinker1.LinkPeExe64 '{exe}' := Kernel+ WinKernel CheckWinSubsystem64$ ~
"""
	result = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		text=True, capture_output=True, timeout=180)
	log = result.stdout+result.stderr
	(out/'subsystem-build.log').write_text(log)
	print(log, end='', flush=True)
	assert result.returncode == 0 and not re.search(r'errors? detected|error:|not found|command error', log, re.I), log
	assert f'{exe} written' in log, log
	expected = {re.search(r'MODULE\s+(\w+)', p.read_text())[1] for folder in ('Mod', 'Mod64') for p in (ROOT/'Win'/folder).glob('*.cp')}
	compiled = set(re.findall(r'compiling \"(\w+)\"', log))
	assert expected <= compiled, ('Missing Win modules', sorted(expected-compiled))
	result = subprocess.run([args.wine, str(exe)], cwd=ROOT,
		env=dict(os.environ, WINEDEBUG='-all'), text=True, capture_output=True, timeout=60)
	log = result.stdout+result.stderr
	print(log, flush=True)
	assert result.returncode == 0, (result.returncode, log)
	assert 'Win subsystem x64 ABI and native API OK' in log
	print('All Win modules compiled for AMD64; ABI and API checks passed')


if __name__ == '__main__':
	main()
