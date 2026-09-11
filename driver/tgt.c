#include "drv.h"

#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
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
	struct wr_tgt *dead;
	u32 feats;
	struct wr_bp bps[WR_BP_MAX];
};

static DEFINE_XARRAY(tgts);
static DEFINE_MUTEX(lock);
static DEFINE_SPINLOCK(bplock);

static bool
tgt_live(struct wr_tgt *tgt, pid_t pid)
{
	if (!tgt || !tgt->task) {
		return false;
	}
	if (!pid_alive(tgt->task) || task_tgid_nr(tgt->task) != pid) {
		return false;
	}
	return true;
}

static void
tgt_free(struct wr_tgt *tgt)
{
	if (tgt->mm) {
		mmput(tgt->mm);
	}
	if (tgt->task) {
		put_task_struct(tgt->task);
	}
	kfree(tgt);
}

static void
tgt_reclaim(struct wr_tgt *tgt)
{
	if (!tgt) {
		return;
	}
	synchronize_rcu();
	tgt_free(tgt);
}

static struct wr_tgt *
tgt_find(pid_t pid)
{
	if (pid <= 0) {
		return NULL;
	}
	return xa_load(&tgts, (unsigned long)pid);
}

int
tgt_add(pid_t pid, u32 feats)
{
	struct wr_tgt *tgt, *old;
	struct task_struct *task;
	struct mm_struct *mm;
	int err;

	if (pid <= 0) {
		return -EINVAL;
	}

	tgt = kzalloc(sizeof(*tgt), GFP_KERNEL);
	if (!tgt) {
		return -ENOMEM;
	}
	tgt->feats = feats;

	rcu_read_lock();
	task = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (task) {
		get_task_struct(task);
	}
	mm = task ? get_task_mm(task) : NULL;
	rcu_read_unlock();
	if (!task || !mm) {
		wr_warn("tgt add pid=%d no task/mm\n", pid);
		if (task) {
			put_task_struct(task);
		}
		if (mm) {
			mmput(mm);
		}
		kfree(tgt);
		return -ESRCH;
	}
	tgt->task = task;
	tgt->mm = mm;

	mutex_lock(&lock);
	old = xa_load(&tgts, (unsigned long)pid);
	if (old) {
		struct task_struct *drop_task = NULL;
		struct mm_struct *drop_mm = NULL;

		old->feats = feats;
		if (old->task != task) {
			drop_task = old->task;
			old->task = task;
			tgt->task = NULL;
		}
		if (old->mm != mm) {
			drop_mm = old->mm;
			old->mm = mm;
			tgt->mm = NULL;
		}
		mutex_unlock(&lock);
		if (drop_task || drop_mm) {
			synchronize_rcu();
			if (drop_task) {
				put_task_struct(drop_task);
			}
			if (drop_mm) {
				mmput(drop_mm);
			}
		}
		wr_info("tgt upd pid=%d comm=%s feats=0x%x\n", pid, task->comm, feats);
		tgt_free(tgt);
		return 0;
	}
	err = xa_err(xa_store(&tgts, (unsigned long)pid, tgt, GFP_KERNEL));
	mutex_unlock(&lock);
	if (err) {
		wr_warn("tgt store pid=%d err=%d\n", pid, err);
		tgt_free(tgt);
	} else {
		wr_info("tgt add pid=%d comm=%s feats=0x%x\n", pid, task->comm, feats);
	}
	return err;
}

int
tgt_del(pid_t pid)
{
	struct wr_tgt *tgt;

	if (pid == 0) {
		return tgt_clear();
	}
	mutex_lock(&lock);
	tgt = xa_erase(&tgts, (unsigned long)pid);
	mutex_unlock(&lock);
	if (!tgt) {
		return -ESRCH;
	}
	wr_info("tgt del pid=%d comm=%s feats=0x%x\n", pid,
		tgt->task ? tgt->task->comm : "?", tgt->feats);
	tgt_reclaim(tgt);
	return 0;
}

int
tgt_clear(void)
{
	struct wr_tgt *dead = NULL, *tgt, *next;
	unsigned long idx;

	mutex_lock(&lock);
	xa_for_each(&tgts, idx, tgt)
	{
		wr_info("tgt drop pid=%lu comm=%s feats=0x%x\n", idx,
			tgt->task ? tgt->task->comm : "?", tgt->feats);
		xa_erase(&tgts, idx);
		tgt->dead = dead;
		dead = tgt;
	}
	mutex_unlock(&lock);
	if (dead) {
		synchronize_rcu();
		while (dead) {
			next = dead->dead;
			tgt_free(dead);
			dead = next;
		}
	}
	wr_info("tgt clear\n");
	return 0;
}

