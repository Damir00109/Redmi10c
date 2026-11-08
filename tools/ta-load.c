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

/* /dev/silead_fp — vendor-compatible AP-side ioctls (magic 's') */
struct fp_dev_init_t {
	uint8_t mode;
	uint8_t bits;
	uint16_t delay;
	uint32_t speed;
	char dev[16];
	uint8_t nl_id;
	uint8_t dev_id;
	uint16_t reserve;
	uint32_t reg;
	char ta[16];
};

#define SIFP_IOC_RESET		_IOW('s', 10, uint8_t)
#define SIFP_IOC_ENABLE_IRQ	_IO('s', 11)
#define SIFP_IOC_DISABLE_IRQ	_IO('s', 12)
#define SIFP_IOC_WAIT_IRQ	_IOR('s', 13, uint8_t)
#define SIFP_IOC_CLR_IRQ	_IO('s', 14)
#define SIFP_IOC_INIT		_IOR('s', 16, struct fp_dev_init_t)
#define SIFP_IOC_DEINIT		_IO('s', 17)
#define SIFP_IOC_IRQ_STATUS	_IOR('s', 18, uint8_t)
#define SIFP_IOC_PWDN		_IOW('s', 28, uint8_t)

static int fp_fd = -1;

static int fp_open(void)
{
	struct fp_dev_init_t init;
	int r;

	if (fp_fd >= 0)
		return 0;
	fp_fd = open("/dev/silead_fp", O_RDWR);
	if (fp_fd < 0) {
		printf("open /dev/silead_fp: %s\n", strerror(errno));
		return -1;
	}
	memset(&init, 0, sizeof(init));
	r = ioctl(fp_fd, SIFP_IOC_INIT, &init);
	printf("silead_fp INIT ret=%d dev_id=%u speed=%u ta=%s\n",
	       r, init.dev_id, init.speed, init.ta);
	if (r)
		return -1;
	return 0;
}

static void fp_reset(uint8_t delay)
{
	if (fp_fd >= 0)
		ioctl(fp_fd, SIFP_IOC_RESET, &delay);
}

static void fp_irq_enable(void)
{
	if (fp_fd >= 0)
		ioctl(fp_fd, SIFP_IOC_ENABLE_IRQ);
}

static int fp_wait_irq(void)
{
	uint8_t seen = 0;
	int r;

	if (fp_fd < 0)
		return -1;
	r = ioctl(fp_fd, SIFP_IOC_WAIT_IRQ, &seen);
	printf("WAIT_IRQ ret=%d seen=%u\n", r, seen);
	return seen ? 0 : -1;
}

static void fp_pwdn(uint8_t flag)
{
	if (fp_fd >= 0)
		ioctl(fp_fd, SIFP_IOC_PWDN, &flag);
}

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

