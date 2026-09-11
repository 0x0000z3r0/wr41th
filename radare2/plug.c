#include "io.h"

#include <r_core.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
starts(const char *s, const char *p)
{
	size_t n = strlen(p);

	return s && !strncmp(s, p, n);
}

static pid_t
dbg_pid(RCore *core)
{
	if (core && core->dbg && core->dbg->pid > 0) {
		return (pid_t)core->dbg->pid;
	}
	return 0;
}

static uint32_t
feat_bit(const char *n)
{
	if (!n) {
		return 0;
	}
	if (!strcmp(n, "ptrace")) {
		return WR_FEAT_PTRACE;
	}
	if (!strcmp(n, "proc")) {
		return WR_FEAT_PROC;
	}
	if (!strcmp(n, "brk")) {
		return WR_FEAT_BRK;
	}
	if (!strcmp(n, "hw")) {
		return WR_FEAT_HW;
	}
	if (!strcmp(n, "maps")) {
		return WR_FEAT_MAPS;
	}
	if (!strcmp(n, "ppid")) {
		return WR_FEAT_PPID;
	}
	if (!strcmp(n, "time")) {
		return WR_FEAT_TIME;
	}
	return 0;
}

#define WR_CYN "\033[36m"
#define WR_RED "\033[31m"
#define WR_RST "\033[0m"

static void
say(RCore *core, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	r_cons_printf_list(core->cons, fmt, ap);
	va_end(ap);
}

static void
say_ok(RCore *core, const char *fmt, ...)
{
	va_list ap;

	say(core, WR_CYN "[*]" WR_RST " ");
	va_start(ap, fmt);
	r_cons_printf_list(core->cons, fmt, ap);
	va_end(ap);
}

static void
say_err(RCore *core, const char *fmt, ...)
{
	va_list ap;

	say(core, WR_RED "[!]" WR_RST " ");
	va_start(ap, fmt);
	r_cons_printf_list(core->cons, fmt, ap);
	va_end(ap);
}

static void
print_feats(RCore *core, pid_t pid, uint32_t f)
{
	say_ok(core, "pid %d feats 0x%x%s%s%s%s%s%s%s\n", pid, f,
	       (f & WR_FEAT_PTRACE) ? " ptrace" : "",
	       (f & WR_FEAT_PROC) ? " proc" : "",
	       (f & WR_FEAT_BRK) ? " brk" : "",
	       (f & WR_FEAT_HW) ? " hw" : "",
	       (f & WR_FEAT_MAPS) ? " maps" : "",
	       (f & WR_FEAT_PPID) ? " ppid" : "",
	       (f & WR_FEAT_TIME) ? " time" : "");
}

static int
open_dev(RCore *core)
{
	int fd = wr_open();

	if (fd < 0) {
		say_err(core, "cannot open %s\n", WR_DEV);
	}
	return fd;
}

static int
need_dbg(RCore *core, pid_t *pid)
{
	*pid = dbg_pid(core);
	if (*pid <= 0) {
		say_err(core, "no debuggee; dbg/attach in r2 first\n");
		return 0;
	}
	return 1;
}

static int
ensure(int fd, pid_t pid, uint32_t *feats)
{
	if (wr_get(fd, pid, feats) == 0) {
		return 0;
	}
	if (wr_attach(fd, pid, 0) < 0) {
		return -1;
	}
	return wr_get(fd, pid, feats);
}

static void
help(RCore *core)
{
	say(core, "=== WR41TH COMMANDS ===\n");
	say(core, "wrh on <feat...>   hide current debuggee (ptrace proc brk ...)\n");
	say(core, "wrh off            stop hiding current debuggee\n");
	say(core, "wrh feat <n> <on|off>  ptrace|proc|brk|hw|maps|ppid|time\n");
	say(core, "wrh stat           hide flags for current debuggee\n");
}

