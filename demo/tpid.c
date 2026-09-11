#include "msg.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
tracer(void)
{
	char line[256];
	FILE *status;
	int pid = -1;

	status = fopen("/proc/self/status", "r");
	if (!status) {
		return -1;
	}
	while (fgets(line, sizeof(line), status)) {
		if (strncmp(line, "TracerPid:", 10)) {
			continue;
		}
		sscanf(line + 10, "%d", &pid);
		break;
	}
	fclose(status);
	return pid;
}

int
main(void)
{
	printf("pid %d\n", getpid());
	fflush(stdout);
	int pid = tracer();
	if (pid < 0) {
		return fail("no TracerPid field");
	}
	if (!pid) {
		return ok();
	}
	return bad("TracerPid");
}
