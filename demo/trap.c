#include "msg.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

volatile sig_atomic_t executed = 0;

void
sigtrap_handler(int signo)
{
    (void)signo;
	executed = 1;
}

int
main()
{
	if (signal(SIGTRAP, sigtrap_handler) == SIG_ERR) {
		return bad("signal");
	}

	__asm__ __volatile__("int3");

	signal(SIGTRAP, SIG_DFL);

	if (!executed) {
		return bad("SIGTRAP not executed");
	}

	return ok();
}
