#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

/* IOTEMBSYS5: Add the HTTP client include(s). */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#include <stdlib.h>
#include <stdio.h>
#include "app_version.h"

// Helper for converting macros into strings
#define str(s) #s
#define xstr(s) str(s)

/* 1000 msec = 1 sec */
#define DEFAULT_SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

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

/* IOTEMBSYS5: This has been implemented for you. Some of the actions will be unused until future assignments. */
// Synchronization to unblock the sender task
static struct k_event unblock_sender_;
typedef enum {
	BUTTON_ACTION_NONE = 0,
	BUTTON_ACTION_GENERIC_HTTP,
	BUTTON_ACTION_OTA_DOWNLOAD,
	BUTTON_ACTION_PROTO_REQ,
	BUTTON_ACTION_GET_OTA_PATH,
} button_action_e;

struct k_fifo socket_queue_;

static void change_blink_interval(uint32_t new_interval_ms) {
	blink_interval_ = new_interval_ms;
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins) {
	// Sophomoric "debouncing" implementation
	printk("Button %d pressed at %" PRIu32 "\n", pins, k_cycle_get_32());
	k_msleep(100);

	uint32_t interval_ms = 0;
	if (pins == BIT(sw0.pin)) {
		interval_ms = 100;
	} else if (pins == BIT(sw1.pin)) {
		// Down
		interval_ms = 200;
		k_event_set(&unblock_sender_, (1 << BUTTON_ACTION_OTA_DOWNLOAD));
	} else if (pins == BIT(sw2.pin)) {
		// Right
		interval_ms = 500;
		k_event_set(&unblock_sender_, (1 << BUTTON_ACTION_GENERIC_HTTP));
	} else if (pins == BIT(sw3.pin)) {
		// Up
		interval_ms = 1000;
		k_event_set(&unblock_sender_, (1 << BUTTON_ACTION_PROTO_REQ));
	} else if (pins == BIT(sw4.pin)) {
		// Left
		k_event_set(&unblock_sender_, (1 << BUTTON_ACTION_GET_OTA_PATH));
		interval_ms = 2000;
	} else {
		printk("Unrecognized pin");
	}

	if (interval_ms != 0) {
		printk("Setting interval to %d", interval_ms);
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

//
// Networking/sockets helpers
//

/* IOTEMBSYS5: This helper function has been added for you. */
static void dump_addrinfo(const struct addrinfo *ai) {
	printf("addrinfo @%p: ai_family=%d, ai_socktype=%d, ai_protocol=%d, "
	       "sa_family=%d, sin_port=%x\n",
	       ai, ai->ai_family, ai->ai_socktype, ai->ai_protocol,
	       ai->ai_addr->sa_family,
	       ((struct sockaddr_in *)ai->ai_addr)->sin_port);
}

/* IOTEMBSYS5: This helper function has been added for you. */
static int get_addr_if_needed(struct addrinfo **ai, const char* host, const char* port) {
	if (*ai != NULL) {
		// We already have the address.
		return 0;
	}
	struct addrinfo hints;
	int st;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	st = getaddrinfo(host, port, &hints, ai);
	LOG_INF("getaddrinfo status: %d\n", st);
	if (st == 0) {
		dump_addrinfo(*ai);
	}
	return st;
}

//
// Generic HTTP Request Section
//

// WARNING: These IPs are not static! Use a DNS lookup tool
// to get the latest IP.
#define TCPBIN_IP "45.79.112.203"
#define TCP_PORT 4242
#define IS_POST_REQ 1
#define USE_PROTO 1

/* IOTEMBSYS5: The host and port for httpbin requests */
#define HTTPBIN_PORT 80
#define HTTPBIN_HOST "httpbin.org"
static struct addrinfo* httpbin_addr_;

/* IOTEMBSYS5: Create a HTTP response handler/callback for GET requests. */

/* IOTEMBSYS5: Create a HTTP response handler/callback for POST requests. */

/* IOTEMBSYS5: Implement the HTTP client functionality */
static void generic_http_request(void) {
	/* IOTEMBSYS5: Get the IP address using our get_addr_if_needed helper, or getaddrinfo directly. */

	/* IOTEMBSYS5: Create a socket and connect to it */

	/* IOTEMBSYS5: Declare and fill out a request struct */

	/* IOTEMBSYS5: Send the request */

	/* IOTEMBSYS5: Close the socket */
}

/* IOTEMBSYS5: This thread has been added for you. */
// This thread is responsible for making all HTTP requests in the app.
// This enforces simplicity, and prevents requests from stepping on one another.
void http_client_thread(void* p1, void* p2, void* p3) {
	k_event_init(&unblock_sender_);

	while (true) {
		uint32_t  events;

		LOG_INF("Waiting for button");
		events = k_event_wait(&unblock_sender_, 0xFFF, true, K_FOREVER);
		if (events == 0) {
			printk("This should not be happening!");
			continue;
		}

		// Multiple button events are possible, so handle all without exclusion.
		if (events & (1 << BUTTON_ACTION_GENERIC_HTTP)) {
			/* IOTEMBSYS5: The HTTP request functionality should go into this function, triggered by the right button. */
			generic_http_request();
		}
		if (events & (1 << BUTTON_ACTION_OTA_DOWNLOAD)) {
			LOG_INF("OTA not implemented");
		}
		if (events & (1 << BUTTON_ACTION_PROTO_REQ)) {
			LOG_INF("Server requests not implemented");
		}
		if (events & (1 << BUTTON_ACTION_GET_OTA_PATH)) {
			LOG_INF("OTA not implemented");
		}
	}
}

K_THREAD_DEFINE(http_client_tid, 4000 /*stack size*/,
                http_client_thread, NULL, NULL, NULL,
                5 /*priority*/, 0, 0);

void main(void)
{
	int ret;
	const struct device *modem;

	if (!gpio_is_ready_dt(&led)) {
		return;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}

	init_joystick_gpio(&sw0, &button_cb_data_0);
	init_joystick_gpio(&sw1, &button_cb_data_1);
	init_joystick_gpio(&sw2, &button_cb_data_2);
	init_joystick_gpio(&sw3, &button_cb_data_3);
	init_joystick_gpio(&sw4, &button_cb_data_4);

	modem = DEVICE_DT_GET(DT_NODELABEL(quectel_bg96));
	if (!device_is_ready(modem)) {
		LOG_ERR("Modem is not ready");
		return;
	}

	LOG_INF("Running blinky");
	while (1) {
		ret = gpio_pin_toggle_dt(&led);

		if (ret < 0) {
			return;
		}
		k_msleep(blink_interval_);
	}
}

