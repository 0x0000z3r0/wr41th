#include "drv.h"
#include "hook.h"

#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/percpu.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/string.h>

#define WR_INT3 0xcc
#define WR_CHUNK 64

struct vm_stash {
	struct mm_struct *mm;
	unsigned long addr;
	void *buf;
	int len;
	unsigned int flags;
	pid_t pid;
	u8 hide;
};

static DEFINE_PER_CPU(int, wr_busy);

static u8
peek_byte(struct mm_struct *mm, unsigned long addr)
{
	struct page *page;
	void *k;
	u8 b = 0;
	long n;
	int *busy = this_cpu_ptr(&wr_busy);

	if (*busy)
		return 0;
	(*busy)++;
	mmap_read_lock(mm);
	n = get_user_pages_remote(mm, addr, 1, FOLL_FORCE, &page, NULL);
	if (n > 0) {
		k = kmap_local_page(page);
		b = *((u8 *)k + offset_in_page(addr));
		kunmap_local(k);
		put_page(page);
	}
	mmap_read_unlock(mm);
	(*busy)--;
	return b;
}

static void
note_write(struct mm_struct *mm, unsigned long addr, const u8 *buf, int len)
{
	pid_t pid;
	int i;

	if (!tgt_mm(mm, WR_FEAT_BRK, &pid))
		return;
	for (i = 0; i < len; i++) {
		if (buf[i] != WR_INT3)
			continue;
		if (tgt_bp_get(pid, addr + i, NULL))
			continue;
		tgt_bp_set(pid, addr + i, peek_byte(mm, addr + i));
	}
}

static void
hide_read(u8 *p, int len, unsigned long addr, pid_t pid)
{
	u8 orig;
	int i;

	for (i = 0; i < len; i++) {
		if (p[i] != WR_INT3)
			continue;
		if (tgt_bp_get(pid, addr + i, &orig))
			p[i] = orig;
	}
}

static void
scan_write(struct vm_stash *s)
{
	u8 tmp[WR_CHUNK];
	int off = 0, n;

	while (off < s->len) {
		n = s->len - off;
		if (n > WR_CHUNK)
			n = WR_CHUNK;
		memcpy(tmp, (u8 *)s->buf + off, n);
		note_write(s->mm, s->addr + off, tmp, n);
		off += n;
	}
}

static void
fill_stash(struct vm_stash *s, struct mm_struct *mm, unsigned long addr, void *buf, int len, unsigned int flags)
{
	s->mm = mm;
	s->addr = addr;
	s->buf = buf;
	s->len = len;
	s->flags = flags;
	s->hide = 0;
	s->pid = 0;

	if (*this_cpu_ptr(&wr_busy) || !mm || len <= 0)
		return;
	if (flags & FOLL_WRITE)
		scan_write(s);
	else if (tgt_task(current, WR_FEAT_BRK) && tgt_mm(mm, WR_FEAT_BRK, &s->pid))
		s->hide = 1;
}

static WR_FENTRY
vm_ent(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	fill_stash(data,
		   (struct mm_struct *)hook_arg(regs, 0),
		   hook_arg(regs, 1),
		   (void *)hook_arg(regs, 2),
		   (int)hook_arg(regs, 3),
		   (unsigned int)hook_arg(regs, 4));
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static WR_FENTRY
ptvm_ent(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct task_struct *task = (struct task_struct *)hook_arg(regs, 0);
	struct mm_struct *mm = task ? task->mm : NULL;

	fill_stash(data, mm,
		   hook_arg(regs, 1),
		   (void *)hook_arg(regs, 2),
		   (int)hook_arg(regs, 3),
		   (unsigned int)hook_arg(regs, 4));
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static void
vm_ex(struct fprobe *fp, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct vm_stash *s = data;
	u8 tmp[WR_CHUNK];
	int got, off, n;

	if (!s->hide || s->pid <= 0)
		return;
	got = (int)hook_ret(regs);
	if (got <= 0)
		return;
	off = 0;
	while (off < got) {
		n = got - off;
		if (n > WR_CHUNK)
			n = WR_CHUNK;
		memcpy(tmp, (u8 *)s->buf + off, n);
		hide_read(tmp, n, s->addr + off, s->pid);
		memcpy((u8 *)s->buf + off, tmp, n);
		off += n;
	}
}

static struct fprobe vm_fp = {
    .entry_handler = vm_ent,
    .exit_handler = vm_ex,
    .entry_data_size = sizeof(struct vm_stash),
};

static struct fprobe ptvm_fp = {
    .entry_handler = ptvm_ent,
    .exit_handler = vm_ex,
    .entry_data_size = sizeof(struct vm_stash),
};

static bool vm_on;
static bool ptvm_on;

int
brk_init(void)
{
	vm_on = !hook_reg(&vm_fp, "__access_remote_vm");
	if (!vm_on)
		vm_on = !hook_reg(&vm_fp, "access_remote_vm");
	ptvm_on = !hook_reg(&ptvm_fp, "ptrace_access_vm");
	return 0;
}

void
brk_fini(void)
{
	if (vm_on)
		hook_unreg(&vm_fp);
	if (ptvm_on)
		hook_unreg(&ptvm_fp);
	vm_on = false;
	ptvm_on = false;
}
