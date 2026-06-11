#include "tcp_nmea_server.h"

#include "tcp_nmea_session.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(tcp_nmea_server, LOG_LEVEL_INF);

struct peer_slot {
	int fd;
	bool active;
	struct k_thread thread;
	K_KERNEL_STACK_MEMBER(stack, 2048);
};

static struct peer_slot peers[CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_MAX_PEERS];
static int listen_fd = -1;
static bool started;
static struct k_mutex peers_lock;

static void close_peer(struct peer_slot *peer)
{
	if (peer->fd >= 0) {
		(void)zsock_close(peer->fd);
		peer->fd = -1;
	}

	k_mutex_lock(&peers_lock, K_FOREVER);
	peer->active = false;
	k_mutex_unlock(&peers_lock);
}

static void peer_thread(void *a, void *b, void *c)
{
	struct peer_slot *peer = a;
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	(void)tcp_nmea_session_run(peer->fd, "tcp-server-peer");
	peer->fd = -1;

	close_peer(peer);
	LOG_INF("TCP peer disconnected");
}

static struct peer_slot *alloc_peer(void)
{
	struct peer_slot *peer = NULL;

	k_mutex_lock(&peers_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(peers); i++) {
		if (!peers[i].active) {
			peers[i].fd = -1;
			peers[i].active = true;
			peer = &peers[i];
			break;
		}
	}
	k_mutex_unlock(&peers_lock);

	return peer;
}

static void server_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	struct sockaddr_in addr = { 0 };
	int opt = 1;

	listen_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_fd < 0) {
		LOG_ERR("socket failed: errno=%d", errno);
		return;
	}

	(void)zsock_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("bind failed: errno=%d", errno);
		(void)zsock_close(listen_fd);
		return;
	}

	if (zsock_listen(listen_fd, CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_MAX_PEERS) < 0) {
		LOG_ERR("listen failed: errno=%d", errno);
		(void)zsock_close(listen_fd);
		return;
	}

	LOG_INF("TCP NMEA server listening on 0.0.0.0:%d max_peers=%d",
		CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_PORT,
		CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_MAX_PEERS);

	for (;;) {
		struct sockaddr_in peer_addr;
		socklen_t peer_addr_len = sizeof(peer_addr);
		int fd = zsock_accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_addr_len);
		if (fd < 0) {
			LOG_WRN("accept failed: errno=%d", errno);
			k_sleep(K_SECONDS(1));
			continue;
		}

		struct peer_slot *peer = alloc_peer();
		if (peer == NULL) {
			LOG_WRN("Rejecting TCP peer: no free slots");
			(void)zsock_close(fd);
			continue;
		}

		peer->fd = fd;
		k_thread_create(&peer->thread, peer->stack, K_KERNEL_STACK_SIZEOF(peer->stack),
				peer_thread, peer, NULL, NULL, 7, 0, K_NO_WAIT);
		LOG_INF("TCP peer connected");
	}
}

static struct k_thread tcp_server_thread;
K_THREAD_STACK_DEFINE(tcp_server_stack, 4096);

int tcp_nmea_server_start(void)
{
	if (!IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_ENABLE)) {
		LOG_INF("TCP NMEA server disabled");
		return 0;
	}

	if (!started) {
		k_mutex_init(&peers_lock);
		for (int i = 0; i < ARRAY_SIZE(peers); i++) {
			peers[i].fd = -1;
		}
		started = true;
		k_thread_create(&tcp_server_thread, tcp_server_stack,
				K_THREAD_STACK_SIZEOF(tcp_server_stack), server_thread,
				NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	}

	return 0;
}

void tcp_nmea_server_get_stats(struct tcp_nmea_server_stats *stats)
{
	uint32_t active = 0;

	if (stats == NULL) {
		return;
	}

	k_mutex_lock(&peers_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(peers); i++) {
		if (peers[i].active) {
			active++;
		}
	}
	k_mutex_unlock(&peers_lock);

	stats->active_peers = active;
	stats->max_peers = ARRAY_SIZE(peers);
}
