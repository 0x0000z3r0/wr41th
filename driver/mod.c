#include "drv.h"

#include <linux/module.h>

static int
wr_init(void)
{
	int err;

	err = tgt_init();
	if (err)
		return err;
	err = hook_init();
	if (err)
		goto e_tgt;
	err = ptrace_init();
	if (err)
		goto e_hook;
	err = proc_init();
	if (err)
		goto e_ptrace;
	err = brk_init();
	if (err)
		goto e_proc;
	err = dev_init();
	if (err)
		goto e_brk;
	pr_info("wr41th: loaded\n");
	return 0;

e_brk:
	brk_fini();
e_proc:
	proc_fini();
e_ptrace:
	ptrace_fini();
e_hook:
	hook_fini();
e_tgt:
	tgt_fini();
	return err;
}

static void
wr_exit(void)
{
	dev_fini();
	brk_fini();
	proc_fini();
	ptrace_fini();
	hook_fini();
	tgt_fini();
	pr_info("wr41th: gone\n");
}

int
hook_init(void)
{
	return 0;
}

void
hook_fini(void)
{
}

module_init(wr_init);
module_exit(wr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wr41th");
MODULE_DESCRIPTION("anti-anti-debug hide");
MODULE_VERSION("0.1");
