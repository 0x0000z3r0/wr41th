#include "drv.h"
#include "hook.h"

#include <linux/hardirq.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
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
	void *kaddr;
	u8 byte = 0;
	long nread;
	int *busy = this_cpu_ptr(&wr_busy);

	if (*busy || in_atomic() || irqs_disabled()) {
		return 0;
	}
	(*busy)++;
	mmap_read_lock(mm);
	nread = get_user_pages_remote(mm, addr, 1, FOLL_FORCE, &page, NULL);
	if (nread > 0) {
		kaddr = kmap_local_page(page);
		byte = *((u8 *)kaddr + offset_in_page(addr));
		kunmap_local(kaddr);
		put_page(page);
	}
	mmap_read_unlock(mm);
	(*busy)--;
	return byte;
}

static void
note_write(struct mm_struct *mm, unsigned long addr, const u8 *buf, int len)
{
	pid_t pid;
	int off;

	if (!tgt_mm(mm, WR_FEAT_BRK, &pid)) {
		return;
	}
	for (off = 0; off < len; off++) {
		if (buf[off] != WR_INT3) {
			continue;
		}
		if (tgt_bp_get(pid, addr + off, NULL)) {
			continue;
		}
		if (in_atomic() || irqs_disabled()) {
			continue;
		}
		tgt_bp_set(pid, addr + off, peek_byte(mm, addr + off));
	}
}

static void
hide_read(u8 *buf, int len, unsigned long addr, pid_t pid)
{
	u8 orig;
	int off;

	for (off = 0; off < len; off++) {
		if (buf[off] != WR_INT3) {
			continue;
		}
		if (tgt_bp_get(pid, addr + off, &orig)) {
			wr_dbg("bp hide pid=%d addr=0x%lx orig=0x%02x\n",
			       pid, addr + off, orig);
			buf[off] = orig;
		}
	}
}

static void
scan_write(struct vm_stash *stash)
{
	u8 tmp[WR_CHUNK];
	int off = 0, chunk;

	while (off < stash->len) {
		chunk = stash->len - off;
		if (chunk > WR_CHUNK) {
			chunk = WR_CHUNK;
		}
		memcpy(tmp, (u8 *)stash->buf + off, chunk);
		note_write(stash->mm, stash->addr + off, tmp, chunk);
		off += chunk;
	}
}

static void
fill_stash(struct vm_stash *stash, struct mm_struct *mm, unsigned long addr, void *buf, int len, unsigned int flags)
{
	stash->mm = mm;
	stash->addr = addr;
	stash->buf = buf;
	stash->len = len;
	stash->flags = flags;
	stash->hide = 0;
	stash->pid = 0;

	if (*this_cpu_ptr(&wr_busy) || !mm || len <= 0) {
		return;
	}
	if (flags & FOLL_WRITE) {
		scan_write(stash);
	} else if (tgt_task(current, WR_FEAT_BRK) && tgt_mm(mm, WR_FEAT_BRK, &stash->pid)) {
		stash->hide = 1;
	}
}

static WR_FENTRY
vm_ent(struct fprobe *probe, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	(void)probe;
	(void)ip;
	(void)rip;
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
ptvm_ent(struct fprobe *probe, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct task_struct *task = (struct task_struct *)hook_arg(regs, 0);
	struct mm_struct *mm = task ? task->mm : NULL;

	(void)probe;
	(void)ip;
	(void)rip;
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
vm_ex(struct fprobe *probe, unsigned long ip, unsigned long rip, WR_FREGS *regs, void *data)
{
	struct vm_stash *stash = data;
	u8 tmp[WR_CHUNK];
	int got, off, chunk;

	(void)probe;
	(void)ip;
	(void)rip;
	if (!stash->hide || stash->pid <= 0) {
		return;
	}
	got = (int)hook_ret(regs);
	if (got <= 0) {
		return;
	}
	off = 0;
	while (off < got) {
		chunk = got - off;
		if (chunk > WR_CHUNK) {
			chunk = WR_CHUNK;
		}
		memcpy(tmp, (u8 *)stash->buf + off, chunk);
		hide_read(tmp, chunk, stash->addr + off, stash->pid);
		memcpy((u8 *)stash->buf + off, tmp, chunk);
		off += chunk;
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
	if (!vm_on) {
		vm_on = !hook_reg(&vm_fp, "access_remote_vm");
	}
	ptvm_on = !hook_reg(&ptvm_fp, "ptrace_access_vm");
	wr_info("brk vm=%d ptvm=%d\n", vm_on, ptvm_on);
	return 0;
}

void
brk_fini(void)
{
	if (vm_on) {
		hook_unreg(&vm_fp);
	}
	if (ptvm_on) {
		hook_unreg(&ptvm_fp);
	}
	vm_on = false;
	ptvm_on = false;
}
