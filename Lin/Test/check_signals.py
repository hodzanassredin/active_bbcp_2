#!/usr/bin/env python3
"""Check Linux signal stacks, nested traps and local GC roots on i386 and AMD64."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SUBSYSTEMS = 'Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket'


def compile_commands(commands, log):
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		text=True, capture_output=True, timeout=240)
	text = r.stdout+r.stderr
	log.write_text(text)
	assert not r.returncode and not re.search(r'errors? detected|error:|not found|failed|TRAP REPORT|:= missing', text, re.I), text


def run(arch, out, skip):
	out.mkdir(parents=True, exist_ok=True)
	compiler = 'DevCompiler64' if arch == 'x64' else 'DevCompiler'
	folder = 'Mod64' if arch == 'x64' else 'Mod'
	preamble = f"{compiler}.InitLogMarks\nDevLibrarian.EnableDevLib('{out}', '{folder}')\n"
	if not skip:
		compile_commands(preamble+f'{compiler}.CompileSubs @Lin {SUBSYSTEMS}\n', out/'signals-subsystems.log')
	compile_commands(preamble+f'{compiler}.CompileThis LinKernel CheckLinSignalApi CheckLinSignals\n', out/'signals-build.log')
	if arch == 'x64':
		compile_commands(preamble+f"DevLinker1.LinkElfExe64 '{out}/signals-console' := Kernel+ LinKernel Files LinFiles StdLoader LinIntInit\n", out/'signals-link.log')
	else:
		with tempfile.NamedTemporaryFile(prefix='signals32-', dir=ROOT, delete=False) as f:
			linked = Path(f.name)
		linked.unlink()
		try:
			compile_commands(preamble+f"DevLinker1.LinkElfExe Linux '{linked.name}' := Kernel$+ Utf LinKernel Files LinEnv LinFiles StdLoader Services LinIntInit\n", out/'signals-link.log')
			shutil.move(linked, out/'signals-console')
		finally:
			linked.unlink(missing_ok=True)
	(out/'signals-console').chmod(0o755)
	for sub in SUBSYSTEMS.split():
		if (ROOT/sub/'Rsrc').is_dir():
			shutil.copytree(ROOT/sub/'Rsrc', out/sub/'Rsrc', dirs_exist_ok=True)
	r = subprocess.run([str(out/'signals-console')], input='CheckLinSignals.Run\n',
		cwd=out, text=True, capture_output=True, timeout=60)
	text = r.stdout+r.stderr
	(out/'signals-run.log').write_text(text)
	assert not r.returncode and 'Linux signals, nested traps and local GC roots OK' in text, (r.returncode, text)
	assert 'Recursive trap at the same PC' not in text and '~TRAP' not in text and 'REPEAT' not in text, text
	print(f'{arch}: signal stack, SIGURG, SIGBUS, invalid reads, nested traps and GC OK', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--arch', choices=['x32', 'x64', 'both'], default='both')
	parser.add_argument('--out32', default='build/blackbox-lin32')
	parser.add_argument('--out64', default='build/blackbox-lin64')
	parser.add_argument('--skip-build', action='store_true', help='reuse compiled subsystem dependencies')
	args = parser.parse_args()
	for arch, directory in [('x32', args.out32), ('x64', args.out64)]:
		if args.arch in [arch, 'both']:
			out = Path(directory).resolve()
			if any(c in str(out) for c in "'\"\r\n"):
				parser.error('output directory cannot contain quotes or newlines')
			run(arch, out, args.skip_build)


if __name__ == '__main__':
	main()
