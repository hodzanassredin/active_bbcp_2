#!/bin/sh
# Запуск консольного 64-битного BlackBox (blackbox-console).
#
#   echo 'StdLog.String("hi"); StdLog.Ln' | tools/run64.sh
#   tools/run64.sh команды.txt
set -e
. "$(dirname "$0")/env.sh"
[ -x "$INTERP" ] || { echo "run64: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }
if [ $# -gt 0 ]; then exec < "$1"; fi
run64
