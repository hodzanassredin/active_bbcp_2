#!/usr/bin/env python3
"""Build Linux x64 with CompileSubs @Lin and check the shared high-address GC."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SUBSYSTEMS = 'Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket'


def compile_commands(commands, log_path):
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		capture_output=True, text=True, timeout=180)
	log = r.stdout+r.stderr
	log_path.write_text(log)
	assert not r.returncode and not re.search(
		r'error:|errors? detected|file .* not found|command error|incompatible assignment', log, re.I), log


def run_test(out, arguments, command, marker, name, large=False):
	r = subprocess.run([str(out/'blackbox-console'), *arguments], cwd=out,
		input=command, env=dict(os.environ, BB_HEAP_LARGE='1' if large else '0'),
		capture_output=True, text=True, timeout=300 if large else 90)
	log = r.stdout+r.stderr
	(out/name).write_text(log)
	assert not r.returncode and marker in log and not any(
		s in log for s in ('TRAP REPORT', 'FAILED', 'FATAL:')), (r.returncode, log)
	print(marker, flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/blackbox-lin64')
	parser.add_argument('--skip-build', action='store_true', help='reuse the compiled subsystem tree')
	parser.add_argument('--large', action='store_true', help='also allocate and touch a 4.25 GiB live heap')
	parser.add_argument('--gui', action='store_true', help='check the ordinary GUI; requires a display')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	directory = str(out.relative_to(ROOT)) if out.is_relative_to(ROOT) else str(out)
	if not args.skip_build:
		compile_commands(f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{directory}', 'Mod64');
DevCompiler64.CompileSubs @Lin {SUBSYSTEMS}
DevLinker1.LinkElfExe64 '{out}/blackbox' := Kernel+ LinKernel Files LinFiles StdLoader LinLoader$ ~
DevLibrarian.DisableDevLib
''', out/'linux64-build.log')
		for name in SUBSYSTEMS.split():
			if (ROOT/name/'Rsrc').is_dir():
				shutil.copytree(ROOT/name/'Rsrc', out/name/'Rsrc', dirs_exist_ok=True)
		shutil.copytree(ROOT/'Docu', out/'Docu', dirs_exist_ok=True)

	compile_commands(f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{directory}', 'Mod64');
DevCompiler64.CompileThis CheckLinHeap64 CheckLinGuiAuto64 CheckLinGui64
DevLinker1.LinkElfExe64 '{out}/blackbox-console' := Kernel+ LinKernel Files LinFiles StdLoader LinIntInit$ ~
DevLibrarian.DisableDevLib
''', out/'linux64-tests-build.log')
	run_test(out, [], 'CheckLinHeap64.Run\n', 'LinHeap x64 OK', 'linux64-heap.log')
	if args.large:
		run_test(out, [], 'CheckLinHeap64.Run\n',
			'4.25 GiB live heap, collection and reuse OK', 'linux64-heap-large.log', large=True)
	if args.gui:
		# Link the scheduling fixture explicitly; normal bootstrap stays unchanged.
		compile_commands(f"""DevLibrarian.EnableDevLib('{directory}', 'Mod64');
DevLinker1.LinkElfExe64 '{out}/blackbox-gui-test' := Kernel+ LinKernel Files LinFiles StdLoader CheckLinGuiAuto64 LinLoader$ ~
""", out/'linux64-gui-link.log')
		r = subprocess.run([str(out/'blackbox-gui-test')], cwd=out,
			capture_output=True, text=True, timeout=90)
		log = r.stdout+r.stderr; (out/'linux64-gui.log').write_text(log)
		assert not r.returncode and 'Linux GUI x64 GC, clipboard, undo/redo and Find OK' in log and 'TRAP REPORT' not in log and 'CRITICAL' not in log, log
		print('Linux GUI x64 GC, clipboard, undo/redo and Find OK', flush=True)

	print(f'Linux x64 checks passed: {out/"blackbox"}')


if __name__ == '__main__':
	main()
