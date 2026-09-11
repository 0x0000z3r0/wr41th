#include "io.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t
feat_bit(const char *name)
{
	if (!name) {
		return 0;
	}
	if (!strcmp(name, "ptrace")) {
		return WR_FEAT_PTRACE;
	}
	if (!strcmp(name, "proc")) {
		return WR_FEAT_PROC;
	}
	if (!strcmp(name, "brk")) {
		return WR_FEAT_BRK;
	}
	if (!strcmp(name, "hw")) {
		return WR_FEAT_HW;
	}
	if (!strcmp(name, "maps")) {
		return WR_FEAT_MAPS;
	}
	if (!strcmp(name, "ppid")) {
		return WR_FEAT_PPID;
	}
	if (!strcmp(name, "time")) {
		return WR_FEAT_TIME;
	}
	return 0;
}

static const char *
feat_names(uint32_t feats)
{
	static char buf[80];

	snprintf(buf, sizeof(buf), "0x%x%s%s%s%s%s%s%s", feats,
		 (feats & WR_FEAT_PTRACE) ? " ptrace" : "",
		 (feats & WR_FEAT_PROC) ? " proc" : "",
		 (feats & WR_FEAT_BRK) ? " brk" : "",
		 (feats & WR_FEAT_HW) ? " hw" : "",
		 (feats & WR_FEAT_MAPS) ? " maps" : "",
		 (feats & WR_FEAT_PPID) ? " ppid" : "",
		 (feats & WR_FEAT_TIME) ? " time" : "");
	return buf;
}

static void
usage(FILE *out)
{
	fprintf(out, "usage: wr41th add  <pid> <feat...>\n");
	fprintf(out, "       wr41th del  <pid>|all\n");
	fprintf(out, "       wr41th set  <pid> <feat...>\n");
	fprintf(out, "       wr41th feat <pid> <name> <on|off>\n");
	fprintf(out, "       wr41th stat <pid>\n");
	fprintf(out, "       wr41th ls\n");
	fprintf(out, "feats: ptrace proc brk hw maps ppid time\n");
}

static int
parse_pid(const char *str, pid_t *pid)
{
	char *end;
	long val;

	if (!str || !*str) {
		return -1;
	}
	errno = 0;
	val = strtol(str, &end, 10);
	if (errno || end == str || *end || val <= 0) {
		return -1;
	}
	*pid = (pid_t)val;
	return 0;
}

static int
parse_feats(char **argv, int argc, uint32_t *mask)
{
	int arg;
	uint32_t bit;

	*mask = 0;
	for (arg = 0; arg < argc; arg++) {
		bit = feat_bit(argv[arg]);
		if (!bit) {
			wr_err("unknown feat %s\n", argv[arg]);
			return -1;
		}
		*mask |= bit;
	}
	return 0;
}

static int
open_dev(void)
{
	int fd = wr_open();

	if (fd < 0) {
		wr_err("cannot open %s\n", WR_DEV);
	}
	return fd;
}

static int
die_ioctl(const char *op, pid_t pid)
{
	if (pid) {
		wr_err("%s %d failed\n", op, pid);
	} else {
		wr_err("%s all failed\n", op);
	}
	return 1;
}

static int
cmd_add(pid_t pid, uint32_t mask)
{
	int fd = open_dev();

	if (fd < 0) {
		return 1;
	}
	if (wr_attach(fd, pid, mask) < 0) {
		close(fd);
		return die_ioctl("add", pid);
	}
	wr_ok("pid %d feats %s\n", pid, feat_names(mask));
	close(fd);
	return 0;
}

static int
cmd_del(pid_t pid)
{
	int fd = open_dev();

	if (fd < 0) {
		return 1;
	}
	if (wr_detach(fd, pid) < 0) {
		close(fd);
		return die_ioctl("del", pid);
	}
	wr_ok("del %d\n", pid);
	close(fd);
	return 0;
}

static int
cmd_del_all(void)
{
	int fd = open_dev();

	if (fd < 0) {
		return 1;
	}
	if (wr_detach(fd, 0) < 0) {
		close(fd);
		return die_ioctl("del", 0);
	}
	wr_ok("emptied the target list\n");
	close(fd);
	return 0;
}

