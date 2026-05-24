#include "nmea_bridge.h"
#include "status_led.h"
#include "tcp_nmea_server.h"
#include "tcp_nmea_client.h"
#include "uart_nmea.h"
#include "web_app.h"
#include "wifi_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static void stats_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		struct uart_nmea_stats uart_stats;
		struct nmea_bridge_stats bridge_stats;

		uart_nmea_get_stats(&uart_stats);
		nmea_bridge_get_stats(&bridge_stats);

		LOG_INF("stats: uart_bytes=%u lines=%u overlong=%u frames=%u ingest_drop=%u sink_drop=%u no_sinks=%u invalid=%u oversize=%u",
			uart_stats.bytes_rx, uart_stats.lines_rx, uart_stats.overlong_lines,
			bridge_stats.frames_in, bridge_stats.ingest_dropped_oldest,
			bridge_stats.sink_dropped_oldest, bridge_stats.publish_no_sinks,
			bridge_stats.publish_invalid, bridge_stats.publish_oversize);

		k_sleep(K_SECONDS(30));
	}
}

K_THREAD_DEFINE(stats_tid, 2048, stats_thread, NULL, NULL, NULL,
	       10, 0, 0);

int main(void)
{
	LOG_INF("ESP NMEA bridge on %s", CONFIG_BOARD_TARGET);

	nmea_bridge_init();
	(void)status_led_start();

	int ret = uart_nmea_start();
	if (ret != 0) {
		LOG_ERR("UART NMEA startup failed: %d", ret);
		return ret;
	}

	/* Let the ESP32 Wi-Fi stack settle, matching Zephyr AP+STA sample practice. */
	k_sleep(K_SECONDS(5));

	ret = wifi_manager_start();
	if (ret != 0) {
		LOG_WRN("Wi-Fi startup returned %d; UART ingestion continues", ret);
	}

	ret = tcp_nmea_server_start();
	if (ret != 0) {
		LOG_WRN("TCP NMEA server startup returned %d; UART ingestion continues", ret);
	}

	ret = tcp_nmea_client_start();
	if (ret != 0) {
		LOG_WRN("TCP NMEA client startup returned %d; UART ingestion continues", ret);
	}

	ret = web_app_start();
	if (ret != 0) {
		LOG_WRN("Web app startup returned %d; UART ingestion continues", ret);
	}

	return 0;
}
