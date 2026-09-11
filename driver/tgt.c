#include "drv.h"

#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#define WR_BP_MAX 256

struct wr_bp {
	unsigned long addr;
	u8 orig;
	u8 used;
};

struct wr_tgt {
	struct task_struct *task;
	struct mm_struct *mm;
	u32 feats;
	struct wr_bp bps[WR_BP_MAX];
};

static DEFINE_XARRAY(tgts);
static DEFINE_MUTEX(lock);

static bool
tgt_live(struct wr_tgt *t, pid_t pid)
{
	if (!t || !t->task)
		return false;
	if (!pid_alive(t->task) || task_tgid_nr(t->task) != pid)
		return false;
	return true;
}

static void
tgt_free(struct wr_tgt *t)
{
	if (t->mm)
		mmput(t->mm);
	if (t->task)
		put_task_struct(t->task);
	kfree(t);
}

static struct wr_tgt *
tgt_find(pid_t pid)
{
	if (pid <= 0)
		return NULL;
	return xa_load(&tgts, (unsigned long)pid);
}

int
tgt_add(pid_t pid, u32 feats)
{
	struct wr_tgt *t, *old;
	struct task_struct *task;
	struct mm_struct *mm;
	int err;

	if (pid <= 0)
		return -EINVAL;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;
	t->feats = feats;

	rcu_read_lock();
	task = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (task)
		get_task_struct(task);
	mm = task ? get_task_mm(task) : NULL;
	rcu_read_unlock();
	if (!task || !mm) {
		wr_warn("tgt add pid=%d no task/mm\n", pid);
		if (task)
			put_task_struct(task);
		if (mm)
			mmput(mm);
		kfree(t);
		return -ESRCH;
	}
	t->task = task;
	t->mm = mm;

	mutex_lock(&lock);
	old = xa_load(&tgts, (unsigned long)pid);
	if (old) {
		old->feats = feats;
		if (old->task != task) {
			put_task_struct(old->task);
			old->task = task;
			t->task = NULL;
		}
		if (old->mm != mm) {
			mmput(old->mm);
			old->mm = mm;
			t->mm = NULL;
		}
		mutex_unlock(&lock);
		wr_info("tgt upd pid=%d comm=%s feats=0x%x\n", pid, task->comm, feats);
		tgt_free(t);
		return 0;
	}
	err = xa_err(xa_store(&tgts, (unsigned long)pid, t, GFP_KERNEL));
	mutex_unlock(&lock);
	if (err) {
		wr_warn("tgt store pid=%d err=%d\n", pid, err);
		tgt_free(t);
	} else {
		wr_info("tgt add pid=%d comm=%s feats=0x%x\n", pid, task->comm, feats);
	}
	return err;
}

static void
tgt_drop_all(void)
{
	unsigned long idx;
	struct wr_tgt *t;

	xa_for_each(&tgts, idx, t)
	{
		wr_info("tgt drop pid=%lu comm=%s feats=0x%x\n", idx,
			t->task ? t->task->comm : "?", t->feats);
		xa_erase(&tgts, idx);
		tgt_free(t);
	}
}

int
tgt_del(pid_t pid)
{
	struct wr_tgt *t;

	if (pid == 0)
		return tgt_clear();
	mutex_lock(&lock);
	t = xa_erase(&tgts, (unsigned long)pid);
	mutex_unlock(&lock);
	if (!t)
		return -ESRCH;
	wr_info("tgt del pid=%d comm=%s feats=0x%x\n", pid,
		t->task ? t->task->comm : "?", t->feats);
	tgt_free(t);
	return 0;
}

int
tgt_clear(void)
{
	mutex_lock(&lock);
	tgt_drop_all();
	mutex_unlock(&lock);
	wr_info("tgt clear\n");
	return 0;
}

