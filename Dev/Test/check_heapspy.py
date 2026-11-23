#!/usr/bin/env python3
"""Check Heap Spy layout and bottom scrolling on existing GTK4 x32/x64 builds."""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from Lin.Test.check_gtk4 import compile_commands


def run(arch, out):
	compiler = 'DevCompiler64' if arch == 'x64' else 'DevCompiler'
	folder = 'Mod64' if arch == 'x64' else 'Mod'
	preamble = f"DevLibrarian.EnableDevLib('{out}', '{folder}')\n"
	compile_commands(preamble + f'{compiler}.CompileThis DevHeapSpy CheckHeapSpy CheckHeapSpyAuto\n', out/'heapspy-build.log')
	if arch == 'x64':
		compile_commands(preamble + f"DevLinker1.LinkElfExe64 '{out}/heapspy-test' := Kernel+ LinKernel Files LinFiles StdLoader CheckHeapSpyAuto LinLoader\n", out/'heapspy-link.log')
	else:
		with tempfile.NamedTemporaryFile(prefix='heapspy32-', dir=ROOT, delete=False) as f:
			linked = Path(f.name)
		linked.unlink()
		try:
			compile_commands(preamble + f"DevLinker1.LinkElfExe Linux '{linked.name}' := Kernel$+ Utf LinKernel Files LinEnv LinFiles StdLoader Services CheckHeapSpyAuto LinLoader\n", out/'heapspy-link.log')
			shutil.move(linked, out/'heapspy-test')
		finally:
			linked.unlink(missing_ok=True)
	r = subprocess.run([str(out/'heapspy-test')], cwd=out, env=dict(os.environ, GTK_MODULES=''), text=True, capture_output=True, timeout=30)
	text = r.stdout+r.stderr
	(out/'heapspy-test.log').write_text(text)
	assert r.returncode == 0 and 'Heap Spy large heap layout and bottom scrolling OK' in text, (r.returncode, text)
	assert 'TRAP REPORT' not in text and 'CRITICAL' not in text, text
	print(f'{arch}: Heap Spy 128 MiB layout and bottom scrolling OK', flush=True)


def main():
	p = argparse.ArgumentParser(description=__doc__)
	p.add_argument('--arch', choices=['x32', 'x64', 'both'], default='both')
	p.add_argument('--out32', default='build/blackbox-gtk4-lin32')
	p.add_argument('--out64', default='build/blackbox-gtk4-lin64')
	a = p.parse_args()
	for arch, out in [('x32', a.out32), ('x64', a.out64)]:
		if a.arch in [arch, 'both']:
			run(arch, (ROOT/out).resolve())


if __name__ == '__main__':
	main()
