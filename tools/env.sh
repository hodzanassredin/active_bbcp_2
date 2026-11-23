# Общие пути стенда active2 (снимок Ивана: .cp, dev0Linux, GTK4).
#   . tools/env.sh
#
# BB      — корень репозитория.
# INSTALL — каталог установки/сборки (Code/Sym и исполняемые файлы).
#           По умолчанию active_bb/.work/BB64LIN; переопределяется BB64INSTALL.
# INTERP  — консольный 64-битный BlackBox (blackbox-console).
# DEV0    — 32-битный bootstrap с компиляторами (dev0Linux).

BB="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
INSTALL="${BB64INSTALL:-$BB/.work/BB64LIN}"
INTERP="$INSTALL/blackbox-console"
DEV0="$BB/dev0Linux"

# Подсистемы, которые собираются по умолчанию (порядок как у Ивана).
DEFAULT_SUBSYSTEMS="Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Unit"

# Запуск консольного 64-битного BlackBox из каталога установки: StdLoader
# ищет <Подсистема>/Code рядом с процессом.
run64() {
	(cd "$INSTALL" && LD_LIBRARY_PATH="$INSTALL${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$INTERP" "$@")
}

# Запуск dev0Linux из корня репозитория: DevLibrarian читает <Sub>/Mod64 и
# <Sub>/Mod отсюда.
run_dev0() {
	(cd "$BB" && "$DEV0" "$@")
}
