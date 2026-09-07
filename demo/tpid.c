#include "msg.h"

#include <stdio.h>
#include <string.h>

int
main(void)
{
	char line[256];
	FILE *f;
	int pid = -1;

	f = fopen("/proc/self/status", "r");
	if (!f)
		return fail("cannot read /proc/self/status");
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "TracerPid:", 10))
			continue;
		sscanf(line + 10, "%d", &pid);
		break;
	}
	fclose(f);
	if (pid < 0)
		return fail("no TracerPid field");
	if (pid)
		return bad("TracerPid");
	return ok();
}
