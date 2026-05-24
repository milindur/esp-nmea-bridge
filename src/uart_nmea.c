#include "uart_nmea.h"

#include "nmea_bridge.h"
#include "status_led.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_nmea, LOG_LEVEL_INF);

#define NMEA_UART_NODE DT_ALIAS(serial1)
#define NMEA_UART_NAME "serial1"

#if !DT_NODE_HAS_STATUS(NMEA_UART_NODE, okay)
#error "serial1 devicetree alias must point to an enabled UART"
#endif

static const struct device *const nmea_uart = DEVICE_DT_GET(NMEA_UART_NODE);
static struct uart_nmea_stats uart_stats;
static bool started;

static void uart_rx_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	uint8_t line[CONFIG_ESP_SERIAL_BRIDGE_NMEA_FRAME_MAX_LEN];
	size_t line_len = 0;
	bool dropping_overlong = false;

	for (;;) {
		unsigned char ch;
		bool got = false;

		while (uart_poll_in(nmea_uart, &ch) == 0) {
			got = true;
			uart_stats.bytes_rx++;

			if (dropping_overlong) {
				if (ch == '\n') {
					dropping_overlong = false;
					line_len = 0;
				}
				continue;
			}

			if (line_len < sizeof(line)) {
				line[line_len++] = ch;
			} else {
				uart_stats.overlong_lines++;
				dropping_overlong = true;
				line_len = 0;
				continue;
			}

			if (ch == '\n') {
				nmea_bridge_publish(line, line_len);
				status_led_nmea_frame_received();
				uart_stats.lines_rx++;
				line_len = 0;
			}
		}

		if (!got) {
			k_sleep(K_MSEC(2));
		}
	}
}

static struct k_thread uart_nmea_thread;
K_THREAD_STACK_DEFINE(uart_nmea_stack, 2048);

int uart_nmea_start(void)
{
	if (!device_is_ready(nmea_uart)) {
		LOG_ERR("%s is not ready", NMEA_UART_NAME);
		return -ENODEV;
	}

	if (!started) {
		started = true;
		k_thread_create(&uart_nmea_thread, uart_nmea_stack,
				K_THREAD_STACK_SIZEOF(uart_nmea_stack), uart_rx_thread,
				NULL, NULL, NULL, 4, 0, K_NO_WAIT);
	}

	LOG_INF("NMEA UART RX started: %s (%s)", NMEA_UART_NAME, nmea_uart->name);
	return 0;
}

void uart_nmea_get_stats(struct uart_nmea_stats *stats)
{
	if (stats != NULL) {
		*stats = uart_stats;
	}
}
