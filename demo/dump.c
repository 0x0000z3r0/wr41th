#include "msg.h"

#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

int
main(void)
{
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) == -1) {
		return bad("PR_SET_DUMPABLE");
	}
    sleep(10);
	return ok();
}