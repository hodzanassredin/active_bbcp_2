#!/bin/sh
# Бутстрап компилятора: три прохода CompileSubs Dev.
#
#   tools/bootstrap.sh
#
# Проход A: упакованный dev0Linux (без Dev/Code) собирает весь Dev по .cp.
#   Полученный компилятор ставится в Dev/Code; проход B собирает Dev уже им,
#   проход C — компилятором из B.
#   В проходах B и C задан BB_COMPILE_TIME: метка времени компиляции в .ocf
#   фиксирована, поэтому B и C обязаны совпасть побайтно, без маскирования
#   (tools/bootstrap-compare.py). Устанавливается результат B.
#
# Прежний Dev/Code при ошибке возвращается на место.
set -e
. "$(dirname "$0")/env.sh"

[ -x "$DEV0" ] || { echo "bootstrap: нет $DEV0" >&2; exit 2; }
clean_host_temp
BASE="$BB/.work/i386"
A="$BB/.work/bootA"
B="$BB/.work/bootB"
C="$BB/.work/bootC"
save="$BB/.work/devsave.$$"
# Фиксированная метка времени компиляции для детерминированных .ocf.
FIXED_TIME="2000-01-01 00:00:00"

run_pass () {
	log="$1"; fixed="$2"
	{
		echo 'DevCompiler.InitLogMarks'
		echo "DevLibrarian.EnableDevLib('.work/i386', 'Mod')"
		echo 'DevCompiler.CompileSubs @Lin Dev'
		echo 'DevLibrarian.DisableDevLib'
	} > "$log.cmd"
	[ -x "$DEV0" ] || exit 2
	# Запуск из корня репозитория: DevLibrarian читает <Sub>/Mod отсюда.
	if [ "$fixed" -eq 1 ]; then
		( cd "$BB" && "$DEV0" --BB_COMPILE_TIME="$FIXED_TIME" < "$log.cmd" > "$log" 2>&1 )
	else
		( cd "$BB" && "$DEV0" < "$log.cmd" > "$log" 2>&1 )
	fi
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
rm -rf "$A" "$B" "$C"
run_pass "$BB/.work/bootA.log" 0
mkdir -p "$A"; cp -a "$BASE/Dev" "$A/"
install_dev

echo "bootstrap: проход B (собранный компилятор, BB_COMPILE_TIME)"
run_pass "$BB/.work/bootB.log" 1
mkdir -p "$B"; cp -a "$BASE/Dev" "$B/"
install_dev

echo "bootstrap: проход C (проверка детерминизма)"
run_pass "$BB/.work/bootC.log" 1
mkdir -p "$C"; cp -a "$BASE/Dev" "$C/"

python3 -I "$BB/tools/bootstrap-compare.py" "$B/Dev" "$C/Dev"

install_dev
trap - EXIT INT TERM
rm -rf "$save"
echo "bootstrap: готово; B и C совпали побайтно, установлен результат B"
