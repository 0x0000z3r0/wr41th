#include "drv.h"
#include "hook.h"

#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#define WR_TPID "TracerPid:"

struct proc_stash {
	struct seq_file *m;
};

static void
proc_fix(struct seq_file *m)
{
	char *p, *e, *nl;

	if (!m || !m->buf || !m->count || m->count > m->size) {
		return;
	}
	if (seq_has_overflowed(m)) {
		return;
	}
	p = strnstr(m->buf, WR_TPID, m->count);
	if (!p) {
		return;
	}
	p += sizeof(WR_TPID) - 1;
	e = m->buf + m->count;
	while (p < e && (*p == ' ' || *p == '\t')) {
		p++;
	}
	nl = p;
	while (nl < e && *nl != '\n') {
		nl++;
	}
	if (nl == p) {
		return;
	}
	*p++ = '0';
	while (p < nl) {
		*p++ = ' ';
	}
	wr_dbg("proc hide TracerPid\n");
}

static WR_FENTRY
proc_ent(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct proc_stash *s = data;
	struct task_struct *task = (struct task_struct *)hook_arg(regs, 3);

	s->m = NULL;
	if (task && (tgt_task(task, WR_FEAT_PROC) || tgt_task(current, WR_FEAT_PROC))) {
		s->m = (struct seq_file *)hook_arg(regs, 0);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static void
proc_ex(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct proc_stash *s = data;

	if (s->m) {
		proc_fix(s->m);
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
