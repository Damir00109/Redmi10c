// SPDX-License-Identifier: GPL-2.0
/*
 * nfc-probe — bring the NXP PN80T-class NFC chip out of power-down from
 * userspace and check whether it answers on I2C.
 *
 * The upstream nxp-nci driver keeps VEN low until the NCI device is
 * opened, so a bare boot tells us nothing about the chip.  This tool
 * drives the same control lines the vendor driver owns (VEN=gpio56,
 * FIRM=gpio31, CLKREQ=gpio71 input) through the gpiolib chardev and then
 * probes address 0x2a on the GENI I2C bus.
 *
 * CLKREQ is an input from the chip: idle is pulled up, the chip pulls it
 * low while it needs the PMIC clock.  If CLKREQ never moves, the chip is
 * either unpowered or has no clock.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define VEN_LINE	56
#define FIRM_LINE	31
#define IRQ_LINE	70
#define CLKREQ_LINE	71

static int gpio_request(const char *chip, unsigned int line, int output,
			int init_val)
{
	struct gpio_v2_line_request req;
	int fd, r;

	fd = open(chip, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		printf("open %s: %s\n", chip, strerror(errno));
		return -1;
	}
	memset(&req, 0, sizeof(req));
	req.offsets[0] = line;
	req.num_lines = 1;
	req.config.flags = output ? GPIO_V2_LINE_FLAG_OUTPUT
				  : GPIO_V2_LINE_FLAG_INPUT;
	if (output) {
		req.config.num_attrs = 1;
		req.config.attrs[0].attr.id =
			GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
		req.config.attrs[0].attr.values = init_val ? 1 : 0;
		req.config.attrs[0].mask = 1;
	}
	strncpy(req.consumer, "nfc-probe", sizeof(req.consumer) - 1);
	r = ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req);
	close(fd);
	if (r < 0) {
		printf("gpio %u request: %s\n", line, strerror(errno));
		return -1;
	}
	return req.fd;
}

static int gpio_set(int fd, int val)
{
	struct gpio_v2_line_values v = { .bits = val ? 1 : 0, .mask = 1 };

	return ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

static int gpio_get(int fd)
{
	struct gpio_v2_line_values v = { .bits = 0, .mask = 1 };

	if (ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0)
		return -1;
	return v.bits & 1;
}

static const char *i2c_probe(int bus, int addr)
{
	static char msg[64];
	union i2c_smbus_data data;
	struct i2c_smbus_ioctl_data args = {
		.read_write = I2C_SMBUS_WRITE,
		.command = 0,
		.size = I2C_SMBUS_QUICK,
		.data = &data,
	};
	char path[32];
	int fd, r;

	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		snprintf(msg, sizeof(msg), "open %s: %s", path,
			 strerror(errno));
		return msg;
	}
	if (ioctl(fd, I2C_SLAVE, addr) < 0) {
		snprintf(msg, sizeof(msg), "I2C_SLAVE: %s", strerror(errno));
		close(fd);
		return msg;
	}
	r = ioctl(fd, I2C_SMBUS, &args);
	if (r < 0)
		snprintf(msg, sizeof(msg), "NACK (%s)", strerror(errno));
	else
		snprintf(msg, sizeof(msg), "ACK");
	close(fd);
	return msg;
}

static void msleep(long ms)
{
	struct timespec ts = { .tv_sec = ms / 1000,
			       .tv_nsec = (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

int main(int argc, char **argv)
{
	const char *chip = argc > 1 ? argv[1] : "/dev/gpiochip0";
	int bus = argc > 2 ? atoi(argv[2]) : 2;
	int addr = argc > 3 ? (int)strtol(argv[3], NULL, 0) : 0x2a;
	int ven, firm, irq, clk;

	ven = gpio_request(chip, VEN_LINE, 1, 0);
	firm = gpio_request(chip, FIRM_LINE, 1, 0);
	irq = gpio_request(chip, IRQ_LINE, 0, 0);
	clk = gpio_request(chip, CLKREQ_LINE, 0, 0);

	/*
	 * If the nxp-nci driver already owns the lines (it does once the
	 * device is registered), fall back to a passive run: bring the
	 * netdev up from the shell, then only probe I2C from here.
	 */
	if (ven < 0 || firm < 0 || irq < 0 || clk < 0) {
		printf("gpio busy (driver owns the lines) — I2C probe only\n");
		printf("i2c-%d 0x%02x: %s\n", bus, addr, i2c_probe(bus, addr));
		return 0;
	}

	msleep(20);
	printf("VEN=0: irq=%d clkreq=%d\n", gpio_get(irq), gpio_get(clk));

	gpio_set(ven, 1);
	msleep(20);
	printf("VEN=1: irq=%d clkreq=%d\n", gpio_get(irq), gpio_get(clk));
	printf("i2c-%d 0x%02x: %s\n", bus, addr, i2c_probe(bus, addr));

	for (int i = 0; i < 5; i++) {
		msleep(40);
		printf("  +%3dms: irq=%d clkreq=%d\n", (i + 1) * 40,
		       gpio_get(irq), gpio_get(clk));
	}

	gpio_set(ven, 0);
	msleep(20);
	printf("VEN=0: irq=%d clkreq=%d\n", gpio_get(irq), gpio_get(clk));
	return 0;
}
