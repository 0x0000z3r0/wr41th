#include "msg.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
tracer(void)
{
	char line[256];
	FILE *f;
	int pid = -1;

	f = fopen("/proc/self/status", "r");
	if (!f) {
		return -1;
	}
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "TracerPid:", 10)) {
			continue;
		}
		sscanf(line + 10, "%d", &pid);
		break;
	}
	fclose(f);
	return pid;
}

int
main(void)
{
	int i, pid;

	printf("pid %d\n", getpid());
	fflush(stdout);
	for (i = 0; i < 15; i++) {
		pid = tracer();
		if (pid < 0) {
			return fail("no TracerPid field");
		}
		if (!pid) {
			return ok();
		}
		sleep(1);
	}
	return bad("TracerPid");
}
