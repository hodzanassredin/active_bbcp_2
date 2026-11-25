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

# Подсистемы, которые собираются по умолчанию (порядок как у Ивана; Cas —
# до Obx: её импортируют примеры ObxCas).
DEFAULT_SUBSYSTEMS="Lin System Std Cons Text Fig Form Dev Comm Crypto Cas Obx Xhtml Sql Paket Unit Demo"

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

# Конвертер .cp в хосте dev0Linux пишет текст во временный файл
# /tmp/odcNNNNN (LinFiles.GetTempFileName: "odc" + пять цифр номера из
# Libc.clock()). Номер подбирается перебором, но не более 200 имён
# (LinFiles.CreateFile). После HALT/аварии временные файлы остаются в /tmp,
# и накопленный сплошной блок длиннее 200 имён запирает конверсию: файл не
# создаётся, компилятор получает пустой текст, печатает «MODULE expected» и
# CompileSubs обрывается на середине списка. Поэтому перед каждым запуском
# хоста убираем этот мусор. Живой процесс держит свой файл по дескриптору,
# так что удаление ему не мешает.
clean_host_temp() {
	[ -d /tmp ] || return 0
	n=$(find /tmp -maxdepth 1 -type f -name 'odc[0-9][0-9][0-9][0-9][0-9]' 2>/dev/null | wc -l)
	[ "$n" -gt 0 ] || return 0
	find /tmp -maxdepth 1 -type f -name 'odc[0-9][0-9][0-9][0-9][0-9]' -delete 2>/dev/null || true
	echo "env: убрано $n временных файлов BlackBox в /tmp" >&2
}
