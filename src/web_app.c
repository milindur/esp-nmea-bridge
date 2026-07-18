#include "web_app.h"

#include "bridge_config.h"
#include "bridge_telemetry.h"
#include "ota_update.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>

#ifndef CONFIG_ZTEST
#include <zephyr/sys/reboot.h>
#endif

#if __has_include(<app_version.h>)
#include <app_version.h>
#endif

#ifndef APP_VERSION_STRING
#define APP_VERSION_STRING ""
#endif

#ifdef CONFIG_ZTEST
void web_app_test_request_reboot(void);
#define web_request_reboot web_app_test_request_reboot
ssize_t web_app_test_zsock_recv(int sock, void *buf, size_t max_len, int flags);
ssize_t web_app_test_zsock_send(int sock, const void *buf, size_t len, int flags);
int web_app_test_zsock_socket(int family, int type, int proto);
int web_app_test_zsock_setsockopt(int sock, int level, int optname, const void *optval,
				       socklen_t optlen);
int web_app_test_zsock_bind(int sock, const struct sockaddr *addr, socklen_t addrlen);
int web_app_test_zsock_listen(int sock, int backlog);
int web_app_test_zsock_accept(int sock, struct sockaddr *addr, socklen_t *addrlen);
int web_app_test_zsock_close(int sock);
#define web_recv web_app_test_zsock_recv
#define web_send web_app_test_zsock_send
#define web_socket web_app_test_zsock_socket
#define web_setsockopt web_app_test_zsock_setsockopt
#define web_bind web_app_test_zsock_bind
#define web_listen web_app_test_zsock_listen
#define web_accept web_app_test_zsock_accept
#define web_close web_app_test_zsock_close
#else
#define web_recv zsock_recv
#define web_send zsock_send
#define web_socket zsock_socket
#define web_setsockopt zsock_setsockopt
#define web_bind zsock_bind
#define web_listen zsock_listen
#define web_accept zsock_accept
#define web_close zsock_close
#endif

LOG_MODULE_REGISTER(web_app, LOG_LEVEL_INF);

#define WEB_APP_RX_BUF_SIZE 1024
#define WEB_APP_JSON_BUF_SIZE 1536
#define WEB_APP_OTA_UPLOAD_PATH "/api/ota/upload"
#define WEB_APP_RECV_TIMEOUT_MS 5000
#define WEB_APP_CONFIG_PATH "/api/config"
#define WEB_APP_REBOOT_PATH "/api/reboot"
/* Must hold a combined POST of every field with escape-heavy strings. */
#define WEB_APP_CONFIG_BODY_MAX 512
#define WEB_APP_REBOOT_DELAY_MS 750

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

struct http_request {
	const char *method;
	size_t method_len;
	const char *path;
	size_t path_len;
	size_t content_length;
	bool has_content_length;
	const uint8_t *body;
	size_t body_len;
};

static const struct web_asset assets[] = {
	{ "/", "text/html; charset=utf-8", web_asset_index_html_start, web_asset_index_html_end },
	{ "/index.html", "text/html; charset=utf-8", web_asset_index_html_start, web_asset_index_html_end },
	{ "/app.js", "application/javascript", web_asset_app_js_start, web_asset_app_js_end },
	{ "/style.css", "text/css; charset=utf-8", web_asset_style_css_start, web_asset_style_css_end },
};

static bool started;
static struct k_thread web_thread;
static uint8_t ota_rx_buf[WEB_APP_RX_BUF_SIZE];
#ifdef CONFIG_ZTEST
static bool web_app_test_force_ota_upload_disallowed;
static size_t web_app_test_json_capacity;
#endif
K_THREAD_STACK_DEFINE(web_stack, 4096);

