#!/bin/sh
# Сверка примеров в документации с исходниками.
#
#   tools/check-examples.sh            # руководства и страницы примеров
#   tools/check-examples.sh файл.md …  # только эти
#   tools/check-examples.sh --run      # ещё и прогон страниц примеров
#
# Полный пример в документе — блок ``` … ```, который начинается строкой
# «MODULE Имя;». Такой блок обязан совпадать знак в знак с файлом модуля
# <Sub>/Mod64/*.cp или <Sub>/Mod/*.cp, в первой строке которого стоит
# тот же «MODULE Имя;». Пример, у которого нет файла, и пример,
# расходящийся с файлом, — ошибка: текст в документе не компилируется
# сам по себе, его проверяет только файл.
#
# С --run для каждой страницы примера Obx/Docu/*.md, Obx/Docu/ru/*.md и
# Demo/Docu/*.md,
# где есть строка «echo 'Модуль.Команда' | tools/run64.sh», команда
# исполняется (tools/run64.sh), и её вывод сверяется с блоком кода после
# строки «The log shows:» или «В журнале появится:». Нужна сборка
# tools/build64.sh.
set -e
run=0
if [ "$1" = --run ]; then run=1; shift; fi
BB="$(cd "$(dirname "$0")/.." && pwd)"
cd "$BB"
if [ $# -eq 0 ]; then
	# Руководства и страницы примеров. Сообщение о языке, README и
	# документы устройства показывают модули-образцы, а не примеры с
	# командой, и в проверку не входят.
	set -- Docu/Tasks.md Docu/ru/Tasks.md Docu/Async.md Docu/ru/Async.md \
		Unit/Docu/Testing.md Unit/Docu/ru/Testing.md \
		Cas/Docu/Cas.md Cas/Docu/ru/Cas.md \
		Actor/Docu/Actors.md Actor/Docu/ru/Actors.md \
		Obx/Docu/*.md Obx/Docu/ru/*.md Demo/Docu/*.md
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Индекс «имя модуля → файл» по первой строке исходников.
for f in */Mod64/*.cp */Mod/*.cp; do
	[ -f "$f" ] || continue
	m=$(sed -n '1s/^MODULE \([A-Za-z0-9_]*\);.*$/\1/p' "$f")
	[ -n "$m" ] && [ ! -f "$tmp/idx.$m" ] && printf '%s\n' "$f" > "$tmp/idx.$m"
done

bad=0; n=0
for doc in "$@"; do
	rm -f "$tmp"/blk.*
	# Блоки кода, которые начинаются с MODULE, — в файлы blk.1, blk.2 …
	awk -v dir="$tmp" '
		/^```/ { if (inb) { inb = 0; if (out != "") close(out); out = "" } else { inb = 1; first = 1 } ; next }
		inb && first { first = 0; if ($0 ~ /^MODULE [A-Za-z0-9_]+;/) { k++; out = dir "/blk." k } }
		inb && out != "" { print > out }
	' "$doc"
	for b in "$tmp"/blk.*; do
		[ -f "$b" ] || continue
		n=$((n + 1))
		m=$(sed -n '1s/^MODULE \([A-Za-z0-9_]*\);.*$/\1/p' "$b")
		if [ ! -f "$tmp/idx.$m" ]; then
			echo "check-examples: $doc: модуль $m — нет файла */Mod*/*.cp" >&2
			bad=$((bad + 1)); continue
		fi
		src=$(cat "$tmp/idx.$m")
		if ! diff -q "$b" "$src" > /dev/null 2>&1; then
			# Файл может кончаться без перевода строки или с лишней пустой строкой.
			sed -e '$a\' "$src" | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' > "$tmp/src"
			sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' "$b" > "$tmp/blk"
			if ! diff -q "$tmp/blk" "$tmp/src" > /dev/null 2>&1; then
				echo "check-examples: $doc: модуль $m расходится с $src" >&2
				diff "$tmp/blk" "$tmp/src" | head -10 >&2
				bad=$((bad + 1))
			fi
		fi
	done
done
echo "check-examples: примеров $n, расхождений $bad"

if [ "$run" -eq 1 ]; then
	r=0
	for page in Obx/Docu/*.md Obx/Docu/ru/*.md Demo/Docu/*.md; do
		cmd=$(sed -n "s/^echo '\([A-Za-z0-9_]*\.[A-Za-z0-9_]*\)' | tools\/run64.sh$/\1/p" "$page" | head -1)
		[ -n "$cmd" ] || continue
		awk '
			/^(The log shows:|В журнале появится:)$/ { want = 1; next }
			want && /^```/ { if (inb) exit; inb = 1; next }
			inb { print }
		' "$page" > "$tmp/want"
		echo "$cmd" | timeout 120 tools/run64.sh > "$tmp/got" 2>&1 || true
		r=$((r + 1))
		if ! diff -q "$tmp/want" "$tmp/got" > /dev/null; then
			echo "check-examples: $page: вывод $cmd расходится с документом" >&2
			diff "$tmp/want" "$tmp/got" | head -10 >&2
			bad=$((bad + 1))
		fi
	done
	echo "check-examples: прогонов $r, расхождений всего $bad"
fi
[ "$bad" -eq 0 ]
