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
#include <dirent.h>
#include <errno.h>
#include <linux/input.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>
#include <poll.h>
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

struct send_mod_req {
	uint32_t app_id;
	uint32_t req_len;
	uint32_t rsp_len;
	uint32_t data_len;
	uint32_t phys_offset;
	uint32_t rsp_offset;
	uint64_t req_ptr;
	uint64_t rsp_ptr;
	uint64_t data_ptr;
};

#define QSEECOM_FP_IOCTL_LOAD _IOWR(0xf5, 1, struct load_req)
#define QSEECOM_FP_IOCTL_SEND _IOWR(0xf5, 2, struct send_req)
#define QSEECOM_FP_IOCTL_SEND_MOD _IOWR(0xf5, 3, struct send_mod_req)
#define QSEECOM_FP_IOCTL_UNLOAD _IOW(0xf5, 4, struct load_req)

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
		sleep_ms(200);
		write_str(avdd, "1");
		sleep_ms(50);
	}
	pulse_reset();
	sleep_ms(30);
}

static int wait_fingerprint_irq(unsigned int timeout_ms)
{
	struct dirent *entry;
	struct pollfd pfd[16];
	char path[64], name[128];
	DIR *dir;
	unsigned int count = 0, i;
	int fd, ready, ret = -1;
	struct input_event ev;
	time_t end = time(NULL) + (timeout_ms + 999) / 1000;

	dir = opendir("/dev/input");
	if (!dir)
		return -1;

	while ((entry = readdir(dir)) && count < sizeof(pfd) / sizeof(pfd[0])) {
		if (strncmp(entry->d_name, "event", 5))
			continue;

		snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0 ||
		    strcmp(name, "gpio-keys")) {
			close(fd);
			continue;
		}
		pfd[count].fd = fd;
		pfd[count].events = POLLIN;
		pfd[count].revents = 0;
		count++;
	}
	closedir(dir);
	if (!count)
		return -1;

	printf("waiting for fingerprint IRQ (KEY_F13)...\n");
	while (time(NULL) < end) {
		int ms = (int)((end - time(NULL)) * 1000);

		ready = poll(pfd, count, ms > 1000 ? 1000 : ms);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (!ready)
			continue;
		for (i = 0; i < count; i++) {
			if (!(pfd[i].revents & POLLIN))
				continue;
			while (read(pfd[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_KEY && ev.code == KEY_F13 &&
				    ev.value == 1) {
					printf("fingerprint IRQ event received\n");
					ret = 0;
					goto out;
				}
			}
		}
	}
	printf("fingerprint IRQ wait timed out\n");
out:
	for (i = 0; i < count; i++)
		close(pfd[i].fd);
	return ret;
}

/*
 * Modified command: request[0]=cmd, request[4]=phys(shared data) filled by the
 * driver, request[12]=arg; the TA may read/write the shared buffer.
 */
static uint32_t g_dlen = 0x1000;
static int g_dump;
static int g_quiet;
static uint32_t g_size8;
static uint32_t g_last_rx[16];
static const uint8_t *g_data_in;
static uint32_t g_data_in_len;

/*
 * Fetch the session key data from the keymaster TA, as the vendor HAL does
 * in silfp_qsee_keymaster_get(): look up "keymaster64" (resident in TZ, no
 * image needed), send command 0x205/2 as a plain request and extract the
 * [status][offset][size][blob] response.  The blob (~152 bytes) is then fed
 * to the fingerprint TA via SET_KEY_DATA.
 */
static int km_get_key_data(int fd, uint8_t *out, uint32_t out_cap,
			   uint32_t *out_len)
{
	struct load_req kreq;
	uint32_t tx[16] = { 0x205, 0x2 };
	uint8_t rx[0x200];
	struct send_req sreq;
	uint32_t status, off, size;
	uint8_t dummy[4] = { 0 };
	int ret;

	memset(&kreq, 0, sizeof(kreq));
	kreq.name_len = 11;
	kreq.img_len = sizeof(dummy);
	kreq.name_ptr = (uint64_t)(uintptr_t)"keymaster64";
	kreq.img_ptr = (uint64_t)(uintptr_t)dummy;

	ret = ioctl(fd, QSEECOM_FP_IOCTL_LOAD, &kreq);
	if (ret) {
		printf("keymaster64 lookup failed: %s\n", strerror(errno));
		return -1;
	}
	printf("keymaster64 app_id %u\n", kreq.app_id);

	memset(rx, 0, sizeof(rx));
	memset(&sreq, 0, sizeof(sreq));
	sreq.app_id = kreq.app_id;
	sreq.req_len = sizeof(tx);
	sreq.rsp_len = sizeof(rx);
	sreq.req_ptr = (uint64_t)(uintptr_t)tx;
	sreq.rsp_ptr = (uint64_t)(uintptr_t)rx;
	if (ioctl(fd, QSEECOM_FP_IOCTL_SEND, &sreq)) {
		printf("keymaster cmd: %s\n", strerror(errno));
		return -1;
	}

	memcpy(&status, rx, 4);
	memcpy(&off, rx + 4, 4);
	memcpy(&size, rx + 8, 4);
	printf("keymaster rsp: status=%u off=%u size=%u\n",
	       status, off, size);
	if (status || !size || off + size > sizeof(rx) || size > out_cap)
		return -1;
	memcpy(out, rx + off, size);
	*out_len = size;
	return 0;
}

