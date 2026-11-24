#!/bin/sh
# 32-битный C-тулчейн (i386) без sudo.
#
#   FLAGS=$(tools/i386-toolchain.sh)    # подготовить .work/i386dev, напечатать флаги
#
# Идемпотентно готовит .work/i386dev (переопределяется BB_I386DEV):
#   debs/  — apt-get download libc6-dev-i386 и lib32gcc-<N>-dev
#            (N — старшая версия системного gcc), без установки в систему;
#   root/  — dpkg-deb -x этих пакетов; в root/usr/lib32/libc.so путь к
#            libc_nonshared.a заменён на путь внутри root;
#   flags.txt — готовая строка флагов gcc (-m32 -B… -L… -isystem …).
#
# В stdout печатается только строка флагов (её подставляют в gcc без
# кавычек): gcc $FLAGS -shared -fPIC -O2 -o libtest32.so tools/test64.c.
# Готовность проверяется сборкой и запуском пробника с ucontext.
#
# Нет сети/пакета/gcc/32-битного рантайма — «SKIP i386-toolchain: причина»
# в stderr и код 0 с пустым stdout: это пропуск, а не падение.
set -u

BB=$(cd "$(dirname "$0")/.." && pwd)
ROOT="${BB_I386DEV:-$BB/.work/i386dev}"
DEBS="$ROOT/debs"
DEST="$ROOT/root"
FLAGSFILE="$ROOT/flags.txt"

say() { echo "i386-toolchain: $*" >&2; }
skip() { say "SKIP: $*"; exit 0; }

command -v gcc >/dev/null 2>&1 || skip "нет gcc"
command -v apt-get >/dev/null 2>&1 || skip "нет apt-get (нужен apt-get download, без установки)"
command -v dpkg-deb >/dev/null 2>&1 || skip "нет dpkg-deb"

MACHINE=$(gcc -dumpmachine 2>/dev/null) || skip "gcc -dumpmachine не работает"
GCCVER=$(gcc -dumpversion 2>/dev/null | cut -d. -f1)
[ -n "$MACHINE" ] && [ -n "$GCCVER" ] || skip "не удалось определить версию gcc"
case "$MACHINE" in
	*x86_64*) ;;
	*) skip "хост $MACHINE: пакеты i386 рассчитаны на x86_64" ;;
esac

PKGC=libc6-dev-i386
PKGG="lib32gcc-$GCCVER-dev"
GCCDIR="$DEST/usr/lib/gcc/$MACHINE/$GCCVER/32"

flags() {
	echo "-m32 -B$DEST/usr/lib32 -B$GCCDIR -L$DEST/usr/lib32 -L$GCCDIR" \
		"-isystem $DEST/usr/include -isystem $DEST/usr/include/$MACHINE" \
		"-isystem /usr/include/$MACHINE"
}

ready() {
	[ -f "$FLAGSFILE" ] || return 1
	[ -f "$DEST/usr/lib32/libc.so" ] || return 1
	[ -f "$DEST/usr/lib32/libc_nonshared.a" ] || return 1
	[ -f "$DEST/usr/lib32/crt1.o" ] || return 1
	[ -f "$GCCDIR/crtbegin.o" ] || return 1
	[ -f "$GCCDIR/libgcc.a" ] || return 1
	grep -q "$DEST/usr/lib32/libc_nonshared.a" "$DEST/usr/lib32/libc.so" || return 1
	[ "$(cat "$FLAGSFILE")" = "$(flags)" ] || return 1
}

# Пробник проверяет то, ради чего тулчейн нужен: ucontext в 32 битах.
probe() {
	mkdir -p "$ROOT/probe" || return 1
	cat > "$ROOT/probe/probe.c" <<'EOF'
#include <stdio.h>
#include <ucontext.h>
static ucontext_t mainctx, coctx;
static int stage;
static void body (void) { stage = 1; swapcontext (&coctx, &mainctx); stage = 2; }
int main (void) {
	static char stk[256 * 1024];
	if (sizeof (void *) != 4 || sizeof (ucontext_t) != 364) return 1;
	getcontext (&coctx);
	coctx.uc_stack.ss_sp = stk;
	coctx.uc_stack.ss_size = sizeof stk;
	coctx.uc_link = &mainctx;
	makecontext (&coctx, body, 0);
	swapcontext (&mainctx, &coctx);
	if (stage != 1) return 2;
	swapcontext (&mainctx, &coctx);
	return stage == 2 ? 0 : 3;
}
EOF
	# shellcheck disable=SC2046
	gcc $(flags) -o "$ROOT/probe/probe" "$ROOT/probe/probe.c" || return 1
	"$ROOT/probe/probe" || return 1
}

prepare() {
	mkdir -p "$DEBS" "$DEST" || skip "не создать $ROOT"

	# 1. Пакеты. apt-get download не требует root и не ставит ничего в систему.
	if ! ls "$DEBS/$PKGC"_*.deb >/dev/null 2>&1; then
		say "скачиваю $PKGC $PKGG (apt-get download)"
		if ! (cd "$DEBS" && apt-get download "$PKGC" "$PKGG") >/dev/null 2>&1; then
			skip "apt-get download $PKGC $PKGG не удался (нет сети, пакета или списков apt)"
		fi
	fi
	if ! ls "$DEBS/$PKGG"_*.deb >/dev/null 2>&1; then
		say "скачиваю $PKGG (apt-get download)"
		if ! (cd "$DEBS" && apt-get download "$PKGG") >/dev/null 2>&1; then
			skip "apt-get download $PKGG не удался (нет сети или пакета для gcc $GCCVER)"
		fi
	fi

	# 2. Распаковка. dpkg-deb -x пишет в root и ничего не устанавливает.
	say "распаковываю пакеты в $DEST"
	for deb in "$DEBS/$PKGC"_*.deb "$DEBS/$PKGG"_*.deb; do
		dpkg-deb -x "$deb" "$DEST" >/dev/null 2>&1 || skip "dpkg-deb -x $deb не удался"
	done
	[ -d "$GCCDIR" ] || skip "нет $GCCDIR — пакету $PKGG не хватает каталога gcc $GCCVER/32"

	# 3. libc.so — скрипт линкера: libc_nonshared.a должен браться из root,
	#    иначе ld ищет его в /usr/lib32, которого без установки пакета нет.
	libcso="$DEST/usr/lib32/libc.so"
	[ -f "$libcso" ] || skip "в $DEST нет usr/lib32/libc.so"
	if ! grep -q "$DEST/usr/lib32/libc_nonshared.a" "$libcso"; then
		sed -i "s|/usr/lib32/libc_nonshared.a|$DEST/usr/lib32/libc_nonshared.a|g" "$libcso" \
			|| skip "не удалось поправить $libcso"
	fi

	# 4. Пробная сборка и запуск: без 32-битного рантайма libc.so.6
	#    бинарник не запустится, и тесты i386 всё равно не пойдут.
	if ! probe; then
		skip "пробник ucontext (-m32) не собрался или не запустился: нет 32-битного рантайма libc/ld.so или заголовков"
	fi

	flags > "$FLAGSFILE" || skip "не записать $FLAGSFILE"
}

if ! ready; then
	prepare
	ready || skip "подготовка прошла, но проверка готовности не удалась"
fi

cat "$FLAGSFILE"
