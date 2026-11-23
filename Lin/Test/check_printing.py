#!/usr/bin/env python3
"""Test Linux printing through PDF and GTK's virtual file printer; never print physically."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
SUBSYSTEMS = 'Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket'


def compile_commands(commands, log):
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT,
		text=True, capture_output=True, timeout=240)
	text = r.stdout+r.stderr; log.write_text(text)
	assert not r.returncode and not re.search(r'errors? detected|error:|not found|failed|TRAP REPORT', text, re.I), text
	return text


def run(arch, out, skip):
	out.mkdir(parents=True, exist_ok=True)
	compiler = 'DevCompiler64' if arch == 'x64' else 'DevCompiler'
	folder = 'Mod64' if arch == 'x64' else 'Mod'
	preamble = f"{compiler}.InitLogMarks\nDevLibrarian.EnableDevLib('{out}', '{folder}')\n"
	if not skip:
		compile_commands(preamble+f'{compiler}.CompileSubs @Lin {SUBSYSTEMS}\n', out/'printing-build.log')
	compile_commands(preamble+f'{compiler}.CompileThis LinGtk4Gdk LinGtk4Gtk LinGtk4IO LinGtk4Util LinCairo LinCairoPango LinGtk4Print LinGtk2GLib LinGtk2GObject LinGdkPixbuf LinPrintPorts LinPrinters LinRastersPng LinConfig CheckLinPrintApi CheckLinPrinting CheckLinPrintingAuto\n', out/'printing-tests-build.log')
	exe = out/'printing-test'
	if arch == 'x64':
		compile_commands(preamble+f"DevLinker1.LinkElfExe64 '{exe}' := Kernel+ LinKernel Files LinFiles StdLoader CheckLinPrintingAuto LinLoader\n", out/'printing-link.log')
	else:
		# The ELF32 writer registers a simple filename in the repository root.
		with tempfile.NamedTemporaryFile(prefix='printing32-', dir=ROOT, delete=False) as stream:
			linked = Path(stream.name)
		linked.unlink()
		try:
			compile_commands(preamble+f"DevLinker1.LinkElfExe Linux '{linked.name}' := Kernel$+ Utf LinKernel Files LinEnv LinFiles StdLoader Services CheckLinPrintingAuto LinLoader\n", out/'printing-link.log')
			shutil.move(linked, exe)
		finally: linked.unlink(missing_ok=True)
	for sub in SUBSYSTEMS.split():
		if (ROOT/sub/'Rsrc').is_dir():
			shutil.copytree(ROOT/sub/'Rsrc', out/sub/'Rsrc', dirs_exist_ok=True)
	data = out/'print-test-output'; data.mkdir(exist_ok=True)
	for name in ('vector.pdf', 'document.pdf', 'gtk-file.pdf', 'image.pdf', 'copies.pdf', 'menu.pdf'):
		(data/name).unlink(missing_ok=True)
	def chunk(kind, payload):
		return struct.pack('>I', len(payload))+kind+payload+struct.pack('>I', zlib.crc32(kind+payload))
	rows = b''.join(b'\0'+b'\xff\0\0'*8+b'\0\0\xff'*8 for _ in range(16))
	(data/'image.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>2I5B', 16, 16, 8, 2, 0, 0, 0))+chunk(b'IDAT', zlib.compress(rows))+chunk(b'IEND', b''))
	r = subprocess.run([str(exe)], cwd=out,
		env=dict(os.environ, BB_PRINT_DIR=str(data)), text=True, capture_output=True, timeout=90)
	text = r.stdout+r.stderr; (out/'printing-run.log').write_text(text)
	assert not r.returncode and 'Linux printing PDF, native dialogs, ranges and GTK file backend OK' in text and 'TRAP REPORT' not in text and 'CRITICAL' not in text, (r.returncode, text)
	# Prefer distribution Poppler, not a bundled executable requiring a newer glibc.
	pdfinfo = '/usr/bin/pdfinfo' if Path('/usr/bin/pdfinfo').exists() else 'pdfinfo'
	def info(name, *args):
		return subprocess.check_output([pdfinfo, *args, str(data/name)], text=True)
	assert re.search(r'Pages:\s+2\b', info('vector.pdf'))
	assert '841.89 x 595.276' in info('vector.pdf', '-f', '2', '-l', '2')
	assert re.search(r'Pages:\s+2\b', info('gtk-file.pdf'))
	def content(name):
		return subprocess.check_output(['pdftotext', str(data/name), '-'], text=True)
	assert 'Document pipeline — Печать Ж' in content('document.pdf')
	assert 'ABORTED' not in content('document.pdf')
	assert re.search(r'Pages:\s+2\b', info('copies.pdf'))
	assert not list(data.glob('*-??????')), 'temporary PDF leaked'
	images = subprocess.check_output(['/usr/bin/pdfimages', '-list', str(data/'image.pdf')], text=True)
	assert re.search(r'\b16\s+16\b', images), images
	text = content('gtk-file.pdf')
	assert 'Range page ONE' in text and 'Range page THREE' in text and 'Range page TWO' not in text
	assert 'Печать Linux' in text
	r = subprocess.run([str(exe)], cwd=out,
		env=dict(os.environ, BB_PRINT_DIR=str(data), BB_PRINT_MENU='1'), text=True, capture_output=True, timeout=90)
	text = r.stdout+r.stderr; (out/'printing-menu-run.log').write_text(text)
	assert not r.returncode and 'Linux printing PDF, native dialogs, ranges and GTK file backend OK' in text and 'TRAP REPORT' not in text and 'CRITICAL' not in text, (r.returncode, text)
	assert 'Document pipeline — Печать Ж' in content('menu.pdf')
	print(f'{arch}: PDF, Unicode, orientation, document printing, GTK dialogs and virtual printer ranges OK', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--arch', choices=('x32', 'x64', 'both'), default='both')
	parser.add_argument('--out32', default='build/blackbox-lin32')
	parser.add_argument('--out64', default='build/blackbox-lin64')
	parser.add_argument('--skip-build', action='store_true')
	args = parser.parse_args()
	for arch in ('x32', 'x64') if args.arch == 'both' else (args.arch,):
		out = (ROOT/(args.out32 if arch == 'x32' else args.out64)).resolve()
		if any(c in str(out) for c in "'\"\r\n"):
			parser.error('test output directory must not contain quotes or newlines')
		run(arch, out, args.skip_build)


if __name__ == '__main__':
	main()
