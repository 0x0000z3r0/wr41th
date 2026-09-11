#include "msg.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
status_state(void)
{
	char line[256];
	char *pos;
	FILE *status;

	status = fopen("/proc/self/status", "r");
	if (!status) {
		return -1;
	}
	while (fgets(line, sizeof(line), status)) {
		if (strncmp(line, "State:", 6)) {
			continue;
		}
		pos = line + 6;
		while (*pos == ' ' || *pos == '\t') {
			pos++;
		}
		fclose(status);
		if (!*pos) {
			return -1;
		}
		return (unsigned char)*pos;
	}
	fclose(status);
	return -1;
}

int
main(void)
{
	int state;

	printf("pid %d\n", getpid());
	fflush(stdout);
	state = status_state();
	if (state < 0) {
		return fail("no State field");
	}
	if (state == 't') {
		return bad("State t");
	}
	return ok();
}
