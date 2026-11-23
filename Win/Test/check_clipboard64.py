#!/usr/bin/env python3
"""Exchange clipboard data with a separate native Windows process under Wine."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import re
import select

ROOT = Path(__file__).resolve().parents[2]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/clipboard64')
	parser.add_argument('--wine', default='wine64')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "\'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	subprocess.run([sys.executable, str(ROOT/'Win/Test/build_blackbox64.py'), '--out', str(out)], check=True)
	commands = f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64');
DevCompiler64.CompileThis CheckWinClipboardConfig64 CheckWinClipboard64
DevLinker1.LinkPeExe64 '{out}/clipboard.exe' := Kernel+ WinKernel Files WinFiles StdLoader CheckWinClipboardConfig64 WinLoader$ ~
'''
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT, text=True, capture_output=True, timeout=120)
	log = r.stdout+r.stderr
	print(log, end='', flush=True)
	assert not r.returncode and not re.search(r'error:|errors? detected|not found|failed', log, re.I), log
	subprocess.run(['x86_64-w64-mingw32-gcc', '-Wall', '-Wextra', '-O2', str(ROOT/'Win/Test/clipboard64_native.c'), '-o', str(out/'native.exe'), '-lgdi32', '-luser32'], check=True)
	# Wine discards custom clipboard formats when its last USER32 client exits.
	# Keep a neutral client alive across the three independent participants.
	keeper = subprocess.Popen([args.wine, str(out/'native.exe'), 'keep', 'all'], cwd=out,
		env=dict(os.environ, WINEDEBUG='-all'), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
	try:
		assert select.select([keeper.stdout], [], [], 45)[0], 'clipboard keeper did not start'
		assert keeper.stdout.readline().strip() == 'Clipboard native keeper ready'
		for mode in ('unicode', 'ansi', 'rtf', 'native', 'native-only', 'picture'):
			env = dict(os.environ, WINEDEBUG='-all', BB_CLIP_MODE=mode)
			for exe, arguments in [('native.exe', ['set', mode]), ('clipboard.exe', []), ('native.exe', ['get', mode])]:
				r = subprocess.run([args.wine, str(out/exe), *arguments], cwd=out, env=env, capture_output=True, text=True, timeout=90)
				print(r.stdout+r.stderr, end='', flush=True)
				assert r.returncode == 0, (mode, exe, r.returncode)
			print(f'{mode}: external clipboard exchange passed', flush=True)
	finally:
		keeper.terminate()
		keeper.wait(timeout=10)
	print('Windows x64 clipboard: all checks passed')


if __name__ == '__main__':
	main()
