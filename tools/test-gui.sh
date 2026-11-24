#!/bin/sh
# GUI-прогон этапа 3/1 без участия человека и без экрана пользователя.
#
#   tools/test-gui.sh
#
# Поднимает gtk4-broadwayd в своём XDG_RUNTIME_DIR (Broadway-дисплей),
# запускает blackbox с --BB_RUN=DemoTestsGui.Run: команда заводит задачи
# производителя-потребителя на [exclusive]+ACTIVE.AWAIT и действие-тикер,
# дальше работает штатный главный цикл окон. Тикер печатает промежутки;
# итог «DemoTestsGui: ok=1» означает, что задачи посчитали 100 элементов,
# а главный цикл не замирал (maxgap меньше 500 мс).
#
# Системный broadwayd — от GTK3 и с клиентом GTK4 падает; нужен
# gtk4-broadwayd (пакет libgtk-4-bin). Его нет — тест пропускается.
# Мышь не эмулируется; DISPLAY пользователя не используется.
set -u
. "$(dirname "$0")/env.sh"

MOD=DemoTestsGui
TMO=60

skip() {
	echo "SKIP $MOD: $*"
	exit 0
}

BROADWAYD="$BB/.work/gtk4-bin/usr/bin/gtk4-broadwayd"
if [ ! -x "$BROADWAYD" ]; then
	BROADWAYD=$(command -v gtk4-broadwayd 2>/dev/null || true)
fi
[ -n "$BROADWAYD" ] || skip "нет gtk4-broadwayd (пакет libgtk-4-bin); системный broadwayd — GTK3 и несовместим"
[ -x "$INTERP" ] || { echo "test-gui: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }
[ -x "$INSTALL/blackbox" ] || { echo "test-gui: нет $INSTALL/blackbox — соберите tools/build64.sh" >&2; exit 2; }

# Хук --BB_RUN живёт в LinInit; исходники компилируем в установку.
mods="LinInit $MOD"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"${TMPDIR:-/tmp}/gui-compile.log" 2>&1; then
		break
	fi
	if [ $pass -eq 3 ]; then
		echo "test-gui: компиляция не удалась (проход $pass):" >&2
		tail -40 "${TMPDIR:-/tmp}/gui-compile.log" >&2; exit 1
	fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/bbgui.XXXXXX") || exit 1
rt="$work/runtime"
mkdir -m 700 "$rt" || { rm -rf "$work"; exit 1; }
dpid=""
cleanup() {
	[ -n "$dpid" ] && kill "$dpid" 2>/dev/null
	rm -rf "$work"
}
trap cleanup EXIT INT TERM

# Broadway слушает http-порт 8080+display; берём свободный, чтобы не
# столкнуться с чужим/устаревшим демоном. Сокет дисплея — broadway<d+1>.
d=100
while [ $d -lt 1000 ]; do
	if ! ss -ltn 2>/dev/null | grep -q ":$((d + 8080)) "; then break; fi
	d=$((d + 1))
done
port=$((d + 8080))
socket="$rt/broadway$((d + 1)).socket"

XDG_RUNTIME_DIR="$rt" "$BROADWAYD" -p "$port" ":$d" >"$work/daemon.log" 2>&1 &
dpid=$!
i=0
while [ ! -S "$socket" ] && [ $i -lt 50 ]; do
	sleep 0.1; i=$((i + 1))
done
if [ ! -S "$socket" ]; then
	echo "test-gui: gtk4-broadwayd не поднялся:" >&2
	cat "$work/daemon.log" >&2; exit 1
fi

(
	cd "$INSTALL" &&
	env -u DISPLAY XDG_RUNTIME_DIR="$rt" GDK_BACKEND=broadway BROADWAY_DISPLAY=":$d" \
		LD_LIBRARY_PATH="$INSTALL" timeout -s KILL "$TMO" ./blackbox --BB_RUN="DemoTestsGui.Run"
) >"$work/out" 2>&1
rc=$?

grep -E '^(DemoTestsGui:|LinGui:)' "$work/out" || true
if [ $rc -ne 0 ] || ! grep -q 'DemoTestsGui: ok=1' "$work/out"; then
	echo "test-gui: $MOD — провал (код $rc)" >&2
	tail -30 "$work/out" >&2; exit 1
fi
exit 0
