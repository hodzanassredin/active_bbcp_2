#!/usr/bin/env python3
"""Check REAL formatting and IEEE 754 exponents on Windows AMD64."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/realformat-win64'
OUT.mkdir(parents=True, exist_ok=True)
commands = f'''DevLibrarian.EnableDevLib('{OUT}', 'Mod64');
DevCompiler64.CompileThis Kernel WinApi WinOle WinNtdll WinTrace WinKernel Math Strings CheckRealFormat64 CheckWinRealFormat64
DevLinker1.LinkPeExe64 '{OUT}/test.exe' := Kernel+ WinKernel Math Strings CheckRealFormat64 CheckWinRealFormat64$ ~
'''
result = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
	text=True, capture_output=True, timeout=120)
log = result.stdout + result.stderr
print(log, end='', flush=True)
assert result.returncode == 0 and f'{OUT}/test.exe written' in log
result = subprocess.run(['wine64', str(OUT/'test.exe')], cwd=ROOT,
	env=dict(os.environ, WINEDEBUG='-all'), text=True, capture_output=True, timeout=30)
log = result.stdout + result.stderr
print(log, end='', flush=True)
assert result.returncode == 0 and 'REAL formatting OK' in log and 'TRAP REPORT' not in log
