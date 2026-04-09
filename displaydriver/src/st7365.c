/*
 * ST7365 display driver — 8080 MCU parallel interface
 * FLPR (RISC-V) core, all pins on GPIO Port 1
 *
 * 16-bit mode: 2 GPIO writes per pixel (vs 4 in 8-bit mode)
 * DB0-7 on P1.00-07, DB8-15 on P1.10-17, gap at P1.08-09 (buttons)
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <hal/nrf_gpio.h>
#include "st7365.h"

/* ------------------------------------------------------------------ */
/*  LUT: palette index → Port 1 pin pattern                          */
/* ------------------------------------------------------------------ */

#if DISPLAY_16BIT
/* 16-bit: one entry per palette index = full 16-bit pin pattern */
static uint32_t pixel_lut[256];
#else
/* 8-bit: hi/lo byte pin patterns */
static struct pixel_pins pixel_lut_8[256];
#endif

/* Full 256-color palette from shared header */
#include "display_palette.h"

/* ------------------------------------------------------------------ */
/*  Pin tables for configuration                                      */
/* ------------------------------------------------------------------ */

#if DISPLAY_16BIT
static const uint8_t data_pins[] = {
	/* DB0-DB4 = P1.03-P1.07 (same as 8-bit) */
	NRF_GPIO_PIN_MAP(1, 3),  NRF_GPIO_PIN_MAP(1, 4),
	NRF_GPIO_PIN_MAP(1, 5),  NRF_GPIO_PIN_MAP(1, 6),
	NRF_GPIO_PIN_MAP(1, 7),
	/* DB5-DB7 = P1.10-P1.12 (same as 8-bit) */
	NRF_GPIO_PIN_MAP(1, 10), NRF_GPIO_PIN_MAP(1, 11),
	NRF_GPIO_PIN_MAP(1, 12),
	/* DB8-DB11 = P1.16-P1.19 (new) */
	NRF_GPIO_PIN_MAP(1, 16), NRF_GPIO_PIN_MAP(1, 17),
	NRF_GPIO_PIN_MAP(1, 18), NRF_GPIO_PIN_MAP(1, 19),
	/* DB12-DB14 = P1.29-P1.31 (new) */
	NRF_GPIO_PIN_MAP(1, 29), NRF_GPIO_PIN_MAP(1, 30),
	NRF_GPIO_PIN_MAP(1, 31),
	/* DB15 = P1.00 (new) */
	NRF_GPIO_PIN_MAP(1, 0),
};
#else
static const uint8_t data_pins[] = {
	NRF_GPIO_PIN_MAP(1, 3),  NRF_GPIO_PIN_MAP(1, 4),
	NRF_GPIO_PIN_MAP(1, 5),  NRF_GPIO_PIN_MAP(1, 6),
	NRF_GPIO_PIN_MAP(1, 7),  NRF_GPIO_PIN_MAP(1, 10),
	NRF_GPIO_PIN_MAP(1, 11), NRF_GPIO_PIN_MAP(1, 12),
};
#endif

static const uint8_t ctrl_pins[] = {
	NRF_GPIO_PIN_MAP(1, PIN_CS),  NRF_GPIO_PIN_MAP(1, PIN_WR),
	NRF_GPIO_PIN_MAP(1, PIN_DC),  NRF_GPIO_PIN_MAP(1, PIN_RD),
	NRF_GPIO_PIN_MAP(1, PIN_RESET),
};

/* ------------------------------------------------------------------ */
/*  Low-level GPIO                                                    */
/* ------------------------------------------------------------------ */

static inline void pin_high(uint32_t mask)  { NRF_P1->OUTSET = mask; }
static inline void pin_low(uint32_t mask)   { NRF_P1->OUTCLR = mask; }

/* Write an 8-bit command/parameter byte over the bus.
 * In 16-bit mode, DB8-15 are cleared (don't-care for commands). */
static void write_byte(uint8_t val)
{
	uint32_t bits = byte_to_port1_pins(val);
	NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
	NRF_P1->OUTSET = bits;
	NRF_P1->OUTSET = WR_MASK;
}

static void write_command(uint8_t cmd)
{
	pin_low(DC_MASK);
	write_byte(cmd);
	pin_high(DC_MASK);
}

static void write_data(uint8_t data)
{
	write_byte(data);
}

static void write_command_data(uint8_t cmd, const uint8_t *data, int len)
{
	write_command(cmd);
	for (int i = 0; i < len; i++) write_data(data[i]);
}

/* ------------------------------------------------------------------ */
/*  Init                                                              */
/* ------------------------------------------------------------------ */

static void configure_pins(void)
{
	for (int i = 0; i < ARRAY_SIZE(data_pins); i++) {
		nrf_gpio_cfg(data_pins[i], NRF_GPIO_PIN_DIR_OUTPUT,
			     NRF_GPIO_PIN_INPUT_DISCONNECT,
			     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1,
			     NRF_GPIO_PIN_NOSENSE);
	}
	for (int i = 0; i < ARRAY_SIZE(ctrl_pins); i++) {
		nrf_gpio_cfg(ctrl_pins[i], NRF_GPIO_PIN_DIR_OUTPUT,
			     NRF_GPIO_PIN_INPUT_DISCONNECT,
			     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1,
			     NRF_GPIO_PIN_NOSENSE);
	}
	pin_high(CS_MASK | WR_MASK | DC_MASK | RD_MASK | RESET_MASK);
	pin_low(DATA_MASK);
}

