#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static int xfer(int fd, int addr, const unsigned char *w, int wl,
                unsigned char *r, int rl)
{
	struct i2c_msg m[2];
	struct i2c_rdwr_ioctl_data d;
	int n = 0;
	if (wl) {
		m[n].addr = addr; m[n].flags = 0; m[n].len = wl;
		m[n].buf = (unsigned char *)w; n++;
	}
	if (rl) {
		m[n].addr = addr; m[n].flags = I2C_M_RD;
		m[n].len = rl; m[n].buf = r; n++;
	}
	d.msgs = m; d.nmsgs = n;
	return ioctl(fd, I2C_RDWR, &d) == n ? 0 : -errno;
}

/* read len bytes at word-address off; wmode=16 -> BE u16 addr, wmode=8 -> u8 addr */
static int ee_read(int fd, int addr, int wmode, unsigned int off,
                   unsigned char *buf, int len)
{
	unsigned char w[2];
	int wl;
	if (wmode == 16) { w[0] = off >> 8; w[1] = off & 0xff; wl = 2; }
	else             { w[0] = off & 0xff; wl = 1; }
	/* read in small chunks; cci/i2c adapters limit per-msg length sometimes */
	int done = 0;
	while (done < len) {
		int chunk = len - done;
		if (chunk > 8) chunk = 8;
		if (wmode == 16) { w[0] = (off + done) >> 8; w[1] = (off + done) & 0xff; }
		else             { w[0] = (off + done) & 0xff; }
		if (xfer(fd, addr, w, wl, buf + done, chunk) != 0)
			return done ? done : -1;
		done += chunk;
	}
	return done;
}

int main(int argc, char **argv)
{
	/* usage: eetool <dev> scan
	          eetool <dev> dump <addr> <wmode 8|16> <off> <len> [file]
	          eetool <dev> size <addr> <wmode>   -- detect wrap-around */
	const char *dev = argc > 1 ? argv[1] : "/dev/i2c-0";
	const char *cmd = argc > 2 ? argv[2] : "scan";
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }

	if (!strcmp(cmd, "scan")) {
		for (int a = 8; a < 0x78; a++) {
			unsigned char b = 0;
			int rd = xfer(fd, a, NULL, 0, &b, 1);
			unsigned char w[2] = {0, 0}, r[4] = {0};
			int rr = xfer(fd, a, w, 2, r, 4);
			if (rd == 0 || rr == 0)
				printf("0x%02x%s first16=%02x%02x%02x%02x\n",
				       a, rd == 0 ? " [rd]" : "", r[0], r[1], r[2], r[3]);
		}
		return 0;
	}

	int addr = strtol(argv[3], NULL, 0);
	int wmode = atoi(argv[4]);

	if (!strcmp(cmd, "dump")) {
		unsigned int off = strtol(argv[5], NULL, 0);
		int len = strtol(argv[6], NULL, 0);
		unsigned char *buf = malloc(len);
		int n = ee_read(fd, addr, wmode, off, buf, len);
		if (n <= 0) { fprintf(stderr, "read fail @%x\n", off); return 1; }
		if (argc > 7) {
			FILE *f = fopen(argv[7], "wb");
			fwrite(buf, 1, n, f); fclose(f);
			fprintf(stderr, "wrote %d bytes to %s\n", n, argv[7]);
		} else {
			for (int i = 0; i < n; i += 16) {
				printf("%06x  ", off + i);
				for (int j = i; j < i + 16 && j < n; j++) printf("%02x ", buf[j]);
				printf(" |");
				for (int j = i; j < i + 16 && j < n; j++)
					putchar(buf[j] >= 32 && buf[j] < 127 ? buf[j] : '.');
				printf("|\n");
			}
		}
		return 0;
	}

	if (!strcmp(cmd, "raw")) {
		int a = strtol(argv[3], NULL, 0);
		int len = atoi(argv[4]);
		unsigned char r[512] = {0};
		if (xfer(fd, a, NULL, 0, r, len) == 0) {
			for (int i = 0; i < len; i++) {
				printf("%02x ", r[i]);
				if ((i & 15) == 15) printf("\n");
			}
			printf("\n");
		} else puts("ERR");
		return 0;
	}

	if (!strcmp(cmd, "w8")) {
		/* w8 <addr> <reg> <val> — single-byte write */
		unsigned char w[2] = { strtol(argv[4], NULL, 0), strtol(argv[5], NULL, 0) };
		int a = strtol(argv[3], NULL, 0);
		return xfer(fd, a, w, 2, NULL, 0);
	}

	if (!strcmp(cmd, "w16")) {
		/* w16 <addr> <reg> <val16> — reg + 16-bit BE data (VCM DAC) */
		unsigned int v = strtol(argv[5], NULL, 0);
		unsigned char w[3] = { strtol(argv[4], NULL, 0), v >> 8, v & 0xff };
		int a = strtol(argv[3], NULL, 0);
		return xfer(fd, a, w, 3, NULL, 0);
	}

	if (!strcmp(cmd, "rd16")) {
		int a = strtol(argv[3], NULL, 0);
		unsigned int reg = strtol(argv[4], NULL, 0);
		int len = argc > 5 ? atoi(argv[5]) : 2;
		unsigned char w[2] = {reg >> 8, reg & 0xff}, r[64] = {0};
		if (xfer(fd, a, w, 2, r, len) == 0) {
			for (int i = 0; i < len; i++) printf("%02x ", r[i]);
			printf("\n");
		} else puts("ERR");
		return 0;
	}
	if (!strcmp(cmd, "size")) {
		/* probe reads at increasing offsets; report where reads fail or data wraps */
		unsigned char b0[16], b[16];
		if (ee_read(fd, addr, wmode, 0, b0, 16) <= 0) { puts("no dev"); return 1; }
		for (unsigned int off = 0x100; off <= 0x10000; off <<= 1) {
			int n = ee_read(fd, addr, wmode, off, b, 16);
			if (n <= 0) { printf("fail @0x%x\n", off); break; }
			if (!memcmp(b, b0, 16)) { printf("wrap @0x%x (size ~0x%x)\n", off, off); break; }
			printf("ok @0x%x: %02x%02x%02x%02x\n", off, b[0], b[1], b[2], b[3]);
		}
		return 0;
	}
	return 1;
}
