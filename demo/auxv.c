#include "msg.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>

int
main(void)
{
	int detected = 0;

	unsigned long vdso = getauxval(AT_SYSINFO_EHDR);
	if (!vdso) {
		bad("AT_SYSINFO_EHDR missing");
		detected = 1;
	}

	unsigned long base = getauxval(AT_BASE);
	if (!base) {
		bad("AT_BASE missing (static binary?)");
		detected = 1;
	}

	char *platform = (char *)getauxval(AT_PLATFORM);
	if (!platform) {
		bad("AT_PLATFORM missing");
		detected = 1;
	}

	unsigned char *rnd = (unsigned char *)getauxval(AT_RANDOM);
	if (!rnd) {
		bad("AT_RANDOM missing");
		detected = 1;
	} else {
		int all_zero = 1;

		for (int i = 0; i < 16; ++i) {
			if (rnd[i] != 0) {
				all_zero = 0;
				break;
			}
		}

		if (all_zero) {
			bad("suspicious AT_RANDOM contents");
			detected = 1;
		}
	}

	unsigned long secure = getauxval(AT_SECURE);
	if (secure) {
		bad("AT_SECURE set");
		detected = 1;
	}

	unsigned long phdr = getauxval(AT_PHDR);
	unsigned long phnum = getauxval(AT_PHNUM);
	unsigned long phent = getauxval(AT_PHENT);

	if (!phdr || !phnum || !phent) {
		bad("malformed program-header AUXV entries");
		detected = 1;
	}

	char *execfn = (char *)getauxval(AT_EXECFN);
	if (!execfn) {
		bad("AT_EXECFN missing");
		detected = 1;
	}

	if (detected) {
		return fail("suspicious AUXV environment detected");
	}

	return ok();
}