static int send_all(int fd, const char *data, size_t len)
{
	size_t sent_total = 0;

	while (sent_total < len) {
		ssize_t sent = web_send(fd, data + sent_total, len - sent_total, 0);

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

static int write_response_header(int fd, const char *status, const char *content_type,
				 size_t content_len)
{
	char header[192];
	int header_len = snprintk(header, sizeof(header),
			       "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
			       "Connection: close\r\nCache-Control: no-store\r\n\r\n",
			       status, content_type, content_len);

	if (header_len <= 0) {
		return -EINVAL;
	}

	return send_all(fd, header, (size_t)header_len);
}

static int write_text_response(int fd, const char *status, const char *content_type,
			       const char *body)
{
	int ret = write_response_header(fd, status, content_type, strlen(body));

	if (ret != 0) {
		return ret;
	}

	return send_all(fd, body, strlen(body));
}

static void write_asset_response(int fd, const struct web_asset *asset)
{
	size_t len = (size_t)(asset->end - asset->start);

	if (write_response_header(fd, "200 OK", asset->content_type, len) == 0) {
		(void)send_all(fd, asset->start, len);
	}
}

static const char *web_ota_state_name(enum ota_update_state state)
{
#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
	return ota_update_state_name(state);
#else
	ARG_UNUSED(state);
	return "disabled";
#endif
}

static void get_ota_status_for_json(struct ota_update_status *status)
{
	memset(status, 0, sizeof(*status));
#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
	ota_update_get_status(status);
#else
	status->enabled = false;
	status->state = OTA_UPDATE_STATE_DISABLED;
	status->confirmed = true;
#endif
}

static void json_escape_string(const char *src, char *dst, size_t dst_len)
{
	size_t out = 0U;

	if (dst_len == 0U) {
		return;
	}

	for (; src != NULL && *src != '\0' && out + 1U < dst_len; src++) {
		unsigned char ch = (unsigned char)*src;
		const char *escape = NULL;

		switch (ch) {
		case '\\':
			escape = "\\\\";
			break;
		case '"':
			escape = "\\\"";
			break;
		case '\b':
			escape = "\\b";
			break;
		case '\f':
			escape = "\\f";
			break;
		case '\n':
			escape = "\\n";
			break;
		case '\r':
			escape = "\\r";
			break;
		case '\t':
			escape = "\\t";
			break;
		default:
			break;
		}

		if (escape != NULL) {
			size_t escape_len = strlen(escape);

			if (out + escape_len >= dst_len) {
				break;
			}
			memcpy(dst + out, escape, escape_len);
			out += escape_len;
		} else if (ch >= 0x20U) {
			dst[out++] = (char)ch;
		}
	}
	dst[out] = '\0';
}

static bool ota_upload_allowed(void)
{
#ifdef CONFIG_ZTEST
	if (web_app_test_force_ota_upload_disallowed) {
		return false;
	}
#endif
	return IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE) &&
	       IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_OTA_UPLOAD_TRUSTED_NETWORK_ENABLE);
}

static int write_status_json(int fd)
{
	struct bridge_telemetry_snapshot snapshot;
	struct ota_update_status ota_status;
	const struct bridge_telemetry_counters *counters = &snapshot.counters;
	const char *connection_state;
	const char *input_state;
	static char body[WEB_APP_JSON_BUF_SIZE];
	size_t body_capacity = sizeof(body);
	char escaped_last_error[OTA_UPDATE_LAST_ERROR_LEN * 2U];
	char sta_ipv4_json[BRIDGE_TELEMETRY_IPV4_ADDR_STR_LEN + 2U];
	char sta_rssi_json[12];
	int body_len;

	bridge_telemetry_get_snapshot(&snapshot);
	get_ota_status_for_json(&ota_status);
	json_escape_string(ota_status.last_error, escaped_last_error, sizeof(escaped_last_error));
	connection_state = snapshot.connection_state == BRIDGE_TELEMETRY_NMEA_CONNECTED ?
		"connected" : "disconnected";
	input_state = snapshot.input_state == BRIDGE_TELEMETRY_NMEA_INPUT_ACTIVE ?
		"active" : "idle";

	if (snapshot.sta_ipv4[0] != '\0') {
		(void)snprintk(sta_ipv4_json, sizeof(sta_ipv4_json), "\"%s\"", snapshot.sta_ipv4);
	} else {
		(void)snprintk(sta_ipv4_json, sizeof(sta_ipv4_json), "null");
	}
	if (snapshot.sta_rssi_valid) {
		(void)snprintk(sta_rssi_json, sizeof(sta_rssi_json), "%d", snapshot.sta_rssi_dbm);
	} else {
		(void)snprintk(sta_rssi_json, sizeof(sta_rssi_json), "null");
	}

#ifdef CONFIG_ZTEST
	if (web_app_test_json_capacity != 0U && web_app_test_json_capacity < body_capacity) {
		body_capacity = web_app_test_json_capacity;
	}
#endif

	body_len = snprintk(body, body_capacity,
		"{\"firmware_version\":\"%s\","
		"\"connection_state\":\"%s\",\"input_state\":\"%s\","
		"\"warnings\":{\"data_quality\":%s,\"frame_loss\":%s},"
		"\"wifi\":{\"sta_ready\":%s,\"ip\":%s,\"rssi\":%s},"
		"\"uart\":{\"bytes_rx\":%u,\"frames_rx\":%u,\"overlong_frames\":%u,"
		"\"ais_self_mmsi_filtered\":%u},"
		"\"bridge\":{\"frames_in\":%u,\"ingest_dropped_oldest\":%u,"
		"\"sink_dropped_oldest\":%u,\"publish_no_sinks\":%u,"
		"\"publish_invalid\":%u,\"publish_oversize\":%u},"
		"\"tcp\":{\"active_sessions\":%u},"
		"\"tcp_server\":{\"active_peers\":%u,\"max_peers\":%u},"
		"\"ota\":{\"enabled\":%s,\"state\":\"%s\",\"uploaded_bytes\":%zu,"
		"\"expected_bytes\":%zu,\"max_upload_bytes\":%zu,\"upload_allowed\":%s,"
		"\"slot\":%u,\"confirmed\":%s,\"last_error\":\"%s\"}}",
		APP_VERSION_STRING,
		connection_state, input_state,
		snapshot.warnings.data_quality ? "true" : "false",
		snapshot.warnings.frame_loss ? "true" : "false",
		snapshot.sta_ready ? "true" : "false",
		sta_ipv4_json, sta_rssi_json,
		counters->uart_bytes_rx, counters->uart_frames_rx, counters->uart_overlong_frames,
		counters->uart_ais_self_mmsi_filtered,
		counters->bridge_frames_in, counters->bridge_ingest_dropped_oldest,
		counters->bridge_sink_dropped_oldest, counters->bridge_publish_no_sinks,
		counters->bridge_publish_invalid, counters->bridge_publish_oversize,
		counters->tcp_nmea_active_sessions,
		counters->tcp_server_active_peers, counters->tcp_server_max_peers,
		ota_status.enabled ? "true" : "false",
		web_ota_state_name(ota_status.state), ota_status.uploaded_bytes,
		ota_status.expected_bytes, ota_status.max_upload_bytes,
		ota_upload_allowed() ? "true" : "false", ota_status.slot,
		ota_status.confirmed ? "true" : "false", escaped_last_error);

	if (body_len < 0 || (size_t)body_len >= body_capacity) {
		LOG_ERR("Status JSON needs %d bytes, buffer holds %zu", body_len, body_capacity);
		return write_text_response(fd, "500 Internal Server Error", "application/json",
					   "{\"error\":\"status payload exceeds buffer\"}\n");
	}

	int ret = write_text_response(fd, "200 OK", "application/json", body);

#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
	if (ret == 0) {
		ota_update_self_check_web_reachable();
	}
#endif
	return ret;
}

