#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* IOTEMBSYS: Add required iheadersmport shell and/or others */
//#include <zephyr/shell/shell.h>

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

/* IOTEMBSYS4: Add synchronization to unblock the sender task */
static struct k_event unblock_sender_;

/* IOTEMBSYS4: Add synchronization to pass the socket to the receiver task */
struct k_fifo socket_queue_;

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

	/* IOTEMBSYS4: Send data over TCP every time a joystick button is pressed. */
	k_event_set(&unblock_sender_, 0x001);

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

/* IOTEMBSYS4: Configure the appropriate IP address and port */
// WARNING: This IP might not be static! Use a DNS lookup tool
// to get the latest IP.
#define TCPBIN_IP "45.79.112.203"
#define TCPBIN_PORT 4242

static int setup_socket(sa_family_t family, const char *server, int port,
			int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	const char *family_str = family == AF_INET ? "IPv4" : "IPv6";
	int ret = 0;

	memset(addr, 0, addr_len);

	net_sin(addr)->sin_family = AF_INET;
	net_sin(addr)->sin_port = htons(port);
	inet_pton(family, server, &net_sin(addr)->sin_addr);

	*sock = socket(family, SOCK_STREAM, IPPROTO_TCP);

	if (*sock < 0) {
		LOG_ERR("Failed to create %s HTTP socket (%d)", family_str,
			-errno);
	}

	return ret;
}

static int connect_socket(sa_family_t family, const char *server, int port,
			  int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret;

	ret = setup_socket(family, server, port, sock, addr, addr_len);
	if (ret < 0 || *sock < 0) {
		return -1;
	}

	ret = connect(*sock, addr, addr_len);
	if (ret < 0) {
		LOG_ERR("Cannot connect to %s remote (%d)",
			family == AF_INET ? "IPv4" : "IPv6",
			-errno);
		ret = -errno;
	}

	return ret;
}

static bool await_data(int sock, int32_t timeout) {
	int ret;
	struct zsock_pollfd fds[1];
	int nfds = 1;

	fds[0].fd = sock;
	fds[0].events = ZSOCK_POLLIN;

	ret = zsock_poll(fds, nfds, timeout);
	if (ret == 0) {
		LOG_DBG("Timeout");
		return false;
	} else if (ret < 0) {
		return false;
	}
	if (fds[0].revents & (ZSOCK_POLLERR | ZSOCK_POLLNVAL)) {
		return false;
	} else if (fds[0].revents & ZSOCK_POLLHUP) {
		/* Connection closed */
		LOG_DBG("Connection closed");
		return false;
	} else if (fds[0].revents & ZSOCK_POLLIN) {
		return true;
	}

	return false;
}

struct socket_queue_item {
	void* fifo_reserved;
	int sock;
};

#define USE_EC2_ECHO_SERVER 0

/* IOTEMBSYS4: Create the TCP sender thread */
void tcp_sender_thread(void* p1, void* p2, void* p3) {
	int sock;
	struct sockaddr_in addr4;
#if !USE_EC2_ECHO_SERVER
	static const char kEchoServerIP[] = TCPBIN_IP;
	static const int kEchoServerPort = TCPBIN_PORT;
#else
	// The IP of your EC2 instance
	static const char kEchoServerIP[] = "44.203.155.243";
	static const int kEchoServerPort = 4242;
#endif
	static const char kData[] = "hello!\r\n";
	struct socket_queue_item socket_item;

	k_event_init(&unblock_sender_);

	while (true) {
		uint32_t  events;

		printk("Sender waiting for button press\n");
		events = k_event_wait(&unblock_sender_, 0xFFF, true, K_FOREVER);
		if (events == 0) {
			printk("This should not be happening!");
			continue;
		}
		printk("Connecting\n");

		if (connect_socket(AF_INET, kEchoServerIP, kEchoServerPort,  &sock, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
			LOG_ERR("Connect failed");
			continue;
		}
		printk("Connected\n");

		(void)send(sock, kData, sizeof(kData), 0);
		printk("Data sent\n");

		// Relinquish ownership of this socket by passing it to the receiver task
		socket_item.sock = sock;
		k_fifo_put(&socket_queue_, &socket_item);
		printk("Socket passed to receiver\n");
	}
}
K_THREAD_DEFINE(tcp_sender_tid, 1000 /*stack size*/,
                tcp_sender_thread, NULL, NULL, NULL,
                5 /*priority*/, 0, 0);

/* IOTEMBSYS4: Create the TCP receiver thread */
void tcp_receiver_thread(void* p1, void* p2, void* p3) {
	struct socket_queue_item* socket_item;
	static char recv_buf_[100];

	k_fifo_init(&socket_queue_);

	while (true) {
		printk("Receiver waiting for socket\n");
		socket_item = k_fifo_get(&socket_queue_, K_FOREVER);
		if (socket_item == 0) {
			printk("This should not be happening!");
			continue;
		}
		printk("Receiving data...");
		bool data_exists = await_data(socket_item->sock, 5000);
		if (!data_exists) {
			goto close;
		}
		if (recv(socket_item->sock, recv_buf_, sizeof(recv_buf_) - 1, 0) == 0) {
			printk("No data was received");
		} else {
			printk("Received: %s\n", recv_buf_);
		}
close:
		printk("Closing socket\n");
		close(socket_item->sock);
		printk("Socket closed\n");
		req_in_progress_ = false;
	}
}

K_THREAD_DEFINE(tcp_receiver_tid, 1000 /*stack size*/,
                tcp_receiver_thread, NULL, NULL, NULL,
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

	/* IOTEMBSYS: Configure joystick GPIOs. */
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
		/* IOTEMBSYS: Print GPIO state to console. */
		if (ret < 0) {
			return;
		}
		k_msleep(blink_interval_);
	}
}

