#include "drv.h"
#include "hook.h"

#include <linux/ptrace.h>
#include <linux/sched.h>

struct pt_stash {
	u8 fake;
};

static WR_FENTRY
pt_ent(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct pt_regs *uregs;
	long req, tid;
	struct pt_stash *s = data;

	s->fake = 0;
	if (!tgt_task(current, WR_FEAT_PTRACE)) {
		goto done;
	}

	uregs = (struct pt_regs *)hook_arg(regs, 0);
	if (!uregs) {
		goto done;
	}
#ifdef CONFIG_X86_64
	req = (long)uregs->di;
	tid = (long)uregs->si;
#else
	(void)uregs;
	req = 0;
	tid = 0;
#endif

	if (req == PTRACE_TRACEME) {
		s->fake = 1;
	} else if ((req == PTRACE_ATTACH || req == PTRACE_SEIZE) &&
		   (tid == task_tgid_nr(current) || tid == task_pid_nr(current))) {
		s->fake = 1;
	}
	if (s->fake) {
		wr_dbg("ptrace hide pid=%d req=%ld tid=%ld\n",
		       task_tgid_nr(current), req, tid);
	}

done:
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static void
pt_ex(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct pt_stash *s = data;

	if (!s || !s->fake) {
		return;
	}
	if ((long)hook_ret(regs) == -EPERM) {
		wr_dbg("ptrace fake 0 pid=%d\n", task_tgid_nr(current));
		hook_set_ret(regs, 0);
	}
}

static struct fprobe pt_fp = {
    .entry_handler = pt_ent,
    .exit_handler = pt_ex,
    .entry_data_size = sizeof(struct pt_stash),
};

static bool pt_on;

int
ptrace_init(void)
{
	int err;

	err = hook_reg(&pt_fp, "__x64_sys_ptrace");
	pt_on = !err;
	wr_info("ptrace hook=%d\n", pt_on);
	return 0;
}

void
ptrace_fini(void)
{
	if (pt_on) {
		hook_unreg(&pt_fp);
	}
	pt_on = false;
}
