/*
 * Software rendering engine with pluggable dirty tracking.
 *
 * Three modes:
 *   RMODE_ROWS   — track dirty rows, stream full 480px per row (default)
 *   RMODE_TILES  — track dirty 16×16 tiles, stream per-tile
 *   RMODE_PIXELS — track per-row x_min/x_max span, stream clipped rows
 *
 * All modes also maintain the row bitmap for y_min/y_max hints.
 */

#include <string.h>
#include <stdlib.h>
#include "render.h"
#include "font.h"

static uint8_t *fb;
static uint8_t mode = RMODE_ROWS;

/* --- Row tracking (all modes use this for y extents) --- */
static uint32_t last_dirty[DIRTY_WORDS];
static int last_y_min, last_y_max;
static uint32_t dirty[DIRTY_WORDS];
static int dirty_y_min, dirty_y_max;

/* --- Tile tracking (mode 1) --- */
static uint32_t last_tiles[TILE_WORDS];
static uint32_t cur_tiles[TILE_WORDS];

/* --- Span tracking (mode 2) --- */
static struct row_span last_spans[FB_HEIGHT];
static struct row_span cur_spans[FB_HEIGHT];

/* ------------------------------------------------------------------ */
/*  Mark helpers                                                      */
/* ------------------------------------------------------------------ */

static inline void mark_row(int y)
{
	dirty[y >> 5] |= (1U << (y & 31));
	if (y < dirty_y_min) dirty_y_min = y;
	if (y > dirty_y_max) dirty_y_max = y;
}

static inline void mark_tile(int x, int y)
{
	int idx = (y / TILE_H) * TILES_X + (x / TILE_W);
	cur_tiles[idx >> 5] |= (1U << (idx & 31));
}

static inline void mark_span(int x, int y)
{
	struct row_span *s = &cur_spans[y];
	if (x < s->x_min) s->x_min = x;
	if (x > s->x_max) s->x_max = x;
}

static inline void mark_dirty(int x, int y)
{
	mark_row(y);
	if (mode == RMODE_TILES) {
		mark_tile(x, y);
	} else if (mode == RMODE_PIXELS) {
		mark_span(x, y);
	}
}

/* ------------------------------------------------------------------ */
/*  Init / Mode                                                       */
/* ------------------------------------------------------------------ */

void render_init(uint8_t *framebuffer)
{
	fb = framebuffer;
	memset(last_dirty, 0, sizeof(last_dirty));
	memset(dirty, 0, sizeof(dirty));
	memset(last_tiles, 0, sizeof(last_tiles));
	memset(cur_tiles, 0, sizeof(cur_tiles));
	for (int i = 0; i < FB_HEIGHT; i++) {
		last_spans[i].x_min = FB_WIDTH;
		last_spans[i].x_max = 0;
		cur_spans[i].x_min = FB_WIDTH;
		cur_spans[i].x_max = 0;
	}
	last_y_min = FB_HEIGHT;
	last_y_max = -1;
	dirty_y_min = FB_HEIGHT;
	dirty_y_max = -1;
}

void render_set_mode(uint8_t m)
{
	mode = m;
	SHARED_DISP->render_mode = m;
}

/* ------------------------------------------------------------------ */
/*  Clear                                                             */
/* ------------------------------------------------------------------ */

void render_clear(void)
{
	switch (mode) {
	case RMODE_ROWS:
		/* Clear full rows that were drawn last frame */
		for (int w = 0; w < DIRTY_WORDS; w++) {
			uint32_t bits = last_dirty[w];
			while (bits) {
				int bit = __builtin_ctz(bits);
				int y = (w << 5) | bit;
				if (y < FB_HEIGHT)
					memset(&fb[y * FB_WIDTH], 0, FB_WIDTH);
				bits &= bits - 1;
			}
		}
		break;

	case RMODE_TILES:
		/* Clear only tiles that were drawn last frame */
		for (int w = 0; w < TILE_WORDS; w++) {
			uint32_t bits = last_tiles[w];
			while (bits) {
				int bit = __builtin_ctz(bits);
				int idx = (w << 5) | bit;
				if (idx < TILE_COUNT) {
					int tx = (idx % TILES_X) * TILE_W;
					int ty = (idx / TILES_X) * TILE_H;
					for (int r = 0; r < TILE_H && ty + r < FB_HEIGHT; r++) {
						memset(&fb[(ty + r) * FB_WIDTH + tx], 0, TILE_W);
					}
				}
				bits &= bits - 1;
			}
		}
		break;

	case RMODE_PIXELS:
		/* Clear only the span of each row that was drawn last frame */
		for (int y = last_y_min; y <= last_y_max && y < FB_HEIGHT; y++) {
			struct row_span *s = &last_spans[y];
			if (s->x_min <= s->x_max && s->x_max < FB_WIDTH) {
				memset(&fb[y * FB_WIDTH + s->x_min], 0,
				       s->x_max - s->x_min + 1);
			}
		}
		break;
	}

	/* Reset current dirty state */
	memset(dirty, 0, sizeof(dirty));
	dirty_y_min = FB_HEIGHT;
	dirty_y_max = -1;

	if (mode == RMODE_TILES) {
		memset(cur_tiles, 0, sizeof(cur_tiles));
	} else if (mode == RMODE_PIXELS) {
		for (int i = 0; i < FB_HEIGHT; i++) {
			cur_spans[i].x_min = FB_WIDTH;
			cur_spans[i].x_max = 0;
		}
	}
}

