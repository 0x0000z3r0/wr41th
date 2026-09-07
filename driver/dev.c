#include "drv.h"

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uaccess.h>

static int
dev_ok(void)
{
	if (!capable(CAP_SYS_PTRACE))
		return -EPERM;
	return 0;
}

static long
dev_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct wr_req req;
	int err;

	err = dev_ok();
	if (err)
		return err;
	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	switch (cmd) {
	case WR_IOC_ATTACH:
		return tgt_add(req.pid, req.feats);
	case WR_IOC_DETACH:
		return tgt_del(req.pid);
	case WR_IOC_SET:
		return tgt_set(req.pid, req.feats);
	case WR_IOC_GET:
		err = tgt_get(req.pid, &req.feats);
		if (err)
			return err;
		if (copy_to_user((void __user *)arg, &req, sizeof(req)))
			return -EFAULT;
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations wr_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = dev_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = dev_ioctl,
#endif
};

static struct miscdevice wr_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = WR_NAME,
    .fops = &wr_fops,
    .mode = 0600,
};

int
dev_init(void)
{
	return misc_register(&wr_dev);
}

void
dev_fini(void)
{
	misc_deregister(&wr_dev);
}
