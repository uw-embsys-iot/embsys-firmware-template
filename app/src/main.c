#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* IOTEMBSYS: Add required iheadersmport shell and/or others */
//#include <zephyr/shell/shell.h>

#include <stdlib.h>
#include <stdio.h>
#include "app_version.h"

/* 1000 msec = 1 sec */
#define DEFAULT_SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/* IOTEMBSYS: Add joystick key declarations. */
#define SW0_NODE	DT_ALIAS(sw0)
#define SW1_NODE	DT_ALIAS(sw1)
#define SW2_NODE	DT_ALIAS(sw2)
#define SW3_NODE	DT_ALIAS(sw3)
#define SW4_NODE	DT_ALIAS(sw4)
static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static const struct gpio_dt_spec sw1 = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios,
							      {0});
static const struct gpio_dt_spec sw2 = GPIO_DT_SPEC_GET_OR(SW2_NODE, gpios,
							      {0});
static const struct gpio_dt_spec sw3 = GPIO_DT_SPEC_GET_OR(SW3_NODE, gpios,
							      {0});								  								  								  
static const struct gpio_dt_spec sw4 = GPIO_DT_SPEC_GET_OR(SW4_NODE, gpios,
							      {0});								  
static struct gpio_callback button_cb_data_0;
static struct gpio_callback button_cb_data_1;
static struct gpio_callback button_cb_data_2;
static struct gpio_callback button_cb_data_3;
static struct gpio_callback button_cb_data_4;

/*
 * A build error on this line means your board is unsupported.
 * See the blinky sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* The amount of time between GPIO blinking. */
static uint32_t blink_interval_ = DEFAULT_SLEEP_TIME_MS;

static void change_blink_interval(uint32_t new_interval_ms) {
	blink_interval_ = new_interval_ms;
}

/* IOTEMBSYS: Add joystick press handler. Metaphorical bonus points for debouncing. */
static bool req_in_progress_ = false;
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins) {
	if (req_in_progress_) {
		return;
	}
	req_in_progress_ = true;

	// Sophomoric "debouncing" implementation
	printk("Button %d pressed at %" PRIu32 "\n", pins, k_cycle_get_32());
	k_msleep(100);

	uint32_t interval_ms = 0;
	if (pins == BIT(sw0.pin)) {
		interval_ms = 100;
	} else if (pins == BIT(sw1.pin)) {
		// Down
		interval_ms = 200;
	} else if (pins == BIT(sw2.pin)) {
		// Right
		interval_ms = 500;
	} else if (pins == BIT(sw3.pin)) {
		// Up
		interval_ms = 1000;
	} else if (pins == BIT(sw4.pin)) {
		// Left
		interval_ms = 2000;
	} else {
		printk("Unrecognized pin");
	}

	if (interval_ms != 0) {
		printk("Setting interval to %d\n", interval_ms);
		change_blink_interval(interval_ms);
	}
}

static int init_joystick_gpio(const struct gpio_dt_spec* button, struct gpio_callback* data) {
	int ret = -1;

	if (!gpio_is_ready_dt(button)) {
		printk("Error: button device %s is not ready\n",
		       button->port->name);
		return ret;
	}

	ret = gpio_pin_configure_dt(button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button->port->name, button->pin);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button->port->name, button->pin);
		return ret;
	}

	gpio_init_callback(data, button_pressed, BIT(button->pin));
	gpio_add_callback(button->port, data);
	return ret;
}

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

	/* IOTEMBSYS: Configure joystick GPIOs. */
	init_joystick_gpio(&sw0, &button_cb_data_0);
	init_joystick_gpio(&sw1, &button_cb_data_1);
	init_joystick_gpio(&sw2, &button_cb_data_2);
	init_joystick_gpio(&sw3, &button_cb_data_3);
	init_joystick_gpio(&sw4, &button_cb_data_4);

	/* 
	 * IOTEMBSYS3: Get the handle to the modem from the devicetree and check if it's ready.
	 * Hint: try DT_NODELABEL and DEVICE_DT_GET
	 */

	LOG_INF("Running blinky");
	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		/* IOTEMBSYS: Print GPIO state to console. */
		if (ret < 0) {
			return;
		}
		k_msleep(blink_interval_);
	}
}

