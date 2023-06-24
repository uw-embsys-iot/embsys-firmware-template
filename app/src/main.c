#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* IOTEMBSYS: Add required import shell and/or others */
//#include <zephyr/shell/shell.h>

#include <stdlib.h>

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

/* IOTEMBSYS: Add synchronization to unblock the sender task */
static struct k_event unblock_sender_;

/* IOTEMBSYS: Add synchronization to pass the socket to the receiver task */
struct k_fifo socket_queue_;

/* IOTEMBSYS: Create a buffer for receiving HTTP responses */
#define MAX_RECV_BUF_LEN 512
static uint8_t recv_buf_[MAX_RECV_BUF_LEN];

static void change_blink_interval(uint32_t new_interval_ms) {
	blink_interval_ = new_interval_ms;
}

/* IOTEMBSYS: Add joystick press handler. Metaphorical bonus points for debouncing. */
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins) {
	printk("Button %d pressed at %" PRIu32 "\n", pins, k_cycle_get_32());
	k_event_set(&unblock_sender_, 0x001);

	uint32_t interval_ms = 0;
	if (pins == BIT(sw0.pin)) {
		interval_ms = 100;
	} else if (pins == BIT(sw1.pin)) {
		interval_ms = 200;
	} else if (pins == BIT(sw2.pin)) {
		interval_ms = 500;
	} else if (pins == BIT(sw3.pin)) {
		interval_ms = 1000;
	} else if (pins == BIT(sw4.pin)) {
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

struct socket_queue_item {
	void* fifo_reserved;
	int sock;
};

/* IOTEMBSYS: Create a HTTP response handler/callback. */
static void http_response_cb(struct http_response *rsp,
			enum http_final_call final_data,
			void *user_data)
{
	if (final_data == HTTP_DATA_MORE) {
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %s", rsp->http_status);
}

#define USE_EC2_ECHO_SERVER 0

#define TCPBIN_IP "45.79.112.203"
#define HTTPBIN_IP "100.26.90.23"
#define TCP_PORT 4242
#define HTTP_PORT 80

#if !USE_EC2_ECHO_SERVER
	static const char kEchoServerIP[] = HTTPBIN_IP;
#else
	static const char kEchoServerIP[] = "44.203.155.243";
#endif

/* IOTEMBSYS: Create the TCP sender thread */
void tcp_sender_thread(void* p1, void* p2, void* p3) {
	int sock;
	struct sockaddr_in addr4;
	struct socket_queue_item socket_item;

	k_event_init(&unblock_sender_);

	while (true) {
		uint32_t  events;

		events = k_event_wait(&unblock_sender_, 0xFFF, true, K_FOREVER);
		if (events == 0) {
			printk("This should not be happening!");
			continue;
		}
		printk("Sending data");

		if (connect_socket(AF_INET, kEchoServerIP, HTTP_PORT,  &sock, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
			LOG_ERR("Connect failed");
			continue;
		}
		
		// TODO: shouldn't need a delay here
		k_msleep(1000);

		// Relinquish ownership of this socket by passing it to the receiver task
		socket_item.sock = sock;
		k_fifo_put(&socket_queue_, &socket_item);
	}
}
K_THREAD_DEFINE(tcp_sender_tid, 1000 /*stack size*/,
                tcp_sender_thread, NULL, NULL, NULL,
                5 /*priority*/, 0, 0);

/* IOTEMBSYS: Create the TCP receiver thread */
void tcp_receiver_thread(void* p1, void* p2, void* p3) {
	struct socket_queue_item* socket_item;
	const int32_t timeout = 5 * MSEC_PER_SEC;

	k_fifo_init(&socket_queue_);

	while (true) {
		socket_item = k_fifo_get(&socket_queue_, K_FOREVER);

		struct http_request req;

		memset(&req, 0, sizeof(req));
		memset(recv_buf_, 0, sizeof(recv_buf_));

		req.method = HTTP_GET;
		req.url = "/get";
		req.host = "httpbin.org";
		req.protocol = "HTTP/1.1";
		req.response = http_response_cb;
		req.recv_buf = recv_buf_;
		req.recv_buf_len = sizeof(recv_buf_);

		// This request is synchronous and blocks the thread.
		printk("Sending HTTP request");
		int ret = http_client_req(socket_item->sock, &req, timeout, "IPv4 GET");
		if (ret > 0) {
			printk("HTTP request sent %d bytes", ret);
		} else {
			printk("HTTP request failed: %d", ret);
		}

		printk("Closing socket");
		close(socket_item->sock);

		printk("HTTP response: %s", recv_buf_);
	}
}

K_THREAD_DEFINE(tcp_receiver_tid, 4000 /*stack size*/,
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
		LOG_ERR("Modem not ready");
		return;
	}

	// struct pollfd pfds[1];

	while (1) {
	// 	if (send_data_) {
	// 		if (connect_socket(AF_INET, "45.79.112.203", 4242,  &sock, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
	// 			LOG_ERR("Connect failed");
	// 			continue;
	// 		}
	// 		(void)send(sock, kData, sizeof(kData), 0);

	// 		pfds[0].fd = 0; // Standard input
	// 		pfds[0].events = POLLIN; // Tell me when ready to read

	// 		int num_events = poll(pfds, 1, 2500); // 2.5 second timeout

	// 		if (num_events == 0) {
	// 			printk("No data received");
	// 		} else {
	// 			if (recv(sock, recv_buf_, sizeof(recv_buf_) - 1, 0) > 0) {
	// 				printk("Received: %s", recv_buf_);
	// 				memset(recv_buf_, 0, sizeof(recv_buf_));
	// 			}
	// 		}

	// 		close(sock);
	// 	}
		ret = gpio_pin_toggle_dt(&led);
		/* IOTEMBSYS: Print GPIO state to console. */
		if (ret < 0) {
			return;
		}
		k_msleep(blink_interval_);
	}
}

// /* IOTEMBSYS: Add shell commands and handler. */
// static int cmd_demo_blink(const struct shell *shell, size_t argc, char **argv)
// {
// 	uint32_t interval_ms = atoi(argv[1]);
// 	shell_print(shell, "Setting interval to: %d", interval_ms);

// 	change_blink_interval(interval_ms);
// 	return 0;
// }

// SHELL_STATIC_SUBCMD_SET_CREATE(sub_app,
// 	SHELL_CMD(blink, NULL, "Change blink interval", cmd_demo_blink),
// );
// SHELL_CMD_REGISTER(app, &sub_app, "Application commands", NULL);
