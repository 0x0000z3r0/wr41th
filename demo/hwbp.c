#include "msg.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#define DR_OFFSET(rid) (offsetof(struct user, u_debugreg[rid]))

void
protected_logic()
{
	info("{child} executing sensitive operations safely...");
}

int
main()
{
	pid_t child_pid = fork();
	if (child_pid < 0) {
		return bad("fork failed");
	}

	if (child_pid == 0) {
		if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
			perror("PTRACE_TRACEME failed");
			exit(1);
		}

		raise(SIGSTOP);

		for (int i = 0; i < 3; i++) {
			protected_logic();
			sleep(2);
		}
	} else {
		int status;
		waitpid(child_pid, &status, 0);

		if (ptrace(PTRACE_CONT, child_pid, NULL, NULL) < 0) {
			return bad("PTRACE_CONT failed");
		}

		info("{parent} actively scanning for hardware breakpoints...");
		while (1) {
			pid_t who = waitpid(child_pid, &status, WNOHANG);
			if (who == child_pid) {
				if (WIFEXITED(status) || WIFSIGNALED(status)) {
					info("{parent} child terminated");
					break;
				}
			}

			int hwbp_detected = 0;
			for (int i = 0; i < 4; i++) {
				unsigned long dr_value = ptrace(PTRACE_PEEKUSER, child_pid, (void *)DR_OFFSET(i), NULL);

				if (dr_value != 0UL) {
					bad("{parent} hardware breakpoint detected in DR%d at address: 0x%lx", i, dr_value);
					hwbp_detected = 1;
				}
			}

			unsigned long dr7_value = ptrace(PTRACE_PEEKUSER, child_pid, (void *)DR_OFFSET(7), NULL);
			if (dr7_value != ~0UL) {
				bad("{parent} hardware breakpoint control bits active in DR7: 0x%lx", dr7_value);
				hwbp_detected = 1;
			}

			if (hwbp_detected) {
				kill(child_pid, SIGKILL);
				return bad("{parent} terminating tampered process");
			}

			usleep(100000);
		}
	}

	return ok();
}
