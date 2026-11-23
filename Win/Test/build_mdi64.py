#!/usr/bin/env python3
"""Build BlackBox Windows AMD64 with MdiLoader/MdiInit and native MDI windows."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from build_blackbox64 import ROOT, RESOURCES


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/blackbox-mdi-win64')
	parser.add_argument('--test', action='store_true')
	parser.add_argument('--test-printing', action='store_true', help='test print dialogs and abort a job on the default virtual PDF printer')
	parser.add_argument('--wine', default='wine64')
	args = parser.parse_args()
	if args.test_printing: args.test = True
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	subprocess.run([sys.executable, str(ROOT/'Win/Test/build_blackbox64.py'), '--out', str(out)], cwd=ROOT, check=True)
	shutil.copytree(ROOT/'Mdi/Rsrc', out/'Mdi/Rsrc', dirs_exist_ok=True)
	modules = 'WinWindows WinMM WinCFrames MdiShowHook MdiMenus MdiConfig MdiInit MdiLoader'
	if args.test: modules += ' CheckWinCFrames64 CheckWinColor64 CheckMdiFramework64 CheckMdiAuto64'
	if args.test_printing: modules += ' CheckWinPrinting64'
	exe = out/'blackbox.exe'
	root = 'Services CheckMdiAuto64 ' if args.test else ''
	commands = f"""DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64')
DevCompiler64.CompileThis {modules}
DevLinker1.LinkPeExe64 '{exe}' := Kernel+ WinKernel Files WinFiles StdLoader {root}MdiLoader$ {RESOURCES} ~
"""
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT, text=True, capture_output=True, timeout=120)
	log = r.stdout+r.stderr
	print(log, end='', flush=True); (out/'mdi-build.log').write_text(log)
	assert r.returncode == 0 and not re.search(r'errors? detected|error:|not found|failed', log, re.I), log
	assert f'{exe} written' in log
	assert b'MdiInit\0' not in exe.read_bytes()
	for checker in ['check_pe_layout.py', 'check_unwind64.py']:
		subprocess.run([sys.executable, str(ROOT/'Dev/Test'/checker), str(exe)], check=True)
	if args.test:
		startup = out/'MDI startup Ж.odc'
		shutil.copyfile(ROOT/'System/Rsrc/About.odc', startup)
		argument = 'Z:'+str(startup).replace('/', '\\')
		r = subprocess.run([args.wine, str(exe), argument], cwd=ROOT, env=dict(os.environ, WINEDEBUG='-all', BB_PRINT_TEST='1' if args.test_printing else '0'), text=True, capture_output=True, timeout=45)
		log = r.stdout+r.stderr; (out/'mdi-test.log').write_text(log); print(log, flush=True)
		assert r.returncode == 0 and 'BlackBox MDI x64 OK' in log and 'TRAP REPORT' not in log, (r.returncode, log)
	if args.test_printing:
		data = (out/'printing-test.ps').read_bytes()
		assert data.startswith(b'%!PS-Adobe-3.0') and b'%%Page: 1 1' in data and b'showpage' in data
	print(f'MDI executable: {exe}')


if __name__ == '__main__':
	main()
