/*
 * Asteroids game logic and rendering.
 * Pure game code - no hardware dependencies.
 */

#include <string.h>
#include "game.h"
#include "render.h"

/* Precomputed float sin/cos tables */
static float sin_table[DEG_COUNT];
static float cos_table[DEG_COUNT];
static bool trig_initialized;

/* No more precomputed R2 defines — stored per-asteroid at spawn time */

/* Precomputed vertex angle step */
#define VERT_STEP (360.0f / ASTEROID_VERTICES)

/* Half-world constants for wrapping (avoid repeated division) */
#define HALF_W (WORLD_W / 2)
#define HALF_H (WORLD_H / 2)

/* ------------------------------------------------------------------ */
/*  PRNG                                                              */
/* ------------------------------------------------------------------ */

static inline uint32_t rng_next(struct game_state *g)
{
	uint32_t x = g->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g->rng = x;
	return x;
}

static inline float rng_float(struct game_state *g, float lo, float hi)
{
	return lo + (float)(rng_next(g) & 0xFFFF) * (1.0f / 65536.0f) * (hi - lo);
}

static inline int rng_int(struct game_state *g, int lo, int hi)
{
	return lo + (int)(rng_next(g) % (uint32_t)(hi - lo + 1));
}

/* ------------------------------------------------------------------ */
/*  Trig (inlined lookups, no function call overhead)                 */
/* ------------------------------------------------------------------ */

static void init_trig(void)
{
	if (trig_initialized) return;

	for (int d = 0; d < DEG_COUNT; d++) {
		float rad = (float)d * (3.14159265f / 180.0f);
		float x = rad;
		float x2 = x * x;
		float x3 = x2 * x;
		float x5 = x3 * x2;
		float x7 = x5 * x2;
		float s = x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
		if (s > 1.0f) s = 1.0f;
		if (s < -1.0f) s = -1.0f;
		sin_table[d] = s;
	}
	for (int d = 0; d < DEG_COUNT; d++) {
		cos_table[d] = sin_table[(d + 90) % DEG_COUNT];
	}
	trig_initialized = true;
}

/* Branchless angle index: handles negatives without conditional */
static inline int angle_idx(float deg)
{
	int d = (int)deg % DEG_COUNT;
	return d + (DEG_COUNT & (d >> 31)); /* add 360 if negative */
}

static inline float fsin(float deg) { return sin_table[angle_idx(deg)]; }
static inline float fcos(float deg) { return cos_table[angle_idx(deg)]; }

/* ------------------------------------------------------------------ */
/*  Wrapping (if, not while — entities never overshoot by >1 world)   */
/* ------------------------------------------------------------------ */

static inline void wrap_pos(struct vec2 *p)
{
	if (p->x < 0) p->x += WORLD_W;
	else if (p->x >= WORLD_W) p->x -= WORLD_W;
	if (p->y < 0) p->y += WORLD_H;
	else if (p->y >= WORLD_H) p->y -= WORLD_H;
}

/* ------------------------------------------------------------------ */
/*  World-to-screen (inlined)                                         */
/* ------------------------------------------------------------------ */

static inline int to_sx(float wx, float cx)
{
	float dx = wx - cx;
	if (dx > HALF_W) dx -= WORLD_W;
	else if (dx < -HALF_W) dx += WORLD_W;
	return (int)dx + SCREEN_W / 2;
}

static inline int to_sy(float wy, float cy)
{
	float dy = wy - cy;
	if (dy > HALF_H) dy -= WORLD_H;
	else if (dy < -HALF_H) dy += WORLD_H;
	return (int)dy + SCREEN_H / 2;
}

static inline bool on_screen(int sx, int sy, int margin)
{
	return (unsigned)(sx + margin) < (unsigned)(SCREEN_W + 2 * margin) &&
	       (unsigned)(sy + margin) < (unsigned)(SCREEN_H + 2 * margin);
}

