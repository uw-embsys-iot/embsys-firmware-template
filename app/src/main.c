#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* IOTEMBSYS2: Add required headers for shell and/or others */

#include <stdlib.h>
#include <stdio.h>
#include "app_version.h"

/* 1000 msec = 1 sec */
#define DEFAULT_SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/* IOTEMBSYS2: Add joystick key declarations/references from the device tree. */

/*
 * A build error on this line means your board is unsupported.
 * See the blinky sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* The amount of time between GPIO blinking. */
/* IOTEMBSYS2: This will change based on button presses and shell commands. */
static uint32_t blink_interval_ = DEFAULT_SLEEP_TIME_MS;

/* IOTEMBSYS2: Add joystick press handler. Metaphorical bonus points for debouncing. 
 * An example function signature is provided for you below.
 */

/*
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins) {
	
}
*/

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

	/* IOTEMBSYS2: Configure joystick GPIOs. Each gpio must be configured as GPIO_INPUT and have a callback handler. */

	LOG_INF("Running blinky");
	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		
		/* IOTEMBSYS2: Print the LED GPIO state to console. */
		
		if (ret < 0) {
			return;
		}
		k_msleep(blink_interval_);
	}
}

/* IOTEMBSYS2: Add shell commands and handler. */
