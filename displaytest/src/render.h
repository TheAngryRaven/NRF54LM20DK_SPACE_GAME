#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include "shared_display.h"

void render_init(uint8_t *framebuffer);
void render_set_mode(uint8_t mode);  /* RMODE_ROWS, RMODE_TILES, RMODE_PIXELS */
void render_clear(void);
void render_full_clear(void);
void render_commit_dirty(void);

void render_pixel(int x, int y, uint8_t color);
void render_line(int x0, int y0, int x1, int y1, uint8_t color);
void render_circle(int cx, int cy, int r, uint8_t color);
void render_circle_filled(int cx, int cy, int r, uint8_t color);
void render_polygon(const int *vx, const int *vy, int count, uint8_t color);
void render_char(int x, int y, char ch, uint8_t color);
void render_string(int x, int y, const char *str, uint8_t color);
void render_string_centered(int y, const char *str, uint8_t color);
void render_number(int x, int y, int num, uint8_t color);
void render_number_centered(int y, int num, uint8_t color);

/* Draw a sprite with transparency (index 0 = skip) */
void render_sprite(int x, int y, const uint8_t *data, int w, int h);

#endif /* RENDER_H */