/* ------------------------------------------------------------------ */
/*  Collision — ALL INTEGER, no float in the hot path                 */
/*  Truncate positions to int, integer wrapping, manhattan reject,    */
/*  integer distance squared. ~6 cycles for the 90% rejection case.  */
/* ------------------------------------------------------------------ */

static inline bool hit_test(struct vec2 a, struct vec2 b, int r, int r_sq)
{
	int dx = (int)a.x - (int)b.x;
	int dy = (int)a.y - (int)b.y;

	/* Integer wrapping */
	if (dx > HALF_W) dx -= WORLD_W;
	else if (dx < -HALF_W) dx += WORLD_W;
	if (dy > HALF_H) dy -= WORLD_H;
	else if (dy < -HALF_H) dy += WORLD_H;

	/* Manhattan early-out with tight radius (not hardcoded 40) */
	if (dx > r || dx < -r || dy > r || dy < -r) {
		return false;
	}

	return dx * dx + dy * dy < r_sq;
}

/* Float version only used for spawn distance check (called rarely) */
static inline float dist_sq_f(struct vec2 a, struct vec2 b)
{
	float dx = a.x - b.x;
	float dy = a.y - b.y;
	if (dx > HALF_W) dx -= WORLD_W;
	else if (dx < -HALF_W) dx += WORLD_W;
	if (dy > HALF_H) dy -= WORLD_H;
	else if (dy < -HALF_H) dy += WORLD_H;
	return dx * dx + dy * dy;
}

/* ------------------------------------------------------------------ */
/*  Spawning                                                          */
/* ------------------------------------------------------------------ */

static void spawn_particles(struct game_state *g, struct vec2 pos, int count)
{
	for (int i = 0; i < MAX_PARTICLES && count > 0; i++) {
		if (g->particles[i].life > 0) continue;
		struct particle *p = &g->particles[i];
		p->pos = pos;
		float angle = rng_float(g, 0, 360);
		float speed = rng_float(g, 0.5f, PARTICLE_SPEED);
		p->vel.x = fcos(angle) * speed;
		p->vel.y = fsin(angle) * speed;
		p->life = rng_int(g, PARTICLE_LIFE_MIN, PARTICLE_LIFE_MAX);
		p->color = rng_int(g, COLOR_RED, COLOR_ORANGE);
		count--;
	}
}

static void spawn_asteroid(struct game_state *g, int size, float x, float y)
{
	for (int i = 0; i < MAX_ASTEROIDS; i++) {
		if (g->asteroids[i].size != 0) continue;
		struct asteroid *a = &g->asteroids[i];
		a->pos.x = x;
		a->pos.y = y;
		a->size = size;
		int r = (size == 1) ? ASTEROID_LARGE_R : ASTEROID_SMALL_R;
		a->radius = r;
		int br = r + BULLET_RADIUS;
		int sr = r + SHIP_RADIUS;
		a->bullet_r2 = br * br;
		a->ship_r2 = sr * sr;
		a->shape_seed = (uint8_t)(rng_next(g) & 0xFF);
		float angle = rng_float(g, 0, 360);
		float speed = rng_float(g, ASTEROID_SPEED_MIN, ASTEROID_SPEED_MAX);
		if (size == 2) speed *= 1.5f;
		a->vel.x = fcos(angle) * speed;
		a->vel.y = fsin(angle) * speed;
		a->angle = rng_float(g, 0, 360);
		a->spin = rng_float(g, -2.0f, 2.0f);
		g->active_asteroids++;
		return;
	}
}

static void spawn_wave(struct game_state *g)
{
	g->wave++;
	int count = INITIAL_ASTEROIDS + (g->wave - 1) * WAVE_EXTRA;
	if (count > MAX_ASTEROIDS) count = MAX_ASTEROIDS;

	for (int i = 0; i < count; i++) {
		float x, y;
		do {
			x = rng_float(g, 0, WORLD_W);
			y = rng_float(g, 0, WORLD_H);
		} while (dist_sq_f((struct vec2){x, y}, g->ship.pos) < 150.0f * 150.0f);
		spawn_asteroid(g, 1, x, y);
	}
}

