// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
// https://github.com/Damir00109/Redmi10c
/*
 * fb-keyboard — on-screen keyboard for fbcon (Redmi 10C rain/fog).
 * Draws a keyboard on the lower portion of /dev/fb0, reads touchscreen
 * events, and injects key presses via /dev/uinput.
 * Also resizes the vt console so text doesn't hide behind the keyboard.
 *
 * Build: aarch64-linux-gnu-gcc -O2 -Wall -o fb-keyboard fb-keyboard.c
 * Run:   ./fb-keyboard                 # auto-find fts_ts
 *        ./fb-keyboard /dev/input/event4
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>

/* ── Layout constants ─────────────────────────────────────────────── */
#define KB_HEIGHT 340          /* total keyboard area */
#define KB_ROWS    5           /* 1 top bar + 3 letter rows + 1 bottom bar */
#define MOD_H      40          /* top/bottom bar height */
#define KBD_KEY_H  80          /* letter key row height */

static int fb_w, fb_h, fb_stride;
static uint32_t *fb;
static size_t fb_bytes;
static int kb_y0;              /* top of keyboard = fb_h - KB_HEIGHT */

static volatile sig_atomic_t g_run = 1;
static void on_sig(int s) { (void)s; g_run = 0; }

/* ── Drawing ──────────────────────────────────────────────────────── */
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

static void draw_rect_outline(int x0, int y0, int x1, int y1, uint32_t c, int t)
{
	fill_rect(x0, y0, x1, y0 + t, c);
	fill_rect(x0, y1 - t, x1, y1, c);
	fill_rect(x0, y0, x0 + t, y1, c);
	fill_rect(x1 - t, y0, x1, y1, c);
}

