/*
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
 * touch_rings — framebuffer multitouch visualizer for rain/fog bringup.
 * Draws a coloured ring at each contact; HUD shows active / peak / total.
 *
 * Build: aarch64-linux-gnu-gcc -O2 -Wall -o touch_rings touch_rings.c
 * Run:   ./touch_rings                 # auto-find fts_ts
 *        ./touch_rings /dev/input/event3
 * Quit:  Ctrl-C or volume key (KEY_VOLUMEDOWN / KEY_VOLUMEUP / KEY_POWER)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAX_SLOTS 10
#define RING_R    28
#define RING_W     4

struct slot {
	bool active;
	int x, y;
	int pressure;
};

static volatile sig_atomic_t g_run = 1;
static void on_sig(int s) { (void)s; g_run = 0; }

static uint32_t *fb;
static int fb_w, fb_h, fb_stride; /* stride in pixels */
static size_t fb_bytes;

static const uint32_t palette[MAX_SLOTS] = {
	0x00FF5555, 0x0055FF55, 0x005555FF, 0x00FFFF55,
	0x00FF55FF, 0x0055FFFF, 0x00FFAA00, 0x00AAFF00,
	0x00FF00AA, 0x00AAAAFF,
};

static void put_px(int x, int y, uint32_t c)
{
	if ((unsigned)x >= (unsigned)fb_w || (unsigned)y >= (unsigned)fb_h)
		return;
	fb[y * fb_stride + x] = c;
}

static void fill_rect(int x0, int y0, int x1, int y1, uint32_t c)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > fb_w) x1 = fb_w;
	if (y1 > fb_h) y1 = fb_h;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			fb[y * fb_stride + x] = c;
}

static void clear_fb(uint32_t c)
{
	for (int y = 0; y < fb_h; y++)
		for (int x = 0; x < fb_w; x++)
			fb[y * fb_stride + x] = c;
}

