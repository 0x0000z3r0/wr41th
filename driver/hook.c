#include "hook.h"
#include "drv.h"

#include <linux/ftrace.h>
#include <linux/printk.h>
#include <linux/ptrace.h>

int
hook_reg(struct fprobe *probe, const char *pat)
{
	int err;

	err = register_fprobe(probe, pat, NULL);
	if (err) {
		wr_warn("hook %s fail %d\n", pat, err);
	} else {
		wr_info("hook %s ok\n", pat);
	}
	return err;
}

void
hook_unreg(struct fprobe *probe)
{
	unregister_fprobe(probe);
}

unsigned long
hook_arg(WR_FREGS *regs, int argn)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	return ftrace_regs_get_argument(regs, argn);
#else
	return regs_get_kernel_argument((struct pt_regs *)regs, argn);
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
hook_set_ret(WR_FREGS *regs, unsigned long val)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	ftrace_regs_set_return_value(regs, val);
#else
	regs_set_return_value((struct pt_regs *)regs, val);
#endif
}
