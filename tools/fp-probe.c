// SPDX-License-Identifier: GPL-2.0
/*
 * fp-probe — FPC1020 SPI sanity check for the Redmi 10C (fog/rain).
 *
 * Pulses the sensor reset line (exposed as the gpio-leds "fp:reset" device)
 * and reads the HWID register (0xfc, 2 bytes) over /dev/spidevX.Y.
 *
 * Android drives this sensor from the TEE (qsee_spi, dev_id 5 → QUPv3 SE5),
 * so this program is the AP-side experiment: it proves whether the SE5 bus is
 * reachable from the non-secure world and whether the sensor answers.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>

#define FPC102X_REG_HWID	0xfc
#define FPC_CHIP_MASK		0xff00
#define FPC1020_CHIP		0x1000

static void msleep(unsigned int ms)
{
	struct timespec ts = { .tv_sec = ms / 1000,
			       .tv_nsec = (long)(ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static int write_file(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = write(fd, val, strlen(val));
	close(fd);
	return n == (ssize_t)strlen(val) ? 0 : -1;
}

/* gpio-leds is active-low here: brightness 1 asserts reset. */
static int fp_reset(void)
{
	const char *led = "/sys/class/leds/fp:reset/brightness";

	if (access(led, W_OK))
		return -1;
	write_file(led, "0");
	msleep(1);
	write_file(led, "1");	/* assert */
	msleep(5);
	write_file(led, "0");	/* deassert */
	msleep(10);
	return 0;
}

static int find_spidev(char *out, size_t len)
{
	DIR *d = opendir("/dev");
	struct dirent *de;
	int found = 0;

	if (!d)
		return -1;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "spidev", 6))
			continue;
		snprintf(out, len, "/dev/%s", de->d_name);
		found++;
	}
	closedir(d);
	return found;
}

static int spi_hwid(int fd, uint8_t *rx)
{
	uint8_t addr = FPC102X_REG_HWID;
	uint8_t dummy[2] = { 0, 0 };
	struct spi_ioc_transfer xfer[2] = {
		{
			.tx_buf = (unsigned long)&addr,
			.len = 1,
			.delay_usecs = 5,
		},
		{
			.tx_buf = (unsigned long)dummy,
			.rx_buf = (unsigned long)rx,
			.len = 2,
			.delay_usecs = 5,
		},
	};

	return ioctl(fd, SPI_IOC_MESSAGE(2), xfer);
}

int main(int argc, char **argv)
{
	uint32_t speed = argc > 1 ? strtoul(argv[1], NULL, 0) : 4800000;
	char dev[64];
	uint8_t mode = 0, bits = 8;
	uint8_t rx[2];
	int fd, i, ndev;

	printf("fp-probe: looking for spidev\n");
	ndev = find_spidev(dev, sizeof(dev));
	if (ndev <= 0) {
		printf("fp-probe: no /dev/spidev* (%d)\n", ndev);
		return 1;
	}
	printf("fp-probe: using %s speed=%u\n", dev, speed);

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		printf("fp-probe: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) ||
	    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) ||
	    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed))
		printf("fp-probe: ioctl setup: %s\n", strerror(errno));

	if (fp_reset())
		printf("fp-probe: no fp:reset led, skipping reset pulse\n");

	for (i = 0; i < 10; i++) {
		int ret;

		memset(rx, 0, sizeof(rx));
		ret = spi_hwid(fd, rx);
		if (ret < 0) {
			printf("fp-probe: [%d] SPI_IOC_MESSAGE: %s\n", i,
			       strerror(errno));
			break;
		}
		printf("fp-probe: [%d] hwid=%02x%02x%s\n", i, rx[0], rx[1],
		       ((rx[0] << 8 | rx[1]) & FPC_CHIP_MASK) == FPC1020_CHIP ?
		       "  <- FPC1020" : "");
		msleep(20);
	}
	close(fd);
	return 0;
}
