#!/usr/bin/env python3
"""Check WinApi export coverage, SDK layouts and representative native x64 calls."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def without_comments(source):
	out = []; i = 0; depth = 0
	while i < len(source):
		if source.startswith('(*', i): depth += 1; i += 2
		elif depth and source.startswith('*)', i): depth -= 1; i += 2
		elif depth: i += 1
		elif source[i] in "\"'":
			end = source.index(source[i], i+1)+1; out.append(source[i:end]); i = end
		else: out.append(source[i]); i += 1
	assert depth == 0
	return ''.join(out)


def functions(path):
	source = without_comments(path.read_text())
	module = re.search(r'MODULE\s+\w+\s*\["([^"]+)"\]', source)[1]
	result = {}
	for m in re.finditer(r'\bPROCEDURE\s*(\[[^]]*\])?\s*(\w+)\*\s*(\[[^]]*\])?', source):
		flags, name, alias = m.groups(); library = module; entry = name
		if alias:
			parts = re.findall(r'"([^"]*)"', alias)
			if len(parts) == 2: library, entry = parts; entry = entry or name
			elif parts: entry = parts[0]
		assert name not in result, name
		result[name] = (library.lower(), entry, flags)
	return result


def coverage():
	old = functions(ROOT/'Win/Mod/Api.cp'); new = functions(ROOT/'Win/Mod64/Api.cp')
	missing = old.keys()-new.keys(); assert not missing, sorted(missing)
	for name, (library, entry, _) in old.items():
		assert new[name][:2] == (library, entry), (name, old[name], new[name])
	assert all(flags == '[ms64]' for _, _, flags in new.values())
	print(f'WinApi coverage: {len(old)} legacy functions, {len(new)} native functions; none missing', flush=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--out', default='build/win-api64')
	parser.add_argument('--coverage-only', action='store_true')
	parser.add_argument('--wine', default='wine64')
	args = parser.parse_args(); coverage()
	if args.coverage_only: return
	out = Path(args.out).resolve()
	if any(c in str(out) for c in "'\"\r\n"):
		parser.error('output directory cannot contain quotes or newlines')
	subprocess.run([sys.executable, str(ROOT/'Win/Test/build_blackbox64.py'), '--out', str(out)], cwd=ROOT, check=True)
	subprocess.run(['x86_64-w64-mingw32-gcc', '-c', str(ROOT/'Win/Test/check_api64_layout.c'), '-o', str(out/'api-layout.o')], check=True)
	exe = out/'check-api.exe'
	commands = f"""DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('{out}', 'Mod64')
DevCompiler64.CompileThis CheckWinApi64
DevLinker1.LinkPeExe64 '{exe}' := Kernel+ WinKernel CheckWinApi64$ ~
"""
	r = subprocess.run([str(ROOT/'dev0Linux')], input=commands, cwd=ROOT, text=True, capture_output=True, timeout=120)
	log = r.stdout+r.stderr; (out/'api-build.log').write_text(log)
	assert r.returncode == 0 and not re.search(r'errors? detected|error:|not found|failed', log, re.I), log
	assert f'{exe} written' in log, log
	r = subprocess.run([args.wine, str(exe)], cwd=ROOT, env=dict(os.environ, WINEDEBUG='-all'), text=True, capture_output=True, timeout=30)
	log = r.stdout+r.stderr; (out/'api-test.log').write_text(log); print(log, flush=True)
	assert r.returncode == 0 and 'WinApi x64 full coverage, layouts and native calls OK' in log, (r.returncode, log)


if __name__ == '__main__':
	main()
