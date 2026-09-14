#ifndef WR_DEMO_MSG_H
#define WR_DEMO_MSG_H

#include <stdio.h>
#include <stdarg.h>

#define WR_GRN "\033[32m"
#define WR_CYAN "\033[36m"
#define WR_RED "\033[31m"
#define WR_YEL "\033[33m"
#define WR_RST "\033[0m"

static inline int
ok(void)
{
	puts(WR_GRN "[*]" WR_RST " nothing detected");
	return 0;
}

static inline void
info(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf(WR_CYAN  "[*]" WR_RST " ");
	vprintf(fmt, args);
	printf("\n");
	va_end(args);	
}

static inline int
bad(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf(WR_RED "[!]" WR_RST " ");
	vprintf(fmt, args);
	printf("\n");
	va_end(args);
	return 1;
}

static inline int
fail(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	printf(WR_RED "[!]" WR_RST " ");
	vprintf(fmt, args);
	printf("\n");
	va_end(args);
	return 2;
}

#endif
