#!/bin/sh
# GUI-проверка DemoAnts без экрана пользователя (gtk4-broadwayd).
#
#   tools/test-demo-gui.sh
#
# Окно открывается командой DemoAnts.GuiCheck до Loop.Start; таймер
# заказывает Views.Update каждые 100 мс, а проверяющее действие раз в
# 10 тиков печатает ASCII-кадр (журнал модели) и через 40 тиков
# завершает процесс строкой «DemoAnts: ok=1». Мышь не эмулируется,
# DISPLAY пользователя не используется.
set -u
. "$(dirname "$0")/env.sh"

MOD=DemoAnts
TMO=90

skip() {
	echo "SKIP $MOD gui: $*"
	exit 0
}

BROADWAYD="$BB/.work/gtk4-bin/usr/bin/gtk4-broadwayd"
if [ ! -x "$BROADWAYD" ]; then
	BROADWAYD=$(command -v gtk4-broadwayd 2>/dev/null || true)
fi
[ -n "$BROADWAYD" ] || skip "нет gtk4-broadwayd (пакет libgtk-4-bin); системный broadwayd — GTK3 и несовместим"
[ -x "$INTERP" ] || { echo "test-demo-gui: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }
[ -x "$INSTALL/blackbox" ] || { echo "test-demo-gui: нет $INSTALL/blackbox — соберите tools/build64.sh" >&2; exit 2; }

# LinInit ставит хук --BB_RUN; DemoSupport и DemoAnts компилируются в мир.
mods="LinInit DemoSupport DemoAnts"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"${TMPDIR:-/tmp}/demo-gui-compile.log" 2>&1; then
		break
	fi
	if [ $pass -eq 3 ]; then
		echo "test-demo-gui: компиляция не удалась (проход $pass):" >&2
		tail -40 "${TMPDIR:-/tmp}/demo-gui-compile.log" >&2; exit 1
	fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/bbants.XXXXXX") || exit 1
rt="$work/runtime"
mkdir -m 700 "$rt" || { rm -rf "$work"; exit 1; }
dpid=""
cleanup() {
	[ -n "$dpid" ] && kill "$dpid" 2>/dev/null
	rm -rf "$work"
}
trap cleanup EXIT INT TERM

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
	echo "test-demo-gui: gtk4-broadwayd не поднялся:" >&2
	cat "$work/daemon.log" >&2; exit 1
fi

(
	cd "$INSTALL" &&
	env -u DISPLAY XDG_RUNTIME_DIR="$rt" GDK_BACKEND=broadway BROADWAY_DISPLAY=":$d" \
		LD_LIBRARY_PATH="$INSTALL" timeout -s KILL "$TMO" ./blackbox --BB_RUN="DemoAnts.GuiCheck"
) >"$work/out" 2>&1
rc=$?

grep -E '^(DemoAnts:|DemoAnts gui:|DemoAnts frame:)' "$work/out" | tail -30 || true
if [ $rc -ne 0 ] || ! grep -q 'DemoAnts: ok=1' "$work/out"; then
	echo "test-demo-gui: $MOD — провал (код $rc)" >&2
	tail -40 "$work/out" >&2; exit 1
fi
exit 0
