#ifndef ST7365_H
#define ST7365_H

#include <stdint.h>
#include <stdbool.h>
#include "shared_display.h"

/*
 * ST7365 display driver for UEED035HV-RX40-L001
 * 480x320 landscape, 8080 MCU parallel interface
 * All pins on GPIO Port 1 for fast register writes from FLPR
 *
 * Set DISPLAY_16BIT to 1 for 16-bit bus (2 GPIO writes/pixel)
 * Set DISPLAY_16BIT to 0 for 8-bit bus  (4 GPIO writes/pixel)
 */

#define DISPLAY_16BIT   1

/* Screen dimensions (landscape) */
#define DISP_WIDTH      480
#define DISP_HEIGHT     320
#define DISP_PIXELS     (DISP_WIDTH * DISP_HEIGHT)

/* ------------------------------------------------------------------ */
/*  Pin mapping                                                       */
/* ------------------------------------------------------------------ */

#if DISPLAY_16BIT

/*
 * 16-bit data bus — extends the proven 8-bit pinout with 8 more pins.
 *
 *   DB0-DB4  = P1.03-P1.07  (bits 3-7)     — same as 8-bit
 *   DB5-DB7  = P1.10-P1.12  (bits 10-12)   — same as 8-bit
 *   DB8      = P1.16  (bit 16)
 *   DB9      = P1.17  (bit 17)
 *   DB10     = P1.18  (bit 18)
 *   DB11     = P1.19  (bit 19)
 *   DB12     = P1.29  (bit 29)
 *   DB13     = P1.30  (bit 30)
 *   DB14     = P1.31  (bit 31)
 *   DB15     = P1.00  (bit 0)
 *
 * Control: same as 8-bit (CS=13, WR=14, DC=15, RD=23, RESET=24)
 */

#define DATA_MASK  ( (1UL << 0)              /* DB15=P1.00 */ \
                   | (0x1FUL << 3)           /* DB0-4=P1.03-07 */ \
                   | (0x07UL << 10)          /* DB5-7=P1.10-12 */ \
                   | (0x0FUL << 16)          /* DB8-11=P1.16-19 */ \
                   | (0x07UL << 29) )        /* DB12-14=P1.29-31 */

#define PIN_CS          13
#define PIN_WR          14
#define PIN_DC          15
#define PIN_RD          23
#define PIN_RESET       24

/*
 * Convert RGB565 to Port 1 pin positions.
 * Scattered mapping — but only computed once per pixel via LUT.
 *
 * RGB565 bit -> Display DB -> Port 1 bit
 *   0-4      -> DB0-4     -> 3-7    (shift left 3)
 *   5-7      -> DB5-7     -> 10-12  (shift left 5)
 *   8-11     -> DB8-11    -> 16-19  (shift left 8)
 *   12-14    -> DB12-14   -> 29-31  (shift left 17)
 *   15       -> DB15      -> 0      (shift right 15)
 */
static inline uint32_t rgb565_to_pins(uint16_t rgb)
{
	return ((uint32_t)(rgb & 0x001F) << 3)     /* DB0-4  -> P1.03-07 */
	     | ((uint32_t)(rgb & 0x00E0) << 5)     /* DB5-7  -> P1.10-12 */
	     | ((uint32_t)(rgb & 0x0F00) << 8)     /* DB8-11 -> P1.16-19 */
	     | ((uint32_t)(rgb & 0x7000) << 17)    /* DB12-14-> P1.29-31 */
	     | ((uint32_t)(rgb >> 15) & 0x01);     /* DB15   -> P1.00    */
}

/* Command/param byte: only DB0-DB7 (same mapping as 8-bit mode) */
static inline uint32_t byte_to_port1_pins(uint8_t byte)
{
	return ((uint32_t)(byte & 0x1F) << 3)
	     | ((uint32_t)((byte >> 5) & 0x07) << 10);
}

#else /* 8-bit mode */

/*
 * 8-bit data bus:
 *   DB0-DB4 = P1.03-P1.07  (Port 1 bits 3-7)
 *   DB5-DB7 = P1.10-P1.12  (Port 1 bits 10-12)
 *
 * Control signals:
 *   CS    = P1.13
 *   WR    = P1.14
 *   DC    = P1.15
 *   RD    = P1.23
 *   RESET = P1.24
 */