static bool
cmd_wr(RCore *core, const char *input)
{
	char name[16], onoff[8];
	pid_t pid;
	uint32_t feats, bit;
	int fd, n, on;

	if (starts(input, "on") && (input[2] == '\0' || input[2] == ' ')) {
		char tok[16];
		const char *p;
		uint32_t mask = 0;

		if (!need_dbg(core, &pid)) {
			return true;
		}
		p = input + 2;
		while (sscanf(p, " %15s%n", tok, &n) == 1) {
			bit = feat_bit(tok);
			if (!bit) {
				say_err(core, "unknown feat %s\n", tok);
				return true;
			}
			mask |= bit;
			p += n;
		}
		if (!mask) {
			say(core, "wrh on <feat...>   e.g. wrh on ptrace proc\n");
			return true;
		}
		fd = open_dev(core);
		if (fd < 0) {
			return true;
		}
		if (wr_attach(fd, pid, mask) < 0) {
			say_err(core, "hide %d failed\n", pid);
		} else {
			print_feats(core, pid, mask);
		}
		close(fd);
		return true;
	}
	if (starts(input, "off") && (input[3] == '\0' || input[3] == ' ')) {
		if (!need_dbg(core, &pid)) {
			return true;
		}
		fd = open_dev(core);
		if (fd < 0) {
			return true;
		}
		if (wr_detach(fd, pid) < 0) {
			say_err(core, "%d not hidden\n", pid);
		} else {
			say_ok(core, "off %d\n", pid);
		}
		close(fd);
		return true;
	}
	if (starts(input, "feat")) {
		memset(name, 0, sizeof(name));
		memset(onoff, 0, sizeof(onoff));
		n = sscanf(input, "feat %15s %7s", name, onoff);
		if (n < 2) {
			say(core, "wrh feat <name> <on|off>\n");
			return true;
		}
		if (!need_dbg(core, &pid)) {
			return true;
		}
		bit = feat_bit(name);
		if (!bit) {
			say_err(core, "unknown feat\n");
			return true;
		}
		on = (!strcmp(onoff, "on") || !strcmp(onoff, "1"));
		if (!on && strcmp(onoff, "off") && strcmp(onoff, "0")) {
			say_err(core, "use on|off\n");
			return true;
		}
		fd = open_dev(core);
		if (fd < 0) {
			return true;
		}
		if (ensure(fd, pid, &feats) < 0) {
			say_err(core, "hide %d failed\n", pid);
			close(fd);
			return true;
		}
		if (on) {
			feats |= bit;
		} else {
			feats &= ~bit;
		}
		if (wr_set(fd, pid, feats) < 0) {
			say_err(core, "set failed\n");
		} else {
			print_feats(core, pid, feats);
		}
		close(fd);
		return true;
	}
	if (starts(input, "stat")) {
		if (!need_dbg(core, &pid)) {
			return true;
		}
		fd = open_dev(core);
		if (fd < 0) {
			return true;
		}
		if (wr_get(fd, pid, &feats) < 0) {
			say_ok(core, "pid %d hidden no\n", pid);
		} else {
			print_feats(core, pid, feats);
		}
		close(fd);
		return true;
	}
	help(core);
	return true;
}

static bool
wr_call(RCorePluginSession *ctx, const char *input)
{
	RCore *core = ctx ? ctx->core : NULL;

	if (!core || !input) {
		return false;
	}
	if (starts(input, "wrh?")) {
		return cmd_wr(core, "");
	}
	if (strncmp(input, "wrh", 3)) {
		return false;
	}
	input += 3;
	if (*input == ' ') {
		input++;
	} else if (*input != '\0' && *input != '?') {
		return false;
	}
	if (*input == '?') {
		input = "";
	}
	return cmd_wr(core, input);
}

static RCorePlugin r_core_plugin_wr = {
    .meta = {
	.name = "wr41th",
	.desc = "wr41th hide ctl",
	.author = "wr41th",
	.license = "GPL-3.0",
    },
    .call = wr_call,
};

#ifndef R2_PLUGIN_INCORE
R_API RLibStruct radare_plugin = {
    .type = R_LIB_TYPE_CORE,
    .data = &r_core_plugin_wr,
    .version = R2_VERSION,
#ifdef R2_ABIVERSION
    .abiversion = R2_ABIVERSION,
#endif
};
#endif