static int
cmd_set(pid_t pid, uint32_t mask)
{
	int fd = open_dev();

	if (fd < 0) {
		return 1;
	}
	if (wr_set(fd, pid, mask) < 0) {
		close(fd);
		return die_ioctl("set", pid);
	}
	wr_ok("pid %d feats %s\n", pid, feat_names(mask));
	close(fd);
	return 0;
}

static int
cmd_feat(pid_t pid, const char *name, const char *onoff)
{
	uint32_t bit, feats;
	int fd, on;

	bit = feat_bit(name);
	if (!bit) {
		wr_err("unknown feat %s\n", name);
		return 1;
	}
	on = (!strcmp(onoff, "on") || !strcmp(onoff, "1"));
	if (!on && strcmp(onoff, "off") && strcmp(onoff, "0")) {
		wr_err("use on|off\n");
		return 1;
	}
	fd = open_dev();
	if (fd < 0) {
		return 1;
	}
	if (wr_get(fd, pid, &feats) < 0) {
		if (wr_attach(fd, pid, 0) < 0 || wr_get(fd, pid, &feats) < 0) {
			close(fd);
			return die_ioctl("feat", pid);
		}
	}
	if (on) {
		feats |= bit;
	} else {
		feats &= ~bit;
	}
	if (wr_set(fd, pid, feats) < 0) {
		close(fd);
		return die_ioctl("set", pid);
	}
	wr_ok("pid %d feats %s\n", pid, feat_names(feats));
	close(fd);
	return 0;
}

static int
cmd_ls(void)
{
	struct wr_list lst;
	unsigned int idx;
	int fd = open_dev();

	if (fd < 0) {
		return 1;
	}
	if (wr_list(fd, &lst) < 0) {
		wr_err("ls failed\n");
		close(fd);
		return 1;
	}
	close(fd);
	if (!lst.n) {
		wr_ok("none\n");
		return 0;
	}
	for (idx = 0; idx < lst.n && idx < WR_LIST_MAX; idx++) {
		wr_ok("pid %d feats %s\n", lst.ents[idx].pid, feat_names(lst.ents[idx].feats));
	}
	return 0;
}

static int
cmd_stat(pid_t pid)
{
	uint32_t feats;
	int fd = open_dev();

	if (fd < 0) {
		return 1;
	}
	if (wr_get(fd, pid, &feats) < 0) {
		wr_ok("pid %d hidden no\n", pid);
	} else {
		wr_ok("pid %d feats %s\n", pid, feat_names(feats));
	}
	close(fd);
	return 0;
}

int
main(int argc, char **argv)
{
	const char *cmd;
	pid_t pid;
	uint32_t mask;

	if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(stdout);
		return argc < 2 ? 1 : 0;
	}
	cmd = argv[1];
	if (!strcmp(cmd, "ls") || !strcmp(cmd, "list")) {
		if (argc != 2) {
			usage(stderr);
			return 1;
		}
		return cmd_ls();
	}
	if (!strcmp(cmd, "del") && argc == 3 && !strcmp(argv[2], "all")) {
		return cmd_del_all();
	}
	if (argc < 3 || parse_pid(argv[2], &pid) < 0) {
		usage(stderr);
		return 1;
	}
	if (!strcmp(cmd, "add")) {
		if (argc < 4) {
			usage(stderr);
			return 1;
		}
		if (parse_feats(argv + 3, argc - 3, &mask) < 0) {
			return 1;
		}
		return cmd_add(pid, mask);
	}
	if (!strcmp(cmd, "del")) {
		if (argc != 3) {
			usage(stderr);
			return 1;
		}
		return cmd_del(pid);
	}
	if (!strcmp(cmd, "set")) {
		if (argc < 4) {
			usage(stderr);
			return 1;
		}
		if (parse_feats(argv + 3, argc - 3, &mask) < 0) {
			return 1;
		}
		return cmd_set(pid, mask);
	}
	if (!strcmp(cmd, "feat")) {
		if (argc != 5) {
			usage(stderr);
			return 1;
		}
		return cmd_feat(pid, argv[3], argv[4]);
	}
	if (!strcmp(cmd, "stat")) {
		if (argc != 3) {
			usage(stderr);
			return 1;
		}
		return cmd_stat(pid);
	}
	usage(stderr);
	return 1;
}
