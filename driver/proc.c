#include "drv.h"
#include "hook.h"

#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#define WR_TPID "TracerPid:"

struct proc_stash {
	struct seq_file *seq;
};

static void
proc_fix(struct seq_file *seq)
{
	char *pos, *end, *nl;

	if (!seq || !seq->buf || !seq->count || seq->count > seq->size) {
		return;
	}
	if (seq_has_overflowed(seq)) {
		return;
	}
	pos = strnstr(seq->buf, WR_TPID, seq->count);
	if (!pos) {
		return;
	}
	pos += sizeof(WR_TPID) - 1;
	end = seq->buf + seq->count;
	while (pos < end && (*pos == ' ' || *pos == '\t')) {
		pos++;
	}
	nl = pos;
	while (nl < end && *nl != '\n') {
		nl++;
	}
	if (nl == pos) {
		return;
	}
	*pos++ = '0';
	while (pos < nl) {
		*pos++ = ' ';
	}
	wr_dbg("proc hide TracerPid\n");
}

static WR_FENTRY
proc_ent(struct fprobe *probe, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct proc_stash *stash = data;
	struct task_struct *task = (struct task_struct *)hook_arg(regs, 3);

	(void)probe;
	(void)ip;
	(void)rip;
	stash->seq = NULL;
	if (task && (tgt_task(task, WR_FEAT_PROC) || tgt_task(current, WR_FEAT_PROC))) {
		stash->seq = (struct seq_file *)hook_arg(regs, 0);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static void
proc_ex(struct fprobe *probe, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct proc_stash *stash = data;

	(void)probe;
	(void)ip;
	(void)rip;
	(void)regs;
	if (stash->seq) {
		proc_fix(stash->seq);
	}
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
	if (status_on) {
		hook_unreg(&status_fp);
	}
	if (stat_on) {
		hook_unreg(&stat_fp);
	}
	status_on = false;
	stat_on = false;
}