static int send_mod2(int fd, uint32_t app_id, uint32_t cmd, uint32_t arg,
		     uint32_t arg2)
{
	uint8_t req[0x40];
	uint8_t rsp[0x40];
	uint8_t *data = calloc(1, g_dlen);
	struct send_mod_req sreq;
	int i;

	if (!data)
		return -1;
	if (g_data_in && g_data_in_len <= g_dlen)
		memcpy(data, g_data_in, g_data_in_len);
	memset(req, 0, sizeof(req));
	memset(rsp, 0, sizeof(rsp));
	{
		uint32_t zero = 0;

		memcpy(req, &cmd, 4);
		memcpy(req + 8, &g_size8, 4);
		memcpy(req + 12, &arg, 4);
		memcpy(req + 16, &arg2, 4);
	}

	memset(&sreq, 0, sizeof(sreq));
	sreq.app_id = app_id;
	sreq.req_len = sizeof(req);
	sreq.rsp_len = sizeof(rsp);
	sreq.data_len = g_dlen;
	sreq.phys_offset = 4;
	sreq.rsp_offset = 0x500;
	sreq.req_ptr = (uint64_t)(uintptr_t)req;
	sreq.rsp_ptr = (uint64_t)(uintptr_t)rsp;
	sreq.data_ptr = (uint64_t)(uintptr_t)data;

	if (ioctl(fd, QSEECOM_FP_IOCTL_SEND_MOD, &sreq)) {
		printf("mod 0x%02x(%u): ioctl failed: %s\n", cmd, arg,
		       strerror(errno));
		free(data);
		return -1;
	}
	printf("mod 0x%02x(%u,%u) -> req[4]=0x%08x rsp:", cmd, arg, arg2,
	       ((uint32_t *)req)[1]);
	for (i = 0; i < 6; i++)
		printf(" 0x%08x", ((uint32_t *)rsp)[i]);
	printf(" | data:");
	for (i = 0; i < 8; i++)
		printf(" 0x%08x", ((uint32_t *)data)[i]);
	printf("\n");
	if (g_dump) {
		uint32_t sum = 0, j;

		for (j = 0; j < g_dlen; j++)
			sum += data[j];
		printf("  data[0..0x%x] sum=0x%08x\n", g_dlen, sum);
	}
	free(data);
	return 0;
}

static int send_mod(int fd, uint32_t app_id, uint32_t cmd, uint32_t arg)
{
	return send_mod2(fd, app_id, cmd, arg, 0);
}

static int send_cmd(int fd, uint32_t app_id, uint32_t cmd, uint32_t a0,
		    uint32_t a1, uint32_t a2, uint32_t a3)
{
	uint32_t tx[8] = { cmd, 0, 0, 0, a0, a1, a2, a3 };
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
	memcpy(g_last_rx, rx, sizeof(g_last_rx));
	if (!g_quiet) {
		printf("cmd 0x%02x(%u,%u,%u,%u) ->", cmd, a0, a1, a2, a3);
		for (i = 0; i < 6; i++)
			printf(" 0x%08x", rx[i]);
		printf("\n");
	}
	return 0;
}

/*
 * Plain NAV polling (NAV_CAPTURE_IMG + NAV_STEP, ~200ms apart) is the only
 * detector proven to see touch events (0x8/0x9) on this sensor.  Inserting
 * CHECK_ESD / MODE_DOWNLOAD commands between polls masks the event queue.
 */
