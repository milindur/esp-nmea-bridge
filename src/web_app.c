#include "web_app.h"

#include "nmea_bridge.h"
#include "tcp_nmea_server.h"
#include "uart_nmea.h"
#include "wifi_manager.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(web_app, LOG_LEVEL_INF);

#define WEB_APP_HTTP_PORT 80
#define WEB_APP_RX_BUF_SIZE 384
#define WEB_APP_JSON_BUF_SIZE 768

extern const char web_asset_index_html_start[];
extern const char web_asset_index_html_end[];
extern const char web_asset_app_js_start[];
extern const char web_asset_app_js_end[];
extern const char web_asset_style_css_start[];
extern const char web_asset_style_css_end[];

struct web_asset {
	const char *path;
	const char *content_type;
	const char *start;
	const char *end;
};

static const struct web_asset assets[] = {
	{ "/", "text/html; charset=utf-8", web_asset_index_html_start, web_asset_index_html_end },
	{ "/index.html", "text/html; charset=utf-8", web_asset_index_html_start, web_asset_index_html_end },
	{ "/app.js", "application/javascript", web_asset_app_js_start, web_asset_app_js_end },
	{ "/style.css", "text/css; charset=utf-8", web_asset_style_css_start, web_asset_style_css_end },
};

static bool started;
static struct k_thread web_thread;
K_THREAD_STACK_DEFINE(web_stack, 4096);

static int send_all(int fd, const char *data, size_t len)
{
	size_t sent_total = 0;

	while (sent_total < len) {
		ssize_t sent = zsock_send(fd, data + sent_total, len - sent_total, 0);

		if (sent > 0) {
			sent_total += sent;
			continue;
		}

		if (sent < 0 && errno == EINTR) {
			continue;
		}

		return -errno;
	}

	return 0;
}

static void write_response_header(int fd, const char *status, const char *content_type,
				  size_t content_len)
{
	char header[192];
	int header_len = snprintk(header, sizeof(header),
			       "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
			       "Connection: close\r\nCache-Control: no-store\r\n\r\n",
			       status, content_type, content_len);

	if (header_len > 0) {
		(void)send_all(fd, header, (size_t)header_len);
	}
}

static void write_text_response(int fd, const char *status, const char *content_type,
				const char *body)
{
	write_response_header(fd, status, content_type, strlen(body));
	(void)send_all(fd, body, strlen(body));
}

static void write_asset_response(int fd, const struct web_asset *asset)
{
	size_t len = (size_t)(asset->end - asset->start);

	write_response_header(fd, "200 OK", asset->content_type, len);
	(void)send_all(fd, asset->start, len);
}

static void write_status_json(int fd)
{
	struct uart_nmea_stats uart_stats;
	struct nmea_bridge_stats bridge_stats;
	struct tcp_nmea_server_stats server_stats;
	char body[WEB_APP_JSON_BUF_SIZE];
	bool sta_ready = wifi_manager_sta_ready();
	const char *health = "ok";

	uart_nmea_get_stats(&uart_stats);
	nmea_bridge_get_stats(&bridge_stats);
	tcp_nmea_server_get_stats(&server_stats);

	if (!sta_ready || bridge_stats.publish_invalid > 0U || bridge_stats.publish_oversize > 0U ||
	    uart_stats.overlong_lines > 0U) {
		health = "degraded";
	}

	(void)snprintk(body, sizeof(body),
		"{\"health\":\"%s\",\"wifi\":{\"sta_ready\":%s},"
		"\"uart\":{\"bytes_rx\":%u,\"lines_rx\":%u,\"overlong_lines\":%u},"
		"\"bridge\":{\"frames_in\":%u,\"ingest_dropped_oldest\":%u,"
		"\"sink_dropped_oldest\":%u,\"publish_no_sinks\":%u,"
		"\"publish_invalid\":%u,\"publish_oversize\":%u},"
		"\"tcp_server\":{\"active_clients\":%u,\"max_clients\":%u}}",
		health, sta_ready ? "true" : "false",
		uart_stats.bytes_rx, uart_stats.lines_rx, uart_stats.overlong_lines,
		bridge_stats.frames_in, bridge_stats.ingest_dropped_oldest,
		bridge_stats.sink_dropped_oldest, bridge_stats.publish_no_sinks,
		bridge_stats.publish_invalid, bridge_stats.publish_oversize,
		server_stats.active_clients, server_stats.max_clients);

	write_text_response(fd, "200 OK", "application/json", body);
}

static const struct web_asset *find_asset(const char *path, size_t path_len)
{
	for (int i = 0; i < ARRAY_SIZE(assets); i++) {
		if (strlen(assets[i].path) == path_len && strncmp(path, assets[i].path, path_len) == 0) {
			return &assets[i];
		}
	}

	return NULL;
}

static void handle_client(int fd)
{
	char request[WEB_APP_RX_BUF_SIZE];
	char *path;
	char *path_end;
	ssize_t received = zsock_recv(fd, request, sizeof(request) - 1, 0);

	if (received <= 0) {
		return;
	}
	request[received] = '\0';

	if (strncmp(request, "GET ", 4) != 0) {
		write_text_response(fd, "405 Method Not Allowed", "text/plain; charset=utf-8",
				    "method not allowed\n");
		return;
	}

	path = &request[4];
	path_end = strchr(path, ' ');
	if (path_end == NULL) {
		write_text_response(fd, "400 Bad Request", "text/plain; charset=utf-8", "bad request\n");
		return;
	}

	if (strncmp(path, "/api/status", (size_t)(path_end - path)) == 0 &&
	    (size_t)(path_end - path) == strlen("/api/status")) {
		write_status_json(fd);
		return;
	}

	const struct web_asset *asset = find_asset(path, (size_t)(path_end - path));

	if (asset != NULL) {
		write_asset_response(fd, asset);
		return;
	}

	write_text_response(fd, "404 Not Found", "text/plain; charset=utf-8", "not found\n");
}

static void web_app_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	struct sockaddr_in addr = { 0 };
	int opt = 1;
	int listen_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (listen_fd < 0) {
		LOG_ERR("HTTP socket failed: errno=%d", errno);
		return;
	}

	(void)zsock_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(WEB_APP_HTTP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (zsock_bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("HTTP bind failed: errno=%d", errno);
		(void)zsock_close(listen_fd);
		return;
	}

	if (zsock_listen(listen_fd, 2) < 0) {
		LOG_ERR("HTTP listen failed: errno=%d", errno);
		(void)zsock_close(listen_fd);
		return;
	}

	LOG_INF("Web app listening on 0.0.0.0:%d", WEB_APP_HTTP_PORT);

	for (;;) {
		int fd = zsock_accept(listen_fd, NULL, NULL);

		if (fd < 0) {
			LOG_WRN("HTTP accept failed: errno=%d", errno);
			k_sleep(K_SECONDS(1));
			continue;
		}

		handle_client(fd);
		(void)zsock_close(fd);
	}
}

int web_app_start(void)
{
	if (!IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_WEB_APP_ENABLE)) {
		LOG_INF("Web app disabled");
		return 0;
	}

	if (!started) {
		started = true;
		k_thread_create(&web_thread, web_stack, K_THREAD_STACK_SIZEOF(web_stack),
				web_app_thread, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	}

	return 0;
}
