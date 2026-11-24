#!/bin/sh
# Замер-ориентир на Go для нагрузочного теста задач (System_devTasks):
# n сопрограмм Go, каждая один раз уступает и увеличивает счётчик,
# код Go исполняет одна нить (GOMAXPROCS=1).
#
#   tools/bench-go.sh            # 1000 и 10000
#   tools/bench-go.sh 100000
#
# Нужен компилятор Go в PATH или в переменной GO.
set -e
BB=$(cd "$(dirname "$0")/.." && pwd)
GO=${GO:-go}
command -v "$GO" >/dev/null 2>&1 || { echo "bench-go: нет компилятора Go (задайте GO=путь)" >&2; exit 2; }
out="$BB/.work/bench-go"
mkdir -p "$BB/.work"
(cd "$BB/tools/bench-go" && GOCACHE="$BB/.work/go-cache" "$GO" build -o "$out" main.go)
[ $# -gt 0 ] || set -- 1000 10000
for n in "$@"; do "$out" "$n"; done
