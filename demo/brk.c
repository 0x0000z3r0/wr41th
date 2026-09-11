#define _GNU_SOURCE
#include "msg.h"

#include <sys/uio.h>
#include <unistd.h>

static void __attribute__((noinline))
mark(void)
{
	__asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop");
}

int
main(void)
{
	unsigned char buf[16];
	struct iovec loc = {.iov_base = buf, .iov_len = sizeof(buf)};
	struct iovec rem = {.iov_base = (void *)mark, .iov_len = sizeof(buf)};

	if (process_vm_readv(getpid(), &loc, 1, &rem, 1, 0) != (ssize_t)sizeof(buf)) {
		return fail("process_vm_readv");
	}

	for (size_t off = 0; off < sizeof(buf); off++) {
		if (buf[off] == 0xcc) {
			return bad("int3");
		}
	}

	return ok();
}
