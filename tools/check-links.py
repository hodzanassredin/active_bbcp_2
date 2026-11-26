#!/usr/bin/env python3
"""Проверка относительных ссылок в документации.

    tools/check-links.py             # все .md репозитория, кроме OLD/
    tools/check-links.py файл.md …   # только эти

Ссылка вида [текст](путь) разрешается относительно каталога документа.
Проверяются только относительные пути; http(s):, mailto:, ftp: и якоря #…
пропускаются. Блоки кода ```…``` в разбор не входят.
Печатает «файл:строка: битая ссылка: путь» и выходит с кодом 1.
"""
import os
import re
import sys

BB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {"OLD", ".git", ".work", "build"}
LINK = re.compile(r"\]\(([^)]*)\)")
SKIP_PREFIX = ("http:", "https:", "mailto:", "ftp:", "#", "/")


def docs():
    for root, dirs, files in os.walk(BB):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for name in sorted(files):
            if name.endswith(".md"):
                yield os.path.relpath(os.path.join(root, name), BB)


def links(path):
    fence = False
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            if line.lstrip().startswith("```"):
                fence = not fence
                continue
            if fence:
                continue
            # Ссылка внутри `кода` — образец, а не ссылка.
            line = re.sub(r"`[^`]*`", "", line)
            for m in LINK.finditer(line):
                target = m.group(1)
                if not target or target.startswith(SKIP_PREFIX):
                    continue
                # [текст](путь "подпись") — проверяется путь.
                target = target.split()[0]
                yield number, target


def main(argv):
    files = argv or list(docs())
    bad = 0
    for path in files:
        base = os.path.dirname(path)
        for number, target in links(path):
            resolved = os.path.join(base, target)
            if not os.path.exists(os.path.join(BB, resolved)):
                print(f"{path}:{number}: битая ссылка: {target}", file=sys.stderr)
                bad += 1
    if bad:
        print(f"check-links: битых ссылок {bad}", file=sys.stderr)
        return 1
    print("check-links: битых ссылок 0")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
