#ifndef WR_IO_H
#define WR_IO_H

#include "abi.h"

#include <sys/types.h>

int wr_open(void);
int wr_attach(int fd, pid_t pid, uint32_t feats);
int wr_detach(int fd, pid_t pid);
int wr_set(int fd, pid_t pid, uint32_t feats);
int wr_get(int fd, pid_t pid, uint32_t *feats);
int wr_list(int fd, struct wr_list *out);

#endif
