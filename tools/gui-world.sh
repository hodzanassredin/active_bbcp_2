#!/bin/sh
# Тексты для окна BlackBox: документы и исходники как .odc в каталоге установки.
#
#   tools/gui-world.sh
#
# В репозитории исходники лежат как .cp, документы — как .md, а окно
# открывает .odc: Help → Examples ищет Obx/Docu/Sys-Map.odc. Скрипт копирует
# <Подсистема>/Docu и исходники (<Подсистема>/Mod, поверх — Mod64) в
# установку и командой Dev_devOdc.All кладёт рядом с каждым текстом .odc.
# Разметка .md разбирается: заголовки, жирный, курсив, код, списки и
# таблицы получают атрибуты, ссылки [текст](Путь.md) и !Модуль.Процедура
# становятся StdLinks-ссылками. После преобразования Check открывает
# Obx/Docu/Sys-Map.odc через Views.OldView и требует непустые ссылки.
# Запускать после tools/build64.sh.
set -e
. "$(dirname "$0")/env.sh"
[ -x "$INTERP" ] || { echo "gui-world: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }

for sub in "$BB"/*/; do
	sub=$(basename "$sub")
	[ -d "$INSTALL/$sub/Code" ] || continue
	if [ -d "$BB/$sub/Docu" ]; then
		mkdir -p "$INSTALL/$sub/Docu"
		cp -a "$BB/$sub/Docu/." "$INSTALL/$sub/Docu/"
	fi
	for m in Mod Mod64; do
		ls "$BB/$sub/$m"/*.cp >/dev/null 2>&1 || continue
		mkdir -p "$INSTALL/$sub/Mod"
		cp "$BB/$sub/$m"/*.cp "$INSTALL/$sub/Mod/"
	done
done

"$(dirname "$0")/go64.sh" Dev_devOdc >/dev/null
out=$(echo 'Dev_devOdc.All' | run64 2>&1) || { printf '%s\n' "$out" | tail -20 >&2; exit 1; }
printf '%s\n' "$out" | grep 'Dev_devOdc:' | tail -5
printf '%s\n' "$out" | grep -q 'failed 0$' || { echo "gui-world: не все тексты преобразованы" >&2; exit 1; }
# .cp в установке нужны были только как вход преобразования.
find "$INSTALL" -path '*/Mod/*.cp' -delete
[ -f "$INSTALL/Obx/Docu/Sys-Map.odc" ] || { echo "gui-world: нет Obx/Docu/Sys-Map.odc" >&2; exit 1; }
for spec in "Obx/Docu Sys-Map.odc" "Obx/Docu/ru Sys-Map.odc" "Dev/Docu/ru Sys-Map.odc"; do
	set -- $spec
	out=$(printf "Dev_devOdc.Check('%s', '%s')\n" "$1" "$2" | run64 2>&1) || {
		printf '%s\n' "$out" | tail -20 >&2
		echo "gui-world: Check $1/$2 не выполнился" >&2; exit 1
	}
	printf '%s\n' "$out" | grep 'Dev_devOdc: check' | tail -1
	printf '%s\n' "$out" | grep -q 'empty 0$' || {
		echo "gui-world: пустые ссылки в $1/$2" >&2; exit 1
	}
	printf '%s\n' "$out" | grep -q 'check links [1-9]' || {
		echo "gui-world: в $1/$2 нет ссылок" >&2; exit 1
	}
done
echo "gui-world: готово"