int
tgt_set(pid_t pid, u32 feats)
{
	struct wr_tgt *t;
	int err = 0;

	mutex_lock(&lock);
	t = tgt_find(pid);
	if (!t || !tgt_live(t, pid))
		err = -ESRCH;
	else
		t->feats = feats;
	mutex_unlock(&lock);
	if (!err)
		wr_info("tgt set pid=%d feats=0x%x\n", pid, feats);
	return err;
}

int
tgt_get(pid_t pid, u32 *feats)
{
	struct wr_tgt *t;
	int err = 0;

	mutex_lock(&lock);
	t = tgt_find(pid);
	if (!t || !tgt_live(t, pid))
		err = -ESRCH;
	else if (feats)
		*feats = t->feats;
	mutex_unlock(&lock);
	return err;
}

int
tgt_list(struct wr_req *ents, u32 max, u32 *n)
{
	unsigned long idx;
	struct wr_tgt *t;
	u32 i = 0;

	if (!n)
		return -EINVAL;
	mutex_lock(&lock);
	xa_for_each(&tgts, idx, t)
	{
		if (i >= max)
			break;
		if (ents) {
			ents[i].pid = (__s32)idx;
			ents[i].feats = t->feats;
		}
		i++;
	}
	mutex_unlock(&lock);
	*n = i;
	wr_dbg("tgt list n=%u\n", i);
	return 0;
}

bool
tgt_has(pid_t pid, u32 feat)
{
	struct wr_tgt *t;
	bool ok = false;

	if (pid <= 0)
		return false;
	rcu_read_lock();
	t = tgt_find(pid);
	if (t && tgt_live(t, pid))
		ok = (t->feats & feat) == feat;
	rcu_read_unlock();
	return ok;
}

bool
tgt_task(struct task_struct *task, u32 feat)
{
	if (!task)
		return false;
	return tgt_has(task_tgid_nr(task), feat);
}

bool
tgt_mm(struct mm_struct *mm, u32 feat, pid_t *pid)
{
	unsigned long idx;
	struct wr_tgt *t;
	bool ok = false;

	if (!mm)
		return false;
	mutex_lock(&lock);
	xa_for_each(&tgts, idx, t)
	{
		if (t->mm == mm && tgt_live(t, (pid_t)idx) && (t->feats & feat) == feat) {
			if (pid)
				*pid = (pid_t)idx;
			ok = true;
			break;
		}
	}
	mutex_unlock(&lock);
	return ok;
}

void
tgt_bp_set(pid_t pid, unsigned long addr, u8 orig)
{
	struct wr_tgt *t;
	int i, slot = -1;

	mutex_lock(&lock);
	t = tgt_find(pid);
	if (!t)
		goto out;
	for (i = 0; i < WR_BP_MAX; i++) {
		if (t->bps[i].used && t->bps[i].addr == addr) {
			mutex_unlock(&lock);
			return;
		}
		if (!t->bps[i].used && slot < 0)
			slot = i;
	}
	if (slot >= 0) {
		t->bps[slot].addr = addr;
		t->bps[slot].orig = orig;
		t->bps[slot].used = 1;
		wr_info("bp set pid=%d addr=0x%lx orig=0x%02x slot=%d\n",
			pid, addr, orig, slot);
	} else {
		wr_warn("bp full pid=%d addr=0x%lx\n", pid, addr);
	}
out:
	mutex_unlock(&lock);
}

bool
tgt_bp_get(pid_t pid, unsigned long addr, u8 *orig)
{
	struct wr_tgt *t;
	int i;
	bool ok = false;

	mutex_lock(&lock);
	t = tgt_find(pid);
	if (!t)
		goto out;
	for (i = 0; i < WR_BP_MAX; i++) {
		if (t->bps[i].used && t->bps[i].addr == addr) {
			if (orig)
				*orig = t->bps[i].orig;
			ok = true;
			break;
		}
	}
out:
	mutex_unlock(&lock);
	return ok;
}

int
tgt_init(void)
{
	return 0;
}

void
tgt_fini(void)
{
	tgt_clear();
	xa_destroy(&tgts);
}
