// SPDX-License-Identifier: GPL-2.0
/*
 * ta-load - build a QSEE TA image from .mdt/.b0x files and load it into the
 * secure world through /dev/qseecom-fp.
 *
 * The image layout matches what libQSEEComAPI produces: every ELF segment
 * (b00..bNN) is copied to its p_offset, the buffer is zero-padded up to
 * max(p_offset + p_filesz).  APP_START is called with mdt_len = 0, which is
 * what the vendor loader passes on this platform.
 *
 * Usage: ta-load <dir> <name>
 */
#include <elf.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

struct load_req {
	uint32_t name_len;
	uint32_t img_len;
	uint64_t name_ptr;
	uint64_t img_ptr;
	uint32_t app_id;
	uint32_t reserved;
};

struct send_req {
	uint32_t app_id;
	uint32_t req_len;
	uint32_t rsp_len;
	uint32_t reserved;
	uint64_t req_ptr;
	uint64_t rsp_ptr;
};

#define QSEECOM_FP_IOCTL_LOAD _IOWR(0xf5, 1, struct load_req)
#define QSEECOM_FP_IOCTL_SEND _IOWR(0xf5, 2, struct send_req)

static uint8_t *read_file(const char *path, size_t *len)
{
	struct stat st;
	uint8_t *buf;
	int fd;
	ssize_t n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st)) {
		close(fd);
		return NULL;
	}
	buf = calloc(1, st.st_size);
	if (!buf) {
		close(fd);
		return NULL;
	}
	n = read(fd, buf, st.st_size);
	close(fd);
	if (n != st.st_size) {
		free(buf);
		return NULL;
	}
	*len = st.st_size;
	return buf;
}

static int write_str(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = write(fd, val, strlen(val));
	close(fd);
	return n == (ssize_t)strlen(val) ? 0 : -1;
}

