#include "tcp_nmea_client.h"

#include "bridge_config.h"
#include "status_led.h"
#include "tcp_nmea_session.h"
#include "wifi_manager.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(tcp_nmea_client, LOG_LEVEL_INF);

static bool started;

/*
 * Live-apply machinery: a configuration change bumps the generation, wakes
 * any waiting loop through the semaphore, and shuts the active session's
 * socket down. session_fd is only touched under session_fd_lock so the
 * shutdown can never hit a closed (and possibly reused) fd.
 */
static K_SEM_DEFINE(config_change_sem, 0, 1);
static K_MUTEX_DEFINE(session_fd_lock);
static int session_fd = -1;
static atomic_t config_gen;
/* Last configuration this module reacted to; guarded by session_fd_lock. */
static struct bridge_config_tcp_client seen_cfg;
static bool seen_cfg_valid;

void tcp_nmea_client_config_changed(void)
{
	struct bridge_config_tcp_client cfg;
	bool changed;

	bridge_config_get_tcp_client(&cfg);

	k_mutex_lock(&session_fd_lock, K_FOREVER);
	/* The bridge-configuration listener fans out every live change (e.g.
	 * AIS); only a TCP client change may drop the active session.
	 */
	changed = !seen_cfg_valid || seen_cfg.enabled != cfg.enabled ||
		  seen_cfg.port != cfg.port || strcmp(seen_cfg.host, cfg.host) != 0;
	seen_cfg = cfg;
	seen_cfg_valid = true;
	if (changed) {
		atomic_inc(&config_gen);
		k_sem_give(&config_change_sem);
		if (session_fd >= 0) {
			/* Zephyr only implements SHUT_RD: it marks the socket
			 * EOF so the session's next drain sees a peer close.
			 * SHUT_WR/SHUT_RDWR return ENOTSUP.
			 */
			(void)zsock_shutdown(session_fd, ZSOCK_SHUT_RD);
		}
	}
	k_mutex_unlock(&session_fd_lock);
}

static int connect_server(int fd, const struct net_in_addr *host, uint16_t port)
{
	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr = *host;

	char host_buf[NET_IPV4_ADDR_LEN];
	net_addr_ntop(AF_INET, host, host_buf, sizeof(host_buf));
	LOG_INF("TCP NMEA client connecting to %s:%u", host_buf, port);

	if (zsock_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int ret = -errno;
		LOG_WRN("TCP NMEA connect failed: errno=%d", errno);
		return ret;
	}

	int flags = zsock_fcntl(fd, ZVFS_F_GETFL, 0);
	(void)zsock_fcntl(fd, ZVFS_F_SETFL, flags | ZVFS_O_NONBLOCK);

	LOG_INF("TCP NMEA client connected");
	return 0;
}

static bool resolve_host(const struct bridge_config_tcp_client *cfg, struct net_in_addr *host)
{
	if (cfg->host[0] != '\0') {
		if (!wifi_manager_sta_ready()) {
			return false;
		}

		if (net_addr_pton(AF_INET, cfg->host, host) != 0) {
			LOG_ERR("Invalid TCP NMEA client host IP: %s", cfg->host);
			return false;
		}

		return true;
	}

	return wifi_manager_get_sta_gateway(host);
}

/* Interruptible wait: returns early when the configuration changed. */
static void wait_or_config_change(k_timeout_t timeout)
{
	(void)k_sem_take(&config_change_sem, timeout);
}

static void client_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	uint32_t backoff_s = 1;

	for (;;) {
		struct bridge_config_tcp_client cfg;
		atomic_val_t gen = atomic_get(&config_gen);
		struct net_in_addr host;
		uint32_t wait_ticks = 0;
		bool config_changed = false;

		bridge_config_get_tcp_client(&cfg);

		if (!cfg.enabled) {
			status_led_tcp_nmea_client_connecting(false);
			(void)k_sem_take(&config_change_sem, K_FOREVER);
			backoff_s = 1;
			continue;
		}

		while (!resolve_host(&cfg, &host)) {
			if (wait_ticks == 0U) {
				status_led_tcp_nmea_client_connecting(false);
			}
			wait_ticks++;
			if ((wait_ticks % 5U) == 0U) {
				LOG_INF("TCP NMEA client waiting for STA IPv4%s",
					cfg.host[0] != '\0' ? "" : " gateway");
			}
			wait_or_config_change(K_SECONDS(2));
			if (atomic_get(&config_gen) != gen) {
				config_changed = true;
				break;
			}
		}
		if (config_changed) {
			backoff_s = 1;
			continue;
		}

		status_led_tcp_nmea_client_connecting(true);

		int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (fd < 0) {
			LOG_ERR("TCP NMEA client socket failed: errno=%d", errno);
			wait_or_config_change(K_SECONDS(backoff_s));
			backoff_s = MIN(backoff_s * 2, 30U);
			continue;
		}

		/* Registered before connecting so a live configuration change can
		 * reach an in-flight connect attempt.
		 */
		k_mutex_lock(&session_fd_lock, K_FOREVER);
		session_fd = fd;
		k_mutex_unlock(&session_fd_lock);
		if (atomic_get(&config_gen) != gen) {
			/* Changed since the snapshot: drop this attempt. */
			k_mutex_lock(&session_fd_lock, K_FOREVER);
			session_fd = -1;
			(void)zsock_close(fd);
			k_mutex_unlock(&session_fd_lock);
			continue;
		}

		if (connect_server(fd, &host, cfg.port) != 0) {
			k_mutex_lock(&session_fd_lock, K_FOREVER);
			session_fd = -1;
			(void)zsock_close(fd);
			k_mutex_unlock(&session_fd_lock);
			wait_or_config_change(K_SECONDS(backoff_s));
			backoff_s = MIN(backoff_s * 2, 30U);
			continue;
		}

		/* SHUT_RD is a no-op on an unconnected socket, so a change that
		 * fired mid-connect could not end this session; re-check now
		 * that the socket is connected and EOF-markable.
		 */
		if (atomic_get(&config_gen) != gen) {
			(void)zsock_shutdown(fd, ZSOCK_SHUT_RD);
		}

		status_led_tcp_nmea_client_connecting(false);
		backoff_s = 1;
		(void)tcp_nmea_session_run(fd, "tcp-nmea-client");

		k_mutex_lock(&session_fd_lock, K_FOREVER);
		session_fd = -1;
		(void)zsock_close(fd);
		k_mutex_unlock(&session_fd_lock);

		LOG_INF("TCP NMEA client disconnected; reconnecting");
		status_led_tcp_nmea_client_connecting(true);
		wait_or_config_change(K_SECONDS(backoff_s));
		backoff_s = MIN(backoff_s * 2, 30U);
	}
}

static struct k_thread tcp_nmea_client_thread;
K_THREAD_STACK_DEFINE(tcp_nmea_client_stack, 4096);

int tcp_nmea_client_start(void)
{
	if (!started) {
		started = true;
		k_thread_create(&tcp_nmea_client_thread, tcp_nmea_client_stack,
				K_THREAD_STACK_SIZEOF(tcp_nmea_client_stack), client_thread,
				NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	}

	return 0;
}
