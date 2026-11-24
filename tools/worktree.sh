#!/bin/sh
# Параллельная рабочая копия, готовая к сборке.
#
#   tools/worktree.sh <каталог> <ветка> [<база>]
#
# 1. git worktree add: новая ветка <ветка> от <база> (по умолчанию HEAD);
#    если ветка уже есть — копия на ней.
# 2. Ссылки на общие инструменты основной копии в <каталог>/.work:
#    go, i386, i386dev, gtk4-bin, venv, mpi (те, что есть).
# 3. tools/bootstrap.sh: скомпилированный компилятор Dev/Code в git не
#    хранится, а без него хост собирает загрузочным компилятором, который
#    не знает [exclusive].
# 4. tools/build64.sh.
# Шаги 3 и 4 идут через tools/iso.sh — со своим /tmp.
set -e
[ $# -ge 2 ] || { echo "usage: tools/worktree.sh <каталог> <ветка> [<база>]" >&2; exit 2; }
dir="$1"; branch="$2"; base="${3:-HEAD}"
BB="$(cd "$(dirname "$0")/.." && pwd)"
cd "$BB"
[ -e "$dir" ] && { echo "worktree: $dir уже существует" >&2; exit 2; }

if git show-ref --verify --quiet "refs/heads/$branch"; then
	git worktree add "$dir" "$branch"
else
	git worktree add -b "$branch" "$dir" "$base"
fi
dir="$(cd "$dir" && pwd)"

mkdir -p "$dir/.work"
linked=""
for t in go i386 i386dev gtk4-bin venv mpi; do
	[ -e "$BB/.work/$t" ] || continue
	ln -s "$BB/.work/$t" "$dir/.work/$t"
	linked="$linked $t"
done

log="$dir/.work/worktree.log"
echo "worktree: bootstrap (журнал $log)"
if ! "$BB/tools/iso.sh" "$dir/tools/bootstrap.sh" > "$log" 2>&1; then
	tail -20 "$log" >&2; echo "worktree: bootstrap не удался" >&2; exit 1
fi
echo "worktree: build64"
if ! "$BB/tools/iso.sh" "$dir/tools/build64.sh" >> "$log" 2>&1; then
	tail -20 "$log" >&2; echo "worktree: build64 не удался" >&2; exit 1
fi

echo "worktree: готово"
echo "  каталог: $dir"
echo "  ветка:   $branch ($(git -C "$dir" rev-parse --short HEAD))"
echo "  ссылки:  .work/{$(echo $linked | tr ' ' ',')}"
echo "  мир:     $dir/.work/BB64LIN"
echo "  команды: tools/iso.sh tools/... из $dir"