/* count_active_asteroids: just return cached value */
static inline int count_active_asteroids(struct game_state *g)
{
	return g->active_asteroids;
}

/* ------------------------------------------------------------------ */
/*  Stars                                                             */
/* ------------------------------------------------------------------ */

static void generate_stars(struct game_state *g)
{
	uint32_t saved_rng = g->rng;
	g->rng = 0xDEADBEEF;
	for (int i = 0; i < NUM_STARS; i++) {
		g->stars[i].x = rng_int(g, 0, WORLD_W - 1);
		g->stars[i].y = rng_int(g, 0, WORLD_H - 1);
	}
	g->rng = saved_rng;
}

/* ------------------------------------------------------------------ */
/*  Init                                                              */
/* ------------------------------------------------------------------ */

void game_init(struct game_state *g)
{
	init_trig();
	memset(g, 0, sizeof(*g));
	g->screen = SCREEN_TITLE;
	g->prev_screen = SCREEN_TITLE;
	g->rng = 12345678;
	g->ship.pos.x = WORLD_W / 2;
	g->ship.pos.y = WORLD_H / 2;
	g->ship.angle = 270;
	generate_stars(g);
}

static void start_game(struct game_state *g, uint32_t seed)
{
	struct star saved_stars[NUM_STARS];
	memcpy(saved_stars, g->stars, sizeof(saved_stars));

	uint32_t rng = g->rng ^ seed;
	enum game_screen old_screen = g->screen;
	memset(g, 0, sizeof(*g));
	g->rng = rng ? rng : 42;
	g->screen = SCREEN_PLAY;
	g->prev_screen = old_screen;
	memcpy(g->stars, saved_stars, sizeof(saved_stars));

	g->ship.pos.x = WORLD_W / 2;
	g->ship.pos.y = WORLD_H / 2;
	g->ship.angle = 270;
	g->wave = 0;
	spawn_wave(g);
}

/* ------------------------------------------------------------------ */
/*  Update                                                            */
/* ------------------------------------------------------------------ */

