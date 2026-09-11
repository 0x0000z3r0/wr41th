#include "msg.h"

#include <sys/ptrace.h>
#include <unistd.h>

int
main(void)
{
	if (ptrace(PTRACE_ATTACH, getpid(), 0, 0) < 0) {
		return bad("PTRACE_ATTACH");
	}
	return ok();
}