/*
 * The string buffers hold the escaped JSON form, so they exceed the
 * configuration limits on purpose: over-long input still decodes and is then
 * rejected with a per-field error instead of an opaque JSON error.
 */
struct config_payload {
	bool ais_filter_enabled;
	int32_t ais_own_mmsi;
	bool sta_enabled;
	char sta_ssid[BRIDGE_CONFIG_WIFI_SSID_MAX * 2U + 2U];
	char sta_psk[BRIDGE_CONFIG_WIFI_PSK_MAX * 2U + 2U];
	bool sta_psk_clear;
	bool sta_rotate_mac;
	bool tcp_client_enabled;
	char tcp_client_host[BRIDGE_CONFIG_TCP_CLIENT_HOST_MAX * 2U + 2U];
	int32_t tcp_client_port;
};

static const struct json_obj_descr config_payload_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct config_payload, ais_filter_enabled, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_payload, ais_own_mmsi, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct config_payload, sta_enabled, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_payload, sta_ssid, JSON_TOK_STRING_BUF),
	JSON_OBJ_DESCR_PRIM(struct config_payload, sta_psk, JSON_TOK_STRING_BUF),
	JSON_OBJ_DESCR_PRIM(struct config_payload, sta_psk_clear, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_payload, sta_rotate_mac, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_payload, tcp_client_enabled, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_payload, tcp_client_host, JSON_TOK_STRING_BUF),
	JSON_OBJ_DESCR_PRIM(struct config_payload, tcp_client_port, JSON_TOK_NUMBER),
};

#define CONFIG_FIELD_AIS_FILTER_ENABLED BIT(0)
#define CONFIG_FIELD_AIS_OWN_MMSI BIT(1)
#define CONFIG_FIELD_STA_ENABLED BIT(2)
#define CONFIG_FIELD_STA_SSID BIT(3)
#define CONFIG_FIELD_STA_PSK BIT(4)
#define CONFIG_FIELD_STA_PSK_CLEAR BIT(5)
#define CONFIG_FIELD_STA_ROTATE_MAC BIT(6)
#define CONFIG_FIELD_TCP_CLIENT_ENABLED BIT(7)
#define CONFIG_FIELD_TCP_CLIENT_HOST BIT(8)
#define CONFIG_FIELD_TCP_CLIENT_PORT BIT(9)
#define CONFIG_FIELDS_AIS (CONFIG_FIELD_AIS_FILTER_ENABLED | CONFIG_FIELD_AIS_OWN_MMSI)
#define CONFIG_FIELDS_STA (CONFIG_FIELD_STA_ENABLED | CONFIG_FIELD_STA_SSID | \
			   CONFIG_FIELD_STA_PSK | CONFIG_FIELD_STA_PSK_CLEAR | \
			   CONFIG_FIELD_STA_ROTATE_MAC)
