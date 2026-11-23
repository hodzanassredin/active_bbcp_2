#!/usr/bin/env python3
"""Run the shared Math suite on Windows/Wine and an existing Linux x64 console tree."""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def compile_modules(directory, modules, tail=''):
	commands = f"DevLibrarian.EnableDevLib('{directory}', 'Mod64');\nDevCompiler64.CompileThis {modules}\n{tail}"
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		capture_output=True, text=True, timeout=120)
	log = r.stdout+r.stderr
	(directory/'math-build.log').write_text(log)
	assert not r.returncode and not re.search(r'errors? detected|command error|not found|failed', log, re.I), log
	return log


def main():
	def exports(path):
		source = path.read_text().split('END Math.')[0]
		return {m[0]: re.sub(r'\s+', '', m[1]) for m in
			re.findall(r'PROCEDURE (\w+)\*([^;]*);', source)}
	baseline = exports(ROOT/'System/Mod/Math.cp')
	assert baseline == exports(ROOT/'System/Mod64/Math.cp'), 'Math interfaces differ'
	fixture = (ROOT/'Check/Mod/Math64.cp').read_text()
	assert all('Math.'+name+'(' in fixture for name in baseline), 'An exported procedure has no test'
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--windows-dir', default='build/math64-windows')
	parser.add_argument('--linux-dir', default='build/blackbox-lin64')
	parser.add_argument('--platform', choices=('both','windows','linux'), default='both')
	args = parser.parse_args()
	for directory in (args.windows_dir, args.linux_dir):
		if any(c in directory for c in "'\"\r\n"):
			parser.error('directories cannot contain quotes or newlines')
	if args.platform in ('both','windows'):
		out = (ROOT/args.windows_dir).resolve(); out.mkdir(parents=True, exist_ok=True)
		exe = out/'test.exe'
		log = compile_modules(out,
			'Kernel WinApi WinOle WinNtdll WinTrace WinKernel Math Strings CheckRealFormat64 CheckMath64 CheckWinMath64',
			f"DevLinker1.LinkPeExe64 '{exe}' := Kernel+ WinKernel Math Strings CheckRealFormat64 CheckMath64 CheckWinMath64$ ~\n")
		assert f'{exe} written' in log, log
		r = subprocess.run(['wine64', str(exe)], cwd=ROOT,
			env=dict(os.environ,WINEDEBUG='-all'), capture_output=True, text=True, timeout=45)
		log = r.stdout+r.stderr; (out/'math-test.log').write_text(log)
		assert not r.returncode and 'Math x64 OK' in log and 'TRAP REPORT' not in log, log
		print('Windows: '+log, flush=True)
	if args.platform in ('both','linux'):
		out = (ROOT/args.linux_dir).resolve()
		assert (out/'blackbox-console').exists(), 'Build the Linux x64 console tree first'
		compile_modules(out,'Math Strings CheckRealFormat64 CheckMath64 CheckLinMath64')
		r = subprocess.run([str(out/'blackbox-console')], input='CheckLinMath64.Run\n', cwd=out,
			capture_output=True,text=True,timeout=60)
		log = r.stdout+r.stderr; (out/'math-test.log').write_text(log)
		assert not r.returncode and 'Math x64 OK' in log and 'TRAP REPORT' not in log, log
		print('Linux: '+next(line for line in log.splitlines() if 'Math x64 OK' in line), flush=True)


if __name__ == '__main__':
	main()