/* 5x7 digit glyphs, bit0 = leftmost */
static const uint8_t font5x7[10][7] = {
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
	{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
	{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
	{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
	{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
	{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
	{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
	{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
};

static void draw_char(int x, int y, char ch, uint32_t c, int scale)
{
	if (ch < '0' || ch > '9')
		return;
	const uint8_t *g = font5x7[ch - '0'];
	for (int row = 0; row < 7; row++)
		for (int col = 0; col < 5; col++)
			if (g[row] & (1 << (4 - col)))
				fill_rect(x + col * scale, y + row * scale,
					  x + (col + 1) * scale,
					  y + (row + 1) * scale, c);
}

static void draw_text(int x, int y, const char *s, uint32_t c, int scale)
{
	for (; *s; s++) {
		if (*s == ' ') {
			x += 4 * scale;
			continue;
		}
		draw_char(x, y, *s, c, scale);
		x += 6 * scale;
	}
}

static void draw_ring(int cx, int cy, int r, int w, uint32_t c)
{
	int r2 = r * r;
	int ri = r - w;
	if (ri < 0) ri = 0;
	int ri2 = ri * ri;
	for (int dy = -r; dy <= r; dy++) {
		for (int dx = -r; dx <= r; dx++) {
			int d2 = dx * dx + dy * dy;
			if (d2 <= r2 && d2 >= ri2)
				put_px(cx + dx, cy + dy, c);
		}
	}
	/* crosshair */
	for (int i = -6; i <= 6; i++) {
		put_px(cx + i, cy, c);
		put_px(cx, cy + i, c);
	}
}

static int open_fts_event(const char *override_path)
{
	if (override_path)
		return open(override_path, O_RDONLY | O_NONBLOCK);

	for (int i = 0; i < 32; i++) {
		char path[64], name[256];
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
		    strstr(name, "fts"))
			return fd;
		close(fd);
	}
	return -1;
}

static int open_keys(void)
{
	for (int i = 0; i < 32; i++) {
		char path[64];
		unsigned long bits[(KEY_MAX + 64) / (8 * sizeof(long))];
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		memset(bits, 0, sizeof(bits));
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
			close(fd);
			continue;
		}
		/* volume / power present → likely gpio-keys */
		int k = KEY_VOLUMEDOWN;
		if (bits[k / (8 * (int)sizeof(long))] &
		    (1UL << (k % (8 * (int)sizeof(long)))))
			return fd;
		close(fd);
	}
	return -1;
}

int main(int argc, char **argv)
{
	const char *evpath = (argc > 1) ? argv[1] : NULL;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	struct slot slots[MAX_SLOTS];
	int cur_slot = 0;
	int active = 0, peak = 0, total_downs = 0;
	int frame_dirty = 1;

	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
	memset(slots, 0, sizeof(slots));

	int fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd < 0) {
		perror("open /dev/fb0");
		return 1;
	}
	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) < 0 ||
	    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
		perror("fb ioctl");
		return 1;
	}
	fb_w = vinfo.xres;
	fb_h = vinfo.yres;
	fb_stride = finfo.line_length / 4;
	fb_bytes = finfo.smem_len;
	fb = mmap(NULL, fb_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap fb");
		return 1;
	}
	fprintf(stderr, "fb %dx%d stride=%d bpp=%d\n",
		fb_w, fb_h, fb_stride, vinfo.bits_per_pixel);

	int tfd = open_fts_event(evpath);
	if (tfd < 0) {
		fprintf(stderr, "no fts_ts event device (load rain-touch-load first)\n");
		return 1;
	}
	char tname[256] = "?";
	ioctl(tfd, EVIOCGNAME(sizeof(tname)), tname);
	fprintf(stderr, "touch: %s\n", tname);

	int kfd = open_keys();
	if (kfd >= 0)
		fprintf(stderr, "quit: volume or power key\n");
	else
		fprintf(stderr, "quit: Ctrl-C\n");

	clear_fb(0x00101018);

	struct pollfd pf[2];
	int np = 1;
	pf[0].fd = tfd;
	pf[0].events = POLLIN;
	if (kfd >= 0) {
		pf[1].fd = kfd;
		pf[1].events = POLLIN;
		np = 2;
	}

	while (g_run) {
		int pr = poll(pf, np, 50);
		if (pr < 0 && errno == EINTR)
			continue;

		if (kfd >= 0 && (pf[1].revents & POLLIN)) {
			struct input_event ev;
			while (read(kfd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
				if (ev.type == EV_KEY && ev.value == 1 &&
				    (ev.code == KEY_VOLUMEDOWN ||
				     ev.code == KEY_VOLUMEUP ||
				     ev.code == KEY_POWER))
					g_run = 0;
			}
		}

		if (pf[0].revents & POLLIN) {
			struct input_event ev;
			while (read(tfd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
				if (ev.type == EV_ABS) {
					switch (ev.code) {
					case ABS_MT_SLOT:
						if (ev.value >= 0 && ev.value < MAX_SLOTS)
							cur_slot = ev.value;
						break;
					case ABS_MT_TRACKING_ID:
						if (ev.value == -1) {
							if (slots[cur_slot].active) {
								slots[cur_slot].active = false;
								frame_dirty = 1;
							}
						} else {
							if (!slots[cur_slot].active) {
								slots[cur_slot].active = true;
								total_downs++;
								frame_dirty = 1;
							}
						}
						break;
					case ABS_MT_POSITION_X:
						slots[cur_slot].x = ev.value;
						frame_dirty = 1;
						break;
					case ABS_MT_POSITION_Y:
						slots[cur_slot].y = ev.value;
						frame_dirty = 1;
						break;
					case ABS_MT_PRESSURE:
						slots[cur_slot].pressure = ev.value;
						break;
					default:
						break;
					}
				} else if (ev.type == EV_SYN &&
					   ev.code == SYN_REPORT) {
					frame_dirty = 1;
				}
			}
		}

		if (!frame_dirty)
			continue;
		frame_dirty = 0;

		active = 0;
		for (int i = 0; i < MAX_SLOTS; i++)
			if (slots[i].active)
				active++;
		if (active > peak)
			peak = active;

		clear_fb(0x00101018);

		/* HUD bar */
		fill_rect(0, 0, fb_w, 72, 0x00202838);
		char hud[64];
		snprintf(hud, sizeof(hud), "%d", active);
		draw_text(16, 12, hud, 0x00FFFFFF, 4);
		snprintf(hud, sizeof(hud), "%d", peak);
		draw_text(120, 12, hud, 0x00FFAA55, 4);
		snprintf(hud, sizeof(hud), "%d", total_downs);
		draw_text(240, 12, hud, 0x0055FFAA, 3);

		/* legend: now / peak / total (tiny markers via coloured boxes) */
		fill_rect(16, 56, 40, 64, 0x00FFFFFF);
		fill_rect(120, 56, 144, 64, 0x00FFAA55);
		fill_rect(240, 56, 264, 64, 0x0055FFAA);

		for (int i = 0; i < MAX_SLOTS; i++) {
			if (!slots[i].active)
				continue;
			uint32_t c = palette[i];
			draw_ring(slots[i].x, slots[i].y, RING_R, RING_W, c);
			/* slot index next to ring */
			char sn[4];
			snprintf(sn, sizeof(sn), "%d", i);
			draw_text(slots[i].x + RING_R + 4,
				  slots[i].y - 10, sn, c, 2);
			/* coords */
			char xy[32];
			snprintf(xy, sizeof(xy), "%d", slots[i].x);
			draw_text(slots[i].x - 20, slots[i].y + RING_R + 6,
				  xy, 0x00CCCCCC, 2);
			snprintf(xy, sizeof(xy), "%d", slots[i].y);
			draw_text(slots[i].x - 20, slots[i].y + RING_R + 24,
				  xy, 0x00CCCCCC, 2);
		}

		/* corner: resolution */
		char res[32];
		snprintf(res, sizeof(res), "%d", fb_w);
		draw_text(fb_w - 120, fb_h - 40, res, 0x00666666, 2);
	}

	clear_fb(0x00000000);
	munmap(fb, fb_bytes);
	close(fbfd);
	close(tfd);
	if (kfd >= 0)
		close(kfd);
	fprintf(stderr, "done. peak simultaneous=%d total downs=%d\n",
		peak, total_downs);
	return 0;
}
