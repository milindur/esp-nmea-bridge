#include "tcp_nmea_client.h"

#include "status_led.h"
#include "tcp_nmea_session.h"
#include "wifi_manager.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(tcp_nmea_client, LOG_LEVEL_INF);

static bool started;

static int connect_server(const struct net_in_addr *host)
{
	int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("TCP NMEA client socket failed: errno=%d", errno);
		return -errno;
	}

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_PORT);
	addr.sin_addr = *host;

	char host_buf[NET_IPV4_ADDR_LEN];
	net_addr_ntop(AF_INET, host, host_buf, sizeof(host_buf));
	LOG_INF("TCP NMEA client connecting to %s:%d", host_buf,
		CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_PORT);

	if (zsock_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int ret = -errno;
		LOG_WRN("TCP NMEA connect failed: errno=%d", errno);
		(void)zsock_close(fd);
		return ret;
	}

	int flags = zsock_fcntl(fd, ZVFS_F_GETFL, 0);
	(void)zsock_fcntl(fd, ZVFS_F_SETFL, flags | ZVFS_O_NONBLOCK);

	LOG_INF("TCP NMEA client connected");
	return fd;
}

static bool get_tcp_nmea_server_host(struct net_in_addr *host)
{
	if (strlen(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST) > 0) {
		if (!wifi_manager_sta_ready()) {
			return false;
		}

		if (net_addr_pton(AF_INET, CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST, host) != 0) {
			LOG_ERR("Invalid TCP NMEA client host IP: %s", CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST);
			k_sleep(K_SECONDS(30));
			return false;
		}

		return true;
	}

	return wifi_manager_get_sta_gateway(host);
}

static void client_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	uint32_t backoff_s = 1;

	for (;;) {
		struct net_in_addr host;
		uint32_t wait_ticks = 0;

		while (!get_tcp_nmea_server_host(&host)) {
			if (wait_ticks == 0U) {
				status_led_tcp_nmea_client_connecting(false);
			}
			wait_ticks++;
			if ((wait_ticks % 5U) == 0U) {
				LOG_INF("TCP NMEA client waiting for STA IPv4%s",
					strlen(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST) > 0 ? "" : " gateway");
			}
			k_sleep(K_SECONDS(2));
		}

		status_led_tcp_nmea_client_connecting(true);

		int fd = connect_server(&host);
		if (fd < 0) {
			k_sleep(K_SECONDS(backoff_s));
			backoff_s = MIN(backoff_s * 2, 30U);
			continue;
		}

		status_led_tcp_nmea_client_connecting(false);
		backoff_s = 1;
		(void)tcp_nmea_session_run(fd, "tcp-nmea-client");
		LOG_INF("TCP NMEA client disconnected; reconnecting");
		status_led_tcp_nmea_client_connecting(true);
		k_sleep(K_SECONDS(backoff_s));
		backoff_s = MIN(backoff_s * 2, 30U);
	}
}

static struct k_thread tcp_nmea_client_thread;
K_THREAD_STACK_DEFINE(tcp_nmea_client_stack, 4096);

int tcp_nmea_client_start(void)
{
	if (!IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_ENABLE)) {
		LOG_INF("TCP NMEA client disabled");
		return 0;
	}

	if (!started) {
		started = true;
		k_thread_create(&tcp_nmea_client_thread, tcp_nmea_client_stack,
				K_THREAD_STACK_SIZEOF(tcp_nmea_client_stack), client_thread,
				NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	}

	return 0;
}
