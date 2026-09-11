#ifndef WR_LOG_H
#define WR_LOG_H

#include <stdarg.h>
#include <stdio.h>

#define WR_CYN "\033[36m"
#define WR_RED "\033[31m"
#define WR_RST "\033[0m"

static inline void
wr_ok(const char *fmt, ...)
{
	va_list ap;

	fputs(WR_CYN "[*]" WR_RST " ", stdout);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static inline void
wr_err(const char *fmt, ...)
{
	va_list ap;

	fputs(WR_RED "[!]" WR_RST " ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

#endif
