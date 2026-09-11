#include "drv.h"
#include "hook.h"

#include <linux/atomic.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#define WR_TPID "TracerPid:"
#define WR_STATE "State:"
#define WR_SLEEP "S (sleeping)"
#define WR_PT_HIDE_MAX 32

struct proc_stash {
	struct seq_file *seq;
	struct task_struct *task;
};

struct wchan_stash {
	struct task_struct *task;
};

struct proc_hide {
	struct task_struct *task;
	unsigned long flags;
	int refs;
};

static struct proc_hide hides[WR_PT_HIDE_MAX];
static DEFINE_SPINLOCK(hide_lock);
static atomic_t hide_live = ATOMIC_INIT(0);

static bool
proc_dying(struct task_struct *task)
{
	if (!task || !pid_alive(task)) {
		return true;
	}
	if (task->exit_state || (task->flags & PF_EXITING)) {
		return true;
	}
	return false;
}

static bool
proc_want(struct task_struct *task)
{
	return task && (tgt_task(task, WR_FEAT_PROC) || tgt_task(current, WR_FEAT_PROC));
}

static void
proc_pt_hide(struct task_struct *task)
{
	unsigned long irq;
	int slot, empty = -1, found = -1;

	if (!task || !READ_ONCE(task->ptrace) || proc_dying(task)) {
		return;
	}
	spin_lock_irqsave(&hide_lock, irq);
	for (slot = 0; slot < WR_PT_HIDE_MAX; slot++) {
		if (hides[slot].task == task) {
			found = slot;
			break;
		}
		if (empty < 0 && !hides[slot].task) {
			empty = slot;
		}
	}
	if (found >= 0) {
		hides[found].refs++;
	} else if (empty >= 0) {
		hides[empty].task = task;
		hides[empty].flags = READ_ONCE(task->ptrace);
		hides[empty].refs = 1;
		WRITE_ONCE(task->ptrace, 0);
		atomic_inc(&hide_live);
	}
	spin_unlock_irqrestore(&hide_lock, irq);
}

static void
proc_pt_restore(struct task_struct *task, bool force)
{
	unsigned long irq, saved = 0;
	int slot;

	if (!task) {
		return;
	}
	spin_lock_irqsave(&hide_lock, irq);
	for (slot = 0; slot < WR_PT_HIDE_MAX; slot++) {
		if (hides[slot].task != task) {
			continue;
		}
		if (!force && hides[slot].refs > 1) {
			hides[slot].refs--;
			break;
		}
		saved = hides[slot].flags;
		hides[slot].task = NULL;
		hides[slot].flags = 0;
		hides[slot].refs = 0;
		WRITE_ONCE(task->ptrace, READ_ONCE(task->ptrace) | saved);
		atomic_dec(&hide_live);
		break;
	}
	spin_unlock_irqrestore(&hide_lock, irq);
}

static size_t
proc_span(struct seq_file *seq)
{
	if (!seq || !seq->buf || !seq->size) {
		return 0;
	}
	if (seq->count && seq->count <= seq->size) {
		return seq->count;
	}
	return seq->size;
}

static void
proc_fix_tpid(char *buf, size_t count)
{
	char *pos, *end, *nl;

	pos = strnstr(buf, WR_TPID, count);
	if (!pos) {
		return;
	}
	pos += sizeof(WR_TPID) - 1;
	end = buf + count;
	while (pos < end && (*pos == ' ' || *pos == '\t')) {
		pos++;
	}
	nl = pos;
	while (nl < end && *nl != '\n') {
		nl++;
	}
	if (nl == pos || nl == end) {
		return;
	}
	*pos++ = '0';
	while (pos < nl) {
		*pos++ = ' ';
	}
	wr_dbg("proc hide TracerPid\n");
}

static void
proc_fix_status_state(char *buf, size_t count)
{
	char *pos, *end, *nl;
	size_t old_len, new_len, pad;

	pos = strnstr(buf, WR_STATE, count);
	if (!pos) {
		return;
	}
	pos += sizeof(WR_STATE) - 1;
	end = buf + count;
	while (pos < end && (*pos == ' ' || *pos == '\t')) {
		pos++;
	}
	nl = pos;
	while (nl < end && *nl != '\n') {
		nl++;
	}
	if (nl == end) {
		return;
	}
	old_len = (size_t)(nl - pos);
	if (old_len < 1 || *pos != 't') {
		return;
	}
	new_len = sizeof(WR_SLEEP) - 1;
	if (new_len > old_len) {
		*pos = 'S';
		wr_dbg("proc hide State t\n");
		return;
	}
	memcpy(pos, WR_SLEEP, new_len);
	for (pad = new_len; pad < old_len; pad++) {
		pos[pad] = ' ';
	}
	wr_dbg("proc hide State tracing stop\n");
}