static void init_lut(void)
{
#if DISPLAY_16BIT
	memset(pixel_lut, 0, sizeof(pixel_lut));
	for (int i = 0; i < PAL_COUNT; i++) {
		pixel_lut[i] = rgb565_to_pins(palette_rgb565[i]);
	}
#else
	memset(pixel_lut_8, 0, sizeof(pixel_lut_8));
	for (int i = 0; i < PAL_COUNT; i++) {
		uint16_t rgb = palette_rgb565[i];
		pixel_lut_8[i].hi = byte_to_port1_pins(rgb >> 8);
		pixel_lut_8[i].lo = byte_to_port1_pins(rgb & 0xFF);
	}
#endif
}

static void hw_reset(void)
{
	pin_high(RESET_MASK);
	k_msleep(10);
	pin_low(RESET_MASK);
	k_msleep(20);
	pin_high(RESET_MASK);
	k_msleep(150);
}

static void set_window(int x0, int y0, int x1, int y1)
{
	/* Commands and parameters are ALWAYS 8-bit on DB0-DB7,
	 * even in 16-bit bus mode. Only pixel data uses full 16 bits. */
	uint8_t caset[] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
	write_command_data(ST7365_CASET, caset, 4);
	uint8_t raset[] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
	write_command_data(ST7365_RASET, raset, 4);
}

static void reset_window(void)
{
	set_window(0, 0, DISP_WIDTH - 1, DISP_HEIGHT - 1);
}

void st7365_init(void)
{
	configure_pins();
	init_lut();
	hw_reset();
	pin_low(CS_MASK);

	write_command(ST7365_SWRESET);
	k_msleep(150);
	write_command(ST7365_SLPOUT);
	k_msleep(150);

	/* Commands and params are always 8-bit, even in 16-bit bus mode */
	uint8_t colmod = COLMOD_RGB565;
	write_command_data(ST7365_COLMOD, &colmod, 1);

	uint8_t madctl = MADCTL_LANDSCAPE;
	write_command_data(ST7365_MADCTL, &madctl, 1);

	reset_window();

	write_command(ST7365_NORON);
	k_msleep(10);
	write_command(ST7365_INVON);
	write_command(ST7365_DISPON);
	k_msleep(50);
}

/* ------------------------------------------------------------------ */
/*  Fill                                                              */
/* ------------------------------------------------------------------ */

void st7365_fill_rgb565(uint16_t color)
{
	reset_window();
	write_command(ST7365_RAMWR);

#if DISPLAY_16BIT
	/* 16-bit: 3 writes per pixel with OUTCLR/OUTSET (safe) */
	uint32_t pins = rgb565_to_pins(color);
	for (int i = 0; i < DISP_PIXELS; i++) {
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = pins;
		NRF_P1->OUTSET = WR_MASK;
	}
#else
	uint32_t hi = byte_to_port1_pins(color >> 8);
	uint32_t lo = byte_to_port1_pins(color & 0xFF);
	for (int i = 0; i < DISP_PIXELS; i++) {
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = hi;
		NRF_P1->OUTSET = WR_MASK;
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = lo;
		NRF_P1->OUTSET = WR_MASK;
	}
#endif
}

/* ------------------------------------------------------------------ */
/*  Stream full framebuffer                                           */
/* ------------------------------------------------------------------ */

void st7365_stream_framebuffer(const uint8_t *fb, int len)
{
	reset_window();
	write_command(ST7365_RAMWR);

#if DISPLAY_16BIT
	/* 16-bit: 3 writes per pixel, unrolled 4x = 12 per iteration */
	int i = 0, len4 = len & ~3;
	for (; i < len4; i += 4) {
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = pixel_lut[fb[i]];
		NRF_P1->OUTSET = WR_MASK;
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = pixel_lut[fb[i+1]];
		NRF_P1->OUTSET = WR_MASK;
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = pixel_lut[fb[i+2]];
		NRF_P1->OUTSET = WR_MASK;
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = pixel_lut[fb[i+3]];
		NRF_P1->OUTSET = WR_MASK;
	}
	for (; i < len; i++) {
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = pixel_lut[fb[i]];
		NRF_P1->OUTSET = WR_MASK;
	}
#else
	/* 8-bit: OUTCLR/OUTSET — proven safe, doesn't stomp non-display pins */
	for (int i = 0; i < len; i++) {
		const struct pixel_pins *p = &pixel_lut_8[fb[i]];
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = p->hi;
		NRF_P1->OUTSET = WR_MASK;
		NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
		NRF_P1->OUTSET = p->lo;
		NRF_P1->OUTSET = WR_MASK;
	}
#endif
}

