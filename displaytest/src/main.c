/*
 * Asteroids - nRF54LM20-DK
 *
 * Main game loop on CPU (ARM Cortex-M33).
 * Interrupt-driven buttons, dirty-row framebuffer streaming via FLPR.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "shared_display.h"
#include "game.h"
#include "render.h"

/* ------------------------------------------------------------------ */
/*  Interrupt-driven buttons                                          */
/* ------------------------------------------------------------------ */

static const struct gpio_dt_spec btn_specs[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios),
};

static struct gpio_callback btn_cb_data[4];
static volatile uint8_t button_state;

static void btn_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	uint8_t state = 0;
	if (gpio_pin_get_dt(&btn_specs[0])) state |= BTN_THRUST;
	if (gpio_pin_get_dt(&btn_specs[1])) state |= BTN_LEFT;
	if (gpio_pin_get_dt(&btn_specs[2])) state |= BTN_RIGHT;
	if (gpio_pin_get_dt(&btn_specs[3])) state |= BTN_SHOOT;
	button_state = state;
}

static int init_buttons(void)
{
	for (int i = 0; i < ARRAY_SIZE(btn_specs); i++) {
		if (!gpio_is_ready_dt(&btn_specs[i])) {
			printk("ERROR: Button %d not ready\n", i);
			return -1;
		}
		int ret = gpio_pin_configure_dt(&btn_specs[i], GPIO_INPUT);
		if (ret < 0) return ret;

		ret = gpio_pin_interrupt_configure_dt(&btn_specs[i],
						      GPIO_INT_EDGE_BOTH);
		if (ret < 0) {
			printk("WARN: Button %d interrupt failed (%d), using polled\n", i, ret);
			continue;
		}

		gpio_init_callback(&btn_cb_data[i], btn_isr,
				   BIT(btn_specs[i].pin));
		gpio_add_callback(btn_specs[i].port, &btn_cb_data[i]);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Display sync                                                      */
/* ------------------------------------------------------------------ */

static void wait_display_ready(void)
{
	printk("Waiting for FLPR...\n");
	while (SHARED_DISP->status == DISP_STATUS_INIT) {
		k_msleep(10);
	}
	printk("Display ready\n");
}

static void send_frame(void)
{
	/* Commit dirty bitmap to shared memory */
	render_commit_dirty();

	/* Memory barrier: ensure all framebuffer + dirty bitmap writes
	 * are committed before the FLPR sees FRAME_READY */
	__DSB();

	/* Signal FLPR */
	SHARED_DISP->status = DISP_STATUS_FRAME_READY;

	/* Tight spin — CPU has nothing else to do until FLPR finishes */
	while (SHARED_DISP->status != DISP_STATUS_READY) { }
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

static struct game_state game;

int main(void)
{
	if (init_buttons() < 0) {
		return -1;
	}

	wait_display_ready();
	render_init(SHARED_DISP->framebuffer);
	render_set_mode(RMODE_PIXELS);  /* Change to RMODE_ROWS, RMODE_TILES or RMODE_PIXELS to test */
	game_init(&game);

	while (1) {
		uint32_t frame_start = k_uptime_get_32();

		uint8_t btns = button_state;
		game_update(&game, btns);
		game_render(&game);

		/* Measure CPU work time (game logic + rendering) */
		uint32_t work_ms = k_uptime_get_32() - frame_start;
		SHARED_DISP->cpu_work_us = (uint16_t)(work_ms * 1000);

		send_frame();

		uint32_t elapsed = k_uptime_get_32() - frame_start;
		if (elapsed < FRAME_TIME_MS) {
			k_msleep(FRAME_TIME_MS - elapsed);
		}
	}

	return 0;
}
