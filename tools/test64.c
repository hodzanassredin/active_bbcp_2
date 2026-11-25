/* libtest64.so / libtest32.so — помощник тестов System_dev: переключение стека
   в одной нити через ucontext (getcontext/makecontext/swapcontext).
   amd64: tools/test.sh и tools/bench-gc.sh собирают cc -shared -fPIC -O2
   в <BB64LIN>/libtest64.so. i386: tools/build32.sh и tools/test32.sh
   собирают тот же исходник флагами tools/i386-toolchain.sh в
   <BB32LIN>/libtest32.so.

   Стеки: t64_co_create — mmap + сторожевая страница PROT_NONE (вне кучи CP);
   t64_co_create_on — готовый стек (например, NEW-массив в куче CP).

   Входная процедура вызывается по SysV ABI, поэтому в CP она объявляется
   PROCEDURE [ccall] (arg: ADDRESS); адрес берётся SYSTEM.ADR(Proc). */

#define _GNU_SOURCE
#include <ucontext.h>
#include <stddef.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define GUARD 4096

static ucontext_t mainctx, coctx;
static void *stk;
static long stksz;
static int owns_stack;
static int ready;

static void (*co_entry) (void *);
static void *co_arg;

static void *stk_lo, *stk_hi;

static void trampoline (void) {
	co_entry (co_arg);
}

int t64_co_create (void *entry, void *arg, long size) {
	void *p;
	if (size < 64 * 1024) size = 64 * 1024;
	p = mmap (NULL, (size_t) size + GUARD, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) return -1;
	if (mprotect (p, GUARD, PROT_NONE) != 0) { munmap (p, (size_t) size + GUARD); return -2; }
	stk = p; stksz = size; owns_stack = 1;
	stk_lo = (char *) p + GUARD;
	stk_hi = (char *) p + GUARD + size;
	co_entry = entry; co_arg = arg;
	if (getcontext (&coctx) != 0) return -3;
	coctx.uc_stack.ss_sp = (char *) p + GUARD;
	coctx.uc_stack.ss_size = (size_t) size;
	coctx.uc_link = &mainctx;
	makecontext (&coctx, trampoline, 0);
	ready = 1;
	return 0;
}

int t64_co_create_on (void *base, long size, void *entry, void *arg) {
	if (base == NULL || size < 64 * 1024) return -1;
	stk = base; stksz = size; owns_stack = 0;
	stk_lo = base; stk_hi = (char *) base + size;
	co_entry = entry; co_arg = arg;
	if (getcontext (&coctx) != 0) return -3;
	coctx.uc_stack.ss_sp = base;
	coctx.uc_stack.ss_size = (size_t) size;
	coctx.uc_link = &mainctx;
	makecontext (&coctx, trampoline, 0);
	ready = 1;
	return 0;
}

/* «Сырые» переключения: сохранение/восстановление состояния Kernel.Try
   делает CP-обёртка System_devCo (SwitchSave/SwitchRestore вокруг вызова). */
int t64_co_resume (void) {
	if (!ready) return -1;
	return swapcontext (&mainctx, &coctx);
}

int t64_co_yield (void) {
	return swapcontext (&coctx, &mainctx);
}

void *t64_co_stack_base (void) { return stk; }
void *t64_co_stack_low (void) { return stk_lo; }
void *t64_co_stack_high (void) { return stk_hi; }
long t64_co_stack_size (void) { return stksz; }

int t64_co_release (void) {
	int r = 0;
	if (owns_stack && stk != NULL) {
		r = munmap (stk, (size_t) stksz + GUARD);
	}
	stk = NULL; stk_lo = NULL; stk_hi = NULL; owns_stack = 0; ready = 0;
	return r;
}

/* Монотонные наносекунды для замеров (System_devGcBench). */
long test64_ns (void) {
	struct timespec ts;
	if (clock_gettime (CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long) ts.tv_sec * 1000000000L + (long) ts.tv_nsec;
}

/* Обращения процесса к отсутствующим страницам (minor faults) для строк BENCH. */
long test64_minflt (void) {
	struct rusage ru;
	if (getrusage (RUSAGE_SELF, &ru) != 0) return 0;
	return ru.ru_minflt;
}

/* Число отображений процесса (строк /proc/self/maps): стек задачи со
   сторожевой страницей — два отображения. Чтение низкоуровневое (open/read,
   без malloc): функцию зовут и тогда, когда лимит vm.max_map_count исчерпан,
   а выделение памяти в этот момент само может не удаться. */
long test64_vmas (void) {
	static char buf[65536];
	int fd; ssize_t r; char *p; long n = 0;
	fd = open ("/proc/self/maps", O_RDONLY);
	if (fd < 0) return -1;
	while ((r = read (fd, buf, sizeof buf)) > 0)
		for (p = buf; p < buf + r; p++) if (*p == '\n') n++;
	close (fd);
	return r < 0 ? -1 : n;
}

/* Поле /proc/self/status в байтах (VmSize, VmRSS и т.п.), низкоуровневое
   чтение: на пределе числа отображений память может быть недоступна. */
static long status_bytes (const char *name) {
	static char buf[8192];
	int fd; ssize_t r; char *p; size_t nlen = strlen (name);
	fd = open ("/proc/self/status", O_RDONLY);
	if (fd < 0) return -1;
	r = read (fd, buf, sizeof buf - 1);
	close (fd);
	if (r <= 0) return -1;
	buf[r] = 0;
	p = buf;
	while (p != NULL) {
		if (strncmp (p, name, nlen) == 0) {
			p += nlen;
			while (*p == ' ' || *p == '\t') p++;
			return atol (p) * 1024;
		}
		p = strchr (p, '\n');
		if (p != NULL) p++;
	}
	return -1;
}

long test64_vmsize (void) { return status_bytes ("VmSize:"); }
long test64_vmrss (void) { return status_bytes ("VmRSS:"); }

/* Память процесса в байтах: resident из /proc/self/statm. */
long test64_rss (void) {
	FILE *f = fopen ("/proc/self/statm", "r");
	long size = 0, res = 0;
	if (f == NULL) return -1;
	if (fscanf (f, "%ld %ld", &size, &res) != 2) res = -1;
	fclose (f);
	return res < 0 ? -1 : res * sysconf (_SC_PAGESIZE);
}

/* Окно счётчиков perf stat --control fd:CTL,ACK -D -1: фаза phase замера
   включает (on # 0) или выключает счётчики, если она названа в PERF_PHASE.
   Без PERF_CTL_FD ничего не делает. */
void test64_perf (long phase, long on) {
	const char *c = getenv ("PERF_CTL_FD"), *a = getenv ("PERF_ACK_FD"), *p = getenv ("PERF_PHASE");
	char buf[8];
	if (c == NULL || p == NULL || atol (p) != phase) return;
	if (write (atoi (c), on ? "enable\n" : "disable\n", on ? 7 : 8) < 0) return;
	if (a != NULL && read (atoi (a), buf, sizeof buf) < 0) return;
}
