#!/usr/bin/env python3
"""Build PE32 MDI and test native/framework color dialogs without mouse input."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/blackbox-win32')
	parser.add_argument('--wine', default='wine')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	exe = out/'color-check.exe'
	commands = f'''DevCompiler.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod')
DevCompiler.CompileSubs @Win Win System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Mdi
DevCompiler.CompileThis OleServer OleClient CheckWinColor32 CheckMdiColorAuto32
'''
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		text=True, capture_output=True, timeout=180)
	log = r.stdout+r.stderr; (out/'color32-build.log').write_text(log)
	assert not r.returncode and not re.search(r'errors? detected|error:|not found|TRAP REPORT', log, re.I), log
	commands = f'''DevLibrarian.EnableDevLib('{out}', 'Mod')
DevLinker1.LinkPe '{exe}' := Kernel$+ Utf WinKernel Files WinEnv WinFiles WinGui StdLoader Services CheckMdiColorAuto32 MdiLoader ~
'''
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		text=True, capture_output=True, timeout=60)
	log = r.stdout+r.stderr; (out/'color32-link.log').write_text(log)
	assert not r.returncode and f'{exe} written' in log and 'TRAP REPORT' not in log, log
	for sub in ('System', 'Std', 'Text', 'Form', 'Win', 'Mdi', 'Dev', 'Ole'):
		if (ROOT/sub/'Rsrc').is_dir():
			shutil.copytree(ROOT/sub/'Rsrc', out/sub/'Rsrc', dirs_exist_ok=True)
	r = subprocess.run([args.wine, str(exe)], cwd=out,
		env=dict(os.environ, WINEDEBUG='-all'), text=True, capture_output=True, timeout=60)
	log = r.stdout+r.stderr; (out/'color32-run.log').write_text(log)
	assert not r.returncode and 'Windows x32 native and framework color dialogs OK' in log and 'TRAP REPORT' not in log, (r.returncode, log)
	print('Windows x32 MDI: native OK/Cancel/default color and framework dialog passed')


if __name__ == '__main__':
	main()
