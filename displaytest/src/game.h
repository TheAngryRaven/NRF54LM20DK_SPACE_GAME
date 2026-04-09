#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>
#include "shared_display.h"

/* --- Screen & World --- */
#define SCREEN_W        FB_WIDTH    /* 480 */
#define SCREEN_H        FB_HEIGHT   /* 320 */
#define WORLD_W         960
#define WORLD_H         640

/* --- Limits --- */
#define MAX_ASTEROIDS   24
#define MAX_BULLETS     8
#define MAX_PARTICLES   48

/* --- Tuning --- */
#define SHIP_THRUST         0.12f
#define SHIP_MAX_SPEED      5.0f
#define SHIP_ROTATE_SPEED   5.0f    /* degrees per frame */
#define SHIP_RADIUS         10

#define BULLET_SPEED        7.0f
#define BULLET_LIFETIME     60      /* frames (~2 sec) */
#define BULLET_RADIUS       2

#define ASTEROID_LARGE_R    25
#define ASTEROID_SMALL_R    12
#define ASTEROID_SPEED_MIN  0.3f
#define ASTEROID_SPEED_MAX  1.5f
#define ASTEROID_VERTICES   8       /* vertices per asteroid shape */

#define PARTICLE_LIFE_MIN   8
#define PARTICLE_LIFE_MAX   20
#define PARTICLE_SPEED      3.0f

#define SCORE_LARGE         10
#define SCORE_SMALL         20

#define INITIAL_ASTEROIDS   10
#define WAVE_EXTRA          2       /* extra asteroids each wave */

#define FRAME_TIME_MS       33      /* ~30 fps */

/* Set to 1 to disable collisions/death for display stress testing */
#define BENCHMARK_MODE      0

#define NUM_STARS           50      /* sparse background stars */

/* Fixed-point trig: we precompute sin/cos tables for integer degrees */
#define DEG_COUNT 360

/* --- Types --- */

struct vec2 {
	float x;
	float y;
};

enum game_screen {
	SCREEN_TITLE,
	SCREEN_PLAY,
	SCREEN_DEAD,
};

struct ship {
	struct vec2 pos;
	struct vec2 vel;
	float angle;            /* degrees, 0 = right */
	bool thrusting;
};

struct asteroid {
	struct vec2 pos;
	struct vec2 vel;
	float angle;
	float spin;             /* degrees per frame rotation */
	int size;               /* 0=inactive, 1=large, 2=small */
	int radius;             /* cached from size */
	int bullet_r2;          /* (radius + BULLET_RADIUS)^2, precomputed */
	int ship_r2;            /* (radius + SHIP_RADIUS)^2, precomputed */
	uint8_t shape_seed;
};

struct bullet {
	struct vec2 pos;
	struct vec2 vel;
	int life;               /* frames remaining; 0=inactive */
};

struct particle {
	struct vec2 pos;
	struct vec2 vel;
	int life;               /* frames remaining; 0=inactive */
	uint8_t color;          /* palette index */
};

struct star {
	int16_t x;  /* screen-space, fixed */
	int16_t y;
};

struct game_state {
	enum game_screen screen;
	enum game_screen prev_screen; /* for detecting transitions */
	struct ship ship;
	struct asteroid asteroids[MAX_ASTEROIDS];
	struct bullet bullets[MAX_BULLETS];
	struct particle particles[MAX_PARTICLES];
	struct star stars[NUM_STARS];
	int score;
	int wave;
	int active_asteroids;   /* cached count, updated on spawn/destroy */
	uint32_t frame;         /* frame counter */
	uint32_t rng;           /* simple PRNG state */
	int death_timer;        /* delay before allowing restart */
};

/* Button state bitmask */
#define BTN_THRUST  (1 << 0)
#define BTN_LEFT    (1 << 1)
#define BTN_RIGHT   (1 << 2)
#define BTN_SHOOT   (1 << 3)

/* --- API --- */
void game_init(struct game_state *g);
void game_update(struct game_state *g, uint8_t buttons);
void game_render(struct game_state *g);

#endif /* GAME_H */
