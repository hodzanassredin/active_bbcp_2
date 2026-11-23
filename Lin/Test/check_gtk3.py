#!/usr/bin/env python3
"""Compatibility entry point: the active Linux backend is now GTK4."""
from check_gtk4 import main

if __name__ == '__main__':
	print('The Linux backend now uses GTK4; running check_gtk4.py.', flush=True)
	main()