#define DATA_LO_MASK    (0x1FUL << 3)       /* P1.03-P1.07 */
#define DATA_HI_MASK    (0x07UL << 10)      /* P1.10-P1.12 */
#define DATA_MASK       (DATA_LO_MASK | DATA_HI_MASK)

/* Original 8-bit control pins (proven working) */
#define PIN_CS          13
#define PIN_WR          14
#define PIN_DC          15
#define PIN_RD          23
#define PIN_RESET       24

static inline uint32_t byte_to_port1_pins(uint8_t byte)
{
	return ((uint32_t)(byte & 0x1F) << 3)
	     | ((uint32_t)((byte >> 5) & 0x07) << 10);
}

#endif /* DISPLAY_16BIT */

/* Control pin masks (same for both modes, just different pin numbers) */
#define CS_MASK         (1UL << PIN_CS)
#define WR_MASK         (1UL << PIN_WR)
#define DC_MASK         (1UL << PIN_DC)
#define RD_MASK         (1UL << PIN_RD)
#define RESET_MASK      (1UL << PIN_RESET)

#define CTRL_MASK       (CS_MASK | WR_MASK | DC_MASK | RD_MASK | RESET_MASK)
#define ALL_DISP_MASK   (DATA_MASK | CTRL_MASK)

/* ------------------------------------------------------------------ */
/*  ST7365 Commands                                                   */
/* ------------------------------------------------------------------ */

#define ST7365_NOP      0x00
#define ST7365_SWRESET  0x01
#define ST7365_SLPOUT   0x11
#define ST7365_NORON    0x13
#define ST7365_INVOFF   0x20
#define ST7365_INVON    0x21
#define ST7365_DISPON   0x29
#define ST7365_DISPOFF  0x28
#define ST7365_CASET    0x2A
#define ST7365_RASET    0x2B
#define ST7365_RAMWR    0x2C
#define ST7365_MADCTL   0x36
#define ST7365_COLMOD   0x3A

/* MADCTL bits */
#define MADCTL_MY       0x80
#define MADCTL_MX       0x40
#define MADCTL_MV       0x20
#define MADCTL_ML       0x10
#define MADCTL_BGR      0x08
#define MADCTL_MH       0x04

#define MADCTL_LANDSCAPE    (MADCTL_MV | MADCTL_MX | MADCTL_MY | MADCTL_BGR)
#define COLMOD_RGB565   0x55

/* 16-color palette (index 0 = transparent/black) */
#define PAL_TRANSPARENT 0
#define PAL_WHITE       1
#define PAL_BLACK       2
#define PAL_RED         3
#define PAL_YELLOW      4
#define PAL_ORANGE      5
#define PAL_GREEN       6
#define PAL_CYAN        7
#define PAL_BLUE        8
#define PAL_DKGRAY      9
#define PAL_MDGRAY      10
#define PAL_LTGRAY      11
#define PAL_DKRED       12
#define PAL_DKGREEN     13
#define PAL_DKBLUE      14
#define PAL_MAGENTA     15
#define PAL_COUNT       16

#define RGB565_BLACK    0x0000
#define RGB565_WHITE    0xFFFF
#define RGB565_RED      0xF800
#define RGB565_YELLOW   0xFFE0
#define RGB565_ORANGE   0xFC00
#define RGB565_GREEN    0x07E0
#define RGB565_CYAN     0x07FF
#define RGB565_BLUE     0x001F
#define RGB565_DKGRAY   0x4208
#define RGB565_MDGRAY   0x8410
#define RGB565_LTGRAY   0xC618
#define RGB565_DKRED    0x8000
#define RGB565_DKGREEN  0x0400
#define RGB565_DKBLUE   0x0010
#define RGB565_MAGENTA  0xF81F

/* Precomputed pixel pin data */
struct pixel_pins {
	uint32_t hi;
	uint32_t lo;
};

/* Public API */
void st7365_init(void);
void st7365_fill_rgb565(uint16_t color);
void st7365_begin_pixels(void);
void st7365_stream_framebuffer(const uint8_t *fb, int len);
void st7365_stream_dirty(const uint8_t *fb, const uint32_t *dirty_bits,
			 int y_min, int y_max);
void st7365_stream_tiles(const uint8_t *fb, const uint32_t *tile_bits);
void st7365_stream_spans(const uint8_t *fb, const uint32_t *row_bits,
			 const struct row_span *spans, int y_min, int y_max);

#endif /* ST7365_H */
