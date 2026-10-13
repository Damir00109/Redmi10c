/* i2cerr: report exact errno for different transaction shapes on CCI/Geni.
 * usage: i2cerr <dev> <addr> [addr...]
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static int xfer(int fd, int addr, unsigned char *w, int wl, unsigned char *r, int rl)
{
	struct i2c_msg m[2];
	struct i2c_rdwr_ioctl_data d;
	int n = 0;

	if (wl) { m[n].addr = addr; m[n].flags = 0; m[n].len = wl; m[n].buf = w; n++; }
	if (rl) { m[n].addr = addr; m[n].flags = I2C_M_RD; m[n].len = rl; m[n].buf = r; n++; }
	d.msgs = m; d.nmsgs = n;
	errno = 0;
	return ioctl(fd, I2C_RDWR, &d) == n ? 0 : -errno;
}

static const char *nm(int e)
{
	static char b[32];
	if (e == 0) return "OK";
	snprintf(b, sizeof b, "%d(%s)", e, strerror(-e));
	return b;
}

int main(int argc, char **argv)
{
	const char *dev = argv[1];
	int fd = open(dev, O_RDWR);
	unsigned char b3[3], r2[2];

	if (fd < 0) { perror(dev); return 1; }

	for (int i = 2; i < argc; i++) {
		int a = strtol(argv[i], NULL, 0);

		printf("=== 0x%02x ===\n", a);
		printf("  0-len write          : %s\n", nm(xfer(fd, a, NULL, 0, NULL, 0)));
		b3[0] = 0x00;
		printf("  1-byte write (0x00)  : %s\n", nm(xfer(fd, a, b3, 1, NULL, 0)));
		b3[0] = 0x00; b3[1] = 0x00;
		printf("  2-byte write (0,0)   : %s\n", nm(xfer(fd, a, b3, 2, NULL, 0)));
		b3[0] = 0x03; b3[1] = 0x00; b3[2] = 0x00;
		printf("  3-byte write (3,0,0) : %s\n", nm(xfer(fd, a, b3, 3, NULL, 0)));
		b3[0] = 0x00;
		printf("  reg read 8bit        : %s\n", nm(xfer(fd, a, b3, 1, r2, 1)));
		b3[0] = 0x00; b3[1] = 0x00;
		printf("  reg read 16bit       : %s\n", nm(xfer(fd, a, b3, 2, r2, 2)));
	}
	return 0;
}
