/*
 * FLPR Display Driver - nRF54LM20-DK
 * Dedicated display streaming core with pluggable rendering modes.
 */

#include <zephyr/kernel.h>
#include "st7365.h"
#include "shared_display.h"

int main(void)
{
	st7365_init();

	st7365_fill_rgb565(RGB565_BLUE);
	k_msleep(300);
	st7365_fill_rgb565(RGB565_BLACK);

	SHARED_DISP->frame_count = 0;
	SHARED_DISP->fps = 0;
	/* render_mode is set by CPU — don't overwrite it here */
	SHARED_DISP->dirty_y_min = 0;
	SHARED_DISP->dirty_y_max = -1;
	SHARED_DISP->status = DISP_STATUS_READY;

	uint32_t fps_timer = k_uptime_get_32();
	uint32_t fps_frames = 0;

	while (1) {
		while (SHARED_DISP->status != DISP_STATUS_FRAME_READY) {
			/* spin */
		}

		SHARED_DISP->status = DISP_STATUS_FRAME_SENDING;

		uint32_t t0 = k_cyc_to_us_floor32(k_cycle_get_32());

		int y_min = SHARED_DISP->dirty_y_min;
		int y_max = SHARED_DISP->dirty_y_max;
		uint8_t rmode = SHARED_DISP->render_mode;

		if (y_min <= y_max && y_max < FB_HEIGHT) {
			switch (rmode) {
			case RMODE_TILES: {
				/* Tiles don't use y_min/y_max — scan full tile bitmap */
				uint32_t merged[TILE_WORDS];
				for (int i = 0; i < TILE_WORDS; i++) {
					merged[i] = SHARED_DISP->dirty_tiles[i];
				}
				st7365_stream_tiles(SHARED_DISP->framebuffer, merged);
				break;
			}
			case RMODE_PIXELS: {
				/* Merge row bitmaps for row filter */
				uint32_t merged_rows[DIRTY_WORDS];
				for (int i = 0; i < DIRTY_WORDS; i++) {
					merged_rows[i] = SHARED_DISP->dirty_rows[i] |
							 SHARED_DISP->prev_dirty_rows[i];
				}
				/* Read spans (already merged by CPU) */
				st7365_stream_spans(SHARED_DISP->framebuffer,
						    merged_rows,
						    (const struct row_span *)SHARED_DISP->dirty_spans,
						    y_min, y_max);
				break;
			}
			default: /* RMODE_ROWS */ {
				uint32_t merged[DIRTY_WORDS];
				for (int i = 0; i < DIRTY_WORDS; i++) {
					merged[i] = SHARED_DISP->dirty_rows[i] |
						    SHARED_DISP->prev_dirty_rows[i];
				}
				st7365_stream_dirty(SHARED_DISP->framebuffer,
						    merged, y_min, y_max);
				break;
			}
			}
		}

		uint32_t stream_us = k_cyc_to_us_floor32(k_cycle_get_32()) - t0;
		SHARED_DISP->flpr_stream_us = (uint16_t)(stream_us > 65535 ? 65535 : stream_us);
		SHARED_DISP->frame_count++;
		SHARED_DISP->status = DISP_STATUS_READY;

		fps_frames++;
		uint32_t now = k_uptime_get_32();
		uint32_t dt = now - fps_timer;
		if (dt >= 2000) {
			SHARED_DISP->fps = (uint16_t)(fps_frames * 1000 / dt);
			fps_frames = 0;
			fps_timer = now;
		}
	}

	return 0;
}