static void sleep_ms(long ms)
{
	struct timespec ts = { .tv_sec = ms / 1000,
			       .tv_nsec = (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

/*
 * Sensor power/reset go through /dev/silead_fp ioctls (vendor
 * SIFP_IOC_* contract).  The gpio-leds nodes (fp:reset/fp:avdd) and
 * the gpio-keys fingerprint_irq were removed when the real driver
 * landed — these helpers now just wrap the ioctls.
 */
static void pulse_reset(void)
{
	uint8_t d = 0;

	if (fp_open())
		return;
	fp_reset(d);
	sleep_ms(30);
}

/* Power-cycle the sensor: avdd off/on 200ms + reset (vendor PWDN 2). */
static void power_cycle(void)
{
	if (fp_open())
		return;
	fp_pwdn(2);
	pulse_reset();
	sleep_ms(30);
}

static int wait_fingerprint_irq(unsigned int timeout_ms);

/* Prefer the real /dev/silead_fp WAIT_IRQ; fall back to gpio-keys evdev. */
static int wait_irq_any(unsigned int timeout_ms)
{
	if (!fp_open())
		return fp_wait_irq();
	return wait_fingerprint_irq(timeout_ms);
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
static uint32_t g_phys_off = 4;
static int g_dump;
static int g_quiet;
static int esd_recover(int fd, uint32_t app_id);

static uint32_t g_size8;
static double g_last_ms;
static uint32_t g_last_rx[16];
static int g_save_frame;
static uint32_t g_last_data_sum;
static const uint8_t *g_data_in;
static uint32_t g_data_in_len;
/* If set, send_mod4 copies the shared buffer here (caller sizes it). */
static uint8_t *g_frame_out;

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

static int save_frame_pgm(const uint8_t *data, uint32_t len)
{
	struct timespec ts;
	char path[128], header[32];
	size_t left;
	ssize_t n;
	int fd, header_len;

	if (len != 4096)
		return -EINVAL;
	clock_gettime(CLOCK_REALTIME, &ts);
	snprintf(path, sizeof(path),
		 "/tmp/silead-frame-%lld-%09ld.pgm",
		 (long long)ts.tv_sec, ts.tv_nsec);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -errno;
	header_len = snprintf(header, sizeof(header), "P5\n64 64\n255\n");
	if (header_len <= 0 || header_len >= (int)sizeof(header)) {
		close(fd);
		unlink(path);
		return -EIO;
	}
	left = header_len;
	while (left) {
		n = write(fd, header + header_len - left, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			unlink(path);
			return -errno;
		}
		left -= n;
	}
	left = len;
	while (left) {
		n = write(fd, data + len - left, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			unlink(path);
			return -errno;
		}
		left -= n;
	}
	close(fd);
	printf("saved opt-in 64x64 frame: %s\n", path);
	return 0;
}

static int send_mod4(int fd, uint32_t app_id, uint32_t cmd, uint32_t a0,
		     uint32_t a1, uint32_t a2, uint32_t a3)
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
		memcpy(req, &cmd, 4);
		memcpy(req + 12, &a0, 4);
		memcpy(req + 16, &a1, 4);
		memcpy(req + 20, &a2, 4);
		memcpy(req + 24, &a3, 4);
	}

	memset(&sreq, 0, sizeof(sreq));
	sreq.app_id = app_id;
	sreq.req_len = sizeof(req);
	sreq.rsp_len = sizeof(rsp);
	sreq.data_len = g_dlen;
	sreq.phys_offset = g_phys_off;
	sreq.rsp_offset = 0x500;
	sreq.req_ptr = (uint64_t)(uintptr_t)req;
	sreq.rsp_ptr = (uint64_t)(uintptr_t)rsp;
	sreq.data_ptr = (uint64_t)(uintptr_t)data;

	if (ioctl(fd, QSEECOM_FP_IOCTL_SEND_MOD, &sreq)) {
		printf("mod 0x%02x: ioctl failed: %s\n", cmd,
		       strerror(errno));
		free(data);
		return -1;
	}
	memcpy(g_last_rx, rsp, sizeof(g_last_rx));
	if (!g_quiet) {
		printf("mod 0x%02x(%u,%u,%u,%u) -> req[4]=0x%08x rsp:",
		       cmd, a0, a1, a2, a3, ((uint32_t *)req)[1]);
		for (i = 0; i < 6; i++)
			printf(" 0x%08x", ((uint32_t *)rsp)[i]);
		if (cmd == 0x52 || cmd == 0x57 || cmd == 0x1c || cmd == 0x30)
			printf(" | data: [redacted]\n");
		else {
			printf(" | data:");
			for (i = 0; i < 8; i++)
				printf(" 0x%08x", ((uint32_t *)data)[i]);
			printf("\n");
		}
	}
	if (g_frame_out)
		memcpy(g_frame_out, data, g_dlen);
	if (g_dump) {
		uint32_t sum = 0, j;

		for (j = 0; j < g_dlen; j++)
			sum += data[j];
		g_last_data_sum = sum;
		printf("  data[0..0x%x] sum=0x%08x\n", g_dlen, sum);
		if (g_save_frame && cmd == 0x52 && g_dlen == 0x1000 &&
		    sum)
			save_frame_pgm(data, g_dlen);
	}
	free(data);
	return 0;
}

static int send_mod2(int fd, uint32_t app_id, uint32_t cmd, uint32_t arg,
		     uint32_t arg2)
{
	return send_mod4(fd, app_id, cmd, arg, arg2, 0, 0);
}

static int send_mod(int fd, uint32_t app_id, uint32_t cmd, uint32_t arg)
{
	return send_mod4(fd, app_id, cmd, arg, 0, 0, 0);
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

	{
		struct timespec t0, t1;

		clock_gettime(CLOCK_MONOTONIC, &t0);
		i = ioctl(fd, QSEECOM_FP_IOCTL_SEND, &sreq);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		g_last_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
			    (t1.tv_nsec - t0.tv_nsec) / 1e6;
		if (i) {
			printf("cmd 0x%02x: ioctl failed: %s\n", cmd,
			       strerror(errno));
			return -1;
		}
	}
	memcpy(g_last_rx, rx, sizeof(g_last_rx));
	if (!g_quiet) {
		printf("cmd 0x%02x(%u,%u,%u,%u) %5.1fms ->", cmd, a0, a1, a2, a3,
		       g_last_ms);
		for (i = 0; i < 6; i++)
			printf(" 0x%08x", rx[i]);
		printf("\n");
	}
	return 0;
}

static int capture_test_frame(int fd, uint32_t app_id, int save)
{
	int capture_status;

	g_data_in = NULL;
	g_data_in_len = 0;
	g_dlen = 0x1000;
	g_dump = 1;
	g_save_frame = save;
	g_last_data_sum = 0;
	if (send_mod4(fd, app_id, 0x52, 0x1000, 0, 0, 3)) {
		g_save_frame = 0;
		g_dump = 0;
		g_dlen = 0x1000;
		return -1;
	}
	capture_status = (int32_t)g_last_rx[4];
	if (g_last_data_sum)
		printf("0x52 returned nonzero frame data\n");
	g_save_frame = 0;
	g_dump = 0;
	g_dlen = 0x1000;
	printf("TEST_IMAGE_CAPTURE status %d\n", capture_status);
	send_cmd(fd, app_id, 0x56, 0, 0, 0, 0);
	printf("img_info 0x%08x 0x%08x 0x%08x\n",
	       (uint32_t)g_last_rx[0], (uint32_t)g_last_rx[1],
	       (uint32_t)g_last_rx[2]);
	send_cmd(fd, app_id, 0x54, 0, 0, 0, 0);
	return capture_status;
}

/*
 * Plain NAV polling (NAV_CAPTURE_IMG + NAV_STEP, ~200ms apart) is the only
 * detector proven to see touch events (0x8/0x9) on this sensor.  Inserting
 * CHECK_ESD / MODE_DOWNLOAD commands between polls masks the event queue.
 */
/*
 * Vendor ESD handling (wait_finger_down): CHECK_ESD -1035 means the
 * discharge flag is latched; the HAL then runs dev_pwdn(2) = avdd
 * off/on 200ms and re-enters capture mode.  Without this the mode
 * switches answer -1036 INT_INVALID and CAPTURE times out (-1026).
 */
static int esd_recover(int fd, uint32_t app_id)
{
	send_cmd(fd, app_id, 0x38, 0, 0, 0, 0);
	if ((int32_t)g_last_rx[4] == 4)
		return 0;
	if ((int32_t)g_last_rx[4] != -1035) {
		printf("CHECK_ESD unexpected status %d; abort this touch\n",
		       (int32_t)g_last_rx[4]);
		return -1;
	}
	printf("ESD latched -> avdd power cycle\n");
	power_cycle();
	sleep_ms(600);
	send_cmd(fd, app_id, 0x01, 3, 0, 0, 0);
	send_cmd(fd, app_id, 0x01, 1, 0, 0, 0);
	sleep_ms(170);
	send_cmd(fd, app_id, 0x38, 0, 0, 0, 0);
	printf("post-recovery esd %d\n", (int32_t)g_last_rx[4]);
	return (int32_t)g_last_rx[4] == 4 ? 0 : -1;
}

static int wait_nav_finger(int fd, uint32_t app_id, unsigned int attempts)
{
	unsigned int i;

	/*
	 * Vendor NAV cadence (silead-auth-trace.txt): per poll the HAL sends
	 * CHECK_ESD then NAV_CAPTURE_IMG then NAV_STEP.  Mode commands are
	 * only in the NAV_START setup, never inside the poll loop.
	 *
	 * The TA queues events on the touch EDGE (8=down, 9=up) and the
	 * queue survives NAV_START/re-arm cycles, so the first polls after
	 * arming often return a stale event with no finger present.  Drain
	 * a few polls first; any event seen afterwards is a fresh edge.
	 */
	g_quiet = 1;
	for (i = 0; i < 3; i++) {
		send_cmd(fd, app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, app_id, 0x03, 0, 0, 0, 0);
		send_cmd(fd, app_id, 0x0b, 0, 0, 0, 0);
		sleep_ms(150);
	}
	for (i = 0; i < attempts; i++) {
		int32_t esd, cap;
		uint32_t navcap, step;

		send_cmd(fd, app_id, 0x38, 0, 0, 0, 0);
		esd = (int32_t)g_last_rx[4];
		send_cmd(fd, app_id, 0x03, 0, 0, 0, 0);
		cap = (int32_t)g_last_rx[4];
		navcap = g_last_rx[0];
		send_cmd(fd, app_id, 0x0b, 0, 0, 0, 0);
		step = g_last_rx[0];
		/*
		 * A held finger makes the sensor actually respond:
		 * CHECK_ESD returns 4 and NAV_CAPTURE_IMG returns 0.
		 * (Idle responses are -1035/-1026; NAV_STEP 8/9 are
		 * click gestures, not finger presence.)
		 */
		if (esd == 4 && cap == 0) {
			g_quiet = 0;
			printf("NAV finger present: esd=%d cap=%d step=%u after %u polls\n",
			       esd, cap, step, i + 1);
			return 0;
		}
		if (step == 8 || step == 9 || navcap == 8 || navcap == 9) {
			g_quiet = 0;
			printf("NAV gesture event: navcap=%u step=%u after %u polls\n",
			       navcap, step, i + 1);
			return 0;
		}
		if (!(i % 25))
			printf("waiting (%u/%u): esd=%d cap=%d step=%u\n",
			       i, attempts, esd, cap, step);
		sleep_ms(400);
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
	if (ret && argc > 3) {
		/*
		 * A stale instance from a previous tool run is occupying the
		 * slot (name lookup does not find it, so the unload above
		 * could not get its id).  Previous loads record their app_id
		 * in /tmp/ta_app_id: shut that id down and retry the load.
		 */
		uint32_t stale_id = 0;
		FILE *idf = fopen("/tmp/ta_app_id", "r");

		if (idf) {
			fscanf(idf, "%u", &stale_id);
			fclose(idf);
		}
		if (!stale_id)
			stale_id = 3;
		printf("load failed (%s), shutting down app_id %u\n",
		       strerror(errno), stale_id);
		for (; stale_id && stale_id < 1024 && ret; stale_id++) {
			req.app_id = stale_id;
			if (ioctl(fd, QSEECOM_FP_IOCTL_UNLOAD, &req))
				printf("unload app_id %u failed (%s)\n",
				       stale_id, strerror(errno));
			else
				printf("unloaded stale app_id %u\n", stale_id);
			req.app_id = 0;
			ret = ioctl(fd, QSEECOM_FP_IOCTL_LOAD, &req);
		}
	}
	if (ret) {
		perror("QSEECOM_FP_IOCTL_LOAD");
		close(fd);
		return 1;
	}
	{
		FILE *idf = fopen("/tmp/ta_app_id", "w");

		if (idf) {
			fprintf(idf, "%u\n", req.app_id);
			fclose(idf);
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
			{
				struct timespec ts;
				static uint8_t msec8[8];
				uint64_t ms;

				clock_gettime(CLOCK_MONOTONIC, &ts);
				ms = ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000;
				ms >>= 8;
				memcpy(msec8, &ms, 8);
				g_data_in = msec8;
				g_data_in_len = 8;
				send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
				g_data_in = NULL;
				g_data_in_len = 0;
			}
			send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
			send_mod(fd, req.app_id, 0x20, 14);
			send_cmd(fd, req.app_id, 0x0e, 0x80064, speeds[k], 0x64000, 0);
			{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
	} else if (argc > 3 && !strcmp(argv[3], "idlepoll")) {
		/* Idle statistics WITHOUT a finger: the NAV poll answers
		 * CHECK_ESD / NAV_CAPTURE / NAV_STEP.  On Android idle
		 * gives esd=4 cap=0; a latched ESD (-1035) or SPI timeout
		 * (-1026) means the sensor link itself is unhealthy, which
		 * would also explain -1023 on real frames.  Also reports
		 * per-command latency (0x02 scan on Android is ~17ms). */
		int n = argc > 4 ? atoi(argv[4]) : 100;
		int i, e_ok = 0, e_esd = 0, e_oth = 0;
		int c_ok = 0, c_to = 0, c_oth = 0;
		double t_esd = 0, t_cap = 0, t_step = 0;

		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		send_mod(fd, req.app_id, 0x1b, 9);
		g_dlen = 152;
		send_mod(fd, req.app_id, 0x30, 152);
		g_dlen = 0x1000;
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 4, 0x1013);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x36, 4, 2);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
		printf("IDLE_POLL n=%d\n", n);
		for (i = 0; i < n; i++) {
			int esd, cap, step;

			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			esd = (int32_t)g_last_rx[4];
			t_esd += g_last_ms;
			send_cmd(fd, req.app_id, 0x03, 0, 0, 0, 0);
			cap = (int32_t)g_last_rx[4];
			t_cap += g_last_ms;
			send_cmd(fd, req.app_id, 0x0b, 0, 0, 0, 0);
			step = (int32_t)g_last_rx[4];
			t_step += g_last_ms;
			if (esd == 4) e_ok++;
			else if (esd == -1035) e_esd++;
			else e_oth++;
			if (cap == 0) c_ok++;
			else if (cap == -1026) c_to++;
			else c_oth++;
			if (i % 10 == 0 || esd != 4 || cap != 0)
				printf("  [%d] esd=%d cap=%d step=%d (%.1f/%.1f/%.1fms)\n",
				       i, esd, cap, step, g_last_ms, g_last_ms, g_last_ms);
		}
		printf("IDLE_SUMMARY n=%d esd_ok=%d esd_esd=%d esd_other=%d "
		       "cap_ok=%d cap_timeout=%d cap_other=%d "
		       "avg_esd=%.2fms avg_cap=%.2fms avg_step=%.2fms\n",
		       n, e_ok, e_esd, e_oth, c_ok, c_to, c_oth,
		       t_esd / n, t_cap / n, t_step / n);
	} else if (argc > 3 && (!strcmp(argv[3], "authnav") ||
				!strcmp(argv[3], "navv"))) {
		static uint8_t key_data[256];
		uint32_t key_len = 0;

		/*
		 * Fetch the keymaster blob BEFORE the sileadta session starts:
		 * invoking another TA between 0x1b and 0x30 would switch the
		 * secure-world context mid-sequence and break the session.
		 */
		km_get_key_data(fd, key_data, sizeof(key_data), &key_len);

		/*
		 * Minimal "cap" init sequence: the extra config/calibration/mode
		 * commands used by other modes mask the NAV touch events, so
		 * keep exactly what the working cap mode sends before polling.
		 */
		power_cycle();
		{
			struct timespec ts;
			static uint8_t msec8[8];
			uint64_t ms;

			clock_gettime(CLOCK_MONOTONIC, &ts);
			ms = ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000;
			ms >>= 8;
			memcpy(msec8, &ms, 8);
			g_data_in = msec8;
			g_data_in_len = 8;
			send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
			g_data_in = NULL;
			g_data_in_len = 0;
		}
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		send_mod(fd, req.app_id, 0x1b, 9);
		if (key_len) {
			g_data_in = key_data;
			g_data_in_len = key_len;
		}
		send_mod(fd, req.app_id, 0x30, 152);
		g_data_in = NULL;
		g_data_in_len = 0;
		/*
		 * Full vendor init (silead-init-trace.txt): versions, feature
		 * query, OTP, NAV_SUPPORT, then the download-mode calibration
		 * block before the sensor enters NAV state.
		 */
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
		/*
		 * Template/GID setup (decoded from libsl_fp_impl.so vtable):
		 * 0x10 SET_GID(gid, 0xfdca) selects the user group,
		 * 0x11 LOAD_USER_DB(path) tells the TA the db namespace,
		 * 0x1c LOAD_TEMPLATE pushes each encrypted .dat into the
		 * session, 0x18 LOAD_AUTH_ID returns the auth-id list and
		 * 0x13 GET_DB_COUNT reports how many templates registered.
		 * Without this the TA has nothing to match against and
		 * AUTH_STEP answers -1023.
		 */
		if (!strcmp(argv[3], "authnav")) {
			static const char *tpls[] = {
				"/tmp/fff0cad64.dat",
				"/tmp/f7f0961aa.dat",
			};
			/* silead.tpl record +0x50: 64-bit sec_id shared by
			 * both enrolled templates (per-user db id). */
			static const uint8_t sec_id[8] = {
				0xde, 0xeb, 0x59, 0x46, 0x94, 0xae, 0x57, 0x98,
			};
			uint8_t term[2] = { 0xff, 0xff };
			size_t tl;
			int t;

			send_cmd(fd, req.app_id, 0x10, 0, 0xfdca, 0, 0);
			/* Vendor impl_load_user_db(): cmd_load_user_db
			 * sends 0x11 with the tpl-dir path in the
			 * buffer and its length in a0.  Bare 0x11
			 * (a0=0) returns -1012; with pathlen the TA
			 * accepts it and opens the load session. */
			{
				static const char db_path[] =
					"/data/vendor_de/0/fpdata";
				g_data_in = (const uint8_t *)db_path;
				g_data_in_len = sizeof(db_path);
				g_dlen = g_data_in_len;
				g_size8 = g_dlen;
				send_mod2(fd, req.app_id, 0x11,
					  g_data_in_len, 0);
				g_data_in = NULL;
				g_data_in_len = 0;
				g_dlen = 0x100;
				g_size8 = 0;
			}
			for (t = 0; t < 2; t++) {
				uint8_t *tp = read_file(tpls[t], &tl);
				uint8_t *buf;

				if (!tp || !tl) {
					printf("load_template: %s missing\n",
					       tpls[t]);
					continue;
				}
				printf("load_template %s %zu bytes\n",
				       tpls[t], tl);
				buf = malloc(tl + 8);
				memcpy(buf, sec_id, 8);
				memcpy(buf + 8, tp, tl);
				free(tp);
				g_data_in = buf;
				g_data_in_len = tl + 8;
				g_dlen = tl + 8;
				g_size8 = g_data_in_len;
				send_mod2(fd, req.app_id, 0x1c, tl + 8, 0);
				g_data_in = NULL;
				g_data_in_len = 0;
				free(buf);
			}
			/* Vendor terminator: 2-byte 0xffff record. */
			g_data_in = term;
			g_data_in_len = 2;
			g_dlen = 2;
			g_size8 = 2;
			send_mod2(fd, req.app_id, 0x1c, 2, 0);
			g_data_in = NULL;
			g_data_in_len = 0;
			g_dlen = 0x1000;
			g_size8 = 0;
			send_cmd(fd, req.app_id, 0x18, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x13, 0, 0, 0, 0);
		}
		/*
		 * Vendor state machine: AUTH_START opens a long-lived
		 * session in normal mode BEFORE NAV is armed; on touch the
		 * loop only does NAV_END + MODE[3] + MODE[0] to re-enter
		 * capture mode.  Sending 0x04 after NAV_START leaves the
		 * auth session unopened (MODE[0] then fails INT_INVALID).
		 */
		send_cmd(fd, req.app_id, 0x04, 0, 0, 0, 0);
		/* Vendor NAV setup: ESD, MODE 3, CHECK_BROKEN, NAV_START, MODE 3, MODE 2 */
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
		printf("NAV ready; touch and hold the enrolled finger\n");
		if (!strcmp(argv[3], "navv")) {
			int i;

			for (i = 0; i < 600; i++) {
				send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
				send_cmd(fd, req.app_id, 0x03, 0, 0, 0, 0);
				send_cmd(fd, req.app_id, 0x0b, 0, 0, 0, 0);
				sleep_ms(300);
			}
		} else
		{
			int round;

			for (round = 0; round < 4; round++) {
			int attempt, authed = 0;

			if (wait_nav_finger(fd, req.app_id, 1800))
				break;

			/* Vendor flow (silead-auth-trace.txt): the finger
			 * stays DOWN from the NAV edge through the whole
			 * mode dance -- no NAV_END, no reset, no re-press.
			 * MODE[3]->MODE[0] twice with an ESD check between,
			 * then MODE[3]->MODE[1] before capture.  Cutting
			 * power or lifting the finger breaks frame
			 * continuity and CAPTURE answers -1026. */
			/* Vendor wait_finger_down(): on finger event it
			 * calls cmd_wait_finger_status -- a TA "state"
			 * command.  0x0d/0x39 are the state commands seen
			 * in the init trace; try them so the TA's finger
			 * detector is armed before capture. */
			/* Vendor download_normal(): hw_reset = short
			 * reset-GPIO pulse (5ms low, 3ms settle), NOT an
			 * avdd power cycle -- finger stays down. */
			pulse_reset();
			sleep_ms(50);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			printf("hold the finger down...\n");
			sleep_ms(300);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			printf("post-mode0 esd %d\n",
			       (int32_t)g_last_rx[4]);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			sleep_ms(40);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			printf("post-mode0b esd %d\n",
			       (int32_t)g_last_rx[4]);
			/*
			 * The vendor leaves ~170-300ms between the mode switch
			 * and CHECK_ESD/CAPTURE (sensor frame baseline must
			 * settle or CAPTURE returns MOVE_TOO_FAST).
			 */
			sleep_ms(300);
			/*
			 * Vendor retry cycle (silead-auth-trace.txt lines
			 * 11-31): after the finger is seen in mode 0 the
			 * HAL does MODE[3] -> MODE[1] before the winning
			 * CAPTURE+AUTH_STEP pair.  Mode 1 is the auth
			 * capture mode; running AUTH_STEP in mode 0 always
			 * answers -1023.
			 */
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
			sleep_ms(170);
			for (attempt = 0; attempt < 20; attempt++) {
				int capture_status, auth_status, w;

				send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
				/* Vendor ESD recovery (wait_finger_down):
				 * CHECK_ESD -1035 means the discharge flag
				 * is latched -> dev_pwdn(2): avdd off/on
				 * 200ms, then re-enter the mode dance. */
				if ((int32_t)g_last_rx[4] == -1035) {
					printf("ESD latched: avdd power cycle\n");
					power_cycle();
					sleep_ms(600);
					send_cmd(fd, req.app_id, 0x01, 3,
						 0, 0, 0);
					send_cmd(fd, req.app_id, 0x01, 1,
						 0, 0, 0);
					sleep_ms(170);
					send_cmd(fd, req.app_id, 0x38,
						 0, 0, 0, 0);
				}
				send_cmd(fd, req.app_id, 0x02, 0,
					 attempt ? 1 : 0, 0, 0);
				capture_status = (int32_t)g_last_rx[4];
				printf("capture attempt %d status %d\n",
				       attempt + 1, capture_status);
				if (!capture_status) {
					/* Frame probe: 0x56 GET_IMAGE_INFO
					 * then 0x57 TEST_DUMP_DATA pull the
					 * captured frame into the shared
					 * buffer -- if the sum stays 0 the
					 * TA is scanning dead SPI lanes
					 * (explains -1023 on clean frames). */
					send_cmd(fd, req.app_id, 0x56,
						 0, 0, 0, 0);
					g_dlen = 0x8000;
					g_dump = 1;
					send_mod4(fd, req.app_id, 0x57,
						  6, 0, 0, 0);
					send_mod4(fd, req.app_id, 0x57,
						  0, 0, 0, 0);
					g_dump = 0;
					g_dlen = 0x1000;
					/* Give the TA time to process the
					 * frame before polling the verdict. */
					sleep_ms(120);
					send_cmd(fd, req.app_id, 0x05, 0, 0,
						 attempt ? 1 : 0, 0);
					auth_status = (int32_t)g_last_rx[4];
					printf("AUTH_STEP status %d\n", auth_status);
					if (!auth_status) {
						authed = 1;
						break;
					}
				}
				/*
				 * Vendor mistouch path (_auth_check_mistouch):
				 * -1021..-1024 means the algo wants a FRESH
				 * finger edge, not a retry of the held frame.
				 * Do the MODE[3]->MODE[0] reset, wait for the
				 * user to lift and re-press (new ESD edge),
				 * then MODE[3]->MODE[1] again.
				 */
				if (capture_status == 0 &&
				    (int32_t)g_last_rx[4] == -1023) {
					printf("mistouch: LIFT and RE-PRESS\n");
					send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
					send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
					for (w = 0; w < 10; w++) {
						send_cmd(fd, req.app_id, 0x38,
							 0, 0, 0, 0);
						if ((int32_t)g_last_rx[4] != 4)
							break;
						sleep_ms(200);
					}
					for (w = 0; w < 40; w++) {
						send_cmd(fd, req.app_id, 0x38,
							 0, 0, 0, 0);
						if ((int32_t)g_last_rx[4] == 4)
							break;
						sleep_ms(250);
					}
					printf("fresh edge esd %d\n",
					       (int32_t)g_last_rx[4]);
					send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
					send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
					sleep_ms(170);
					continue;
				}
				sleep_ms(300);
			}
			if (authed)
				break;
			/* Re-arm NAV for the next touch (vendor order:
			 * MODE[3], MODE[0], ESD, MODE[3], CHECK_BROKEN,
			 * NAV_START, MODE[3], MODE[2]).  AUTH_END first:
			 * leaving the session open makes MODE[2] answer
			 * INT_INVALID and NAV never re-arms. */
			send_cmd(fd, req.app_id, 0x06, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x04, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x0c, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
			printf("NAV re-armed; touch and hold the enrolled finger\n");
			}
		}
	} else if (argc > 3 && !strcmp(argv[3], "dbpath")) {
		/* Does the TA have its own db (count>0), and does telling it
		 * the tpl path via mod-0x11(path) make 0x11/0x1c behave like
		 * the vendor's?  init -> set path -> bare 0x11 flag ->
		 * 0x13 count before/after pushes. */
		static const char *paths[] = {
			"/data/vendor_de/0/fpdata",
			"/data/vendor_de/0/fpdata/",
			"/vendor/fpdata",
			"/fpdata",
			"/tmp",
			NULL,
		};
		static const uint8_t sec_id2[8] = {
			0xde, 0xeb, 0x59, 0x46, 0x94, 0xae, 0x57, 0x98,
		};
		const char *tpls2[] = { "/tmp/fff0cad64.dat", "/tmp/f7f0961aa.dat" };
		uint8_t term2[2] = { 0xff, 0xff };
		uint8_t *tp; size_t tl; int p, t;

		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		send_mod(fd, req.app_id, 0x1b, 9);
		send_mod(fd, req.app_id, 0x30, 152);
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 4, 0x1013);
		send_cmd(fd, req.app_id, 0x10, 0, 0xfdca, 0, 0);
		printf("=== count before: ");
		send_cmd(fd, req.app_id, 0x13, 0, 0, 0, 0);
		for (p = 0; paths[p]; p++) {
			printf("=== path '%s'\n", paths[p]);
			g_data_in = (const uint8_t *)paths[p];
			g_data_in_len = strlen(paths[p]) + 1;
			g_dlen = g_data_in_len;
			g_size8 = g_dlen;
			/* cmd_load_user_db(path, &out): a0 = buffer
			 * length, buffer = path string.  The a0=0
			 * we used before gave the TA an empty
			 * buffer -> -1012. */
			send_mod2(fd, req.app_id, 0x11,
				  g_data_in_len, 0);
			g_data_in = NULL;
			g_data_in_len = 0;
			g_dlen = 0x100;
			g_size8 = 0;
			printf("--- count ");
			send_cmd(fd, req.app_id, 0x13, 0, 0, 0, 0);
		}
		printf("=== push templates then count ===\n");
		for (t = 0; t < 2; t++) {
			uint8_t *buf;
			tp = read_file(tpls2[t], &tl);
			if (!tp || !tl) { printf("missing %s\n", tpls2[t]); continue; }
			buf = malloc(tl + 8);
			memcpy(buf, sec_id2, 8);
			memcpy(buf + 8, tp, tl);
			free(tp);
			g_data_in = buf; g_data_in_len = tl + 8;
			g_dlen = tl + 8; g_size8 = tl + 8;
			send_mod2(fd, req.app_id, 0x1c, tl + 8, 0);
			g_data_in = NULL; g_data_in_len = 0;
			free(buf);
		}
		g_data_in = term2; g_data_in_len = 2; g_dlen = 2; g_size8 = 2;
		send_mod2(fd, req.app_id, 0x1c, 2, 0);
		g_data_in = NULL; g_data_in_len = 0; g_dlen = 0x1000; g_size8 = 0;
		printf("--- bare 0x11 flag\n");
		g_dlen = 0x100;
		send_mod2(fd, req.app_id, 0x11, 0, 0);
		g_dlen = 0x1000;
		printf("--- count after: ");
		send_cmd(fd, req.app_id, 0x13, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "dbtest")) {
		static uint8_t key_data[256];
		uint32_t key_len = 0;
		static const char *paths[] = {
			"/data/vendor_de/0/fpdata/",
			"/data/vendor_de/0/fpdata",
			"/data/vendor/silead",
			"",
		};
		uint32_t gids[] = { 0, 1, 1000 };
		static const uint32_t fids2[] = { 0xff0cad64, 0x7f0961aa };
		uint8_t *tp;
		size_t tl;
		int p, g;

		km_get_key_data(fd, key_data, sizeof(key_data), &key_len);
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		send_mod(fd, req.app_id, 0x1b, 9);
		if (key_len) {
			g_data_in = key_data;
			g_data_in_len = key_len;
		}
		send_mod(fd, req.app_id, 0x30, 152);
		g_data_in = NULL;
		g_data_in_len = 0;
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 4, 0x1013);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		for (g = 0; g < 3; g++) {
			printf("=== gid %u\n", gids[g]);
			send_cmd(fd, req.app_id, 0x10, gids[g], 0xfdca, 0, 0);
			for (p = 0; p < 4; p++) {
				printf("--- path '%s'\n", paths[p]);
				g_data_in = (const uint8_t *)paths[p];
				g_data_in_len = strlen(paths[p]) + 1;
				g_dlen = g_data_in_len;
				g_size8 = g_dlen;
				send_mod2(fd, req.app_id, 0x11, 0, 0);
				g_data_in = NULL;
				g_data_in_len = 0;
			}
		}
		tp = read_file("/tmp/fff0cad64.dat", &tl);
		printf("=== load_template phys_offset sweep\n");
		send_cmd(fd, req.app_id, 0x10, 0, 0xfdca, 0, 0);
		if (tp) {
			static const uint32_t offs[] = { 4, 8, 20, 24, 0 };

			for (p = 0; p < 5; p++) {
				g_phys_off = offs[p];
				printf("--- phys_off %u fid@+16\n", offs[p]);
				g_data_in = tp;
				g_data_in_len = tl;
				g_dlen = tl;
				g_size8 = tl;
				send_mod2(fd, req.app_id, 0x1c, 0, fids2[0]);
				printf("--- phys_off %u fid@+12\n", offs[p]);
				send_mod2(fd, req.app_id, 0x1c, fids2[0], 0);
				g_data_in = NULL;
				g_data_in_len = 0;
			}
			g_phys_off = 4;
			printf("=== inline tpl in req (dlen 0)\n");
			g_dlen = tl;
			send_mod2(fd, req.app_id, 0x1c, 0, fids2[0]);
			free(tp);
		}
		g_dlen = 0x1000;
		g_size8 = 0;
		send_cmd(fd, req.app_id, 0x18, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x13, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "authirq")) {
		/* Vendor-faithful auth path:
		 *  init -> load tpl db (0x11 path + 0x1c pushes) ->
		 *  AUTH_START -> NAV arm -> wait the HARDWARE irq
		 *  (gpio83, like the Android driver's netlink) ->
		 *  MODE[3]/MODE[0] x2 with ESD checks -> MODE[1] ->
		 *  CAPTURE -> AUTH_STEP; on 0: 0x19 GET_AUTH_OBJ,
		 *  0x06 AUTH_END.  On -1023 (-1069/-1021/-1022) treat
		 *  as mistouch: return to mode 0 and wait a fresh irq. */
		static const uint8_t sec_id[8] = {
			0xde, 0xeb, 0x59, 0x46, 0x94, 0xae, 0x57, 0x98,
		};
		static const char *tpls[] = {
			"/lib/firmware/fp/fff0cad64.dat",
			"/lib/firmware/fp/f7f0961aa.dat",
		};
		uint8_t term[2] = { 0xff, 0xff };
		static uint8_t key_data[256];
		uint32_t key_len = 0;
		size_t tl;
		int round;
		int debug_frame = argc > 4 &&
			!strcmp(argv[4], "--debug-frame");

		km_get_key_data(fd, key_data, sizeof(key_data), &key_len);
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		send_mod(fd, req.app_id, 0x1b, 9);
		if (key_len) {
			g_data_in = key_data;
			g_data_in_len = key_len;
		}
		g_dlen = 152;
		send_mod(fd, req.app_id, 0x30, 152);
		g_data_in = NULL;
		g_data_in_len = 0;
		g_dlen = 0x1000;
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 4, 0x1013);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x36, 4, 2);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);

		/* Template db: 0x11(path,len) then one 0x1c per .dat. */
		send_cmd(fd, req.app_id, 0x10, 0, 0xfdca, 0, 0);
		{
			static const char db_path[] =
				"/data/vendor_de/0/fpdata";
			g_data_in = (const uint8_t *)db_path;
			g_data_in_len = sizeof(db_path);
			g_dlen = g_data_in_len;
			g_size8 = g_dlen;
			send_mod2(fd, req.app_id, 0x11, g_data_in_len, 0);
			g_data_in = NULL;
			g_data_in_len = 0;
			g_dlen = 0x100;
			g_size8 = 0;
		}
		{
			int t;

			for (t = 0; t < 2; t++) {
				uint8_t *tp = read_file(tpls[t], &tl);
				uint8_t *buf;

				if (!tp || !tl) {
					printf("tpl missing: %s\n", tpls[t]);
					continue;
				}
				buf = malloc(tl + 8);
				memcpy(buf, sec_id, 8);
				memcpy(buf + 8, tp, tl);
				free(tp);
				g_data_in = buf;
				g_data_in_len = tl + 8;
				g_dlen = tl + 8;
				g_size8 = g_data_in_len;
				printf("push tpl %s (%zu bytes)\n", tpls[t], tl);
				send_mod2(fd, req.app_id, 0x1c, tl + 8, 0);
				g_data_in = NULL;
				g_data_in_len = 0;
				free(buf);
			}
			g_data_in = term;
			g_data_in_len = 2;
			g_dlen = 2;
			g_size8 = 2;
			send_mod2(fd, req.app_id, 0x1c, 2, 0);
			g_data_in = NULL;
			g_data_in_len = 0;
			g_dlen = 0x1000;
			g_size8 = 0;
		}
		send_cmd(fd, req.app_id, 0x18, 0, 0, 0, 0);
		printf("auth_id rsp: 0x%08x 0x%08x\n",
		       (uint32_t)g_last_rx[0], (uint32_t)g_last_rx[1]);

		send_cmd(fd, req.app_id, 0x04, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
		printf("ARMED: touch and HOLD the enrolled finger\n");
		fflush(stdout);
		fp_irq_enable();

		for (round = 0; round < 6; round++) {
			int attempt;

			if (wait_irq_any(3600000)) {
				printf("irq wait timed out (re-arming)\n");
				fflush(stdout);
				continue;
			}
			printf("hw irq seen (round %d)\n", round);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			sleep_ms(60);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			sleep_ms(60);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
			sleep_ms(90);
			for (attempt = 0; attempt < 8; attempt++) {
				int cap, st;

				if (esd_recover(fd, req.app_id)) {
					printf("ESD recovery did not clear; stop "
					       "retries for this touch\n");
					break;
				}
				send_cmd(fd, req.app_id, 0x02, 0,
					 attempt ? 1 : 0, 0, 0);
				cap = (int32_t)g_last_rx[4];
				printf("capture %d -> %d\n", attempt, cap);
				if (cap)
					continue;
				sleep_ms(120);
				send_cmd(fd, req.app_id, 0x05, 0, 0,
					 attempt ? 1 : 0, 0);
				st = (int32_t)g_last_rx[4];
				printf("AUTH_STEP -> %d\n", st);
				if (!st) {
					uint32_t fid = g_last_rx[0];

					printf("AUTH_OK fid=0x%08x\n", fid);
					g_dlen = 256;
					send_mod2(fd, req.app_id, 0x19,
						  69, 2);
					g_dlen = 0x1000;
					send_cmd(fd, req.app_id, 0x06,
						 0, 0, 0, 0);
					fflush(stdout);
					return 0;
				}
				if (debug_frame)
					capture_test_frame(fd, req.app_id, 1);
				if (st == -1031)
					break;
			}
			send_cmd(fd, req.app_id, 0x06, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
			send_cmd(fd, req.app_id, 0x04, 0, 0, 0, 0);
			printf("RE-ARMED: lift and touch again\n");
			fflush(stdout);
		}
	} else if (argc > 3 && !strcmp(argv[3], "authsnap")) {
		/*
		 * Vendor "once" sequence from silead-snap native-lib.cpp:
		 * full TA init -> template db -> NAV arm -> REAL gpio83 IRQ
		 * edge -> mode dance -> 8x {CAPTURE, AUTH_STEP, 0x52 dump}.
		 * Keeps the highest-variance frame in /tmp/silead-auth-frame.raw.
		 */
		static const char *tpls3[] = {
			"/tmp/fff0cad64.dat",
			"/tmp/f7f0961aa.dat",
		};
		static const uint8_t sec_id3[8] = {
			0xde, 0xeb, 0x59, 0x46, 0x94, 0xae, 0x57, 0x98,
		};
		uint8_t term3[2] = { 0xff, 0xff };
		uint8_t kb[256];
		uint32_t kb_len = 0;
		uint8_t *framebuf, *best;
		uint64_t bestv = 0;
		size_t tl3;
		int t, i, got = -1, authed = 0;

		fp_open();
		fp_irq_enable();
		km_get_key_data(fd, kb, sizeof(kb), &kb_len);

		/* try_init(dev 14), retry dev 6 on CONFIG_INVALID */
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		if ((int32_t)g_last_rx[4] == -1007) {
			printf("INIT CONFIG_INVALID, retry dev 6\n");
			send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
			send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
			send_mod(fd, req.app_id, 0x20, 6);
			send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00,
				 0x64000, 0);
		}
		{ g_dlen = 0xf000;
		  send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004);
		  g_dlen = 0x1000; };
		send_mod(fd, req.app_id, 0x1b, 9);
		g_data_in = kb;
		g_data_in_len = kb_len;
		g_dlen = kb_len ? kb_len : 152;
		send_mod(fd, req.app_id, 0x30, kb_len ? kb_len : 152);
		g_data_in = NULL;
		g_data_in_len = 0;
		g_dlen = 0x1000;
		send_cmd(fd, req.app_id, 0x50, 0, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x59, 4, 0x1013);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		/* post-init mode dance (vendor order) */
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_mod2(fd, req.app_id, 0x36, 4, 2);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x56, 0, 0, 0, 0);

		/* template db: SET_GID -> LOAD_USER_DB -> LOAD_TEMPLATE x2 -> term */
		send_cmd(fd, req.app_id, 0x10, 0, 0xfdca, 0, 0);
		{
			static const char db_path3[] =
				"/data/vendor_de/0/fpdata";
			g_data_in = (const uint8_t *)db_path3;
			g_data_in_len = sizeof(db_path3);
			g_dlen = g_data_in_len;
			g_size8 = g_dlen;
			send_mod2(fd, req.app_id, 0x11, g_data_in_len, 0);
			g_data_in = NULL;
			g_data_in_len = 0;
			g_dlen = 0x100;
			g_size8 = 0;
		}
		for (t = 0; t < 2; t++) {
			uint8_t *tp = read_file(tpls3[t], &tl3);
			uint8_t *buf;

			if (!tp || !tl3) {
				printf("tpl missing: %s\n", tpls3[t]);
				continue;
			}
			buf = malloc(tl3 + 8);
			memcpy(buf, sec_id3, 8);
			memcpy(buf + 8, tp, tl3);
			free(tp);
			g_data_in = buf;
			g_data_in_len = tl3 + 8;
			g_dlen = tl3 + 8;
			g_size8 = g_data_in_len;
			send_mod2(fd, req.app_id, 0x1c, tl3 + 8, 0);
			g_data_in = NULL;
			g_data_in_len = 0;
			free(buf);
		}
		g_data_in = term3;
		g_data_in_len = 2;
		g_dlen = 2;
		g_size8 = 2;
		send_mod2(fd, req.app_id, 0x1c, 2, 0);
		g_data_in = NULL;
		g_data_in_len = 0;
		g_dlen = 0x1000;
		g_size8 = 0;
		send_cmd(fd, req.app_id, 0x18, 0, 0, 0, 0);
		printf("auth_id rsp: 0x%08x 0x%08x\n",
		       (uint32_t)g_last_rx[0], (uint32_t)g_last_rx[1]);

		/* arm NAV, then block on the real gpio83 edge */
		fp_reset(0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0a, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 2, 0, 0, 0);
		printf("ARMED: touch and HOLD the enrolled finger\n");
		fflush(stdout);
		if (fp_wait_irq()) {
			printf("IRQ wait timed out\n");
			goto authsnap_done;
		}
		printf("hw irq seen — mode dance + capture\n");
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 1, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);

		framebuf = malloc(0x10000);
		best = malloc(0x1000);
		if (framebuf && best) {
			for (i = 0; i < 8; i++) {
				uint64_t s = 0, s2 = 0, v;
				int j, cap, st;

				send_cmd(fd, req.app_id, 0x02, 0,
					 i ? 1 : 0, 0, 0);
				cap = (int32_t)g_last_rx[4];
				sleep_ms(120);
				send_cmd(fd, req.app_id, 0x05, 0, 0,
					 i ? 1 : 0, 0);
				st = (int32_t)g_last_rx[4];
				printf("cap %d -> %d  AUTH_STEP -> %d\n",
				       i, cap, st);
				if (!st) {
					printf("AUTH_OK fid=0x%08x\n",
					       (uint32_t)g_last_rx[0]);
					authed = 1;
				}
				/* 0x52 test-frame dump into the shared buf */
				g_dlen = 0x1000;
				g_dump = 1;
				g_frame_out = framebuf;
				send_mod4(fd, req.app_id, 0x52,
					  0x1000, 0, 0, 3);
				g_frame_out = NULL;
				g_dump = 0;
				for (j = 0; j < 0x1000; j++) {
					s += framebuf[j];
					s2 += (uint64_t)framebuf[j] *
					      framebuf[j];
				}
				v = s2 - s * s / 0x1000;
				printf("  frame %d sum=%llx var=%llx\n", i,
				       (unsigned long long)s,
				       (unsigned long long)v);
				if (v > bestv) {
					bestv = v;
					memcpy(best, framebuf, 0x1000);
					got = 0;
				}
			}
			if (got == 0) {
				FILE *bf = fopen("/tmp/silead-auth-frame.raw",
						  "wb");
				if (bf) {
					fwrite(best, 1, 0x1000, bf);
					fclose(bf);
					printf("best frame -> "
					       "/tmp/silead-auth-frame.raw\n");
				}
			}
		}
		free(framebuf);
		free(best);
		(void)authed;
