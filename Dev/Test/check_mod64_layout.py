#!/usr/bin/env python3
"""Check AMD64 source selection references without compiling or opening a GUI."""
import ast
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def main():
	checked = 0
	for subsystem in ('Dev', 'Lin', 'Win', 'System'):
		for path in (ROOT/subsystem/'Test').glob('*.py'):
			source = path.read_text()
			ast.parse(source, filename=str(path))
			if path == Path(__file__).resolve():
				continue
			assert 'CheckCompiler64.EnableDevLib' not in source, path
			assert '__x64' not in source, path
			for line in source.splitlines():
				if 'DevLibrarian.EnableDevLib(' in line:
					if "{folder}" in line:
						assert "folder = 'Mod64' if arch == 'x64' else 'Mod'" in source, path
					else:
						expected = "'Mod'" if path.name.endswith("32.py") else "'Mod64'"
						assert expected in line, (path, line)
			for subsystem_name, folder, name in re.findall(r"([A-Za-z]+)/(Mod64|Mod)/([A-Za-z0-9_]+\.cp)", source):
				assert (ROOT/subsystem_name/folder/name).is_file(), (path, subsystem_name, folder, name)
			checked += 1
	for subsystem in ROOT.iterdir():
		if subsystem.name == 'OLD' or not subsystem.is_dir():
			continue
		for path in (subsystem/'Mod64').glob('*.cp'):
			assert '__x64' not in path.name, path
			assert re.search(r'\bMODULE\s+\w+\s*(?:\[[^\]]*\])?\s*;', path.read_text()), path
	print(f'Mod64 layout and {checked} Python scripts: OK (no archived source selection)')


if __name__ == '__main__':
	main()
