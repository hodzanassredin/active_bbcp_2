#!/bin/sh
# Запуск команды со своим /tmp.
#
#   tools/iso.sh tools/build64.sh
#   tools/iso.sh sh -c 'echo ObxTasks0.Do | tools/run64.sh'
#
# Хост dev0Linux пишет временные файлы /tmp/odcNNNNN, и tools/env.sh
# перед каждым запуском убирает их из /tmp. Две рабочие копии, которые
# собираются одновременно, делят /tmp: одна стирает или занимает файлы
# другой, и компилятор второй получает пустой текст — в журнале «MODULE
# expected». Сценарий запускает команду в своём пространстве имён
# монтирования (unshare -rm) с пустым tmpfs на /tmp; привилегий не нужно.
set -e
[ $# -gt 0 ] || { echo "iso: нет команды" >&2; exit 2; }
exec unshare -rm sh -c 'mount -t tmpfs tmpfs /tmp && exec "$@"' iso "$@"
