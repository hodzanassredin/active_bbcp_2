/* Минимальная C-обёртка MPICH для AsyncMpi (этап 9).
 *
 * Скрывает структуры и константы MPI от Component Pascal: наружу —
 * только целые и байтовые буферы. Собирается mpicc из пакета mpich
 * (pip), поэтому libmpi.so.12 лежит рядом; LD_LIBRARY_PATH задаёт
 * tools/test-mpi.sh. */

#include <mpi.h>
#include <stddef.h>

int bbmpi_init(void) {
	return MPI_Init(NULL, NULL) == MPI_SUCCESS ? 0 : -1;
}

int bbmpi_finalize(void) {
	return MPI_Finalize() == MPI_SUCCESS ? 0 : -1;
}

int bbmpi_rank(void) {
	int r = -1;
	if (MPI_Comm_rank(MPI_COMM_WORLD, &r) != MPI_SUCCESS) return -1;
	return r;
}

int bbmpi_size(void) {
	int s = -1;
	if (MPI_Comm_size(MPI_COMM_WORLD, &s) != MPI_SUCCESS) return -1;
	return s;
}

int bbmpi_send(int dest, int tag, const void *buf, int len) {
	return MPI_Send(buf, len, MPI_BYTE, dest, tag, MPI_COMM_WORLD) == MPI_SUCCESS ? 0 : -1;
}

int bbmpi_recv(int src, int tag, void *buf, int len) {
	return MPI_Recv(buf, len, MPI_BYTE, src, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE) == MPI_SUCCESS ? 0 : -1;
}

/* 1 — сообщение ждёт, 0 — нет, <0 — ошибка. */
int bbmpi_probe(int src, int tag) {
	int flag = 0;
	MPI_Status st;
	if (MPI_Iprobe(src, tag, MPI_COMM_WORLD, &flag, &st) != MPI_SUCCESS) return -1;
	return flag ? 1 : 0;
}