static int wait_nav_finger(int fd, uint32_t app_id, unsigned int attempts)
{
	unsigned int i;

	g_quiet = 1;
	for (i = 0; i < attempts; i++) {
		uint32_t capture, step;

		send_cmd(fd, app_id, 0x03, 0, 0, 0, 0);
		capture = g_last_rx[0];
		send_cmd(fd, app_id, 0x0b, 0, 0, 0, 0);
		step = g_last_rx[0];
		if (capture == 8 || capture == 9 || step == 8 || step == 9) {
			g_quiet = 0;
			printf("NAV finger event: capture=%u step=%u after %u polls\n",
			       capture, step, i + 1);
			return 0;
		}
		sleep_ms(200);
	}
	g_quiet = 0;
	printf("NAV finger wait timed out (%u polls)\n", attempts);
	return -1;
}

static void drain_nav_events(int fd, uint32_t app_id)
{
	int i;

	g_quiet = 1;
	for (i = 0; i < 8; i++) {
		send_cmd(fd, app_id, 0x03, 0, 0, 0, 0);
		send_cmd(fd, app_id, 0x0b, 0, 0, 0, 0);
		sleep_ms(200);
	}
	g_quiet = 0;
	printf("discarded startup NAV events; ready for a fresh touch\n");
}

/*
 * The sensor drives GPIO83 with a pulse train while a finger is held
 * (~25 edges/s).  Count the kernel's interrupt counter as a fallback
 * detector: NAV polling missed touches, but the AP-side IRQ storm is a
 * reliable touch indication once GPIO83 has pull-up + both edges.
 */
static unsigned long read_irq_count(void)
{
	static char buf[65536];
	int fd;
	ssize_t n = 0, r;
	char *p;

	fd = open("/proc/interrupts", O_RDONLY);
	if (fd < 0)
		return 0;
	while (n < (ssize_t)sizeof(buf) - 1 &&
	       (r = read(fd, buf + n, sizeof(buf) - 1 - n)) > 0)
		n += r;
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = 0;

	p = strstr(buf, "fingerprint_irq");
	if (!p)
		return 0;
	for (; p > buf && p[-1] != '\n'; p--)
		;
	p = strchr(p, ':');
	if (!p)
		return 0;
	p++;
	while (*p == ' ')
		p++;
	return strtoul(p, NULL, 10);
}