int
tgt_set(pid_t pid, u32 feats)
{
	struct wr_tgt *tgt;
	int err = 0;

	mutex_lock(&lock);
	tgt = tgt_find(pid);
	if (!tgt || !tgt_live(tgt, pid)) {
		err = -ESRCH;
	} else {
		tgt->feats = feats;
	}
	mutex_unlock(&lock);
	if (!err) {
		wr_info("tgt set pid=%d feats=0x%x\n", pid, feats);
	}
	return err;
}

int
tgt_get(pid_t pid, u32 *feats)
{
	struct wr_tgt *tgt;
	int err = 0;

	mutex_lock(&lock);
	tgt = tgt_find(pid);
	if (!tgt || !tgt_live(tgt, pid)) {
		err = -ESRCH;
	} else if (feats) {
		*feats = tgt->feats;
	}
	mutex_unlock(&lock);
	return err;
}

int
tgt_list(struct wr_req *ents, u32 max, u32 *nents)
{
	unsigned long idx;
	struct wr_tgt *tgt;
	u32 count = 0;

	if (!nents) {
		return -EINVAL;
	}
	mutex_lock(&lock);
	xa_for_each(&tgts, idx, tgt)
	{
		if (count >= max) {
			break;
		}
		if (ents) {
			ents[count].pid = (__s32)idx;
			ents[count].feats = tgt->feats;
		}
		count++;
	}
	mutex_unlock(&lock);
	*nents = count;
	wr_dbg("tgt list n=%u\n", count);
	return 0;
}

bool
tgt_has(pid_t pid, u32 feat)
{
	struct wr_tgt *tgt;
	bool found = false;

	if (pid <= 0) {
		return false;
	}
	rcu_read_lock();
	tgt = tgt_find(pid);
	if (tgt && tgt_live(tgt, pid)) {
		found = (tgt->feats & feat) == feat;
	}
	rcu_read_unlock();
	return found;
}

bool
tgt_task(struct task_struct *task, u32 feat)
{
	if (!task) {
		return false;
	}
	return tgt_has(task_tgid_nr(task), feat);
}

bool
tgt_mm(struct mm_struct *mm, u32 feat, pid_t *pid)
{
	unsigned long idx;
	struct wr_tgt *tgt;
	bool found = false;

	if (!mm) {
		return false;
	}
	rcu_read_lock();
	xa_for_each(&tgts, idx, tgt)
	{
		if (tgt->mm == mm && tgt_live(tgt, (pid_t)idx) && (tgt->feats & feat) == feat) {
			if (pid) {
				*pid = (pid_t)idx;
			}
			found = true;
			break;
		}
	}
	rcu_read_unlock();
	return found;
}

void
tgt_bp_set(pid_t pid, unsigned long addr, u8 orig)
{
	struct wr_tgt *tgt;
	int slot = -1, cur;

	rcu_read_lock();
	tgt = tgt_find(pid);
	if (!tgt) {
		goto out;
	}
	spin_lock(&bplock);
	for (cur = 0; cur < WR_BP_MAX; cur++) {
		if (tgt->bps[cur].used && tgt->bps[cur].addr == addr) {
			spin_unlock(&bplock);
			rcu_read_unlock();
			return;
		}
		if (!tgt->bps[cur].used && slot < 0) {
			slot = cur;
		}
	}
	if (slot >= 0) {
		tgt->bps[slot].addr = addr;
		tgt->bps[slot].orig = orig;
		tgt->bps[slot].used = 1;
		spin_unlock(&bplock);
		wr_info("bp set pid=%d addr=0x%lx orig=0x%02x slot=%d\n",
			pid, addr, orig, slot);
		rcu_read_unlock();
		return;
	}
	spin_unlock(&bplock);
	wr_warn("bp full pid=%d addr=0x%lx\n", pid, addr);
out:
	rcu_read_unlock();
}

bool
tgt_bp_get(pid_t pid, unsigned long addr, u8 *orig)
{
	struct wr_tgt *tgt;
	int cur;
	bool found = false;

	rcu_read_lock();
	tgt = tgt_find(pid);
	if (!tgt) {
		goto out;
	}
	spin_lock(&bplock);
	for (cur = 0; cur < WR_BP_MAX; cur++) {
		if (tgt->bps[cur].used && tgt->bps[cur].addr == addr) {
			if (orig) {
				*orig = tgt->bps[cur].orig;
			}
			found = true;
			break;
		}
	}
	spin_unlock(&bplock);
out:
	rcu_read_unlock();
	return found;
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
