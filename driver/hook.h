#ifndef WR_HOOK_H
#define WR_HOOK_H

#include <linux/fprobe.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
#define WR_FREGS struct ftrace_regs
#define WR_FENTRY void
#else
#define WR_FREGS struct pt_regs
#define WR_FENTRY int
#endif

int hook_reg(struct fprobe *fp, const char *pat);
void hook_unreg(struct fprobe *fp);

unsigned long hook_arg(WR_FREGS *regs, int n);
unsigned long hook_ret(WR_FREGS *regs);
void hook_set_ret(WR_FREGS *regs, unsigned long v);

#endif
