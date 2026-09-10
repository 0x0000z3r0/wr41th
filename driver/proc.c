#include "drv.h"
#include "hook.h"

#include <linux/sched.h>

struct proc_stash {
	struct task_struct *task;
	unsigned long ptrace;
};

static void
proc_hide(struct proc_stash *s, struct task_struct *task)
{
	s->task = NULL;
	if (!task)
		return;
	if (!tgt_task(task, WR_FEAT_PROC) && !tgt_task(current, WR_FEAT_PROC))
		return;
	s->task = task;
	s->ptrace = task->ptrace;
	wr_dbg("proc hide pid=%d comm=%s ptrace=0x%lx by=%d\n",
	       task_tgid_nr(task), task->comm, s->ptrace, task_tgid_nr(current));
	task->ptrace = 0;
}

static void
proc_show(struct proc_stash *s)
{
	if (s->task) {
		s->task->ptrace = s->ptrace;
		s->task = NULL;
	}
}

static WR_FENTRY
proc_ent(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	proc_hide(data, (struct task_struct *)hook_arg(regs, 3));
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static void
proc_ex(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	proc_show(data);
}

static struct fprobe stat_fp = {
    .entry_handler = proc_ent,
    .exit_handler = proc_ex,
    .entry_data_size = sizeof(struct proc_stash),
};

static struct fprobe status_fp = {
    .entry_handler = proc_ent,
    .exit_handler = proc_ex,
    .entry_data_size = sizeof(struct proc_stash),
};

static bool stat_on;
static bool status_on;

int
proc_init(void)
{
	status_on = !hook_reg(&status_fp, "proc_pid_status");
	stat_on = !hook_reg(&stat_fp, "proc_pid_stat");
	wr_info("proc status=%d stat=%d\n", status_on, stat_on);
	return 0;
}

void
proc_fini(void)
{
	if (status_on)
		hook_unreg(&status_fp);
	if (stat_on)
		hook_unreg(&stat_fp);
	status_on = false;
	stat_on = false;
}