/* ------------------------------------------------------------------ */
/*  Stream dirty rows only                                            */
/* ------------------------------------------------------------------ */

void st7365_stream_dirty(const uint8_t *fb, const uint32_t *dirty_bits,
			 int y_min, int y_max)
{
	int y = y_min;

	while (y <= y_max) {
		while (y <= y_max && !(dirty_bits[y >> 5] & (1U << (y & 31)))) y++;
		if (y > y_max) break;

		int run_start = y;
		while (y <= y_max && (dirty_bits[y >> 5] & (1U << (y & 31)))) y++;
		int run_end = y - 1;

		set_window(0, run_start, DISP_WIDTH - 1, run_end);
		write_command(ST7365_RAMWR);

		const uint8_t *row = &fb[run_start * DISP_WIDTH];
		int npix = (run_end - run_start + 1) * DISP_WIDTH;

#if DISPLAY_16BIT
		int i = 0, npix4 = npix & ~3;
		for (; i < npix4; i += 4) {
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = pixel_lut[row[i]];
			NRF_P1->OUTSET = WR_MASK;
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = pixel_lut[row[i+1]];
			NRF_P1->OUTSET = WR_MASK;
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = pixel_lut[row[i+2]];
			NRF_P1->OUTSET = WR_MASK;
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = pixel_lut[row[i+3]];
			NRF_P1->OUTSET = WR_MASK;
		}
		for (; i < npix; i++) {
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = pixel_lut[row[i]];
			NRF_P1->OUTSET = WR_MASK;
		}
#else
		/* 8-bit: OUTCLR/OUTSET */
		for (int i = 0; i < npix; i++) {
			const struct pixel_pins *p = &pixel_lut_8[row[i]];
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = p->hi;
			NRF_P1->OUTSET = WR_MASK;
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = p->lo;
			NRF_P1->OUTSET = WR_MASK;
		}
#endif
	}
}

/* ------------------------------------------------------------------ */
/*  Stream dirty tiles                                                */
/* ------------------------------------------------------------------ */

void st7365_stream_tiles(const uint8_t *fb, const uint32_t *tile_bits)
{
	for (int w = 0; w < TILE_WORDS; w++) {
		uint32_t bits = tile_bits[w];
		while (bits) {
			int bit = __builtin_ctz(bits);
			int idx = (w << 5) | bit;
			bits &= bits - 1;

			if (idx >= TILE_COUNT) continue;

			int tx = (idx % TILES_X) * TILE_W;
			int ty = (idx / TILES_X) * TILE_H;
			int tw = TILE_W;
			int th = TILE_H;
			if (tx + tw > DISP_WIDTH) tw = DISP_WIDTH - tx;
			if (ty + th > DISP_HEIGHT) th = DISP_HEIGHT - ty;

			set_window(tx, ty, tx + tw - 1, ty + th - 1);
			write_command(ST7365_RAMWR);

			for (int r = 0; r < th; r++) {
				const uint8_t *row = &fb[(ty + r) * DISP_WIDTH + tx];
#if DISPLAY_16BIT
				for (int c = 0; c < tw; c++) {
					NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
					NRF_P1->OUTSET = pixel_lut[row[c]];
					NRF_P1->OUTSET = WR_MASK;
				}
#else
				for (int c = 0; c < tw; c++) {
					const struct pixel_pins *p = &pixel_lut_8[row[c]];
					NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
					NRF_P1->OUTSET = p->hi;
					NRF_P1->OUTSET = WR_MASK;
					NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
					NRF_P1->OUTSET = p->lo;
					NRF_P1->OUTSET = WR_MASK;
				}
#endif
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Stream dirty row spans (horizontal clipping)                      */
/* ------------------------------------------------------------------ */

void st7365_stream_spans(const uint8_t *fb, const uint32_t *row_bits,
			 const struct row_span *spans, int y_min, int y_max)
{
	for (int y = y_min; y <= y_max; y++) {
		if (!(row_bits[y >> 5] & (1U << (y & 31)))) continue;

		int x0 = spans[y].x_min;
		int x1 = spans[y].x_max;
		if (x0 > x1 || x0 >= DISP_WIDTH) continue;
		if (x1 >= DISP_WIDTH) x1 = DISP_WIDTH - 1;

		set_window(x0, y, x1, y);
		write_command(ST7365_RAMWR);

		const uint8_t *row = &fb[y * DISP_WIDTH + x0];
		int npix = x1 - x0 + 1;

#if DISPLAY_16BIT
		for (int i = 0; i < npix; i++) {
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = pixel_lut[row[i]];
			NRF_P1->OUTSET = WR_MASK;
		}
#else
		for (int i = 0; i < npix; i++) {
			const struct pixel_pins *p = &pixel_lut_8[row[i]];
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = p->hi;
			NRF_P1->OUTSET = WR_MASK;
			NRF_P1->OUTCLR = DATA_MASK | WR_MASK;
			NRF_P1->OUTSET = p->lo;
			NRF_P1->OUTSET = WR_MASK;
		}
#endif
	}
}