void render_full_clear(void)
{
	memset(fb, 0, FB_PIXELS);
	memset(dirty, 0xFF, sizeof(dirty));
	dirty_y_min = 0;
	dirty_y_max = FB_HEIGHT - 1;
	memset(last_dirty, 0xFF, sizeof(last_dirty));
	last_y_min = 0;
	last_y_max = FB_HEIGHT - 1;

	memset(last_tiles, 0xFF, sizeof(last_tiles));
	memset(cur_tiles, 0xFF, sizeof(cur_tiles));

	for (int i = 0; i < FB_HEIGHT; i++) {
		last_spans[i].x_min = 0;
		last_spans[i].x_max = FB_WIDTH - 1;
		cur_spans[i].x_min = 0;
		cur_spans[i].x_max = FB_WIDTH - 1;
	}
}

/* ------------------------------------------------------------------ */
/*  Commit dirty state to shared memory                               */
/* ------------------------------------------------------------------ */

void render_commit_dirty(void)
{
	/* Row bitmap — always sent (all modes use y_min/y_max) */
	memcpy((void *)SHARED_DISP->dirty_rows, dirty, sizeof(dirty));
	memcpy((void *)SHARED_DISP->prev_dirty_rows, last_dirty, sizeof(last_dirty));

	int y_min = dirty_y_min;
	int y_max = dirty_y_max;
	if (last_y_min < y_min) y_min = last_y_min;
	if (last_y_max > y_max) y_max = last_y_max;
	SHARED_DISP->dirty_y_min = (int16_t)y_min;
	SHARED_DISP->dirty_y_max = (int16_t)y_max;

	if (mode == RMODE_TILES) {
		/* Merge current + last tiles for FLPR */
		for (int w = 0; w < TILE_WORDS; w++) {
			SHARED_DISP->dirty_tiles[w] = cur_tiles[w] | last_tiles[w];
		}
		/* Save current for next frame */
		memcpy(last_tiles, cur_tiles, sizeof(cur_tiles));
	} else if (mode == RMODE_PIXELS) {
		/* Merge current + last spans for FLPR */
		for (int y = y_min; y <= y_max && y < FB_HEIGHT; y++) {
			uint16_t cmin = cur_spans[y].x_min;
			uint16_t cmax = cur_spans[y].x_max;
			uint16_t lmin = last_spans[y].x_min;
			uint16_t lmax = last_spans[y].x_max;

			/* Union of current and last span */
			uint16_t rmin = (cmin < lmin) ? cmin : lmin;
			uint16_t rmax = (cmax > lmax) ? cmax : lmax;

			SHARED_DISP->dirty_spans[y].x_min = rmin;
			SHARED_DISP->dirty_spans[y].x_max = rmax;
		}
		/* Save current for next frame */
		memcpy(last_spans, cur_spans, sizeof(cur_spans));
	}

	/* Save row state for next frame */
	memcpy(last_dirty, dirty, sizeof(dirty));
	last_y_min = dirty_y_min;
	last_y_max = dirty_y_max;
}

/* ------------------------------------------------------------------ */
/*  Pixel                                                             */
/* ------------------------------------------------------------------ */

void render_pixel(int x, int y, uint8_t color)
{
	if ((unsigned)x < FB_WIDTH && (unsigned)y < FB_HEIGHT) {
		fb[y * FB_WIDTH + x] = color;
		mark_dirty(x, y);
	}
}

static inline void pixel_fast(int x, int y, uint8_t color)
{
	fb[y * FB_WIDTH + x] = color;
}

/* ------------------------------------------------------------------ */
/*  Line                                                              */
/* ------------------------------------------------------------------ */

