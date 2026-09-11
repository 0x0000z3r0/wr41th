#include "drv.h"

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static int
dev_ok(void)
{
	if (!capable(CAP_SYS_PTRACE)) {
		return -EPERM;
	}
	return 0;
}

static long
dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct wr_req req;
	int err;

	(void)filp;
	err = dev_ok();
	if (err) {
		return err;
	}
	if (cmd == WR_IOC_LIST) {
		struct wr_list *lst;
		int ret;

		lst = kzalloc(sizeof(*lst), GFP_KERNEL);
		if (!lst) {
			return -ENOMEM;
		}
		ret = tgt_list(lst->ents, WR_LIST_MAX, &lst->n);
		if (!ret && copy_to_user((void __user *)arg, lst, sizeof(*lst))) {
			ret = -EFAULT;
		}
		kfree(lst);
		return ret;
	}
	if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
		return -EFAULT;
	}

	switch (cmd) {
	case WR_IOC_ATTACH:
		wr_info("ioctl attach pid=%d feats=0x%x\n", req.pid, req.feats);
		err = tgt_add(req.pid, req.feats);
		break;
	case WR_IOC_DETACH:
		wr_info("ioctl detach pid=%d\n", req.pid);
		err = tgt_del(req.pid);
		break;
	case WR_IOC_SET:
		wr_info("ioctl set pid=%d feats=0x%x\n", req.pid, req.feats);
		err = tgt_set(req.pid, req.feats);
		break;
	case WR_IOC_GET:
		err = tgt_get(req.pid, &req.feats);
		wr_dbg("ioctl get pid=%d err=%d feats=0x%x\n", req.pid, err, req.feats);
		if (err) {
			return err;
		}
		if (copy_to_user((void __user *)arg, &req, sizeof(req))) {
			return -EFAULT;
		}
		return 0;
	default:
		wr_warn("ioctl unknown 0x%x\n", cmd);
		return -ENOTTY;
	}
	if (err) {
		wr_warn("ioctl pid=%d err=%d\n", req.pid, err);
	}
	return err;
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
