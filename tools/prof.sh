#!/bin/sh
# Выборочный профайлер консольного BlackBox (Linux x64), без прав администратора.
#
#   tools/prof.sh 'UnitRunner.Run System_devTasks proc slow noquit'
#   tools/prof.sh -n 40 -u 100 'Команда1' 'Команда2'
#
# Команды исполняются в blackbox-console под библиотекой tools/prof/bbprof.c
# (LD_PRELOAD): таймер SIGPROF каждые -u микросекунд процессорного времени
# записывает адрес прерванной инструкции. После команд модуль
# Dev_devProfMap печатает карту «модуль — процедура — адреса», и
# tools/prof/resolve.py сводит выборки по модулям и процедурам (-n строк).
# Команда не должна завершать процесс (для UnitRunner — ключ noquit).
set -e
. "$(dirname "$0")/env.sh"
top=30; us=200
while getopts n:u: o; do
	case $o in n) top=$OPTARG;; u) us=$OPTARG;; *) exit 2;; esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || { echo "prof: нужна хотя бы одна команда" >&2; exit 2; }
[ -x "$INTERP" ] || { echo "prof: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }
work="$BB/.work/prof"
mkdir -p "$work"
cc -O2 -shared -fPIC -o "$work/bbprof.so" "$BB/tools/prof/bbprof.c"
"$BB/tools/go64.sh" Dev_devProfMap >/dev/null
{ for c in "$@"; do printf '%s\n' "$c"; done; echo 'Dev_devProfMap.Map'; } > "$work/cmd.txt"
(cd "$INSTALL" && BBPROF_OUT="$work/samples.bin" BBPROF_US="$us" LD_PRELOAD="$work/bbprof.so" \
	LD_LIBRARY_PATH="$INSTALL" "$INTERP" < "$work/cmd.txt" > "$work/out.txt" 2>&1) || true
grep -v '^PROFMAP' "$work/out.txt" | tail -20
python3 "$BB/tools/prof/resolve.py" "$work/samples.bin" "$work/out.txt" "$top"