/* 5x7 font for key labels */
static const uint8_t font5x7[128][7] = {
	[' '] = {0,0,0,0,0,0,0},
	['!'] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
	['"'] = {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00},
	['#'] = {0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00},
	['$'] = {0x04,0x0E,0x15,0x0E,0x14,0x0E,0x04},
	['%'] = {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
	['&'] = {0x0C,0x12,0x10,0x0C,0x12,0x12,0x0C},
	['\'']= {0x04,0x04,0x04,0x00,0x00,0x00,0x00},
	['('] = {0x04,0x08,0x10,0x10,0x10,0x08,0x04},
	[')'] = {0x04,0x02,0x01,0x01,0x01,0x02,0x04},
	['*'] = {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
	['+'] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
	[','] = {0x00,0x00,0x00,0x00,0x04,0x04,0x08},
	['-'] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
	['.'] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
	['/'] = {0x00,0x01,0x02,0x04,0x08,0x10,0x00},
	['0'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
	['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
	['2'] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
	['3'] = {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
	['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
	['5'] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
	['6'] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
	['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
	['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
	['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
	[':'] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
	[';'] = {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08},
	['<'] = {0x00,0x01,0x02,0x04,0x02,0x01,0x00},
	['='] = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
	['>'] = {0x00,0x04,0x02,0x01,0x02,0x04,0x00},
	['?'] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
	['@'] = {0x0E,0x11,0x15,0x15,0x10,0x11,0x0E},
	['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
	['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
	['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
	['D'] = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
	['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
	['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
	['G'] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
	['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
	['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
	['J'] = {0x01,0x01,0x01,0x01,0x01,0x11,0x0E},
	['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
	['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
	['M'] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
	['N'] = {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
	['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
	['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
	['Q'] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
	['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
	['S'] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
	['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
	['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
	['V'] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
	['W'] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
	['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
	['Y'] = {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},
	['Z'] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
	['['] = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
	['\\']= {0x00,0x10,0x08,0x04,0x02,0x01,0x00},
	[']'] = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
	['^'] = {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
	['_'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
	['`'] = {0x08,0x04,0x02,0x00,0x00,0x00,0x00},
	['a'] = {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
	['b'] = {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
	['c'] = {0x00,0x00,0x0F,0x10,0x10,0x10,0x0F},
	['d'] = {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
	['e'] = {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0F},
	['f'] = {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},
	['g'] = {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E},
	['h'] = {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
	['i'] = {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
	['j'] = {0x02,0x00,0x06,0x02,0x02,0x02,0x12,0x0C},
	['k'] = {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
	['l'] = {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
	['m'] = {0x00,0x00,0x1A,0x15,0x15,0x11,0x11},
	['n'] = {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
	['o'] = {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
	['p'] = {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10,0x10},
	['q'] = {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x01},
	['r'] = {0x00,0x00,0x16,0x09,0x08,0x08,0x1C},
	['s'] = {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
	['t'] = {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
	['u'] = {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},
	['v'] = {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
	['w'] = {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
	['x'] = {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
	['y'] = {0x00,0x00,0x11,0x11,0x11,0x0F,0x01,0x0E},
	['z'] = {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
	['{'] = {0x06,0x08,0x08,0x10,0x08,0x08,0x06},
	['|'] = {0x04,0x04,0x04,0x04,0x04,0x04,0x04},
	['}'] = {0x0C,0x02,0x02,0x01,0x02,0x02,0x0C},
	['~'] = {0x00,0x00,0x08,0x15,0x02,0x00,0x00},
};

static void draw_char(int x, int y, char ch, uint32_t fg, int scale)
{
	if (ch < 0x20 || ch > 0x7E) ch = '?';
	const uint8_t *g = font5x7[(int)ch];
	for (int row = 0; row < 7; row++) {
		for (int col = 0; col < 5; col++) {
			if (g[row] & (1 << (4 - col))) {
				fill_rect(x + col * scale, y + row * scale,
					  x + (col + 1) * scale, y + (row + 1) * scale, fg);
			}
		}
	}
}

static void draw_text(int x, int y, const char *s, uint32_t fg, int scale)
{
	for (; *s; s++, x += 6 * scale)
		draw_char(x, y, *s, fg, scale);
}

/* ── Key definitions ──────────────────────────────────────────────── */
struct key {
	int x, w;           /* x position, width in px (row-relative) */
	int code;           /* linux input key code */
	const char *label;  /* display label (NULL → use code name) */
	bool is_mod;        /* modifier (sticky toggle) */
};

/* Layout: modern mobile QWERTY, no gaps
 * Row 0: q w e r t y u i o p  (10 keys x 72)
 * Row 1: a s d f g h j k l    (9 keys x 78, offset 39)
 * Row 2: caps z x c v b n m <-- (shift, 7 letters, backspace)
 * Row 3: ?123  ,   [Space]   .  ↵  (bottom bar)
 * Page 0 = letters, Page 1 = numbers/symbols
 */

/* Key widths scale with fb_w (720px baseline). Computed at runtime. */
#define KBASE_W  720.0f
#define RATIO10  (1.0f/10.0f)   /* 10 keys */
#define RATIO9   (1.0f/9.0f)    /* 9 keys */
#define RATIO7   (1.0f/7.0f)    /* 7 letters */
#define RATIO_S  (74.0f/KBASE_W) /* shift */
#define RATIO_B  (124.0f/KBASE_W)/* backspace */
#define RATIO_L  (80.0f/KBASE_W) /* ?123 */
#define RATIO_P  (46.0f/KBASE_W) /* , . */
#define RATIO_E  (116.0f/KBASE_W)/* enter */

/* Special key code for page switch */
#define KEY_PAGE  0x1000

static int current_page = 0;  /* 0=letters, 1=symbols */

static struct key rows[KB_ROWS][16] = {
	/* Row 0: top bar */
	[0] = {
		{0, 1, KEY_ESC,        "Esc",  false},
		{0, 1, KEY_TAB,        "Tab",  false},
		{0, 1, KEY_LEFTCTRL,   "Ctrl", true},
		{0, 1, KEY_LEFTSHIFT,  "Sft",  true},
		{0, 1, KEY_LEFT,       "<",    false},
		{0, 1, KEY_DOWN,       "v",    false},
		{0, 1, KEY_UP,         "^",    false},
		{0, 1, KEY_RIGHT,      ">",    false},
	},
	/* Row 1: QWERTYUIOP */
	[1] = {
		{0, 1,  KEY_Q,      "q",     false},
		{0, 1,  KEY_W,      "w",     false},
		{0, 1,  KEY_E,      "e",     false},
		{0, 1,  KEY_R,      "r",     false},
		{0, 1,  KEY_T,      "t",     false},
		{0, 1,  KEY_Y,      "y",     false},
		{0, 1,  KEY_U,      "u",     false},
		{0, 1,  KEY_I,      "i",     false},
		{0, 1,  KEY_O,      "o",     false},
		{0, 1,  KEY_P,      "p",     false},
	},
	/* Row 2: ASDFGHJKL */
	[2] = {
		{0, 1,   KEY_A,      "a",     false},
		{0, 1,   KEY_S,      "s",     false},
		{0, 1,   KEY_D,      "d",     false},
		{0, 1,   KEY_F,      "f",     false},
		{0, 1,   KEY_G,      "g",     false},
		{0, 1,   KEY_H,      "h",     false},
		{0, 1,   KEY_J,      "j",     false},
		{0, 1,   KEY_K,      "k",     false},
		{0, 1,   KEY_L,      "l",     false},
	},
	/* Row 3: shift + ZXCVBNM + backspace */
	[3] = {
		{0, 1, KEY_LEFTSHIFT, "caps", true},
		{0, 1,   KEY_Z,      "z",     false},
		{0, 1,   KEY_X,      "x",     false},
		{0, 1,   KEY_C,      "c",     false},
		{0, 1,   KEY_V,      "v",     false},
		{0, 1,   KEY_B,      "b",     false},
		{0, 1,   KEY_N,      "n",     false},
		{0, 1,   KEY_M,      "m",     false},
		{0, 1, KEY_BACKSPACE, "<--", false},
	},
	/* Row 4: bottom bar */
	[4] = {
		{0, 1,  KEY_PAGE,       "?123", false},
		{0, 1,  KEY_COMMA,      ",",    false},
		{0, 1,  KEY_SPACE,      "     ",false},
		{0, 1,  KEY_DOT,        ".",    false},
		{0, 1,  KEY_ENTER,      "Ent",  false},
	},
};
/* Symbol page (page 1) — rows 0-2, row 3 same */
static struct key sym_rows[3][16] = {
	/* Row 0: 1 2 3 4 5 6 7 8 9 0 */
	[0] = {
		{0, 1,  KEY_1,         "1",    false},
		{72, 1,  KEY_2,         "2",    false},
		{144, 1,  KEY_3,         "3",    false},
		{216, 1,  KEY_4,         "4",    false},
		{288, 1,  KEY_5,         "5",    false},
		{360, 1,  KEY_6,         "6",    false},
		{432, 1,  KEY_7,         "7",    false},
		{504, 1,  KEY_8,         "8",    false},
		{576, 1,  KEY_9,         "9",    false},
		{648, 1,  KEY_0,         "0",    false},
	},
	/* Row 1: @ # $ _ & - + ( ) / */
	[1] = {
		{39, 1,   KEY_2,         "@",    false},
		{117, 1,   KEY_3,         "#",    false},
		{195, 1,   KEY_4,         "$",    false},
		{273, 1,   KEY_MINUS,     "_",    false},
		{351, 1,   KEY_7,         "&",    false},
		{429, 1,   KEY_MINUS,     "-",    false},
		{507, 1,   KEY_EQUAL,     "+",    false},
		{585, 1,   KEY_9,         "(",    false},
		{663, 1,   KEY_0,         ")",    false},
	},
	/* Row 2: * " ' : ; ! ? [ ] backspace */
	[2] = {
		{74, 1,   KEY_8,         "*",    false},
		{152, 1,   KEY_APOSTROPHE,"\"",    false},
		{230, 1,   KEY_APOSTROPHE,"'",    false},
		{308, 1,   KEY_SEMICOLON, ":",    false},
		{386, 1,   KEY_SEMICOLON, ";",    false},
		{464, 1,   KEY_1,         "!",    false},
		{542, 1,   KEY_SLASH,     "?",    false},
		{0, 1, KEY_BACKSPACE,"<--",  false},
	},
};

static int kw10, kw9, kw7, kw_shift, kw_bksp, kw_lab, kw_punct, kw_space, kw_enter;

static void compute_key_widths(void)
{
	kw10 = (int)(fb_w * RATIO10);
	kw9  = (int)(fb_w * RATIO9);
	kw7  = (int)(fb_w * RATIO7);
	kw_shift = (int)(fb_w * RATIO_S);
	kw_bksp  = (int)(fb_w * RATIO_B);
	kw_lab   = (int)(fb_w * RATIO_L);
	kw_punct = (int)(fb_w * RATIO_P);
	kw_enter = (int)(fb_w * RATIO_E);
	/* spacebar fills the gap between lab+punct and punct+enter in bottom bar */
	kw_space = fb_w - kw_lab - kw_punct - kw_punct - kw_enter;
}

static void init_layout(void)
{
	int top_w = fb_w / 8;
	/* Row 0: top bar, 8 keys */
	for (int i = 0; i < 8; i++) {
		rows[0][i].x = i * top_w;
		rows[0][i].w = top_w;
	}
	/* Row 1: 10 keys */
	for (int i = 0; i < 10; i++) {
		rows[1][i].x = i * kw10;
		rows[1][i].w = kw10;
	}
	/* Row 2: 9 keys, starts at left edge */
	for (int i = 0; i < 9; i++) {
		rows[2][i].x = i * kw9;
		rows[2][i].w = kw9;
	}
	/* Row 3: shift + 7 letters + backspace */
	rows[3][0].x = 0;       rows[3][0].w = kw_shift;
	int letters_width = fb_w - kw_shift - kw_bksp;
	int kw_letter = letters_width / 7;
	for (int i = 1; i < 8; i++) {
		rows[3][i].x = kw_shift + (i - 1) * kw_letter;
		rows[3][i].w = kw_letter;
	}
	rows[3][8].x = fb_w - kw_bksp;  rows[3][8].w = kw_bksp;
	/* Row 4: bottom bar */
	rows[4][0].x = 0;               rows[4][0].w = kw_lab;
	rows[4][1].x = kw_lab;          rows[4][1].w = kw_punct;
	rows[4][2].x = kw_lab + kw_punct; rows[4][2].w = kw_space;
	rows[4][3].x = fb_w - kw_punct - kw_enter; rows[4][3].w = kw_punct;
	rows[4][4].x = fb_w - kw_enter;  rows[4][4].w = kw_enter;

	/* Symbol page */
	/* Row 0: 10 keys */
	for (int i = 0; i < 10; i++) {
		sym_rows[0][i].x = i * kw10;
		sym_rows[0][i].w = kw10;
	}
	/* Row 1: 9 keys, starts at left edge */
	for (int i = 0; i < 9; i++) {
		sym_rows[1][i].x = i * kw9;
		sym_rows[1][i].w = kw9;
	}
	/* Row 2: 7 symbols + backspace */
	int sym_letters_width = fb_w - kw_shift - kw_bksp;
	int sym_kw7 = sym_letters_width / 7;
	for (int i = 0; i < 7; i++) {
		sym_rows[2][i].x = kw_shift + i * sym_kw7;
		sym_rows[2][i].w = sym_kw7;
	}
	sym_rows[2][7].x = fb_w - kw_bksp;  sym_rows[2][7].w = kw_bksp;
}




static int row_nkeys[KB_ROWS] = {8, 10, 9, 9, 5};

/* Modifier state */
static bool mod_ctrl = false;
static bool mod_shift = false;

/* ── Drawing the keyboard ─────────────────────────────────────────── */
#define COL_BG      0xFF1A1A2E   /* dark navy */
#define COL_KEY     0xFF2D2D44   /* key background */
#define COL_KEY_HI  0xFF4A4A6E   /* key pressed / hover */
#define COL_MOD_ON  0xFF1B5E20   /* modifier active (green) */
#define COL_LABEL   0xFFE0E0E0   /* label text */
#define COL_BORDER  0xFF555577   /* key border */

static struct key *get_key(int row, int idx)
{
	/* Row 0 = top bar (always same), row 4 = bottom bar (always same) */
	if (row == 0 || row == 4) return &rows[row][idx];
	if (current_page == 0) return &rows[row][idx];
	return &sym_rows[row - 1][idx];
}

static void draw_key(int row, int idx, bool pressed)
{
	struct key *k = get_key(row, idx);
	int y0, yh;
	if (row == 0) {
		/* top bar */
		y0 = fb_h - KB_HEIGHT;
		yh = MOD_H;
	} else if (row == 4) {
		/* bottom bar */
		y0 = fb_h - MOD_H;
		yh = MOD_H;
	} else {
		/* letter rows 1-3 */
		y0 = fb_h - KB_HEIGHT + MOD_H + (row - 1) * KBD_KEY_H;
		yh = KBD_KEY_H;
	}
	int x0 = k->x;
	int x1 = k->x + k->w;
	int y1 = y0 + yh;
	uint32_t bg;

	if (k->is_mod) {
		bool on = (k->code == KEY_LEFTCTRL && mod_ctrl) ||
			  (k->code == KEY_LEFTSHIFT && mod_shift);
		bg = on ? COL_MOD_ON : (pressed ? COL_KEY_HI : COL_KEY);
	} else {
		bg = pressed ? COL_KEY_HI : COL_KEY;
	}

	fill_rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, bg);
	draw_rect_outline(x0, y0, x1, y1, COL_BORDER, 1);

	/* Label centered */
	const char *label = k->label ? k->label : "?";
	int scale = (row == 0 || row == 4) ? 1 : 2;
	int lw = strlen(label) * 6 * scale;
	int lh = 7 * scale;
	int lx = x0 + (k->w - lw) / 2;
	int ly = y0 + (yh - lh) / 2;
	draw_text(lx, ly, label, COL_LABEL, scale);
}

static void draw_keyboard(void)
{
	/* Clear keyboard area */
	fill_rect(0, kb_y0, fb_w, fb_h, COL_BG);
	for (int r = 0; r < KB_ROWS; r++)
		for (int i = 0; i < row_nkeys[r]; i++)
			draw_key(r, i, false);
}

/* ── Touch → key lookup ───────────────────────────────────────────── */
static struct key *find_key(int tx, int ty, int *row_out, int *idx_out)
{
	int kb_y0 = fb_h - KB_HEIGHT;
	if (ty < kb_y0) return NULL;
	int dy = ty - kb_y0;
	int row;
	if (dy < MOD_H) {
		row = 0;  /* top bar */
	} else if (dy >= KB_HEIGHT - MOD_H) {
		row = 4;  /* bottom bar */
	} else {
		row = 1 + (dy - MOD_H) / KBD_KEY_H;
	}
	if (row >= KB_ROWS) return NULL;
	for (int i = 0; i < row_nkeys[row]; i++) {
		struct key *k = get_key(row, i);
		if (tx >= k->x && tx < k->x + k->w) {
			*row_out = row;
			*idx_out = i;
			return k;
		}
	}
	return NULL;
}

/* ── uinput setup ─────────────────────────────────────────────────── */
static int uinput_fd;

static void uinput_setup(void)
{
	uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (uinput_fd < 0) {
		perror("open /dev/uinput");
		exit(1);
	}

	/* Enable key events */
	ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);

	/* Enable all common keys */
	int keys[] = {
		KEY_ESC, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
		KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE,
		KEY_TAB, KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U,
		KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_ENTER,
		KEY_LEFTCTRL, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J,
		KEY_K, KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_BACKSLASH,
		KEY_LEFTSHIFT, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M,
		KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_SPACE, KEY_LEFT, KEY_RIGHT,
		KEY_UP, KEY_DOWN, KEY_LEFTALT, KEY_RIGHTALT,
		KEY_GRAVE, KEY_102ND,
	};
	for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++)
		ioctl(uinput_fd, UI_SET_KEYBIT, keys[i]);

	struct uinput_setup usetup = {0};
	strcpy(usetup.name, "fb-keyboard");
	usetup.id.bustype = BUS_VIRTUAL;
	usetup.id.vendor = 0x1;
	usetup.id.product = 0x1;
	usetup.id.version = 1;

	if (ioctl(uinput_fd, UI_DEV_SETUP, &usetup) < 0) {
		perror("UI_DEV_SETUP");
		exit(1);
	}
	if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
		perror("UI_DEV_CREATE");
		exit(1);
	}
}

static void emit_key(int code, int pressed)
{
	struct input_event ev = {0};
	ev.type = EV_KEY;
	ev.code = code;
	ev.value = pressed ? 1 : 0;
	write(uinput_fd, &ev, sizeof(ev));

	ev.type = EV_SYN;
	ev.code = SYN_REPORT;
	ev.value = 0;
	write(uinput_fd, &ev, sizeof(ev));
}

/* ── Swipe handling ───────────────────────────────────────────────── */
static void handle_swipe(int sx, int sy, int ex, int ey)
{
	int dx = ex - sx;
	int dy = ey - sy;
	int thr = 40;  /* lower threshold for quick flicks */
	int dist2 = dx * dx + dy * dy;
	if (dist2 < thr * thr) return;

	/* Allow imperfect swipes: dominant axis must beat the other by 1.3:1 */
	int adx = abs(dx), ady = abs(dy);
	if (adx * 10 > 13 * ady) {
		if (dx > 0)      emit_key(KEY_RIGHT, 1), emit_key(KEY_RIGHT, 0);
		else             emit_key(KEY_LEFT, 1),  emit_key(KEY_LEFT, 0);
	} else if (ady * 10 > 13 * adx) {
		if (dy > 0)      emit_key(KEY_DOWN, 1),  emit_key(KEY_DOWN, 0);
		else             emit_key(KEY_UP, 1),    emit_key(KEY_UP, 0);
	}
}

static void emit_key_with_mods(int code)
{
	if (mod_ctrl) {
		emit_key(KEY_LEFTCTRL, 1);
	}
	if (mod_shift) {
		emit_key(KEY_LEFTSHIFT, 1);
	}
	emit_key(code, 1);
	emit_key(code, 0);
	if (mod_ctrl) {
		emit_key(KEY_LEFTCTRL, 0);
		mod_ctrl = false;  /* ctrl is one-shot */
	}
	if (mod_shift) {
		emit_key(KEY_LEFTSHIFT, 0);
		mod_shift = false;  /* shift is one-shot */
	}
}

/* ── vt resize ────────────────────────────────────────────────────── */
static void resize_vt(int fd)
{
	/* Tell fbcon to use fewer rows so text doesn't go under keyboard */
	struct winsize ws;
	if (ioctl(fd, TIOCGWINSZ, &ws) < 0) return;
	/* font 8x16: rows = (fb_h - KB_HEIGHT) / 16 */
	int new_rows = (fb_h - KB_HEIGHT) / 16;
	ws.ws_row = new_rows;
	ioctl(fd, TIOCSWINSZ, &ws);
}

/* ── Main ─────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
	const char *tsdev = argc > 1 ? argv[1] : NULL;

	/* Open framebuffer */
	int fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd < 0) { perror("open /dev/fb0"); return 1; }

	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0) { perror("vinfo"); return 1; }
	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) < 0) { perror("finfo"); return 1; }

	fb_w = vinfo.xres;
	fb_h = vinfo.yres;
	fb_stride = finfo.line_length / 4;  /* pixels per row (with padding) */
	fb_bytes = (size_t)finfo.line_length * fb_h;
	kb_y0 = fb_h - KB_HEIGHT;

	fb = mmap(NULL, fb_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fb == MAP_FAILED) { perror("mmap"); return 1; }

	compute_key_widths();
	init_layout();

	/* Find touchscreen if not specified */
	char tsbuf[64];
	if (!tsdev) {
		/* Try /dev/input/event4 first (fts_ts on rain/fog) */
		for (int i = 0; i < 8; i++) {
			snprintf(tsbuf, sizeof(tsbuf), "/dev/input/event%d", i);
			int fd = open(tsbuf, O_RDONLY);
			if (fd < 0) continue;
			char name[256] = {0};
			ioctl(fd, EVIOCGNAME(sizeof(name)), name);
			close(fd);
			if (strstr(name, "fts") || strstr(name, "touch")) {
				tsdev = tsbuf;
				break;
			}
		}
	}
	if (!tsdev) {
		fprintf(stderr, "No touchscreen found, using /dev/input/event4\n");
		tsdev = "/dev/input/event4";
	}

	int tsfd = open(tsdev, O_RDONLY);
	if (tsfd < 0) { perror(tsdev); return 1; }

	/* Open current console for vt resize */
	int vtfd = open("/dev/tty1", O_WRONLY);
	if (vtfd < 0) vtfd = open("/dev/tty", O_WRONLY);

	/* Setup uinput */
	uinput_setup();

	/* Resize vt */
	if (vtfd >= 0) {
		resize_vt(vtfd);
	}

	/* Draw keyboard */
	draw_keyboard();

	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);

	/* Touch state */
	int slot_x = -1, slot_y = -1;
	int start_x = -1, start_y = -1;
	int last_x = -1, last_y = -1;
	bool got_start = false;
	int slot_id = -1;
	int tracking_id = -1;
	bool touching = false;
	int active_row = -1, active_idx = -1;
	long hold_start_ms = 0;
	long last_repeat_ms = 0;
	long last_redraw_ms = 0;
	int kb_top = fb_h - KB_HEIGHT;

	struct pollfd pfd = { .fd = tsfd, .events = POLLIN };

	while (g_run) {
		long now_ms;
		{
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			now_ms = (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		}

		/* Redraw keyboard periodically so `clear` doesn't erase it */
		if (now_ms - last_redraw_ms > 1000) {
			draw_keyboard();
			last_redraw_ms = now_ms;
		}

		/* Auto-repeat for held backspace */
		if (touching && active_row >= 0 && active_idx >= 0) {
			struct key *k = get_key(active_row, active_idx);
			if (k && k->code == KEY_BACKSPACE) {
				if (now_ms - hold_start_ms > 500 &&
				    now_ms - last_repeat_ms > 500) {
					emit_key(KEY_BACKSPACE, 1);
					emit_key(KEY_BACKSPACE, 0);
					last_repeat_ms = now_ms;
				}
			}
		}

		if (poll(&pfd, 1, 100) < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (!(pfd.revents & POLLIN)) continue;

		struct input_event ev[16];
		ssize_t n = read(tsfd, ev, sizeof(ev));
		if (n <= 0) continue;
		int cnt = n / sizeof(struct input_event);

		for (int i = 0; i < cnt; i++) {
			if (ev[i].type == EV_ABS) {
				switch (ev[i].code) {
				case ABS_MT_SLOT:
					slot_id = ev[i].value;
					break;
				case ABS_MT_TRACKING_ID:
					if (ev[i].value >= 0) {
						tracking_id = ev[i].value;
						touching = true;
						got_start = false;
						{
							struct timespec ts;
							clock_gettime(CLOCK_MONOTONIC, &ts);
							hold_start_ms = (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
							last_repeat_ms = hold_start_ms;
						}
					} else {
						touching = false;
						/* Swipe gesture if released above keyboard */
						if (got_start && last_x >= 0 && last_y >= 0) {
							handle_swipe(start_x, start_y, last_x, last_y);
						}
						/* Release key */
						if (active_row >= 0 && active_idx >= 0) {
							draw_key(active_row, active_idx, false);
						}
						active_row = -1;
						active_idx = -1;
					}
					break;
				case ABS_MT_POSITION_X:
					slot_x = ev[i].value;
					last_x = slot_x;
					if (touching && !got_start && slot_y >= 0) {
						start_x = slot_x;
						start_y = slot_y;
						got_start = true;
						last_x = slot_x; last_y = slot_y;
					}
					break;
				case ABS_MT_POSITION_Y:
					slot_y = ev[i].value;
					last_y = slot_y;
					if (touching && !got_start && slot_x >= 0) {
						start_x = slot_x;
						start_y = slot_y;
						got_start = true;
						last_x = slot_x; last_y = slot_y;
					}
					break;
				}
			} else if (ev[i].type == EV_KEY && ev[i].code == BTN_TOUCH) {
				if (ev[i].value == 0) {
					touching = false;
					if (active_row >= 0 && active_idx >= 0) {
						draw_key(active_row, active_idx, false);
					}
					active_row = -1;
					active_idx = -1;
				}
			} else if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT) {
				if (touching && slot_x >= 0 && slot_y >= 0) {
					int r, idx;
					struct key *k = find_key(slot_x, slot_y, &r, &idx);
					if (k) {
						if (r != active_row || idx != active_idx) {
							/* New key pressed */
							if (active_row >= 0 && active_idx >= 0) {
								draw_key(active_row, active_idx, false);
							}
							active_row = r;
							active_idx = idx;
							draw_key(r, idx, true);

							/* Handle key */
							if (k->code == KEY_PAGE) {
								current_page = !current_page;
								/* Update label */
								rows[4][0].label = current_page ? "abc" : "?123";
								draw_keyboard();
							} else if (k->is_mod) {
								if (k->code == KEY_LEFTCTRL)
									mod_ctrl = !mod_ctrl;
								else if (k->code == KEY_LEFTSHIFT)
									mod_shift = !mod_shift;
								/* Redraw to show mod state */
								draw_keyboard();
							} else {
								emit_key_with_mods(k->code);
							}
						}
					}
					slot_x = -1;
					slot_y = -1;
				}
			}
		}
	}

	/* Cleanup: clear keyboard area */
	fill_rect(0, kb_y0, fb_w, fb_h, 0xFF000000);
	munmap(fb, fb_bytes);
	close(tsfd);
	close(fbfd);
	if (vtfd >= 0) close(vtfd);
	ioctl(uinput_fd, UI_DEV_DESTROY);
	close(uinput_fd);
	return 0;
}
