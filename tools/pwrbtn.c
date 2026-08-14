/*
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
 * rain: single Power press → poweroff.
 * Waits until pm8941 input nodes appear (PON can probe late), then
 * listens for KEY_POWER (116). Re-scans /dev/input if nodes appear later.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_FD 16
#define KEY_POWER_CODE 116

static int is_event_node(const struct dirent *d)
{
	return strncmp(d->d_name, "event", 5) == 0;
}

static void close_fds(int *fds, int n)
{
	int i;
	for (i = 0; i < n; i++)
		close(fds[i]);
}

static int open_inputs(int *fds, int max)
{
	DIR *dir;
	struct dirent *de;
	int n = 0;

	dir = opendir("/dev/input");
	if (!dir)
		return 0;
	while ((de = readdir(dir)) && n < max) {
		char path[96];
		int fd;
		unsigned long evbits[2] = { 0 };

		if (!is_event_node(de))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%.64s", de->d_name);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) == 0 &&
		    (evbits[0] & (1UL << EV_KEY))) {
			fds[n++] = fd;
		} else {
			close(fd);
		}
	}
	closedir(dir);
	return n;
}

static int sysfs_input_ready(void)
{
	DIR *dir = opendir("/sys/class/input");
	struct dirent *de;
	int ok = 0;

	if (!dir)
		return 0;
	while ((de = readdir(dir))) {
		if (strncmp(de->d_name, "event", 5) == 0 ||
		    strncmp(de->d_name, "input", 5) == 0) {
			if (strcmp(de->d_name, "input") != 0 &&
			    strcmp(de->d_name, ".") != 0 &&
			    strcmp(de->d_name, "..") != 0)
				ok = 1;
		}
	}
	closedir(dir);
	return ok;
}

static void do_poweroff(void)
{
	fprintf(stderr, "pwrbtn: KEY_POWER → poweroff\n");
	sync();
	execl("/bin/busybox", "busybox", "poweroff", "-f", (char *)NULL);
	execl("/sbin/poweroff", "poweroff", "-f", (char *)NULL);
	{
		int fd = open("/proc/sysrq-trigger", O_WRONLY);
		if (fd >= 0) {
			(void)write(fd, "o", 1);
			close(fd);
		}
	}
	_exit(1);
}

int main(void)
{
	int fds[MAX_FD];
	struct pollfd pfd[MAX_FD];
	int n = 0, i, ticks = 0;

	fprintf(stderr, "pwrbtn: waiting for input devices…\n");

	for (;;) {
		if (sysfs_input_ready())
			(void)system("busybox mdev -s >/dev/null 2>&1");

		n = open_inputs(fds, MAX_FD);
		if (n > 0)
			break;

		if ((ticks % 10) == 0)
			fprintf(stderr, "pwrbtn: still waiting (%ds)\n", ticks);
		sleep(1);
		ticks++;
	}

	fprintf(stderr, "pwrbtn: watching %d input device(s)\n", n);

	for (i = 0; i < n; i++) {
		pfd[i].fd = fds[i];
		pfd[i].events = POLLIN;
	}

	for (;;) {
		int pr = poll(pfd, n, 5000);
		if (pr < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (pr == 0) {
			/* periodic rescan in case nodes were replaced */
			int nfds[MAX_FD];
			int nn = open_inputs(nfds, MAX_FD);
			if (nn > 0 && nn != n) {
				close_fds(fds, n);
				n = nn;
				for (i = 0; i < n; i++) {
					fds[i] = nfds[i];
					pfd[i].fd = fds[i];
					pfd[i].events = POLLIN;
				}
				fprintf(stderr, "pwrbtn: rescanned → %d device(s)\n", n);
			} else if (nn > 0) {
				close_fds(nfds, nn);
			}
			continue;
		}
		for (i = 0; i < n; i++) {
			struct input_event ev;
			ssize_t r;

			if (!(pfd[i].revents & POLLIN))
				continue;
			while ((r = read(fds[i], &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
				if (ev.type == EV_KEY && ev.code == KEY_POWER_CODE &&
				    ev.value == 1)
					do_poweroff();
			}
		}
	}
	return 0;
}
