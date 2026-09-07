#ifndef WR_DEMO_MSG_H
#define WR_DEMO_MSG_H

#include <stdio.h>

#define WR_GRN "\033[32m"
#define WR_RED "\033[31m"
#define WR_RST "\033[0m"

static inline int
ok(void)
{
	puts(WR_GRN "[*]" WR_RST " nothing detected");
	return 0;
}

static inline int
bad(const char *feat)
{
	printf(WR_RED "[!]" WR_RST " %s detected\n", feat);
	return 1;
}

static inline int
fail(const char *msg)
{
	printf(WR_RED "[!]" WR_RST " %s\n", msg);
	return 2;
}

#endif
