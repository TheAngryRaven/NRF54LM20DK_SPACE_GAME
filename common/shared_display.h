/*
 * Shared display framebuffer between CPU app and FLPR display driver
 * Memory region: 0x20040000, size 0x26000 (155,648 bytes)
 */

#ifndef SHARED_DISPLAY_H
#define SHARED_DISPLAY_H

#include <stdint.h>

#define SHARED_MEM_ADDR     0x20040000
#define SHARED_MEM_SIZE     0x26000

#define FB_WIDTH            480
#define FB_HEIGHT           320
#define FB_PIXELS           (FB_WIDTH * FB_HEIGHT)

/* Dirty row bitmap: 320 rows = 10 × uint32_t */
#define DIRTY_WORDS         ((FB_HEIGHT + 31) / 32)

/* Rendering modes */
#define RMODE_ROWS          0   /* Stream full dirty rows (default) */
#define RMODE_TILES         1   /* Stream dirty tiles (NxN grid) */
#define RMODE_PIXELS        2   /* Stream dirty row spans (horizontal clipping) */

/* Tile grid config */
#define TILE_W              16
#define TILE_H              16
#define TILES_X             (FB_WIDTH / TILE_W)     /* 30 */
#define TILES_Y             (FB_HEIGHT / TILE_H)    /* 20 */
#define TILE_COUNT          (TILES_X * TILES_Y)     /* 600 */
#define TILE_WORDS          ((TILE_COUNT + 31) / 32) /* 19 */

/* Status values */
#define DISP_STATUS_INIT            0
#define DISP_STATUS_READY           1
#define DISP_STATUS_FRAME_READY     2
#define DISP_STATUS_FRAME_SENDING   3

/* Palette indices — match st7365.h PAL_* and png2sprite.py */
#define COLOR_TRANSPARENT   0
#define COLOR_WHITE         1
#define COLOR_BLACK         2
#define COLOR_RED           3
#define COLOR_YELLOW        4
#define COLOR_ORANGE        5
#define COLOR_GREEN         6
#define COLOR_CYAN          7
#define COLOR_BLUE          8
#define COLOR_DKGRAY        9
#define COLOR_MDGRAY        10
#define COLOR_LTGRAY        11
#define COLOR_DKRED         12
#define COLOR_DKGREEN       13
#define COLOR_DKBLUE        14
#define COLOR_MAGENTA       15

/* Per-row dirty span for RMODE_PIXELS */
struct row_span {
	uint16_t x_min;
	uint16_t x_max;
};

#define ROW_SPAN_EMPTY  { .x_min = FB_WIDTH, .x_max = 0 }

struct shared_display {
	volatile uint32_t status;
	volatile uint32_t frame_count;
	volatile uint8_t  render_mode;                   /* RMODE_ROWS/TILES/PIXELS */
	volatile uint8_t  pad1[3];
	volatile uint16_t fps;
	volatile uint16_t flpr_stream_us;
	volatile uint16_t cpu_work_us;
	volatile int16_t  dirty_y_min;
	volatile int16_t  dirty_y_max;
	volatile uint16_t pad2;

	/* Mode 0: dirty rows */
	volatile uint32_t dirty_rows[DIRTY_WORDS];
	volatile uint32_t prev_dirty_rows[DIRTY_WORDS];

	/* Mode 1: dirty tiles */
	volatile uint32_t dirty_tiles[TILE_WORDS];
	volatile uint32_t prev_dirty_tiles[TILE_WORDS];

	/* Mode 2: dirty spans (per-row x_min/x_max) */
	volatile struct row_span dirty_spans[FB_HEIGHT];
	volatile struct row_span prev_dirty_spans[FB_HEIGHT];

	/* Framebuffer — must be last (largest field) */
	uint8_t framebuffer[FB_PIXELS];
};

#define SHARED_DISP ((struct shared_display *)SHARED_MEM_ADDR)

#endif /* SHARED_DISPLAY_H */
