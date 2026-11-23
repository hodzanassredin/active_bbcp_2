#!/usr/bin/env python3
"""Drop URI lists from a separate native GTK2/X11 application into BlackBox."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_linux64 import ROOT, compile_commands


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/blackbox-lin64')
	parser.add_argument('--skip-build', action='store_true')
	args = parser.parse_args()
	parser.error("Automatic mouse drag testing is disabled after a system crash; use the manual procedure in Lin/Docu/Internal-drag-and-drop.md")
	out = Path(args.out).resolve()
	if not args.skip_build:
		subprocess.run([sys.executable,str(ROOT/'Lin/Test/check_linux64.py'),'--out',str(out),'--gui'],check=True)
	flags = subprocess.check_output(['pkg-config','--cflags','--libs','gtk+-2.0','xtst','x11'],text=True).split()
	peer = out/'file-drop-peer'
	subprocess.run(['cc','-O2','-Wall','-Wextra','-Wno-deprecated-declarations',str(ROOT/'Lin/Test/file_drop_peer.c'),'-o',str(peer),*flags],check=True)
	compile_commands(f'''DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64');
DevCompiler64.CompileThis CheckLinDragAuto64 CheckLinDrag64
DevLibrarian.DisableDevLib
''',out/'linux64-drop-build.log')
	with tempfile.TemporaryDirectory(prefix='file-drop-',dir=out) as directory:
		base = Path(directory)
		one,two = base/'one space.utf8',base/'Два # 100%.utf8'
		one.write_text('Drop one',encoding='utf-8');two.write_text('Drop two Ж',encoding='utf-8')
		valid = '# URI list comment\r\n'+one.as_uri()+'\r\n'+two.as_uri()+'\r\n'
		invalid = 'https://example.invalid/file\r\nfile://remote.invalid'+one.as_posix()+'\r\n'+base.as_uri()+'\r\n'+(base/'missing.utf8').as_uri()+'\r\nfile:///bad%ZZ\r\n'
		(base/'payload0').write_text(valid,encoding='utf-8')
		(base/'payload1').write_text(invalid,encoding='utf-8')
		(base/'payload2').write_text(valid,encoding='utf-8')
		fd,path = tempfile.mkstemp(prefix='__file_drop_test64_',suffix='.ocf',dir=out/'System/Code');os.close(fd)
		try:
			shutil.copyfile(out/'Check/Code/LinDragAuto64.ocf',path)
			with (out/'linux64-drop.log').open('w') as stream:
				r = subprocess.run([str(out/'blackbox')],cwd=out,
					env=dict(os.environ,BB_DROP_BASE=str(base),BB_DROP_PEER=str(peer)),stdout=stream,stderr=subprocess.STDOUT,text=True,timeout=90)
			log = (out/'linux64-drop.log').read_text()
			assert not r.returncode and 'Linux URI file drag/drop OK' in log and 'TRAP REPORT' not in log,log[-5000:]
			assert one.read_text(encoding='utf-8')=='Drop one' and two.read_text(encoding='utf-8')=='Drop two Ж'
		finally:Path(path).unlink(missing_ok=True)
	print('Linux URI file drag/drop: native cross-process checks passed')


if __name__ == '__main__':
	main()