#define CONFIG_FIELDS_TCP (CONFIG_FIELD_TCP_CLIENT_ENABLED | CONFIG_FIELD_TCP_CLIENT_HOST | \
			   CONFIG_FIELD_TCP_CLIENT_PORT)

/*
 * Strict UTF-8 check (rejects overlongs, surrogates, > U+10FFFF): a stored
 * non-UTF-8 SSID would make every /api/config response unparseable JSON in
 * the browser.
 */
static bool utf8_valid(const char *s)
{
	const uint8_t *p = (const uint8_t *)s;

	while (*p != 0U) {
		uint8_t lead = *p;
		size_t cont;

		if (lead < 0x80U) {
			cont = 0U;
		} else if ((lead & 0xE0U) == 0xC0U && lead >= 0xC2U) {
			cont = 1U;
		} else if ((lead & 0xF0U) == 0xE0U) {
			if ((lead == 0xE0U && (p[1] & 0xE0U) == 0x80U) ||
			    (lead == 0xEDU && (p[1] & 0xE0U) == 0xA0U)) {
				return false;
			}
			cont = 2U;
		} else if ((lead & 0xF8U) == 0xF0U && lead <= 0xF4U) {
			if ((lead == 0xF0U && (p[1] & 0xF0U) == 0x80U) ||
			    (lead == 0xF4U && p[1] > 0x8FU)) {
				return false;
			}
			cont = 3U;
		} else {
			return false;
		}
		p++;
		for (; cont > 0U; cont--, p++) {
			if ((*p & 0xC0U) != 0x80U) {
				return false;
			}
		}
	}
	return true;
}

static int write_config_json(int fd, const char *status)
{
	struct bridge_config_ais ais;
	struct bridge_config_sta sta;
	struct bridge_config_tcp_client tcp;
	char ssid_json[BRIDGE_CONFIG_WIFI_SSID_MAX * 2U + 1U];
	char body[448];

	bridge_config_get_ais(&ais);
	bridge_config_get_sta(&sta);
	bridge_config_get_tcp_client(&tcp);
	json_escape_string(sta.ssid, ssid_json, sizeof(ssid_json));
	/* PSKs are write-only through the API; only expose whether one is stored. */
	(void)snprintk(body, sizeof(body),
		       "{\"ais_filter_enabled\":%s,\"ais_own_mmsi\":%u,"
		       "\"sta_enabled\":%s,\"sta_ssid\":\"%s\",\"sta_psk_set\":%s,"
		       "\"sta_rotate_mac\":%s,"
		       "\"tcp_client_enabled\":%s,\"tcp_client_host\":\"%s\","
		       "\"tcp_client_port\":%u,\"reboot_required\":%s}",
		       ais.filter_enabled ? "true" : "false", ais.own_mmsi,
		       sta.enabled ? "true" : "false", ssid_json,
		       sta.psk[0] != '\0' ? "true" : "false",
		       sta.rotate_mac ? "true" : "false",
		       tcp.enabled ? "true" : "false", tcp.host, tcp.port,
		       bridge_config_reboot_required() ? "true" : "false");
	return write_text_response(fd, status, "application/json", body);
}

