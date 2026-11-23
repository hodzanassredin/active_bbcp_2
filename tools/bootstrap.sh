#!/bin/sh
# Бутстрап компилятора: два полных прохода CompileSubs Dev.
#
#   tools/bootstrap.sh
#
# Проход A: упакованный dev0Linux (без Dev/Code) собирает весь Dev по .cp.
#   Полученный компилятор ставится в Dev/Code; проход B собирает Dev уже им.
#   Сравнение: .osf побайтно, .ocf — с обнулённой 12-байтовой меткой времени
#   компиляции (tools/bootstrap-compare.py). Устанавливается результат B.
#
# Прежний Dev/Code при ошибке возвращается на место.
set -e
. "$(dirname "$0")/env.sh"

[ -x "$DEV0" ] || { echo "bootstrap: нет $DEV0" >&2; exit 2; }
BASE="$BB/.work/i386"
A="$BB/.work/bootA"
B="$BB/.work/bootB"
save="$BB/.work/devsave.$$"

run_pass () {
	log="$1"
	{
		echo 'DevCompiler.InitLogMarks'
		echo "DevLibrarian.EnableDevLib('.work/i386', 'Mod')"
		echo 'DevCompiler.CompileSubs @Lin Dev'
		echo 'DevLibrarian.DisableDevLib'
	} > "$log.cmd"
	[ -x "$DEV0" ] || exit 2
	# Запуск из корня репозитория: DevLibrarian читает <Sub>/Mod отсюда.
	( cd "$BB" && "$DEV0" < "$log.cmd" > "$log" 2>&1 )
	grep -qE 'error:|~TRAP|not found' "$log" && {
		echo "bootstrap: ошибки компиляции, см. $log" >&2
		grep -nE 'error:|~TRAP|not found' "$log" | head -10 >&2
		exit 1
	}
	return 0
}

install_dev () {
	mkdir -p "$BB/Dev/Code" "$BB/Dev/Sym"
	cp "$BASE"/Dev/Code/*.ocf "$BB/Dev/Code/"
	cp "$BASE"/Dev/Sym/*.osf "$BB/Dev/Sym/"
}

restore () {
	if [ -d "$save/Code" ]; then
		mkdir -p "$BB/Dev/Code" "$BB/Dev/Sym"
		cp "$save"/Code/*.ocf "$BB/Dev/Code/" 2>/dev/null || true
		cp "$save"/Sym/*.osf "$BB/Dev/Sym/" 2>/dev/null || true
	fi
	rm -rf "$save"
}
trap restore EXIT INT TERM

mkdir -p "$save/Code" "$save/Sym"
cp "$BB"/Dev/Code/*.ocf "$save/Code/" 2>/dev/null || true
cp "$BB"/Dev/Sym/*.osf "$save/Sym/" 2>/dev/null || true

echo "bootstrap: проход A (упакованный компилятор)"
rm -rf "$BB/Dev/Code" "$BB/Dev/Sym"
rm -rf "$A" "$B"
run_pass "$BB/.work/bootA.log"
mkdir -p "$A"; cp -a "$BASE/Dev" "$A/"
install_dev

echo "bootstrap: проход B (собранный компилятор)"
run_pass "$BB/.work/bootB.log"
mkdir -p "$B"; cp -a "$BASE/Dev" "$B/"

python3 -I "$BB/tools/bootstrap-compare.py" "$A/Dev" "$B/Dev"

install_dev
trap - EXIT INT TERM
rm -rf "$save"
echo "bootstrap: готово; установлен результат прохода B"
