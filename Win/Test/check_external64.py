#!/usr/bin/env python3
"""Check Dialog.OpenExternal/RunExternal with private Windows file and URI associations."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/external64')
	parser.add_argument('--wine', default='wine64')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	subprocess.run([sys.executable, str(ROOT/'Win/Test/build_blackbox64.py'), '--out', str(out)], check=True)
	helper = out/'external peer.exe'
	subprocess.run(['x86_64-w64-mingw32-gcc', '-municode', '-Wall', '-Wextra', '-O2', str(ROOT/'Win/Test/external64_native.c'), '-o', str(helper), '-ladvapi32', '-lshell32', '-luser32'], check=True)
	commands = f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64');
DevCompiler64.CompileThis CheckWinExternalConfig64 CheckWinExternal64
DevLinker1.LinkPeExe64 '{out}/external.exe' := Kernel+ WinKernel Files WinFiles StdLoader CheckWinExternalConfig64 WinLoader$ ~
'''
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT, capture_output=True, text=True, timeout=120)
	log = r.stdout+r.stderr
	print(log, end='', flush=True)
	assert not r.returncode and not re.search(r'error:|errors? detected|not found|failed', log, re.I), log
	windows_path = lambda p: 'Z:'+str(p).replace('/', chr(92))
	scheme = f'bbext64test{os.getpid()}'
	document = out/f'External document Ж.{scheme}'
	document.write_text('Private external-call fixture', encoding='utf-8')
	env = dict(os.environ, WINEDEBUG=os.environ.get('BB_EXT_WINEDEBUG', '-all'), BB_EXT_SCHEME=scheme, BB_EXT_OUTPUT=windows_path(out),
		BB_EXT_HELPER=windows_path(helper), BB_EXT_FILE=windows_path(document))
	for name in ('file', 'uri', 'run', 'hidden'):
		(out/f'{name}.marker').unlink(missing_ok=True)
	try:
		# Wine 6 does not merge HKCU\Software\Classes into HKCR.
		# Use unique, temporary HKCR keys and remove them in finally.
		subprocess.run([args.wine, str(helper), '--setup'], cwd=out, env=env, check=True, timeout=60)
		r = subprocess.run([args.wine, str(out/'external.exe')], cwd=out, env=env, capture_output=True, text=True, timeout=90)
		log = r.stdout+r.stderr
		print(log, end='', flush=True)
		assert r.returncode == 0 and 'WinExternal x64 OK' in log and 'TRAP REPORT' not in log, r.returncode
		expected = {'file': windows_path(document), 'uri': scheme+'://example/path?value=42',
			'run': 'quoted value Ж', 'hidden': 'hidden'}
		for name, text in expected.items():
			path = out/f'{name}.marker'
			deadline = time.monotonic()+5
			while not path.exists() and time.monotonic()<deadline:
				time.sleep(.05)
			assert path.read_text(encoding='utf-8') == text, name
	finally:
		subprocess.run([args.wine, str(helper), '--cleanup'], cwd=out, env=env, check=True, timeout=60)
	print('\nWindows x64 external calls: all checks passed')


if __name__ == '__main__':
	main()