static int write_config_field_error(int fd, const char *field, const char *message)
{
	char body[128];

	(void)snprintk(body, sizeof(body), "{\"ok\":false,\"errors\":{\"%s\":\"%s\"}}\n",
		       field, message);
	return write_text_response(fd, "400 Bad Request", "application/json", body);
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

static bool token_equals(const char *token, size_t token_len, const char *expected)
{
	return strlen(expected) == token_len && strncmp(token, expected, token_len) == 0;
}

static bool header_name_equals(const char *name, size_t name_len, const char *expected)
{
	if (strlen(expected) != name_len) {
		return false;
	}

	for (size_t i = 0; i < name_len; i++) {
		char got = name[i];
		char want = expected[i];

		if (got >= 'A' && got <= 'Z') {
			got = (char)(got - 'A' + 'a');
		}
		if (want >= 'A' && want <= 'Z') {
			want = (char)(want - 'A' + 'a');
		}
		if (got != want) {
			return false;
		}
	}

	return true;
}

static const char *find_header_end(const char *request, size_t len)
{
	if (len < 4U) {
		return NULL;
	}

	for (size_t i = 0; i <= len - 4U; i++) {
		if (request[i] == '\r' && request[i + 1U] == '\n' &&
		    request[i + 2U] == '\r' && request[i + 3U] == '\n') {
			return &request[i];
		}
	}

	return NULL;
}

static int parse_content_length(const char *value, size_t *content_length)
{
	size_t parsed = 0U;
	bool saw_digit = false;

	while (*value == ' ' || *value == '\t') {
		value++;
	}

	while (*value >= '0' && *value <= '9') {
		size_t digit = (size_t)(*value - '0');

		if (parsed > (SIZE_MAX - digit) / 10U) {
			return -EOVERFLOW;
		}
		parsed = parsed * 10U + digit;
		saw_digit = true;
		value++;
	}

	while (*value == ' ' || *value == '\t') {
		value++;
	}

	if (!saw_digit || *value != '\0') {
		return -EINVAL;
	}

	*content_length = parsed;
	return 0;
}

static int parse_http_request(char *request, size_t received, size_t header_len,
			      struct http_request *parsed)
{
	char *line_end = strstr(request, "\r\n");
	char *method_end;
	char *path_end;

	memset(parsed, 0, sizeof(*parsed));
	if (line_end == NULL) {
		return -EINVAL;
	}

	method_end = memchr(request, ' ', (size_t)(line_end - request));
	if (method_end == NULL) {
		return -EINVAL;
	}

	parsed->method = request;
	parsed->method_len = (size_t)(method_end - request);
	parsed->path = method_end + 1;
	path_end = memchr(parsed->path, ' ', (size_t)(line_end - parsed->path));
	if (path_end == NULL) {
		return -EINVAL;
	}
	parsed->path_len = (size_t)(path_end - parsed->path);

	for (char *line = line_end + 2; *line != '\0'; line = line_end + 2) {
		char *colon;

		line_end = strstr(line, "\r\n");
		if (line_end == NULL || line_end == line) {
			break;
		}
		*line_end = '\0';
		colon = strchr(line, ':');
		if (colon == NULL) {
			continue;
		}
		if (header_name_equals(line, (size_t)(colon - line), "Content-Length")) {
			int ret = parse_content_length(colon + 1, &parsed->content_length);

			if (ret != 0) {
				return ret;
			}
			parsed->has_content_length = true;
		}
	}

	parsed->body = (const uint8_t *)(request + header_len);
	parsed->body_len = received - header_len;
	return 0;
}

static int read_http_request(int fd, char *request, size_t request_size,
			     struct http_request *parsed)
{
	size_t received = 0U;
	const char *header_end;

	for (;;) {
		ssize_t chunk = web_recv(fd, request + received, request_size - received, 0);

		if (chunk < 0 && errno == EINTR) {
			continue;
		}
		if (chunk <= 0) {
			return -ENOTCONN;
		}
		received += (size_t)chunk;
		header_end = find_header_end(request, received);
		if (header_end != NULL) {
			size_t header_len = (size_t)(header_end - request) + 4U;

			request[header_len - 2U] = '\0';
			return parse_http_request(request, received, header_len, parsed);
		}
		if (received == request_size) {
			return -EOVERFLOW;
		}
	}
}

static int write_ota_error_response(int fd, const char *status, const char *message)
{
	char body[160];
	int len = snprintk(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}\n", message);

	if (len < 0) {
		return -EINVAL;
	}
	return write_text_response(fd, status, "application/json", body);
}

#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
static int write_ota_success_response(int fd)
{
	const char body[] = "{\"ok\":true,\"state\":\"pending_reboot\"}\n";

	return write_text_response(fd, "200 OK", "application/json", body);
}

static int write_ota_body_chunk(const uint8_t *data, size_t len, size_t *uploaded)
{
	int ret;

	if (len == 0U) {
		return 0;
	}

	ret = ota_update_write(data, len);
	if (ret == 0) {
		*uploaded += len;
	}
	return ret;
}

#endif

/* Bounds every receive so one stalled client cannot wedge the single server thread. */
static int set_receive_timeout(int fd)
{
	struct timeval timeout = {
		.tv_sec = WEB_APP_RECV_TIMEOUT_MS / 1000,
		.tv_usec = (WEB_APP_RECV_TIMEOUT_MS % 1000) * 1000,
	};

	if (web_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
		return -errno;
	}

	return 0;
}

static void handle_ota_upload(int fd, const struct http_request *request)
{
#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
	size_t uploaded = 0U;
	int ret;
#endif

	if (!ota_upload_allowed()) {
		(void)write_ota_error_response(fd, "403 Forbidden", "OTA uploads are disabled");
		return;
	}
	if (!request->has_content_length) {
		(void)write_ota_error_response(fd, "411 Length Required", "Content-Length required");
		return;
	}
	if (request->body_len > request->content_length) {
		(void)write_ota_error_response(fd, "400 Bad Request", "body exceeds Content-Length");
		return;
	}

#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
	ret = ota_update_begin(request->content_length);
	if (ret != 0) {
		const char *status = ret == -EFBIG ? "413 Payload Too Large" : "400 Bad Request";

		(void)write_ota_error_response(fd, status, "invalid OTA upload size");
		return;
	}

	ret = write_ota_body_chunk(request->body, request->body_len, &uploaded);
	while (ret == 0 && uploaded < request->content_length) {
		size_t remaining = request->content_length - uploaded;
		size_t want = MIN(sizeof(ota_rx_buf), remaining);
		ssize_t received = web_recv(fd, ota_rx_buf, want, 0);

		if (received < 0 && errno == EINTR) {
			continue;
		}
		if (received <= 0) {
			ota_update_abort("OTA upload connection closed early");
			(void)write_ota_error_response(fd, "400 Bad Request", "upload ended early");
			return;
		}
		ret = write_ota_body_chunk(ota_rx_buf, (size_t)received, &uploaded);
	}

	if (ret != 0) {
		ota_update_abort("OTA image write failed");
		(void)write_ota_error_response(fd, "500 Internal Server Error",
					 "writing OTA image failed");
		return;
	}

	ret = ota_update_finish();
	if (ret != 0) {
		ota_update_abort("OTA image finish failed");
		(void)write_ota_error_response(fd, "500 Internal Server Error",
					 "finishing OTA image failed");
		return;
	}

	ret = write_ota_success_response(fd);
	ota_update_schedule_reboot();
	if (ret != 0) {
		LOG_WRN("OTA success response failed after test boot request: %d", ret);
		return;
	}
#else
	ARG_UNUSED(request);
#endif
}

static int read_remaining_body(int fd, char *request, size_t request_size,
			       struct http_request *parsed)
{
	size_t body_offset = (size_t)((const char *)parsed->body - request);

	/*
	 * Strict bound: Zephyr's JSON number decoder temporarily writes a NUL
	 * one byte past the last token, so the body must not end flush with
	 * the buffer.
	 */
	if (parsed->content_length >= request_size - body_offset) {
		return -EOVERFLOW;
	}

	while (parsed->body_len < parsed->content_length) {
		ssize_t chunk = web_recv(fd, request + body_offset + parsed->body_len,
					 parsed->content_length - parsed->body_len, 0);

		if (chunk < 0 && errno == EINTR) {
			continue;
		}
		if (chunk <= 0) {
			return -ENOTCONN;
		}
		parsed->body_len += (size_t)chunk;
	}

	return 0;
}

static void handle_config_update(int fd, char *request, size_t request_size,
				 struct http_request *parsed)
{
	struct config_payload payload;
	struct bridge_config_ais ais;
	struct bridge_config_sta sta;
	struct bridge_config_tcp_client tcp;
	int fields;

	if (!parsed->has_content_length) {
		(void)write_config_field_error(fd, "body", "Content-Length required");
		return;
	}
	if (parsed->content_length > WEB_APP_CONFIG_BODY_MAX) {
		(void)write_text_response(fd, "413 Payload Too Large", "application/json",
					  "{\"ok\":false,\"errors\":{\"body\":\"payload too large\"}}\n");
		return;
	}
	if (parsed->body_len > parsed->content_length) {
		(void)write_config_field_error(fd, "body", "body exceeds Content-Length");
		return;
	}
	if (read_remaining_body(fd, request, request_size, parsed) != 0) {
		(void)write_config_field_error(fd, "body", "body ended early");
		return;
	}

	fields = json_obj_parse((char *)parsed->body, parsed->content_length,
				config_payload_descr, ARRAY_SIZE(config_payload_descr),
				&payload);
	if (fields < 0) {
		(void)write_config_field_error(fd, "body", "invalid JSON");
		return;
	}
	if (fields == 0) {
		(void)write_config_field_error(fd, "body", "no recognised configuration fields");
		return;
	}

	/* Validate every provided field before changing anything. */
	if ((fields & CONFIG_FIELD_AIS_OWN_MMSI) != 0 &&
	    (payload.ais_own_mmsi < 0 ||
	     (uint32_t)payload.ais_own_mmsi > BRIDGE_CONFIG_AIS_MMSI_MAX)) {
		(void)write_config_field_error(fd, "ais_own_mmsi",
					       "must be between 0 and 999999999");
		return;
	}
	if ((fields & CONFIG_FIELD_STA_SSID) != 0) {
		size_t ssid_len = strlen(payload.sta_ssid);

		if (ssid_len < 1U || ssid_len > BRIDGE_CONFIG_WIFI_SSID_MAX) {
			(void)write_config_field_error(fd, "sta_ssid",
						       "must be 1 to 32 bytes");
			return;
		}
		if (!utf8_valid(payload.sta_ssid)) {
			(void)write_config_field_error(fd, "sta_ssid",
						       "must be valid UTF-8");
			return;
		}
	}
	if ((fields & CONFIG_FIELD_STA_PSK) != 0) {
		size_t psk_len = strlen(payload.sta_psk);

		/* Blank means: keep the stored PSK. */
		if (psk_len != 0U && (psk_len < BRIDGE_CONFIG_WIFI_PSK_MIN ||
				      psk_len > BRIDGE_CONFIG_WIFI_PSK_MAX)) {
			(void)write_config_field_error(fd, "sta_psk",
						       "must be 8 to 63 characters or blank");
			return;
		}
	}
	if ((fields & CONFIG_FIELD_STA_PSK_CLEAR) != 0 && payload.sta_psk_clear &&
	    (fields & CONFIG_FIELD_STA_PSK) != 0 && payload.sta_psk[0] != '\0') {
		(void)write_config_field_error(fd, "sta_psk_clear",
					       "cannot be combined with a new password");
		return;
	}
	if ((fields & CONFIG_FIELD_TCP_CLIENT_HOST) != 0 &&
	    (strlen(payload.tcp_client_host) > BRIDGE_CONFIG_TCP_CLIENT_HOST_MAX ||
	     !bridge_config_tcp_client_host_valid(payload.tcp_client_host))) {
		(void)write_config_field_error(fd, "tcp_client_host",
					       "must be an IPv4 address or blank");
		return;
	}
	if ((fields & CONFIG_FIELD_TCP_CLIENT_PORT) != 0 &&
	    (payload.tcp_client_port < 1 || payload.tcp_client_port > 65535)) {
		(void)write_config_field_error(fd, "tcp_client_port",
					       "must be between 1 and 65535");
		return;
	}

	/* Merge the STA update before any save so the result can be validated. */
	if ((fields & CONFIG_FIELDS_STA) != 0) {
		bridge_config_get_sta(&sta);
		if ((fields & CONFIG_FIELD_STA_ENABLED) != 0) {
			sta.enabled = payload.sta_enabled;
		}
		if ((fields & CONFIG_FIELD_STA_ROTATE_MAC) != 0) {
			sta.rotate_mac = payload.sta_rotate_mac;
		}
		if ((fields & CONFIG_FIELD_STA_SSID) != 0) {
			strcpy(sta.ssid, payload.sta_ssid);
		}
		if ((fields & CONFIG_FIELD_STA_PSK_CLEAR) != 0 && payload.sta_psk_clear) {
			sta.psk[0] = '\0';
		} else if ((fields & CONFIG_FIELD_STA_PSK) != 0 && payload.sta_psk[0] != '\0') {
			strcpy(sta.psk, payload.sta_psk);
		}
		/* Enabling the station without any SSID would silently never connect. */
		if ((fields & CONFIG_FIELD_STA_ENABLED) != 0 && payload.sta_enabled &&
		    sta.ssid[0] == '\0') {
			(void)write_config_field_error(fd, "sta_ssid",
						       "required when enabling station mode");
			return;
		}
	}

	if ((fields & CONFIG_FIELDS_AIS) != 0) {
		bridge_config_get_ais(&ais);
		if ((fields & CONFIG_FIELD_AIS_FILTER_ENABLED) != 0) {
			ais.filter_enabled = payload.ais_filter_enabled;
		}
		if ((fields & CONFIG_FIELD_AIS_OWN_MMSI) != 0) {
			ais.own_mmsi = (uint32_t)payload.ais_own_mmsi;
		}
		if (bridge_config_set_ais(&ais) != 0) {
			(void)write_text_response(fd, "500 Internal Server Error", "application/json",
						  "{\"ok\":false,\"errors\":{\"body\":\"saving AIS configuration failed\"}}\n");
			return;
		}
	}

	if ((fields & CONFIG_FIELDS_TCP) != 0) {
		bridge_config_get_tcp_client(&tcp);
		if ((fields & CONFIG_FIELD_TCP_CLIENT_ENABLED) != 0) {
			tcp.enabled = payload.tcp_client_enabled;
		}
		if ((fields & CONFIG_FIELD_TCP_CLIENT_HOST) != 0) {
			strcpy(tcp.host, payload.tcp_client_host);
		}
		if ((fields & CONFIG_FIELD_TCP_CLIENT_PORT) != 0) {
			tcp.port = (uint16_t)payload.tcp_client_port;
		}
		if (bridge_config_set_tcp_client(&tcp) != 0) {
			(void)write_text_response(fd, "500 Internal Server Error", "application/json",
						  "{\"ok\":false,\"errors\":{\"body\":\"saving TCP client configuration failed\"}}\n");
			return;
		}
	}

	if ((fields & CONFIG_FIELDS_STA) != 0 && bridge_config_set_sta(&sta) != 0) {
		(void)write_text_response(fd, "500 Internal Server Error", "application/json",
					  "{\"ok\":false,\"errors\":{\"body\":\"saving WiFi configuration failed\"}}\n");
		return;
	}

	(void)write_config_json(fd, "200 OK");
}

#ifndef CONFIG_ZTEST
static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(web_reboot_work, reboot_work_handler);

/* Delayed so the HTTP response reaches the client before the link drops. */
static void web_request_reboot(void)
{
	LOG_INF("Reboot requested via web API");
	(void)k_work_schedule(&web_reboot_work, K_MSEC(WEB_APP_REBOOT_DELAY_MS));
}
#endif

static void handle_reboot(int fd)
{
	int ret = write_text_response(fd, "200 OK", "application/json", "{\"ok\":true}\n");

	web_request_reboot();
	if (ret != 0) {
		LOG_WRN("Reboot response failed: %d", ret);
	}
}

static void handle_client(int fd)
{
	char request[WEB_APP_RX_BUF_SIZE];
	struct http_request parsed;
	int ret;

	if (set_receive_timeout(fd) != 0) {
		write_text_response(fd, "500 Internal Server Error", "text/plain; charset=utf-8",
				    "configuring receive timeout failed\n");
		return;
	}

	ret = read_http_request(fd, request, sizeof(request), &parsed);

	if (ret != 0) {
		write_text_response(fd, "400 Bad Request", "text/plain; charset=utf-8", "bad request\n");
		return;
	}

	if (token_equals(parsed.method, parsed.method_len, "GET")) {
		if (token_equals(parsed.path, parsed.path_len, "/api/status")) {
			write_status_json(fd);
			return;
		}

		if (token_equals(parsed.path, parsed.path_len, WEB_APP_CONFIG_PATH)) {
			(void)write_config_json(fd, "200 OK");
			return;
		}

		const struct web_asset *asset = find_asset(parsed.path, parsed.path_len);

		if (asset != NULL) {
			write_asset_response(fd, asset);
			return;
		}
	} else if (token_equals(parsed.method, parsed.method_len, "POST")) {
		if (token_equals(parsed.path, parsed.path_len, WEB_APP_OTA_UPLOAD_PATH)) {
			handle_ota_upload(fd, &parsed);
			return;
		}

		if (token_equals(parsed.path, parsed.path_len, WEB_APP_CONFIG_PATH)) {
			handle_config_update(fd, request, sizeof(request), &parsed);
			return;
		}

		if (token_equals(parsed.path, parsed.path_len, WEB_APP_REBOOT_PATH)) {
			handle_reboot(fd);
			return;
		}
	} else {
		write_text_response(fd, "405 Method Not Allowed", "text/plain; charset=utf-8",
				    "method not allowed\n");
		return;
	}

	write_text_response(fd, "404 Not Found", "text/plain; charset=utf-8", "not found\n");
}

#ifdef CONFIG_ZTEST
void web_app_test_handle_client(int fd)
{
	handle_client(fd);
}

void web_app_test_set_ota_upload_disallowed(bool disallowed)
{
	web_app_test_force_ota_upload_disallowed = disallowed;
}

void web_app_test_set_json_capacity(size_t capacity)
{
	web_app_test_json_capacity = capacity;
}
#endif

static void web_app_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	struct sockaddr_in addr = { 0 };
	int opt = 1;
	int listen_fd = web_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (listen_fd < 0) {
		LOG_ERR("HTTP socket failed: errno=%d", errno);
		return;
	}

	(void)web_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(WEB_APP_HTTP_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (web_bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("HTTP bind failed: errno=%d", errno);
		(void)web_close(listen_fd);
		return;
	}

	if (web_listen(listen_fd, 2) < 0) {
		LOG_ERR("HTTP listen failed: errno=%d", errno);
		(void)web_close(listen_fd);
		return;
	}

	LOG_INF("Web app listening on 0.0.0.0:%d", WEB_APP_HTTP_PORT);

	for (;;) {
		int fd = web_accept(listen_fd, NULL, NULL);

		if (fd < 0) {
			LOG_WRN("HTTP accept failed: errno=%d", errno);
			k_sleep(K_SECONDS(1));
			continue;
		}

		handle_client(fd);
		(void)web_close(fd);
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
