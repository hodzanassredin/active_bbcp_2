#!/bin/sh
# Тест MPI-транспорта компутонов (этап 9) через mpiexec.
#
#   tools/test-mpi.sh
#
# MPICH ставится в пользовательское пространство: uv pip install mpich
# в .work/venv. Обёртка tools/bbmpi.c собирается mpicc в .work/mpi.
# Мир — 5 рангов: 0 гонит тест, 1..4 исполняют AsyncMpi.WorkerLoop.
#
# Нет mpich — тест пропускается с причиной (код 0), как и резолвер.
set -u
. "$(dirname "$0")/env.sh"

MOD=Async_devMpi
MARKS=proc
TMO=180
DEPS="AsyncMpi AsyncComputon AsyncIO AsyncSelect AsyncChan AsyncOnce LinNative LinThreads"
VENV="$BB/.work/venv"
MPICC="$VENV/bin/mpicc"
MPIRUN="$VENV/bin/mpiexec"
MPIDIR="$BB/.work/mpi"

skip() {
	echo "SKIP $MOD: $*"
	exit 0
}

[ -x "$MPICC" ] || skip "нет mpicc в $VENV (uv pip install mpich)"
[ -x "$MPIRUN" ] || skip "нет mpiexec в $VENV (uv pip install mpich)"
[ -x "$INTERP" ] || { echo "test-mpi: нет $INTERP — соберите tools/build64.sh" >&2; exit 2; }

mkdir -p "$MPIDIR" || exit 1
if ! "$MPICC" -shared -fPIC -O2 -o "$MPIDIR/libbbmpi.so" "$BB/tools/bbmpi.c" >"$MPIDIR/build.log" 2>&1; then
	echo "test-mpi: сборка libbbmpi.so не удалась:" >&2
	tail -20 "$MPIDIR/build.log" >&2; exit 1
fi

mods="UnitAssert UnitRunner Unit_devProbe Unit_devMain $DEPS"
pass=0
while [ $pass -lt 3 ]; do
	pass=$((pass + 1))
	if "$BB/tools/go64.sh" $mods >"${TMPDIR:-/tmp}/mpi-compile.log" 2>&1 \
			&& "$BB/tools/go64.sh" "$MOD" >>"${TMPDIR:-/tmp}/mpi-compile.log" 2>&1; then
		break
	fi
	if [ $pass -eq 3 ]; then
		echo "test-mpi: компиляция не удалась (проход $pass):" >&2
		tail -40 "${TMPDIR:-/tmp}/mpi-compile.log" >&2; exit 1
	fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/bbmpi.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT INT TERM
printf 'UnitRunner.Run %s %s quit\n' "$MOD" "$MARKS" >"$work/rank0.cmd"
printf 'AsyncMpi.WorkerLoop\n' >"$work/worker.cmd"
cat >"$work/launch.sh" <<EOF
#!/bin/sh
rank=\${PMI_RANK:-0}
cd "$INSTALL" || exit 1
if [ "\$rank" = 0 ]; then
	exec ./blackbox-console < "$work/rank0.cmd"
fi
exec ./blackbox-console < "$work/worker.cmd"
EOF
chmod +x "$work/launch.sh"

LD_LIBRARY_PATH="$INSTALL:$MPIDIR:$VENV/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	timeout -k 5 "$TMO" "$MPIRUN" -n 5 sh "$work/launch.sh" >"$work/out" 2>&1
rc=$?

grep -E '^(PASS|FAIL|EMPTY|SKIP|== Unit|Async_devMpi:)' "$work/out" || true
if [ $rc -eq 124 ]; then
	echo "test-mpi: $MOD — таймаут ${TMO}s" >&2
	tail -20 "$work/out" >&2; exit 1
fi
if [ $rc -ne 0 ] || ! grep -q 'failed 0,' "$work/out"; then
	echo "test-mpi: $MOD — провал (код $rc)" >&2
	tail -30 "$work/out" >&2; exit 1
fi
exit 0