static void
proc_fix_stat_state(char *buf, size_t count)
{
	char *end = buf + count;
	char *close = NULL;
	char *cur;

	if (strnstr(buf, WR_STATE, count) || strnstr(buf, WR_TPID, count)) {
		return;
	}
	for (cur = buf; cur < end; cur++) {
		if (*cur == ')') {
			close = cur;
		}
	}
	if (!close || close + 2 >= end || close[1] != ' ' || close[2] != 't') {
		return;
	}
	if (close + 3 < end && close[3] != ' ' && close[3] != '\n') {
		return;
	}
	close[2] = 'S';
	wr_dbg("proc hide stat state t\n");
}

static void
proc_fix(struct seq_file *seq)
{
	size_t span = proc_span(seq);

	if (!span) {
		return;
	}
	proc_fix_tpid(seq->buf, span);
	proc_fix_status_state(seq->buf, span);
	proc_fix_stat_state(seq->buf, span);
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
	stash->task = NULL;
	if (proc_want(task)) {
		stash->seq = (struct seq_file *)hook_arg(regs, 0);
		stash->task = task;
		proc_pt_hide(task);
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
	if (stash->task) {
		proc_pt_restore(stash->task, false);
	}
}

static WR_FENTRY
proc_rel_ent(struct fprobe *probe, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	(void)probe;
	(void)ip;
	(void)rip;
	(void)data;
	if (atomic_read(&hide_live)) {
		proc_pt_restore((struct task_struct *)hook_arg(regs, 0), true);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static WR_FENTRY
wchan_ent(struct fprobe *probe, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct wchan_stash *stash = data;
	struct task_struct *task = (struct task_struct *)hook_arg(regs, 0);

	(void)probe;
	(void)ip;
	(void)rip;
	stash->task = proc_want(task) ? task : NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static void
wchan_ex(struct fprobe *probe, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct wchan_stash *stash = data;

	(void)probe;
	(void)ip;
	(void)rip;
	if (stash->task) {
		hook_set_ret(regs, 0);
	}
}

static struct fprobe status_fp = {
    .entry_handler = proc_ent,
    .exit_handler = proc_ex,
    .entry_data_size = sizeof(struct proc_stash),
};

static struct fprobe stat_fp = {
    .entry_handler = proc_ent,
    .exit_handler = proc_ex,
    .entry_data_size = sizeof(struct proc_stash),
};

static struct fprobe tid_fp = {
    .entry_handler = proc_ent,
    .exit_handler = proc_ex,
    .entry_data_size = sizeof(struct proc_stash),
};

static struct fprobe rel_fp = {
    .entry_handler = proc_rel_ent,
};

static struct fprobe wchan_fp = {
    .entry_handler = wchan_ent,
    .exit_handler = wchan_ex,
    .entry_data_size = sizeof(struct wchan_stash),
};

static bool status_on;
static bool stat_on;
static bool tid_on;
static bool rel_on;
static bool wchan_on;

int
proc_init(void)
{
	status_on = !hook_reg(&status_fp, "task_state");
	if (!status_on) {
		status_on = !hook_reg(&status_fp, "proc_pid_status");
	}
	stat_on = !hook_reg(&stat_fp, "do_task_stat");
	if (!stat_on) {
		stat_on = !hook_reg(&stat_fp, "proc_tgid_stat");
		tid_on = !hook_reg(&tid_fp, "proc_tid_stat");
	}
	rel_on = !hook_reg(&rel_fp, "release_task");
	wchan_on = !hook_reg(&wchan_fp, "get_wchan");
	wr_info("proc status=%d stat=%d tid=%d rel=%d wchan=%d\n",
		status_on, stat_on, tid_on, rel_on, wchan_on);
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
	if (tid_on) {
		hook_unreg(&tid_fp);
	}
	if (rel_on) {
		hook_unreg(&rel_fp);
	}
	if (wchan_on) {
		hook_unreg(&wchan_fp);
	}
	status_on = false;
	stat_on = false;
	tid_on = false;
	rel_on = false;
	wchan_on = false;
}
