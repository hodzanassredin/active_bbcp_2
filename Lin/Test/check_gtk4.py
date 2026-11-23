#!/usr/bin/env python3
"""Build and check GTK4 GUI/Cairo on Linux i386 and AMD64, without mouse automation."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

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
		compile_commands(preamble+f'{compiler}.CompileSubs @Lin {SUBSYSTEMS}\n', out/'gtk4-subsystems.log')
	compile_commands(preamble+f'{compiler}.CompileThis CheckLinGtk4 CheckLinGtk4Auto\n', out/'gtk4-tests-build.log')
	for test in [False, True]:
		name = 'gtk4-gui-test' if test else 'blackbox'
		fixture = 'CheckLinGtk4Auto ' if test else ''
		if arch == 'x64':
			compile_commands(preamble+f"DevLinker1.LinkElfExe64 '{out/name}' := Kernel+ LinKernel Files LinFiles StdLoader {fixture}LinLoader\n", out/f'{name}-link.log')
		else:
			with tempfile.NamedTemporaryFile(prefix='gtk4-32-', dir=ROOT, delete=False) as f:
				linked = Path(f.name)
			linked.unlink()
			try:
				compile_commands(preamble+f"DevLinker1.LinkElfExe Linux '{linked.name}' := Kernel$+ Utf LinKernel Files LinEnv LinFiles StdLoader Services {fixture}LinLoader\n", out/f'{name}-link.log')
				shutil.move(linked, out/name)
			finally:
				linked.unlink(missing_ok=True)
		(out/name).chmod(0o755)
	for sub in SUBSYSTEMS.split():
		if (ROOT/sub/'Rsrc').is_dir():
			shutil.copytree(ROOT/sub/'Rsrc', out/sub/'Rsrc', dirs_exist_ok=True)
	shutil.copytree(ROOT/'Docu', out/'Docu', dirs_exist_ok=True)
	subprocess.run(['xclip', '-selection', 'clipboard'], input='External GTK4 — Ж'.encode(), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	libraries = set()
	with (out/'gtk4-gui.log').open('w') as log:
		# Do not load desktop-specific optional modules in architecture-isolated tests.
		p = subprocess.Popen([str(out/'gtk4-gui-test')], cwd=out,
			env=dict(os.environ, GTK_MODULES=''), stdout=log, stderr=subprocess.STDOUT)
		try:
			deadline = time.monotonic()+90
			while p.poll() is None and time.monotonic() < deadline:
				try:
					libraries.update(line.split()[-1] for line in Path(f'/proc/{p.pid}/maps').read_text().splitlines() if '.so' in line)
				except FileNotFoundError:
					pass
				time.sleep(.02)
			code = p.wait(timeout=1)
			assert code == 0, (code, (out/'gtk4-gui.log').read_text())
		finally:
			if p.poll() is None:
				p.kill(); p.wait()
	text = (out/'gtk4-gui.log').read_text()
	assert 'Linux GTK4 GUI GC, clipboard, undo/redo and Find OK' in text, text
	assert 'TRAP REPORT' not in text and 'CRITICAL' not in text, text
	maps = '\n'.join(sorted(libraries))
	assert '/libgtk-4.so.' in maps, 'GTK4 was not loaded'
	assert all(name not in maps for name in ['libgtk-x11-2.0','libgdk-x11-2.0','libgtk-3.so','libgdk-3.so']), 'old GTK loaded into GTK4 process'
	(out/'gtk4-loaded-libraries.log').write_text(maps)
	print(f'{arch}: GTK4 only, window grow/shrink, Cairo pixels, inversion, restore/scroll, GUI, GC, clipboard, undo/redo and visible Find selection pixels OK', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--arch', choices=['x32', 'x64', 'both'], default='both')
	parser.add_argument('--out32', default='build/blackbox-gtk4-lin32')
	parser.add_argument('--out64', default='build/blackbox-gtk4-lin64')
	parser.add_argument('--skip-build', action='store_true')
	args = parser.parse_args()
	for arch, directory in [('x32', args.out32), ('x64', args.out64)]:
		if args.arch in [arch, 'both']:
			out = Path(directory).resolve()
			if any(c in str(out) for c in "'\"\r\n"):
				parser.error('output directory cannot contain quotes or newlines')
			run(arch, out, args.skip_build)


if __name__ == '__main__':
	main()
