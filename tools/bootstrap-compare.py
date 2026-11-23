#!/usr/bin/env python3
# Сравнение двух проходов сборки компилятора.
#
#   python3 -I tools/bootstrap-compare.py <DevA> <DevB>
#
# .osf должны совпадать побайтно. В .ocf единственное допустимое различие —
# 12-байтовое поле времени компиляции в описателе модуля (год, месяц, день,
# час, минута, секунда): второй проход идёт в другую секунду. Поле находится
# по году 2026 и проверяется на правдоподобность; перед сравнением оно
# обнуляется в обоих файлах.
import glob
import os
import sys


def mask_time(path):
    d = bytearray(open(path, 'rb').read())
    i = 0
    masked = 0
    while True:
        i = d.find(b'\xea\x07', i)
        if i < 0 or i + 12 > len(d):
            break
        month = d[i + 2] | (d[i + 3] << 8)
        day = d[i + 4] | (d[i + 5] << 8)
        hour = d[i + 6] | (d[i + 7] << 8)
        minute = d[i + 8] | (d[i + 9] << 8)
        second = d[i + 10] | (d[i + 11] << 8)
        if (1 <= month <= 12 and 1 <= day <= 31 and hour < 24
                and minute < 60 and second < 60):
            d[i:i + 12] = b'\0' * 12
            masked += 1
            i += 12
        else:
            i += 2
    return bytes(d), masked


def main():
    if len(sys.argv) != 3:
        print('usage: bootstrap-compare.py DevA DevB', file=sys.stderr)
        return 2
    a, b = sys.argv[1], sys.argv[2]
    fail = 0
    total = 0
    masked = 0
    for f in sorted(glob.glob(os.path.join(a, 'Code', '*.ocf'))):
        g = os.path.join(b, 'Code', os.path.basename(f))
        total += 1
        if not os.path.exists(g):
            print('MISSING', g)
            fail += 1
            continue
        x, mx = mask_time(f)
        y, _ = mask_time(g)
        masked += 1 if mx else 0
        if x != y:
            print('DIFF', os.path.basename(f))
            fail += 1
    for f in sorted(glob.glob(os.path.join(a, 'Sym', '*.osf'))):
        g = os.path.join(b, 'Sym', os.path.basename(f))
        total += 1
        if not os.path.exists(g) or open(f, 'rb').read() != open(g, 'rb').read():
            print('DIFF SYM', os.path.basename(f))
            fail += 1
    print('bootstrap: файлов %d, с меткой времени %d, различий %d'
          % (total, masked, fail))
    return 0 if fail == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
