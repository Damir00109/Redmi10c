/* ttyexec <dev> <cmd> [args...] - setsid + TIOCSCTTY on <dev>, then exec <cmd>
 * Gives a proper controlling tty to a shell on an arbitrary device
 * (e.g. /dev/ttyGS0), which busybox cttyhack cannot do. */
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>

int main(int argc, char **argv)
{
	int fd;
	if (argc < 3 || argv[1][0] != '/')
		return 2;
	if (setsid() < 0) { perror("setsid"); return 1; }
	fd = open(argv[1], O_RDWR);
	if (fd < 0) { perror(argv[1]); return 1; }
	if (ioctl(fd, TIOCSCTTY, 1) < 0) { perror("TIOCSCTTY"); return 1; }
	if (tcsetpgrp(fd, getpgrp()) < 0) { perror("tcsetpgrp"); return 1; }
	dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
	if (fd > 2) close(fd);
	execvp(argv[2], argv + 2);
	perror(argv[2]);
	return 127;
}
