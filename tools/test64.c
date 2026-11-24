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