void game_update(struct game_state *g, uint8_t buttons)
{
	g->frame++;

#if BENCHMARK_MODE
	/* Auto-start, skip title/death screens */
	if (g->screen != SCREEN_PLAY) {
		start_game(g, g->frame);
	}
#else
	switch (g->screen) {
	case SCREEN_TITLE:
		if (buttons & BTN_SHOOT) start_game(g, g->frame);
		return;
	case SCREEN_DEAD:
		if (g->death_timer > 0) g->death_timer--;
		else if (buttons & BTN_SHOOT) start_game(g, g->frame);
		return;
	case SCREEN_PLAY:
		break;
	}
#endif

	/* Ship rotation */
	if (buttons & BTN_LEFT) {
		g->ship.angle -= SHIP_ROTATE_SPEED;
		if (g->ship.angle < 0) g->ship.angle += 360;
	}
	if (buttons & BTN_RIGHT) {
		g->ship.angle += SHIP_ROTATE_SPEED;
		if (g->ship.angle >= 360) g->ship.angle -= 360;
	}

	/* Ship thrust */
	g->ship.thrusting = (buttons & BTN_THRUST) != 0;
	if (g->ship.thrusting) {
		float ca = fcos(g->ship.angle);
		float sa = fsin(g->ship.angle);
		g->ship.vel.x += ca * SHIP_THRUST;
		g->ship.vel.y += sa * SHIP_THRUST;

		float spd2 = g->ship.vel.x * g->ship.vel.x +
			     g->ship.vel.y * g->ship.vel.y;
		if (spd2 > SHIP_MAX_SPEED * SHIP_MAX_SPEED) {
			g->ship.vel.x *= 0.97f;
			g->ship.vel.y *= 0.97f;
		}
	}

	/* Ship movement */
	g->ship.pos.x += g->ship.vel.x;
	g->ship.pos.y += g->ship.vel.y;
	wrap_pos(&g->ship.pos);

#if !BENCHMARK_MODE
	/* Shooting */
	if ((buttons & BTN_SHOOT) && (g->frame % 8) == 0) {
		float ca = fcos(g->ship.angle);
		float sa = fsin(g->ship.angle);
		for (int i = 0; i < MAX_BULLETS; i++) {
			if (g->bullets[i].life > 0) continue;
			struct bullet *b = &g->bullets[i];
			b->pos.x = g->ship.pos.x + ca * SHIP_RADIUS;
			b->pos.y = g->ship.pos.y + sa * SHIP_RADIUS;
			b->vel.x = ca * BULLET_SPEED + g->ship.vel.x * 0.5f;
			b->vel.y = sa * BULLET_SPEED + g->ship.vel.y * 0.5f;
			b->life = BULLET_LIFETIME;
			break;
		}
	}
#endif

	/* Update bullets */
	for (int i = 0; i < MAX_BULLETS; i++) {
		struct bullet *b = &g->bullets[i];
		if (b->life <= 0) continue;
		b->pos.x += b->vel.x;
		b->pos.y += b->vel.y;
		wrap_pos(&b->pos);
		b->life--;
	}

	/* Update asteroids */
	for (int i = 0; i < MAX_ASTEROIDS; i++) {
		struct asteroid *a = &g->asteroids[i];
		if (a->size == 0) continue;
		a->pos.x += a->vel.x;
		a->pos.y += a->vel.y;
		wrap_pos(&a->pos);
		a->angle += a->spin;
		if (a->angle >= 360) a->angle -= 360;
		if (a->angle < 0) a->angle += 360;
	}

	/* Update particles */
	for (int i = 0; i < MAX_PARTICLES; i++) {
		struct particle *p = &g->particles[i];
		if (p->life <= 0) continue;
		p->pos.x += p->vel.x;
		p->pos.y += p->vel.y;
		p->vel.x *= 0.95f;
		p->vel.y *= 0.95f;
		p->life--;
	}

#if !BENCHMARK_MODE
	/* Bullet-asteroid collisions (pure integer hit_test) */
	for (int bi = 0; bi < MAX_BULLETS; bi++) {
		struct bullet *b = &g->bullets[bi];
		if (b->life <= 0) continue;

		for (int ai = 0; ai < MAX_ASTEROIDS; ai++) {
			struct asteroid *a = &g->asteroids[ai];
			if (a->size == 0) continue;

			if (hit_test(b->pos, a->pos,
				     a->radius + BULLET_RADIUS, a->bullet_r2)) {
				b->life = 0;
				spawn_particles(g, a->pos, 8);
				if (a->size == 1) {
					g->score += SCORE_LARGE;
					float ax = a->pos.x, ay = a->pos.y;
					a->size = 0;
					g->active_asteroids--;
					spawn_asteroid(g, 2, ax, ay);
					spawn_asteroid(g, 2, ax, ay);
				} else {
					g->score += SCORE_SMALL;
					a->size = 0;
					g->active_asteroids--;
				}
				break;
			}
		}
	}

	/* Ship-asteroid collisions */
	for (int i = 0; i < MAX_ASTEROIDS; i++) {
		struct asteroid *a = &g->asteroids[i];
		if (a->size == 0) continue;

		if (hit_test(g->ship.pos, a->pos,
			     a->radius + SHIP_RADIUS, a->ship_r2)) {
			spawn_particles(g, g->ship.pos, 16);
			g->screen = SCREEN_DEAD;
			g->death_timer = 60;
			return;
		}
	}
#endif /* !BENCHMARK_MODE */

	/* Spawn new wave */
	if (g->active_asteroids == 0) {
		spawn_wave(g);
	}
}

/* ------------------------------------------------------------------ */
/*  Rendering                                                         */
/* ------------------------------------------------------------------ */

