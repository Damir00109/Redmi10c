/* i2cack: address-only ACK probe + register write probe.
 * usage: i2cack <dev> [reg val]
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

static int probe0(int fd, int addr)
{
	struct i2c_msg m;
	struct i2c_rdwr_ioctl_data d;
	m.addr = addr; m.flags = 0; m.len = 0; m.buf = NULL;
	d.msgs = &m; d.nmsgs = 1;
	return ioctl(fd, I2C_RDWR, &d) == 1 ? 0 : -errno;
}

static int writeb(int fd, int addr, unsigned char reg, unsigned char val)
{
	unsigned char b[2] = { reg, val };
	struct i2c_msg m;
	struct i2c_rdwr_ioctl_data d;
	m.addr = addr; m.flags = 0; m.len = 2; m.buf = b;
	d.msgs = &m; d.nmsgs = 1;
	return ioctl(fd, I2C_RDWR, &d) == 1 ? 0 : -errno;
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/i2c-0";
	int reg = argc > 2 ? strtol(argv[2], NULL, 0) : 0x00;
	int val = argc > 3 ? strtol(argv[3], NULL, 0) : 0x00;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }

	for (int a = 0x08; a < 0x78; a++) {
		int r0 = probe0(fd, a);
		int r1 = writeb(fd, a, reg, val);
		if (r0 == 0 || r1 == 0)
			printf("0x%02x ack=%s write=%s\n", a,
			       r0 == 0 ? "yes" : "no",
			       r1 == 0 ? "yes" : "no");
	}
	return 0;
}
