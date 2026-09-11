#include "msg.h"

#include <sys/ptrace.h>

int
main(void)
{
	if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
		return bad("PTRACE_TRACEME");
	}
	return ok();
}
