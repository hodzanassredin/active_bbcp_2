/* Sampling profiler for BlackBox, loaded with LD_PRELOAD.
   A SIGPROF timer fires every BBPROF_US microseconds of CPU time (default 200);
   the handler appends the interrupted instruction address (8 bytes) to BBPROF_OUT.
   Addresses are resolved by the BlackBox module that prints the module map. */
#define _GNU_SOURCE
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <ucontext.h>

static int fd = -1;

static void on_prof(int sig, siginfo_t *si, void *uc_)
{
	ucontext_t *uc = (ucontext_t *)uc_;
	unsigned long long pc = (unsigned long long)uc->uc_mcontext.gregs[REG_RIP];
	if (fd >= 0) { ssize_t r = write(fd, &pc, sizeof pc); (void)r; }
}

__attribute__((constructor)) static void start(void)
{
	const char *out = getenv("BBPROF_OUT");
	const char *us = getenv("BBPROF_US");
	struct sigaction sa;
	struct itimerval it;
	long period = us ? atol(us) : 200;
	if (!out) return;
	fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = on_prof;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGPROF, &sa, NULL);
	it.it_interval.tv_sec = 0; it.it_interval.tv_usec = period;
	it.it_value = it.it_interval;
	setitimer(ITIMER_PROF, &it, NULL);
}
