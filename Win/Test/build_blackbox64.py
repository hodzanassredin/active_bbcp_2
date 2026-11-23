#!/usr/bin/env python3
"""Build the dynamically loaded BlackBox Windows AMD64 workspace with native drag and drop."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
# DevLibrarian selects Mod64 overrides before shared Mod sources.
MODULES = '''Kernel WinApi WinOle WinNtdll WinTrace WinKernel Files WinFiles Files64 WinFiles64
StdLoader WinLoader Services Log Math Strings Fonts
Librarian Dialog Meta Utf Stores Converters Loop
Console WinConsole WinGui Ports Sequencers Models
Views Controllers Properties Mechanisms Containers Dates Printers
Printing Documents Windows StdScrollbars StdDocuments StdWindows StdInterpreter
StdRegistry WinRegistry StdCFrames WinFonts Controls TextModels TextRulers
TextSetters TextViews StdDialog StdApi StdCmds StdRasters StdStdCFrames
WinDates WinPorts WinClipboard WinMechanisms TextMappers TextControllers StdLinks StdMenuTool
StdLog WinBackends WinCmds WinEnv WinDlg WinPrinters WinDialog
StdMenus WinShowHook WinInit StdInflate StdRastersPng WinRasters WinConfig
StdGrids StdTiles StdConfig StdFolds StdDebug StdScrollers StdFilesBrowser
StdColorDialog StdTextConv TextCmds FormModels FormViews FormControllers FormCmds WinTextConv WinPictures'''.split()

RESOURCES = '1 BlackBox.res 1 Applogo.ico 2 Doclogo.ico 3 SFLogo.ico 4 CFLogo.ico 5 DtyLogo.ico 6 folderimg.ico 7 openimg.ico 8 leafimg.ico 1 Move.cur 2 Copy.cur 3 Link.cur 4 Pick.cur 5 Stop.cur 6 Hand.cur 7 Table.cur'


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/blackbox-win64')
	parser.add_argument('--wine', default='wine64')
	parser.add_argument('--trace', action='store_true', help='write blackbox-startup.log without requiring the CP heap')
	parser.add_argument('--native-controls', action='store_true', help='exercise WinCFrames instead of StdStdCFrames with --test')
	mode = parser.add_mutually_exclusive_group()
	mode.add_argument('--test', action='store_true', help='run the GUI integration test under Wine')
	mode.add_argument('--run', action='store_true', help='open BlackBox under Wine after building')
	args = parser.parse_args()
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	out.mkdir(parents=True, exist_ok=True)
	def init_body(path):
		source = path.read_text()
		body = re.search(r'\bPROCEDURE Init;(.+?)END Init;', source, re.S)[1]
		body = re.sub(r'\(\*.*?\*\)', '', body, flags=re.S)
		return re.sub(r'\s+', '', body)
	assert init_body(ROOT/'Win/Mod/Init.cp') == init_body(ROOT/'Win/Mod64/Init.cp'), 'WinInit.Init differs from i386' 
	modules = MODULES + (['CheckWinStartup64'] if args.trace else []) + ['WinCtl', 'WinCFrames', 'CheckWinCFramesLayout64'] + (['CheckWinCFrames64', 'CheckWinGuiConfig64', 'CheckWinFramework64', 'CheckWinFrameworkAuto64'] if args.test else [])
	# Compile and link separately: dev0Linux can return zero after a compiler error.
	commands = f"DevCompiler64.InitLogMarks\nDevLibrarian.EnableDevLib('{out}', 'Mod64');\n"
	compile_command = commands + 'DevCompiler64.CompileThis ' + ' '.join(modules) + '\n'
	def invoke(command):
		result = subprocess.run([str(ROOT/'dev0Linux')], input=command, cwd=ROOT,
			text=True, capture_output=True, timeout=120)
		log = result.stdout+result.stderr
		print(log, end='', flush=True)
		if result.returncode or re.search(r'error:|errors? detected|command error|not found|failed', log, re.I):
			raise RuntimeError('BlackBox AMD64 build failed')
		return log
	invoke(compile_command)
	# Remove obsolete generated bindings after every successful rebuild.
	for obsolete in ('Win/Code/GuiApi.ocf', 'Win/Sym/GuiApi.osf'):
		(out/obsolete).unlink(missing_ok=True)
	for subsystem in ('System', 'Std', 'Text', 'Form', 'Win'):
		shutil.copytree(ROOT/subsystem/'Rsrc', out/subsystem/'Rsrc', dirs_exist_ok=True)
	exe = out/'blackbox.exe'
	test_root = 'CheckWinGuiConfig64 ' if args.test else ''
	trace_root = 'CheckWinStartup64 ' if args.trace else ''
	log = invoke(commands + f"DevLinker1.LinkPeExe64 '{exe}' := Kernel+ {trace_root}WinKernel Files WinFiles StdLoader {test_root}WinLoader$ {RESOURCES} ~\n")
	assert f'{exe} written' in log
	# GUI modules must be loaded from OCF rather than hidden in the bootstrap PE.
	assert b'WinInit\0' not in exe.read_bytes() and b'WinBackends\0' not in exe.read_bytes()
	executables = [exe]
	if args.test:
		default_exe = out/'blackbox-default.exe'
		log = invoke(commands + f"DevLinker1.LinkPeExe64 '{default_exe}' := Kernel+ {trace_root}WinKernel Files WinFiles StdLoader Services CheckWinFrameworkAuto64 WinLoader$ {RESOURCES} ~\n")
		assert f'{default_exe} written' in log
		executables.insert(0, default_exe)
	for executable in executables:
		subprocess.run([sys.executable, str(ROOT/'Dev/Test/check_pe_layout.py'), str(executable)], check=True)
		subprocess.run([sys.executable, str(ROOT/'Dev/Test/check_unwind64.py'), str(executable)], check=True)
	if args.test or args.run:
		env = dict(os.environ, WINEDEBUG='-all', BB_NATIVE_CONTROLS='1' if args.native_controls else '0')
		env.pop('BLACKBOX_GUI_TEST', None)
		for executable in executables:
			arguments = ['System/Rsrc/About.odc'] if args.test and executable.name == 'blackbox-default.exe' else []
			result = subprocess.run([args.wine, str(executable), *arguments], cwd=ROOT, env=env,
				text=True, capture_output=args.test, timeout=60 if args.test else None)
			if args.test:
				log = result.stdout+result.stderr
				print(log, end='', flush=True)
				assert result.returncode == 0, (executable, result.returncode)
				assert 'BlackBox x64 framework OK' in log and 'TRAP REPORT' not in log
				print(f'\n{executable.name}: all framework checks passed')
			elif result.returncode:
				raise SystemExit(result.returncode)
	print(f'Executable: {exe}')


if __name__ == '__main__':
	main()
