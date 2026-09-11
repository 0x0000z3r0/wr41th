#include "msg.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
stat_state(void)
{
	char line[1024];
	char *close;
	FILE *stat;

	stat = fopen("/proc/self/stat", "r");
	if (!stat) {
		return -1;
	}
	if (!fgets(line, sizeof(line), stat)) {
		fclose(stat);
		return -1;
	}
	fclose(stat);
	close = strrchr(line, ')');
	if (!close || close[1] != ' ' || !close[2]) {
		return -1;
	}
	return (unsigned char)close[2];
}

int
main(void)
{
	int state;

	printf("pid %d\n", getpid());
	fflush(stdout);
	state = stat_state();
	if (state < 0) {
		return fail("no /proc/self/stat state");
	}
	if (state == 't') {
		return bad("stat t");
	}
	return ok();
}
