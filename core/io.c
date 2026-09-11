#include "io.h"

#include <fcntl.h>
#include <unistd.h>

int
wr_open(void)
{
	return open(WR_DEV, O_RDWR);
}

int
wr_attach(int fd, pid_t pid, uint32_t feats)
{
	struct wr_req req;

	req.pid = pid;
	req.feats = feats;
	return ioctl(fd, WR_IOC_ATTACH, &req);
}

int
wr_detach(int fd, pid_t pid)
{
	struct wr_req req;

	req.pid = pid;
	req.feats = 0;
	return ioctl(fd, WR_IOC_DETACH, &req);
}

int
wr_set(int fd, pid_t pid, uint32_t feats)
{
	struct wr_req req;

	req.pid = pid;
	req.feats = feats;
	return ioctl(fd, WR_IOC_SET, &req);
}

int
wr_get(int fd, pid_t pid, uint32_t *feats)
{
	struct wr_req req;
	int err;

	req.pid = pid;
	req.feats = 0;
	err = ioctl(fd, WR_IOC_GET, &req);
	if (err)
		return err;
	if (feats)
		*feats = req.feats;
	return 0;
}

int
wr_list(int fd, struct wr_list *out)
{
	if (!out)
		return -1;
	out->n = 0;
	out->pad = 0;
	return ioctl(fd, WR_IOC_LIST, out);
}
