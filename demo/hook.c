#define _GNU_SOURCE
#include "msg.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

__attribute__((noinline)) void
target_function(void)
{
	puts("PROTECT ME");
}

int
main(void)
{
	Dl_info target_info;
	if (!dladdr((void *)target_function, &target_info)) {
		return bad("dladdr failed");
	}

	const char *module_path = target_info.dli_fname;
	info("module: %s", module_path);

	int fd = open(module_path, O_RDONLY);
	if (fd < 0) {
		return bad("open");
	}

	struct stat st;
	if (fstat(fd, &st)) {
		return bad("fstat");
	}

	void *file_base = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file_base == MAP_FAILED) {
		return bad("mmap");
	}

	uintptr_t runtime_addr = (uintptr_t)target_function;
	uintptr_t module_base = (uintptr_t)target_info.dli_fbase;

	uintptr_t func_rva = runtime_addr - module_base;

	unsigned char *disk_bytes = (unsigned char *)file_base + func_rva;
	unsigned char *mem_bytes = (unsigned char *)runtime_addr;

	const size_t check_len = 16;
	if (memcmp(mem_bytes, disk_bytes, check_len) != 0) {
		return bad("function appears modified (possible inline hook)");
	}

	munmap(file_base, st.st_size);
	close(fd);

	return ok();
}