/* Active-low reset exposed as the gpio-leds "fp:reset" device. */
static void sleep_ms(long ms)
{
	struct timespec ts = { .tv_sec = ms / 1000,
			       .tv_nsec = (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static void pulse_reset(void)
{
	const char *led = "/sys/class/leds/fp:reset/brightness";

	if (access(led, W_OK))
		return;
	write_str(led, "1");	/* assert (low) */
	sleep_ms(5);
	write_str(led, "0");	/* deassert (high) */
	sleep_ms(20);
}

/* Power-cycle the sensor like the vendor driver: AVDD off/on, then reset. */
static void power_cycle(void)
{
	const char *avdd = "/sys/class/leds/fp:avdd/brightness";

	if (!access(avdd, W_OK)) {
		write_str(avdd, "0");
		sleep_ms(20);
		write_str(avdd, "1");
		sleep_ms(20);
	}
	pulse_reset();
}

static int send_cmd(int fd, uint32_t app_id, uint32_t cmd, uint32_t a0,
		    uint32_t a1, uint32_t a2, uint32_t a3)
{
	uint32_t tx[8] = { cmd, a0, a1, a2, a3, 0, 0, 0 };
	uint32_t rx[16];
	struct send_req sreq;
	int i;

	memset(rx, 0, sizeof(rx));
	memset(&sreq, 0, sizeof(sreq));
	sreq.app_id = app_id;
	sreq.req_len = sizeof(tx);
	sreq.rsp_len = sizeof(rx);
	sreq.req_ptr = (uint64_t)(uintptr_t)tx;
	sreq.rsp_ptr = (uint64_t)(uintptr_t)rx;

	if (ioctl(fd, QSEECOM_FP_IOCTL_SEND, &sreq)) {
		printf("cmd 0x%02x: ioctl failed: %s\n", cmd, strerror(errno));
		return -1;
	}
	printf("cmd 0x%02x(%u,%u,%u,%u) ->", cmd, a0, a1, a2, a3);
	for (i = 0; i < 6; i++)
		printf(" 0x%08x", rx[i]);
	printf("\n");
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir, *name;
	char path[512];
	uint8_t *mdt, *img, *seg;
	size_t mdt_len, seg_len, img_len = 0, off;
	uint16_t phnum, phentsize;
	uint64_t phoff;
	int elfclass, i, fd, ret;
	struct load_req req;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <dir> <name>\n", argv[0]);
		return 1;
	}
	dir = argv[1];
	name = argv[2];

	snprintf(path, sizeof(path), "%s/%s.mdt", dir, name);
	mdt = read_file(path, &mdt_len);
	if (!mdt) {
		fprintf(stderr, "cannot read %s\n", path);
		return 1;
	}

	elfclass = mdt[EI_CLASS];
	if (elfclass == ELFCLASS32) {
		Elf32_Ehdr *eh = (Elf32_Ehdr *)mdt;

		phoff = eh->e_phoff;
		phentsize = eh->e_phentsize;
		phnum = eh->e_phnum;
	} else if (elfclass == ELFCLASS64) {
		Elf64_Ehdr *eh = (Elf64_Ehdr *)mdt;

		phoff = eh->e_phoff;
		phentsize = eh->e_phentsize;
		phnum = eh->e_phnum;
	} else {
		fprintf(stderr, "unsupported ELF class %d\n", elfclass);
		return 1;
	}
	printf("TA %s: ELF%d, %u segments, mdt %zu bytes\n", name,
	       elfclass == ELFCLASS64 ? 64 : 32, phnum, mdt_len);

	for (i = 0; i < phnum; i++) {
		uint64_t p_offset, p_filesz;

		if (elfclass == ELFCLASS64) {
			Elf64_Phdr *ph = (Elf64_Phdr *)(mdt + phoff + i * phentsize);

			p_offset = ph->p_offset;
			p_filesz = ph->p_filesz;
		} else {
			Elf32_Phdr *ph = (Elf32_Phdr *)(mdt + phoff + i * phentsize);

			p_offset = ph->p_offset;
			p_filesz = ph->p_filesz;
		}
		if (p_offset + p_filesz > img_len)
			img_len = p_offset + p_filesz;
	}

	img = calloc(1, img_len);
	if (!img) {
		fprintf(stderr, "out of memory (%zu)\n", img_len);
		return 1;
	}

	for (i = 0; i < phnum; i++) {
		uint64_t p_offset, p_filesz;

		if (elfclass == ELFCLASS64) {
			Elf64_Phdr *ph = (Elf64_Phdr *)(mdt + phoff + i * phentsize);

			p_offset = ph->p_offset;
			p_filesz = ph->p_filesz;
		} else {
			Elf32_Phdr *ph = (Elf32_Phdr *)(mdt + phoff + i * phentsize);

			p_offset = ph->p_offset;
			p_filesz = ph->p_filesz;
		}

		snprintf(path, sizeof(path), "%s/%s.b%02d", dir, name, i);
		seg = read_file(path, &seg_len);
		if (!seg) {
			fprintf(stderr, "cannot read %s\n", path);
			return 1;
		}
		off = seg_len < p_filesz ? seg_len : p_filesz;
		memcpy(img + p_offset, seg, off);
		printf("  seg %d: %s %zu bytes -> +0x%llx\n", i, path, seg_len,
		       (unsigned long long)p_offset);
		free(seg);
	}

	/* Keep a copy for offline comparison. */
	snprintf(path, sizeof(path), "/tmp/%s.img", name);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		if (write(fd, img, img_len) != (ssize_t)img_len)
			fprintf(stderr, "short write to %s\n", path);
		close(fd);
	}

	fd = open("/dev/qseecom-fp", O_RDWR);
	if (fd < 0) {
		perror("/dev/qseecom-fp");
		return 1;
	}

	memset(&req, 0, sizeof(req));
	req.name_len = strlen(name);
	req.img_len = img_len;
	req.name_ptr = (uint64_t)(uintptr_t)name;
	req.img_ptr = (uint64_t)(uintptr_t)img;

	ret = ioctl(fd, QSEECOM_FP_IOCTL_LOAD, &req);
	if (ret) {
		perror("QSEECOM_FP_IOCTL_LOAD");
		close(fd);
		return 1;
	}

	printf("loaded %s: image %zu bytes, app_id %u\n", name, img_len,
	       req.app_id);

	if (argc > 3 && !strcmp(argv[3], "scan")) {
		uint32_t first = argc > 4 ? strtoul(argv[4], NULL, 0) : 0;
		uint32_t last = argc > 5 ? strtoul(argv[5], NULL, 0) : 31;
		uint32_t d;

		power_cycle();
		for (d = first; d <= last; d++) {
			uint32_t rsp[6] = { 0 };

			send_cmd(fd, req.app_id, 0x20, d, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00,
				 0x64000, 0);
			(void)rsp;
		}
	} else if (argc > 3 && !strcmp(argv[3], "probe")) {
		power_cycle();
		/* Same order the vendor HAL uses to bring the sensor up. */
		send_cmd(fd, req.app_id, 0x1f, 0x2, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x20, 14, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
	} else if (argc > 3) {
		uint32_t cmd = strtoul(argv[3], NULL, 0);

		send_cmd(fd, req.app_id, cmd, 0, 0, 0, 0);
	}

	close(fd);
	return 0;
}