static int wait_irq_storm(unsigned int timeout_ms)
{
	unsigned long start, now, waited = 0;

	start = read_irq_count();
	printf("waiting for GPIO83 IRQ activity (start=%lu)...\n", start);
	while (waited < timeout_ms) {
		sleep_ms(100);
		waited += 100;
		now = read_irq_count();
		if (now > start + 5) {
			printf("GPIO83 IRQ activity: %lu -> %lu after %lu ms\n",
			       start, now, waited);
			return 0;
		}
	}
	printf("GPIO83 IRQ wait timed out\n");
	return -1;
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

	setvbuf(stdout, NULL, _IONBF, 0);

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

	/*
	 * A previous run may have left the TA half-configured in TZ (a
	 * killed process never sends cleanup commands, and later loads
	 * then re-attach to that stale instance).  Always shut down any
	 * leftover instance first so each run starts with a fresh TA.
	 */
	if (ioctl(fd, QSEECOM_FP_IOCTL_UNLOAD, &req))
		printf("no stale TA to unload (%s)\n", strerror(errno));
	else
		printf("unloaded stale TA instance\n");

	ret = ioctl(fd, QSEECOM_FP_IOCTL_LOAD, &req);
	if (ret) {
		/* The app stays loaded across tool runs: reuse its id. */
		if (argc > 3) {
			printf("load failed (%s), reusing app_id 3\n",
			       strerror(errno));
			req.app_id = 3;
		} else {
			perror("QSEECOM_FP_IOCTL_LOAD");
			close(fd);
			return 1;
		}
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
	} else if (argc > 3 && !strcmp(argv[3], "scanmod")) {
		uint32_t first = argc > 4 ? strtoul(argv[4], NULL, 0) : 0;
		uint32_t last = argc > 5 ? strtoul(argv[5], NULL, 0) : 31;
		uint32_t d;

		power_cycle();
		for (d = first; d <= last; d++) {
			printf("--- dev %u\n", d);
			send_mod(fd, req.app_id, 0x20, d);
			send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00,
				 0x64000, 0);
		}
	} else if (argc > 3 && !strcmp(argv[3], "mini")) {
		int i;

		power_cycle();
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		printf("TOUCH_NOW\n");
		for (i = 0; i < 20; i++) {
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);
			sleep_ms(400);
		}
	} else if (argc > 3 && !strcmp(argv[3], "speedtest")) {
		static const uint32_t speeds[] = { 1000000, 2000000, 4800000 };
		unsigned k;

		for (k = 0; k < sizeof(speeds) / sizeof(speeds[0]); k++) {
			printf("=== speed %u\n", speeds[k]);
			power_cycle();
			send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
			send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
			send_mod(fd, req.app_id, 0x20, 14);
			send_cmd(fd, req.app_id, 0x0e, 0x80064, speeds[k], 0x64000, 0);
			send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
			send_mod(fd, req.app_id, 0x1b, 9);
			send_mod(fd, req.app_id, 0x30, 152);
			send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_mod2(fd, req.app_id, 0x36, 4, 2);
			send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
			printf("TOUCH_NOW\n");
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x05, 0, 0, 0, 0);
		}
	} else if (argc > 3 && !strcmp(argv[3], "authnav")) {
		/*
		 * Minimal "cap" init sequence: the extra config/calibration/mode
		 * commands used by other modes mask the NAV touch events, so
		 * keep exactly what the working cap mode sends before polling.
		 */
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		{
			static uint8_t key_data[256];
			uint32_t key_len = 0;

			if (!km_get_key_data(fd, key_data, sizeof(key_data),
					     &key_len)) {
				g_data_in = key_data;
				g_data_in_len = key_len;
			}
			send_mod(fd, req.app_id, 0x30, 152);
			g_data_in = NULL;
			g_data_in_len = 0;
		}
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		drain_nav_events(fd, req.app_id);
		printf("NAV ready; touch and hold the enrolled finger\n");
		if (!wait_nav_finger(fd, req.app_id, 1800)) {
			int attempt;

			/* Vendor auth flow after a touch: NAV_END, AUTH_START,
			 * mode [3,0]/[3,1] transitions with ESD checks between
			 * captures. */
			send_cmd(fd, req.app_id, 0x0c, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x04, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			for (attempt = 0; attempt < 8; attempt++) {
				int capture_status, auth_status;

				send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
				send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);
				capture_status = (int32_t)g_last_rx[4];
				printf("capture attempt %d status %d\n",
				       attempt + 1, capture_status);
				if (!capture_status) {
					send_cmd(fd, req.app_id, 0x05, 0, 0, 0, 0);
					auth_status = (int32_t)g_last_rx[4];
					printf("AUTH_STEP status %d\n", auth_status);
					if (!auth_status)
						break;
				}

				if (attempt != 7) {
					send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
					send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
					sleep_ms(250);
				}
			}
			send_cmd(fd, req.app_id, 0x06, 0, 0, 0, 0);
		}
	} else if (argc > 3 && !strcmp(argv[3], "authwait")) {
		pulse_reset();
		sleep_ms(30);
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x8004);
		send_mod(fd, req.app_id, 0x1b, 9);
		g_dlen = 152;
		send_mod(fd, req.app_id, 0x30, 152);
		g_dlen = 0x1000;
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 4, 0x1013);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x36, 4, 2);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0c, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x04, 0, 0, 0, 0);
		printf("AUTH_STARTED; touch the sensor once, then hold\n");
		if (!wait_fingerprint_irq(300000)) {
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x05, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x06, 0, 0, 0, 0);
		}
	} else if (argc > 3 && !strcmp(argv[3], "authreset")) {
		pulse_reset();
		sleep_ms(30);
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);

		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x05, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "esdexact")) {
		pulse_reset();
		sleep_ms(30);
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x8004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 4, 0x1013);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		printf("ESD after nav support\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x36, 4, 2);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
		printf("ESD after auth MODE 3/0 (1)\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
		printf("ESD after auth MODE 3/0 (2)\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
		printf("ESD after auth MODE 3/1\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		printf("TOUCH_NOW\n");
		send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x05, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "esdstep")) {
		pulse_reset();
		sleep_ms(30);
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		printf("ESD after INIT\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		printf("ESD after config\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x8004);
		send_mod(fd, req.app_id, 0x1b, 9);
		printf("ESD after INIT2\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 4, 0x1013);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		printf("ESD after versions/OTP/nav support\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
		printf("ESD after MODE 3/0\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
		printf("ESD after MODE 3/1\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "authvendor")) {
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);

		printf("AUTH_START: vendor MODE_DOWNLOAD sequence\n");
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		printf("TOUCH_NOW\n");
		send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x05, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x06, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "authloop")) {
		int i;

		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x36, 4, 2);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		printf("TOUCH_NOW\n");
		for (i = 0; i < 30; i++) {
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x05, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x06, 0, 0, 0, 0);
			sleep_ms(500);
		}
		printf("TOUCH_DONE\n");
	} else if (argc > 3 && !strcmp(argv[3], "auth")) {
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		printf("TOUCH_NOW\n");
		sleep_ms(3000);
		send_cmd(fd, req.app_id, 0x02, 0, 0, 0, 0);   /* CAPTURE_IMG */
		send_cmd(fd, req.app_id, 0x05, 0, 0, 0, 0);   /* AUTH_STEP */
		send_cmd(fd, req.app_id, 0x06, 0, 0, 0, 0);   /* AUTH_END */
	} else if (argc > 3 && !strcmp(argv[3], "test3")) {
		static const uint32_t sizes[] = { 0x20000, 0x40000, 0x64000 };
		unsigned k;

		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x36, 4, 2);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		g_dump = 1;
		printf("TOUCH_NOW\n");
		for (k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
			printf("--- capture size 0x%x\n", sizes[k]);
			g_dlen = sizes[k];
			g_size8 = sizes[k];
			send_mod2(fd, req.app_id, 0x52, 0, 0);
		}
	} else if (argc > 3 && !strcmp(argv[3], "test2")) {
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		g_dlen = 0x10000;
		g_dump = 1;
		printf("TOUCH_NOW\n");
		printf("--- variant a: [8]=size\n");
		g_size8 = 0x10000;
		send_mod2(fd, req.app_id, 0x52, 0, 0);
		printf("--- variant b: [12]=size\n");
		g_size8 = 0;
		send_mod2(fd, req.app_id, 0x52, 0x10000, 0);
		printf("--- variant c: normal\n");
		send_cmd(fd, req.app_id, 0x52, 0, 0, 0, 0);
		printf("--- variant d: 0x53 modified\n");
		send_mod2(fd, req.app_id, 0x53, 0, 0);
		send_cmd(fd, req.app_id, 0x54, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "test")) {
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x56, 0, 0, 0, 0);   /* img info */
		g_dlen = 0x10000;
		g_dump = 1;
		printf("TOUCH_NOW\n");
		send_mod2(fd, req.app_id, 0x52, 0, 0);        /* TEST_IMAGE_CAPTURE */
		send_cmd(fd, req.app_id, 0x54, 0, 0, 0, 0);   /* TEST_IMAGE_FINISH */
	} else if (argc > 3 && !strcmp(argv[3], "scanid")) {
		uint32_t id;

		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		for (id = 0; id <= 0x60; id++) {
			if (id == 0x0e || id == 0x1b || id == 0x20 || id == 0x30 ||
			    id == 0x50 || id == 0x39 || id == 0x59)
				continue;
			send_cmd(fd, req.app_id, id, 0, 0, 0, 0);
		}
	} else if (argc > 3 && !strcmp(argv[3], "shot")) {
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		printf("TOUCH_NOW\n");
		g_dlen = 0x10000;
		g_dump = 1;
		{
			int i;

			for (i = 0; i < 120; i++) {
				send_cmd(fd, req.app_id, 0x22, 0, 0, 0, 0);
				send_mod2(fd, req.app_id, 0x23, 0, 0);
				send_cmd(fd, req.app_id, 0x24, 0, 0, 0, 0);
				sleep_ms(400);
			}
		}
		printf("TOUCH_DONE\n");
	} else if (argc > 3 && !strcmp(argv[3], "cap")) {
		int i;

		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		printf("TOUCH_NOW\n");
		for (i = 0; i < 900; i++) {
			send_cmd(fd, req.app_id, 0x03, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x0b, 0, 0, 0, 0);
			sleep_ms(200);
		}
		printf("TOUCH_DONE\n");
	} else if (argc > 3 && !strcmp(argv[3], "full")) {
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		send_mod2(fd, req.app_id, 0x59, 0xf000, 0x8004);
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x36, 4, 2);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		/* nav poll: capture + step, as the HAL does on a finger IRQ */
		send_cmd(fd, req.app_id, 0x03, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0b, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x03, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0b, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "seq")) {
		/* Vendor init order, as seen in the Android trace. */
		power_cycle();
		send_mod(fd, req.app_id, 0x59, 8);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "spi")) {
		uint32_t dev = argc > 4 ? strtoul(argv[4], NULL, 0) : 14;

		power_cycle();
		send_mod(fd, req.app_id, 0x20, dev);          /* SET_SPI_DEV */
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);   /* GET_OTP */
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);   /* NAV_SUPPORT */
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "mod")) {
		uint32_t cmd = argc > 4 ? strtoul(argv[4], NULL, 0) : 0x20;
		uint32_t arg = argc > 5 ? strtoul(argv[5], NULL, 0) : 14;

		power_cycle();
		send_mod(fd, req.app_id, cmd, arg);
	} else if (argc > 3) {
		uint32_t cmd = strtoul(argv[3], NULL, 0);

		send_cmd(fd, req.app_id, cmd, 0, 0, 0, 0);
	}

	close(fd);
	return 0;
}
