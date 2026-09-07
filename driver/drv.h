#ifndef WR_DRV_H
#define WR_DRV_H

#include "abi.h"

#include <linux/types.h>

struct mm_struct;
struct task_struct;

int tgt_init(void);
void tgt_fini(void);
int tgt_add(pid_t pid, u32 feats);
int tgt_del(pid_t pid);
int tgt_set(pid_t pid, u32 feats);
int tgt_get(pid_t pid, u32 *feats);
bool tgt_has(pid_t pid, u32 feat);
bool tgt_task(struct task_struct *task, u32 feat);
bool tgt_mm(struct mm_struct *mm, u32 feat, pid_t *pid);
void tgt_bp_set(pid_t pid, unsigned long addr, u8 orig);
bool tgt_bp_get(pid_t pid, unsigned long addr, u8 *orig);

int hook_init(void);
void hook_fini(void);

int ptrace_init(void);
void ptrace_fini(void);
int proc_init(void);
void proc_fini(void);
int brk_init(void);
void brk_fini(void);

int dev_init(void);
void dev_fini(void);

#endif
