#include "tcp_nmea_server.h"

#include "tcp_nmea_session.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(tcp_nmea_server, LOG_LEVEL_INF);

struct client_slot {
	int fd;
	bool active;
	struct k_thread thread;
	K_KERNEL_STACK_MEMBER(stack, 2048);
};

static struct client_slot clients[CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_MAX_CLIENTS];
static int listen_fd = -1;
static bool started;
static struct k_mutex clients_lock;

static void close_client(struct client_slot *client)
{
	if (client->fd >= 0) {
		(void)zsock_close(client->fd);
		client->fd = -1;
	}

	k_mutex_lock(&clients_lock, K_FOREVER);
	client->active = false;
	k_mutex_unlock(&clients_lock);
}

static void client_thread(void *a, void *b, void *c)
{
	struct client_slot *client = a;
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	(void)tcp_nmea_session_run(client->fd, "tcp-server-client");
	client->fd = -1;

	close_client(client);
	LOG_INF("TCP client disconnected");
}

static struct client_slot *alloc_client(void)
{
	struct client_slot *client = NULL;

	k_mutex_lock(&clients_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(clients); i++) {
		if (!clients[i].active) {
			clients[i].fd = -1;
			clients[i].active = true;
			client = &clients[i];
			break;
		}
	}
	k_mutex_unlock(&clients_lock);

	return client;
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

	if (zsock_listen(listen_fd, CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_MAX_CLIENTS) < 0) {
		LOG_ERR("listen failed: errno=%d", errno);
		(void)zsock_close(listen_fd);
		return;
	}

	LOG_INF("TCP NMEA server listening on 0.0.0.0:%d max_clients=%d",
		CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_PORT,
		CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_MAX_CLIENTS);

	for (;;) {
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);
		int fd = zsock_accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
		if (fd < 0) {
			LOG_WRN("accept failed: errno=%d", errno);
			k_sleep(K_SECONDS(1));
			continue;
		}

		struct client_slot *client = alloc_client();
		if (client == NULL) {
			LOG_WRN("Rejecting TCP client: no free slots");
			(void)zsock_close(fd);
			continue;
		}

		client->fd = fd;
		k_thread_create(&client->thread, client->stack, K_KERNEL_STACK_SIZEOF(client->stack),
				client_thread, client, NULL, NULL, 7, 0, K_NO_WAIT);
		LOG_INF("TCP client connected");
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
		k_mutex_init(&clients_lock);
		for (int i = 0; i < ARRAY_SIZE(clients); i++) {
			clients[i].fd = -1;
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

	k_mutex_lock(&clients_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(clients); i++) {
		if (clients[i].active) {
			active++;
		}
	}
	k_mutex_unlock(&clients_lock);

	stats->active_clients = active;
	stats->max_clients = ARRAY_SIZE(clients);
}
