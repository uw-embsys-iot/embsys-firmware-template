#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#include <zephyr/shell/shell.h>

#include <stdlib.h>
#include <stdio.h>
#include "app_version.h"

/* 1000 msec = 1 sec */
#define DEFAULT_SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the blinky sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* The amount of time between GPIO blinking. */
static uint32_t blink_interval_ = DEFAULT_SLEEP_TIME_MS;

/* IOTEMBSYS1: This is a simple "blinky" app that we will be developing on top of. */
void main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		return;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}

	/* IOTEMBSYS1: printk is just like printf. */	
	printk("Running blinky\n");
	while (1) {
		ret = gpio_pin_toggle_dt(&led);

		/* IOTEMBSYS1: LOG_X are like printf, except they include additional details in logs. */
		LOG_INF("LED state: %d", gpio_pin_get_dt(&led));
		if (ret < 0) {
			return;
		}
		k_msleep(blink_interval_);
	}
}