static void render_ship(struct game_state *g, float cam_x, float cam_y)
{
	float a = g->ship.angle;
	int sx = to_sx(g->ship.pos.x, cam_x);
	int sy = to_sy(g->ship.pos.y, cam_y);

	/* Cache trig for the base angle — derive the others from offsets */
	int nose_x  = sx + (int)(fcos(a) * SHIP_RADIUS);
	int nose_y  = sy + (int)(fsin(a) * SHIP_RADIUS);
	int left_x  = sx + (int)(fcos(a + 140) * SHIP_RADIUS);
	int left_y  = sy + (int)(fsin(a + 140) * SHIP_RADIUS);
	int right_x = sx + (int)(fcos(a - 140) * SHIP_RADIUS);
	int right_y = sy + (int)(fsin(a - 140) * SHIP_RADIUS);

	render_line(nose_x, nose_y, left_x, left_y, COLOR_WHITE);
	render_line(left_x, left_y, right_x, right_y, COLOR_WHITE);
	render_line(right_x, right_y, nose_x, nose_y, COLOR_WHITE);

	if (g->ship.thrusting && (g->frame & 2)) {
		int tail_x = sx - (int)(fcos(a) * (SHIP_RADIUS + 6));
		int tail_y = sy - (int)(fsin(a) * (SHIP_RADIUS + 6));
		int bl_x   = sx + (int)(fcos(a + 160) * (SHIP_RADIUS - 3));
		int bl_y   = sy + (int)(fsin(a + 160) * (SHIP_RADIUS - 3));
		int br_x   = sx + (int)(fcos(a - 160) * (SHIP_RADIUS - 3));
		int br_y   = sy + (int)(fsin(a - 160) * (SHIP_RADIUS - 3));

		render_line(bl_x, bl_y, tail_x, tail_y, COLOR_ORANGE);
		render_line(tail_x, tail_y, br_x, br_y, COLOR_ORANGE);
	}
}

static void render_asteroid_shape(struct asteroid *a, int sx, int sy)
{
	int r = a->radius;
	int vx[ASTEROID_VERTICES];
	int vy[ASTEROID_VERTICES];

	uint8_t seed = a->shape_seed;
	float base_angle = a->angle;

	for (int i = 0; i < ASTEROID_VERTICES; i++) {
		float va = base_angle + (float)i * VERT_STEP;
		/* Vary radius: 70% or 100% based on seed bit */
		int vr = (seed & (1 << (i & 7))) ? r : (r * 7 / 10);
		vx[i] = sx + (int)(fcos(va) * vr);
		vy[i] = sy + (int)(fsin(va) * vr);
	}

	render_polygon(vx, vy, ASTEROID_VERTICES, COLOR_WHITE);
}

