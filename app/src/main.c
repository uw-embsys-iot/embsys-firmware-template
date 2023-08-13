#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* IOTEMBSYS: Add required iheadersmport shell and/or others */
//#include <zephyr/shell/shell.h>

/* IOTEMBSYS: Add required headers for settings */
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>

/* IOTEMBSYS: Add required headers for protobufs */
#include <pb_encode.h>
#include <pb_decode.h>
#include "api/api.pb.h"

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
#define MAX_RECV_BUF_LEN 1024
static uint8_t recv_buf_[MAX_RECV_BUF_LEN];

static void change_blink_interval(uint32_t new_interval_ms) {
	blink_interval_ = new_interval_ms;
}

/* IOTEMBSYS: Define a default settings val and configuration access */
#define DEFAULT_BOOT_COUNT_VALUE 0
static uint8_t boot_count = DEFAULT_BOOT_COUNT_VALUE;

static int foo_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(name, "boot_count", &next) && !next) {
        if (len != sizeof(boot_count)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &boot_count, sizeof(boot_count));
        if (rc >= 0) {
            /* key-value pair was properly read.
             * rc contains value length.
             */
            return 0;
        }
        /* read-out error */
        return rc;
    }

    return -ENOENT;
}

static int foo_settings_export(int (*storage_func)(const char *name,
                                                   const void *value,
                                                   size_t val_len))
{
    return storage_func("provisioning/boot_count", &boot_count, sizeof(boot_count));
}

struct settings_handler my_conf = {
    .name = "provisioning",
    .h_set = foo_settings_set,
    .h_export = foo_settings_export
};

/* IOTEMBSYS: Add protobuf encoding and decoding. */
static bool encode_status_update_request(uint8_t *buffer, size_t buffer_size, size_t *message_length)
{
	bool status;

	/* Allocate space on the stack to store the message data.
	 *
	 * Nanopb generates simple struct definitions for all the messages.
	 * - check out the contents of api.pb.h!
	 * It is a good idea to always initialize your structures
	 * so that you do not have garbage data from RAM in there.
	 */
	StatusUpdateRequest message = StatusUpdateRequest_init_zero;

	/* Create a stream that will write to our buffer. */
	pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);

	/* Fill in the reboot count */
	message.boot_count = boot_count;

	/* Now we are ready to encode the message! */
	status = pb_encode(&stream, StatusUpdateRequest_fields, &message);
	*message_length = stream.bytes_written;

	if (!status) {
		printk("Encoding failed: %s\n", PB_GET_ERROR(&stream));
	}

	return status;
}

static bool decode_status_update_response(uint8_t *buffer, size_t message_length)
{
	bool status = false;
	if (message_length == 0) {
		LOG_WRN("Message length is 0");
		return status;
	}

	/* Allocate space for the decoded message. */
	StatusUpdateResponse message = StatusUpdateResponse_init_zero;

	/* Create a stream that reads from the buffer. */
	pb_istream_t stream = pb_istream_from_buffer(buffer, message_length);

	/* Now we are ready to decode the message. */
	status = pb_decode(&stream, StatusUpdateResponse_fields, &message);

	/* Check for errors... */
	if (status) {
		/* Print the data contained in the message. */
		printk("Response message: %s\n", message.message);
	} else {
		printk("Decoding failed: %s\n", PB_GET_ERROR(&stream));
	}

	return status;
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
		
		// This assumes the response fits in a single buffer.
		recv_buf_[rsp->data_len] = '\0';
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %s", rsp->http_status);
}

int http_payload_cb(int sock, struct http_request *req, void *user_data) {
	const char *content[] = {
		"foobar",
		"chunked",
		"last"
	};
	char tmp[64];
	int i, pos = 0;

	for (i = 0; i < ARRAY_SIZE(content); i++) {
		pos += snprintk(tmp + pos, sizeof(tmp) - pos,
				"%x\r\n%s\r\n",
				(unsigned int)strlen(content[i]),
				content[i]);
	}

	pos += snprintk(tmp + pos, sizeof(tmp) - pos, "0\r\n\r\n");

	(void)send(sock, tmp, pos, 0);

	return pos;
}