void render_line(int x0, int y0, int x1, int y1, uint8_t color)
{
	int dx = abs(x1 - x0);
	int dy = -abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		if ((unsigned)x0 < FB_WIDTH && (unsigned)y0 < FB_HEIGHT) {
			pixel_fast(x0, y0, color);
			mark_dirty(x0, y0);
		}
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

/* ------------------------------------------------------------------ */
/*  Circle                                                            */
/* ------------------------------------------------------------------ */

void render_circle(int cx, int cy, int r, uint8_t color)
{
	int x = r, y = 0, d = 1 - r;
	while (x >= y) {
		render_pixel(cx + x, cy + y, color);
		render_pixel(cx - x, cy + y, color);
		render_pixel(cx + x, cy - y, color);
		render_pixel(cx - x, cy - y, color);
		render_pixel(cx + y, cy + x, color);
		render_pixel(cx - y, cy + x, color);
		render_pixel(cx + y, cy - x, color);
		render_pixel(cx - y, cy - x, color);
		y++;
		if (d <= 0) { d += 2 * y + 1; }
		else { x--; d += 2 * (y - x) + 1; }
	}
}

static void hline(int x0, int x1, int y, uint8_t color)
{
	if ((unsigned)y >= FB_HEIGHT) return;
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (x0 < 0) x0 = 0;
	if (x1 >= FB_WIDTH) x1 = FB_WIDTH - 1;
	if (x0 > x1) return;
	memset(&fb[y * FB_WIDTH + x0], color, x1 - x0 + 1);
	mark_row(y);
	if (mode == RMODE_TILES) {
		for (int x = x0; x <= x1; x += TILE_W) mark_tile(x, y);
		mark_tile(x1, y);
	} else if (mode == RMODE_PIXELS) {
		struct row_span *s = &cur_spans[y];
		if (x0 < s->x_min) s->x_min = x0;
		if (x1 > s->x_max) s->x_max = x1;
	}
}

void render_circle_filled(int cx, int cy, int r, uint8_t color)
{
	int x = r, y = 0, d = 1 - r;
	while (x >= y) {
		hline(cx - x, cx + x, cy + y, color);
		hline(cx - x, cx + x, cy - y, color);
		hline(cx - y, cx + y, cy + x, color);
		hline(cx - y, cx + y, cy - x, color);
		y++;
		if (d <= 0) { d += 2 * y + 1; }
		else { x--; d += 2 * (y - x) + 1; }
	}
}

/* ------------------------------------------------------------------ */
/*  Polygon                                                           */
/* ------------------------------------------------------------------ */

void render_polygon(const int *vx, const int *vy, int count, uint8_t color)
{
	int i;
	for (i = 0; i < count - 1; i++) {
		render_line(vx[i], vy[i], vx[i + 1], vy[i + 1], color);
	}
	render_line(vx[i], vy[i], vx[0], vy[0], color);
}

/* ------------------------------------------------------------------ */
/*  Text                                                              */
/* ------------------------------------------------------------------ */

void render_char(int x, int y, char ch, uint8_t color)
{
	if (ch < 32 || ch > 126) return;
	if (x >= FB_WIDTH || y >= FB_HEIGHT || x + 8 <= 0 || y + 8 <= 0) return;

	const uint8_t *glyph = font_8x8[(int)ch - 32];

	for (int row = 0; row < 8; row++) {
		int py = y + row;
		if ((unsigned)py >= FB_HEIGHT) continue;
		uint8_t bits = glyph[row];
		if (!bits) continue;
		for (int col = 0; col < 8; col++) {
			if (bits & (0x80 >> col)) {
				int px = x + col;
				if ((unsigned)px < FB_WIDTH) {
					pixel_fast(px, py, color);
					mark_dirty(px, py);
				}
			}
		}
	}
}

void render_string(int x, int y, const char *str, uint8_t color)
{
	while (*str) {
		render_char(x, y, *str, color);
		x += 8;
		str++;
	}
}

void render_string_centered(int y, const char *str, uint8_t color)
{
	int len = 0;
	const char *s = str;
	while (*s++) len++;
	render_string((FB_WIDTH - len * 8) / 2, y, str, color);
}

void render_number(int x, int y, int num, uint8_t color)
{
	char buf[12];
	int i = 0, neg = 0;

	if (num < 0) { neg = 1; num = -num; }
	if (num == 0) { buf[i++] = '0'; }
	else { while (num > 0) { buf[i++] = '0' + (num % 10); num /= 10; } }
	if (neg) { buf[i++] = '-'; }

	for (int j = i - 1; j >= 0; j--) {
		render_char(x, y, buf[j], color);
		x += 8;
	}
}

void render_number_centered(int y, int num, uint8_t color)
{
	int n = (num < 0) ? -num : num;
	int digits = (n == 0) ? 1 : 0;
	while (n > 0) { digits++; n /= 10; }
	if (num < 0) digits++;
	render_number((FB_WIDTH - digits * 8) / 2, y, num, color);
}

void render_sprite(int x, int y, const uint8_t *data, int w, int h)
{
	for (int row = 0; row < h; row++) {
		int py = y + row;
		if ((unsigned)py >= FB_HEIGHT) { data += w; continue; }
		for (int col = 0; col < w; col++) {
			uint8_t c = *data++;
			if (c != 0) {  /* 0 = transparent, skip */
				int px = x + col;
				if ((unsigned)px < FB_WIDTH) {
					fb[py * FB_WIDTH + px] = c;
					mark_dirty(px, py);
				}
			}
		}
	}
}