void game_render(struct game_state *g)
{
	if (g->screen != g->prev_screen) {
		render_full_clear();
		g->prev_screen = g->screen;
	} else {
		render_clear();
	}

	/* Camera */
	float cam_x, cam_y;
	if (g->screen == SCREEN_PLAY) {
		cam_x = g->ship.pos.x;
		cam_y = g->ship.pos.y;
	} else {
		cam_x = WORLD_W / 2;
		cam_y = WORLD_H / 2;
	}

	/* Stars */
	for (int i = 0; i < NUM_STARS; i++) {
		int sx = to_sx((float)g->stars[i].x, cam_x);
		int sy = to_sy((float)g->stars[i].y, cam_y);
		render_pixel(sx, sy, COLOR_WHITE);
	}

	switch (g->screen) {
	case SCREEN_TITLE:
		render_string_centered(120, "ASTEROIDS", COLOR_WHITE);
		render_string_centered(180, "PRESS FIRE TO START", COLOR_WHITE);
		return;
	case SCREEN_DEAD:
		render_string_centered(100, "GAME OVER", COLOR_RED);
		render_string_centered(150, "SCORE", COLOR_WHITE);
		render_number_centered(170, g->score, COLOR_YELLOW);
		if (g->death_timer <= 0)
			render_string_centered(220, "PRESS FIRE TO RESTART", COLOR_WHITE);
		return;
	case SCREEN_PLAY:
		break;
	}

	/* Asteroids — compute screen pos once, reuse for visibility + render */
	for (int i = 0; i < MAX_ASTEROIDS; i++) {
		struct asteroid *a = &g->asteroids[i];
		if (a->size == 0) continue;
		int sx = to_sx(a->pos.x, cam_x);
		int sy = to_sy(a->pos.y, cam_y);
		if (on_screen(sx, sy, a->radius + 5)) {
			render_asteroid_shape(a, sx, sy);
		}
	}

	/* Bullets */
	for (int i = 0; i < MAX_BULLETS; i++) {
		struct bullet *b = &g->bullets[i];
		if (b->life <= 0) continue;
		int sx = to_sx(b->pos.x, cam_x);
		int sy = to_sy(b->pos.y, cam_y);
		if (on_screen(sx, sy, 4)) {
			render_circle_filled(sx, sy, BULLET_RADIUS, COLOR_WHITE);
		}
	}

	/* Particles */
	for (int i = 0; i < MAX_PARTICLES; i++) {
		struct particle *p = &g->particles[i];
		if (p->life <= 0) continue;
		int sx = to_sx(p->pos.x, cam_x);
		int sy = to_sy(p->pos.y, cam_y);
		if (on_screen(sx, sy, 4)) {
			render_pixel(sx, sy, p->color);
			render_pixel(sx + 1, sy, p->color);
			render_pixel(sx, sy + 1, p->color);
			render_pixel(sx + 1, sy + 1, p->color);
		}
	}

	/* Ship */
	render_ship(g, cam_x, cam_y);

	/* HUD - all text inset from edges to avoid bezel clipping */
	int hx = 16;  /* left margin */
	int hy = 24;  /* top margin */

	/* Row 1: score */
	render_string(hx, hy, "SCORE:", COLOR_WHITE);
	render_number(hx + 48, hy, g->score, COLOR_WHITE);

	/* Row 2: wave, asteroids, FPS */
	render_string(hx, hy + 12, "W:", COLOR_WHITE);
	render_number(hx + 16, hy + 12, g->wave, COLOR_WHITE);
	render_string(hx + 48, hy + 12, "A:", COLOR_WHITE);
	render_number(hx + 64, hy + 12, count_active_asteroids(g), COLOR_WHITE);

	uint16_t fps = SHARED_DISP->fps;
	if (fps > 0) {
		render_number(FB_WIDTH - 52, hy + 12, fps, COLOR_WHITE);
		render_string(FB_WIDTH - 28, hy + 12, "FP", COLOR_WHITE);
	}

	/* Row 3: CPU% and DSP% */
	uint16_t cpu_us = SHARED_DISP->cpu_work_us;
	uint16_t flpr_us = SHARED_DISP->flpr_stream_us;
	int cpu_pct = (cpu_us + 165) / 330;
	int flpr_pct = (flpr_us + 165) / 330;

	render_string(hx, hy + 24, "CPU:", COLOR_WHITE);
	render_number(hx + 32, hy + 24, cpu_pct, COLOR_WHITE);
	render_char(hx + 32 + 8 * (cpu_pct >= 100 ? 3 : cpu_pct >= 10 ? 2 : 1), hy + 24, '%', COLOR_WHITE);

	render_string(hx + 96, hy + 24, "DSP:", COLOR_WHITE);
	render_number(hx + 128, hy + 24, flpr_pct, COLOR_WHITE);
	render_char(hx + 128 + 8 * (flpr_pct >= 100 ? 3 : flpr_pct >= 10 ? 2 : 1), hy + 24, '%', COLOR_WHITE);

	/* Mode label */
	const char *mode_name;
	switch (SHARED_DISP->render_mode) {
	case RMODE_TILES:  mode_name = "R:TILE"; break;
	case RMODE_PIXELS: mode_name = "R:PIX";  break;
	default:           mode_name = "R:ROW";  break;
	}
	render_string(FB_WIDTH - 60, hy + 24, mode_name, COLOR_WHITE);
}