/* IOTEMBSYS: Create a HTTP request and response with protobuf. */
int http_proto_payload_cb(int sock, struct http_request *req, void *user_data) {
	static uint8_t buffer[StatusUpdateRequest_size];
	size_t message_length;

	/* Encode our message */
	if (!encode_status_update_request(buffer, sizeof(buffer), &message_length)) {
		LOG_ERR("Encoding request failed");
		return 0;
	} else {
		LOG_INF("Sending proto to server. Length: %d", (int)message_length);
	}

	(void)send(sock, buffer, message_length, 0);

	return (int)message_length;
}

static void http_proto_response_cb(struct http_response *rsp,
			enum http_final_call final_data,
			void *user_data)
{
	if (final_data == HTTP_DATA_MORE) {
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);

		// Decode the protobuf response.
		decode_status_update_response(rsp->body_frag_start, rsp->body_frag_len);
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %s", rsp->http_status);
}

#define USE_EC2_SERVER 1

// WARNING: These IPs are not static! Use a DNS lookup tool
// to get the latest IP.
#define TCPBIN_IP "45.79.112.203"
#define HTTPBIN_IP "54.204.94.184"
#define EC2_IP "54.158.227.114"
#define TCP_PORT 4242
#define IS_POST_REQ 1
#define USE_PROTO 1

#if !USE_EC2_SERVER
	static const char kEchoServerIP[] = HTTPBIN_IP;
	#define HTTP_PORT 80
	#define HOST "httpbin.org"
#else
	static const char kEchoServerIP[] = EC2_IP;
	#define HTTP_PORT 8080
	#define HOST EC2_IP ":8080"
#endif

/* IOTEMBSYS: Implement the HTTP client functionality */
void http_client_thread(void* p1, void* p2, void* p3) {
	int sock;
	struct sockaddr_in addr4;
	const int32_t timeout = 5 * MSEC_PER_SEC;

	k_event_init(&unblock_sender_);

	while (true) {
		uint32_t  events;

		events = k_event_wait(&unblock_sender_, 0xFFF, true, K_FOREVER);
		if (events == 0) {
			printk("This should not be happening!");
			continue;
		}

		if (connect_socket(AF_INET, kEchoServerIP, HTTP_PORT,  &sock, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
			LOG_ERR("Connect failed");
			continue;
		}

		struct http_request req;

		memset(&req, 0, sizeof(req));
		memset(recv_buf_, 0, sizeof(recv_buf_));

#if !IS_POST_REQ
		req.method = HTTP_GET;
		req.url = "/get";
#else
		req.method = HTTP_POST;
#if !USE_PROTO
		req.url = "/post";
		req.payload_cb = http_payload_cb;
		// This must match the payload-generating function!
		req.payload_len = 37;
#else
		req.url = "/status_update";
		req.payload_cb = http_proto_payload_cb;

		// When set to 0, this does chunked encoding
		req.payload_len = 2;
#endif
		
#endif // IS_POST_REQ
		req.host = HOST;
		req.protocol = "HTTP/1.1";
#if !USE_PROTO
		req.response = http_response_cb;
#else
		req.response = http_proto_response_cb;
#endif
		req.recv_buf = recv_buf_;
		req.recv_buf_len = sizeof(recv_buf_);

		// This request is synchronous and blocks the thread.
		LOG_INF("Sending HTTP request");
		int ret = http_client_req(sock, &req, timeout, "IPv4 GET");
		if (ret > 0) {
			LOG_INF("HTTP request sent %d bytes", ret);
		} else {
			LOG_ERR("HTTP request failed: %d", ret);
		}

		// 
		//LOG_INF("Closing the socket");
		close(sock);

		//LOG_INF("HTTP response: %s", recv_buf_);
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

	// Erase a flash area if previously written to.
	// const struct flash_area *my_area;
	// int err = flash_area_open(FLASH_AREA_ID(storage), &my_area);

	// if (err != 0) {
	// 	printk("Flash area open failed");
	// } else {
	// 	err = flash_area_erase(my_area, 0, FLASH_AREA_SIZE(storage));
	// }

	/* IOTEMBSYS: Initialize settings subsystem. */
	settings_subsys_init();
    settings_register(&my_conf);
    settings_load();

	/* IOTEMBSYS: Increment boot count. */
	boot_count++;
    settings_save_one("provisioning/boot_count", &boot_count, sizeof(boot_count));

    printk("boot_count: %d\n", boot_count);

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

	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		/* IOTEMBSYS: Print GPIO state to console. */
		if (ret < 0) {
			return;
		}
		k_msleep(blink_interval_);
	}
}

