#!/usr/bin/env python3
# Сравнение двух проходов сборки компилятора.
#
#   python3 -I tools/bootstrap-compare.py <DevA> <DevB>
#
# С BB_COMPILE_TIME компилятор пишет в .ocf фиксированную метку времени,
# поэтому оба прохода должны совпадать побайтно: .osf и .ocf без
# маскирования. Различий быть не должно.
import glob
import os
import sys


def compare_dir(a, b, pattern, subdir, label):
    fail = 0
    total = 0
    for f in sorted(glob.glob(os.path.join(a, subdir, pattern))):
        g = os.path.join(b, subdir, os.path.basename(f))
        total += 1
        if not os.path.exists(g):
            print('MISSING', g)
            fail += 1
            continue
        if open(f, 'rb').read() != open(g, 'rb').read():
            print('DIFF', label, os.path.basename(f))
            fail += 1
    return total, fail


def main():
    if len(sys.argv) != 3:
        print('usage: bootstrap-compare.py DevA DevB', file=sys.stderr)
        return 2
    a, b = sys.argv[1], sys.argv[2]
    total = 0
    fail = 0
    n, f = compare_dir(a, b, '*.ocf', 'Code', 'ocf')
    total += n
    fail += f
    n, f = compare_dir(a, b, '*.osf', 'Sym', 'osf')
    total += n
    fail += f
    print('bootstrap: файлов %d, различий %d' % (total, fail))
    return 0 if fail == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