authsnap_done:
		send_cmd(fd, req.app_id, 0x06, 0, 0, 0, 0);
	} else if (argc > 3 && !strcmp(argv[3], "authwait")) {
		pulse_reset();
		sleep_ms(30);
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x8004); g_dlen = 0x1000; };
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
		if (!wait_irq_any(300000)) {
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x8004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		printf("ESD after config\n");
		send_cmd(fd, req.app_id, 0x38, 0, 0, 0, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x8004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
	} else if (argc > 3 && !strcmp(argv[3], "frameprobe")) {
		static uint8_t key_data[256];
		uint32_t key_len = 0;

		/* No-finger reference: capture one test frame while the
		 * sensor is idle, using the same 0x52 layout as authirq. */
		km_get_key_data(fd, key_data, sizeof(key_data), &key_len);
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00,
			 0x64000, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		send_mod(fd, req.app_id, 0x1b, 9);
		if (key_len) {
			g_data_in = key_data;
			g_data_in_len = key_len;
		}
		g_dlen = 152;
		send_mod(fd, req.app_id, 0x30, 152);
		g_data_in = NULL;
		g_data_in_len = 0;
		g_dlen = 0x1000;
		send_cmd(fd, req.app_id, 0x39, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x0d, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x37, 0, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x01, 3, 0, 0, 0);
		send_cmd(fd, req.app_id, 0x60, 0, 0, 0, 0);
		printf("no-finger frameprobe status %d\n",
		       capture_test_frame(fd, req.app_id, 1));
	} else if (argc > 3 && !strcmp(argv[3], "test")) {
		power_cycle();
		send_mod2(fd, req.app_id, 0x59, 8, 0x1003);
		send_cmd(fd, req.app_id, 0x1f, 2, 0, 0, 0);
		send_mod(fd, req.app_id, 0x20, 14);
		send_cmd(fd, req.app_id, 0x0e, 0x80064, 0x493e00, 0x64000, 0);
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
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
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x1004); g_dlen = 0x1000; };
		{ g_dlen = 0xf000; send_mod2(fd, req.app_id, 0x59, 0xf000, 0x8004); g_dlen = 0x1000; };
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
