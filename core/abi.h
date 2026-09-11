#ifndef WR_ABI_H
#define WR_ABI_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
typedef int32_t __s32;
typedef uint32_t __u32;
#endif

#define WR_DEV "/dev/wr41th"
#define WR_NAME "wr41th"
#define WR_IOC_MAGIC 'W'

#define WR_FEAT_PTRACE (1u << 0)
#define WR_FEAT_PROC (1u << 1)
#define WR_FEAT_BRK (1u << 2)
#define WR_FEAT_HW (1u << 3)   /* reserved: debug regs */
#define WR_FEAT_MAPS (1u << 4) /* reserved: /proc/maps */
#define WR_FEAT_PPID (1u << 5) /* reserved: parent / dumpable */
#define WR_FEAT_TIME (1u << 6) /* reserved: timing */

struct wr_req {
	__s32 pid;
	__u32 feats;
};

#define WR_LIST_MAX 256

struct wr_list {
	__u32 n;
	__u32 pad;
	struct wr_req ents[WR_LIST_MAX];
};

#define WR_IOC_ATTACH _IOW(WR_IOC_MAGIC, 1, struct wr_req)
#define WR_IOC_DETACH _IOW(WR_IOC_MAGIC, 2, struct wr_req)
#define WR_IOC_SET _IOW(WR_IOC_MAGIC, 3, struct wr_req)
#define WR_IOC_GET _IOWR(WR_IOC_MAGIC, 4, struct wr_req)
#define WR_IOC_LIST _IOR(WR_IOC_MAGIC, 5, struct wr_list)

#endif
