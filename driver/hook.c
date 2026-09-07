#include "hook.h"

#include <linux/ftrace.h>
#include <linux/printk.h>
#include <linux/ptrace.h>

int
hook_reg(struct fprobe *fp, const char *pat)
{
	int err;

	err = register_fprobe(fp, pat, NULL);
	if (err)
		pr_warn("wr41th: fprobe %s failed (%d)\n", pat, err);
	return err;
}

void
hook_unreg(struct fprobe *fp)
{
	unregister_fprobe(fp);
}

unsigned long
hook_arg(WR_FREGS *regs, int n)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	return ftrace_regs_get_argument(regs, n);
#else
	return regs_get_kernel_argument((struct pt_regs *)regs, n);
#endif
}

unsigned long
hook_ret(WR_FREGS *regs)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	return ftrace_regs_get_return_value(regs);
#else
	return regs_return_value((struct pt_regs *)regs);
#endif
}

void
hook_set_ret(WR_FREGS *regs, unsigned long v)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	ftrace_regs_set_return_value(regs, v);
#else
	regs_set_return_value((struct pt_regs *)regs, v);
#endif
}
