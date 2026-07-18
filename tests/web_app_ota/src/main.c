#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <zephyr/net/socket.h>
#include <zephyr/ztest.h>

#include "bridge_config.h"
#include "bridge_telemetry.h"
#include "ota_update.h"

void web_app_test_handle_client(int fd);
void web_app_test_set_ota_upload_disallowed(bool disallowed);
void web_app_test_set_json_capacity(size_t capacity);

const char web_asset_index_html_start[] = "";
const char web_asset_index_html_end[] = "";
const char web_asset_app_js_start[] = "";
const char web_asset_app_js_end[] = "";
const char web_asset_style_css_start[] = "";
const char web_asset_style_css_end[] = "";

struct socket_script {
	const uint8_t *chunks[4];
	size_t chunk_lens[4];
	int chunk_count;
	int recv_index;
	int send_count;
	int send_fail_at;
	char sent[2048];
	size_t sent_len;
};

static struct socket_script sock;
static struct bridge_telemetry_snapshot fake_snapshot;
static int ota_begin_ret;
static int ota_write_ret;
static int ota_finish_ret;
static int ota_begin_count;
static int ota_write_count;
static int ota_finish_count;
static int ota_abort_count;
static int ota_schedule_count;
static int ota_self_check_count;
static int receive_timeout_count;
static bool schedule_after_success_response;
static struct bridge_config_ais fake_config;
static struct bridge_config_ais last_set_config;
static int config_set_count;
static int config_set_ret;
static struct bridge_config_sta fake_sta;
static struct bridge_config_sta last_set_sta;
static int sta_set_count;
static int sta_set_ret;
static struct bridge_config_tcp_client fake_tcp;
static struct bridge_config_tcp_client last_set_tcp;
static int tcp_set_count;
static int tcp_set_ret;
static bool fake_reboot_required;
static int reboot_request_count;
static bool reboot_after_response;

static void reset_harness(void)
{
	memset(&sock, 0, sizeof(sock));
	sock.send_fail_at = -1;
	ota_begin_ret = 0;
	ota_write_ret = 0;
	ota_finish_ret = 0;
	ota_begin_count = 0;
	ota_write_count = 0;
	ota_finish_count = 0;
	ota_abort_count = 0;
	ota_schedule_count = 0;
	ota_self_check_count = 0;
	receive_timeout_count = 0;
	schedule_after_success_response = false;
	web_app_test_set_ota_upload_disallowed(false);
	web_app_test_set_json_capacity(0);

	memset(&fake_config, 0, sizeof(fake_config));
	memset(&last_set_config, 0, sizeof(last_set_config));
	fake_config.filter_enabled = true;
	fake_config.own_mmsi = 123456789U;
	config_set_count = 0;
	config_set_ret = 0;

	memset(&fake_sta, 0, sizeof(fake_sta));
	memset(&last_set_sta, 0, sizeof(last_set_sta));
	fake_sta.enabled = true;
	fake_sta.rotate_mac = true;
	strcpy(fake_sta.ssid, "BoatNet");
	strcpy(fake_sta.psk, "anchor123");
	sta_set_count = 0;
	sta_set_ret = 0;

	memset(&fake_tcp, 0, sizeof(fake_tcp));
	memset(&last_set_tcp, 0, sizeof(last_set_tcp));
	fake_tcp.enabled = true;
	strcpy(fake_tcp.host, "192.168.1.50");
	fake_tcp.port = 10110;
	tcp_set_count = 0;
	tcp_set_ret = 0;
	fake_reboot_required = false;
	reboot_request_count = 0;
	reboot_after_response = false;

	memset(&fake_snapshot, 0, sizeof(fake_snapshot));
	fake_snapshot.connection_state = BRIDGE_TELEMETRY_NMEA_CONNECTED;
	fake_snapshot.input_state = BRIDGE_TELEMETRY_NMEA_INPUT_ACTIVE;
	fake_snapshot.sta_ready = true;
	strcpy(fake_snapshot.sta_ipv4, "192.168.4.7");
	fake_snapshot.sta_rssi_valid = true;
	fake_snapshot.sta_rssi_dbm = -58;
	fake_snapshot.counters.tcp_server_max_peers = 3;
}

static void set_request(const char *request)
{
	sock.chunks[0] = (const uint8_t *)request;
	sock.chunk_lens[0] = strlen(request);
	sock.chunk_count = 1;
}

static bool response_contains(const char *needle)
{
	return strstr(sock.sent, needle) != NULL;
}

ssize_t web_app_test_zsock_recv(int sock_fd, void *buf, size_t max_len, int flags)
{
	ARG_UNUSED(sock_fd);
	ARG_UNUSED(flags);

	if (sock.recv_index >= sock.chunk_count) {
		return 0;
	}

	size_t len = sock.chunk_lens[sock.recv_index];
	if (len > max_len) {
		len = max_len;
	}
	memcpy(buf, sock.chunks[sock.recv_index], len);
	sock.recv_index++;
	return (ssize_t)len;
}

ssize_t web_app_test_zsock_send(int sock_fd, const void *buf, size_t len, int flags)
{
	ARG_UNUSED(sock_fd);
	ARG_UNUSED(flags);

	if (sock.send_fail_at == sock.send_count) {
		errno = EIO;
		return -1;
	}
	sock.send_count++;

	if (sock.sent_len + len >= sizeof(sock.sent)) {
		len = sizeof(sock.sent) - sock.sent_len - 1U;
	}
	memcpy(sock.sent + sock.sent_len, buf, len);
	sock.sent_len += len;
	sock.sent[sock.sent_len] = '\0';
	return (ssize_t)len;
}

int web_app_test_zsock_socket(int family, int type, int proto)
{
	ARG_UNUSED(family);
	ARG_UNUSED(type);
	ARG_UNUSED(proto);
	return -ENOTSUP;
}

int web_app_test_zsock_setsockopt(int sock_fd, int level, int optname, const void *optval, socklen_t optlen)
{
	ARG_UNUSED(sock_fd);
	ARG_UNUSED(optval);
	ARG_UNUSED(optlen);

	if (level == SOL_SOCKET && optname == SO_RCVTIMEO) {
		receive_timeout_count++;
	}
	return 0;
}

int web_app_test_zsock_bind(int sock_fd, const struct sockaddr *addr, socklen_t addrlen)
{
	ARG_UNUSED(sock_fd);
	ARG_UNUSED(addr);
	ARG_UNUSED(addrlen);
	return 0;
}

int web_app_test_zsock_listen(int sock_fd, int backlog)
{
	ARG_UNUSED(sock_fd);
	ARG_UNUSED(backlog);
	return 0;
}

int web_app_test_zsock_accept(int sock_fd, struct sockaddr *addr, socklen_t *addrlen)
{
	ARG_UNUSED(sock_fd);
	ARG_UNUSED(addr);
	ARG_UNUSED(addrlen);
	return -ENOTSUP;
}

int web_app_test_zsock_close(int sock_fd)
{
	ARG_UNUSED(sock_fd);
	return 0;
}

void bridge_telemetry_get_snapshot(struct bridge_telemetry_snapshot *snapshot)
{
	*snapshot = fake_snapshot;
}

void bridge_config_get_ais(struct bridge_config_ais *out)
{
	*out = fake_config;
}

int bridge_config_set_ais(const struct bridge_config_ais *ais)
{
	config_set_count++;
	last_set_config = *ais;
	if (config_set_ret == 0) {
		fake_config = *ais;
	}
	return config_set_ret;
}

void bridge_config_get_sta(struct bridge_config_sta *out)
{
	*out = fake_sta;
}

int bridge_config_set_sta(const struct bridge_config_sta *sta)
{
	sta_set_count++;
	last_set_sta = *sta;
	if (sta_set_ret == 0) {
		fake_sta = *sta;
		fake_reboot_required = true;
	}
	return sta_set_ret;
}

void bridge_config_get_tcp_client(struct bridge_config_tcp_client *out)
{
	*out = fake_tcp;
}

int bridge_config_set_tcp_client(const struct bridge_config_tcp_client *tcp)
{
	tcp_set_count++;
	last_set_tcp = *tcp;
	if (tcp_set_ret == 0) {
		fake_tcp = *tcp;
	}
	return tcp_set_ret;
}

bool bridge_config_reboot_required(void)
{
	return fake_reboot_required;
}

void web_app_test_request_reboot(void)
{
	reboot_request_count++;
	reboot_after_response = response_contains("{\"ok\":true}");
}

const char *ota_update_state_name(enum ota_update_state state)
{
	return state == OTA_UPDATE_STATE_PENDING_REBOOT ? "pending_reboot" : "ready";
}

void ota_update_get_status(struct ota_update_status *status)
{
	memset(status, 0, sizeof(*status));
	status->enabled = true;
	status->state = OTA_UPDATE_STATE_READY;
	status->max_upload_bytes = 1024U;
	status->confirmed = true;
	status->slot = 1U;
}

int ota_update_begin(size_t content_length)
{
	ARG_UNUSED(content_length);
	ota_begin_count++;
	return ota_begin_ret;
}

int ota_update_write(const uint8_t *data, size_t len)
{
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ota_write_count++;
	return ota_write_ret;
}

int ota_update_finish(void)
{
	ota_finish_count++;
	return ota_finish_ret;
}

void ota_update_schedule_reboot(void)
{
	ota_schedule_count++;
	schedule_after_success_response = response_contains("{\"ok\":true,\"state\":\"pending_reboot\"}");
}

void ota_update_abort(const char *reason)
{
	ARG_UNUSED(reason);
	ota_abort_count++;
}

void ota_update_self_check_web_reachable(void)
{
	ota_self_check_count++;
}

ZTEST(web_app_ota_boundary, test_success_response_precedes_reboot_schedule)
{
	reset_harness();
	set_request("POST /api/ota/upload HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");

	web_app_test_handle_client(1);

	zassert_equal(ota_begin_count, 1);
	zassert_equal(ota_write_count, 1);
	zassert_equal(ota_finish_count, 1);
	zassert_equal(receive_timeout_count, 1);
	zassert_equal(ota_schedule_count, 1);
	zassert_true(schedule_after_success_response);
	zassert_true(response_contains("HTTP/1.1 200 OK"));
}

ZTEST(web_app_ota_boundary, test_trusted_network_gate_rejects_before_flash)
{
	reset_harness();
	web_app_test_set_ota_upload_disallowed(true);
	set_request("POST /api/ota/upload HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 403 Forbidden"));
	zassert_equal(ota_begin_count, 0);
	zassert_equal(ota_write_count, 0);
}

ZTEST(web_app_ota_boundary, test_missing_content_length_rejects_before_flash)
{
	reset_harness();
	set_request("POST /api/ota/upload HTTP/1.1\r\n\r\nabc");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 411 Length Required"));
	zassert_equal(ota_begin_count, 0);
	zassert_equal(ota_write_count, 0);
}

ZTEST(web_app_ota_boundary, test_oversized_content_length_rejects_before_writes)
{
	reset_harness();
	ota_begin_ret = -EFBIG;
	set_request("POST /api/ota/upload HTTP/1.1\r\nContent-Length: 2048\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 413 Payload Too Large"));
	zassert_equal(ota_begin_count, 1);
	zassert_equal(ota_write_count, 0);
}

ZTEST(web_app_ota_boundary, test_early_disconnect_aborts)
{
	reset_harness();
	set_request("POST /api/ota/upload HTTP/1.1\r\nContent-Length: 5\r\n\r\nab");

	web_app_test_handle_client(1);

	zassert_true(response_contains("upload ended early"));
	zassert_equal(ota_abort_count, 1);
	zassert_equal(ota_schedule_count, 0);
}

ZTEST(web_app_ota_boundary, test_write_failure_aborts)
{
	reset_harness();
	ota_write_ret = -EIO;
	set_request("POST /api/ota/upload HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");

	web_app_test_handle_client(1);

	zassert_true(response_contains("writing OTA image failed"));
	zassert_equal(ota_abort_count, 1);
	zassert_equal(ota_schedule_count, 0);
}

ZTEST(web_app_ota_boundary, test_finish_failure_aborts)
{
	reset_harness();
	ota_finish_ret = -EIO;
	set_request("POST /api/ota/upload HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");

	web_app_test_handle_client(1);

	zassert_true(response_contains("finishing OTA image failed"));
	zassert_equal(ota_abort_count, 1);
	zassert_equal(ota_schedule_count, 0);
}

ZTEST(web_app_ota_boundary, test_success_response_failure_preserves_committed_update)
{
	reset_harness();
	sock.send_fail_at = 0;
	set_request("POST /api/ota/upload HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");

	web_app_test_handle_client(1);

	zassert_equal(ota_finish_count, 1);
	zassert_equal(ota_abort_count, 0);
	zassert_equal(ota_schedule_count, 1);
}

ZTEST(web_app_ota_boundary, test_body_larger_than_content_length_rejects_before_flash)
{
	reset_harness();
	set_request("POST /api/ota/upload HTTP/1.1\r\nContent-Length: 2\r\n\r\nabc");

	web_app_test_handle_client(1);

	zassert_true(response_contains("body exceeds Content-Length"));
	zassert_equal(ota_begin_count, 0);
	zassert_equal(ota_write_count, 0);
}

ZTEST(web_app_ota_boundary, test_status_reports_ota_upload_gate)
{
	reset_harness();
	set_request("GET /api/status HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("\"upload_allowed\":true"));
	zassert_true(response_contains("\"max_upload_bytes\":1024"));
}

ZTEST(web_app_ota_boundary, test_status_reports_firmware_and_wifi_details)
{
	reset_harness();
	set_request("GET /api/status HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("\"firmware_version\":\"1.2.3-test\""));
	zassert_true(response_contains("\"ip\":\"192.168.4.7\""));
	zassert_true(response_contains("\"rssi\":-58"));
}

ZTEST(web_app_ota_boundary, test_status_reports_null_wifi_details_when_unavailable)
{
	reset_harness();
	fake_snapshot.sta_ready = false;
	fake_snapshot.sta_ipv4[0] = '\0';
	fake_snapshot.sta_rssi_valid = false;
	set_request("GET /api/status HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("\"ip\":null"));
	zassert_true(response_contains("\"rssi\":null"));
}

ZTEST(web_app_ota_boundary, test_status_json_overflow_is_reported_not_truncated)
{
	reset_harness();
	web_app_test_set_json_capacity(64);
	set_request("GET /api/status HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 500 Internal Server Error"));
	zassert_equal(ota_self_check_count, 0);
}

ZTEST(web_app_ota_boundary, test_status_response_is_self_check_boundary)
{
	reset_harness();
	set_request("GET /api/status HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(ota_self_check_count, 1);
}

ZTEST(web_app_ota_boundary, test_non_status_response_does_not_self_check)
{
	reset_harness();
	set_request("GET /missing HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 404 Not Found"));
	zassert_equal(ota_self_check_count, 0);
}

ZTEST(web_app_ota_boundary, test_failed_status_response_does_not_self_check)
{
	reset_harness();
	sock.send_fail_at = 0;
	set_request("GET /api/status HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_equal(ota_self_check_count, 0);
}

ZTEST_SUITE(web_app_ota_boundary, NULL, NULL, NULL, NULL, NULL);

ZTEST(web_app_config_api, test_get_config_returns_effective_values)
{
	reset_harness();
	set_request("GET /api/config HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_true(response_contains("\"ais_filter_enabled\":true"));
	zassert_true(response_contains("\"ais_own_mmsi\":123456789"));
}

ZTEST(web_app_config_api, test_post_subset_changes_only_that_field)
{
	reset_harness();
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 26\r\n\r\n"
		    "{\"ais_own_mmsi\":211000000}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(config_set_count, 1);
	zassert_true(last_set_config.filter_enabled);
	zassert_equal(last_set_config.own_mmsi, 211000000U);
	zassert_true(response_contains("\"ais_own_mmsi\":211000000"));
	zassert_true(response_contains("\"ais_filter_enabled\":true"));
}

ZTEST(web_app_config_api, test_post_enable_flag_keeps_mmsi)
{
	reset_harness();
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 28\r\n\r\n"
		    "{\"ais_filter_enabled\":false}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(config_set_count, 1);
	zassert_false(last_set_config.filter_enabled);
	zassert_equal(last_set_config.own_mmsi, 123456789U);
}

ZTEST(web_app_config_api, test_post_invalid_mmsi_rejected_with_field_error)
{
	reset_harness();
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 27\r\n\r\n"
		    "{\"ais_own_mmsi\":1000000000}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("\"ais_own_mmsi\":\"must be between 0 and 999999999\""));
	zassert_equal(config_set_count, 0);
}

ZTEST(web_app_config_api, test_post_negative_mmsi_rejected_with_field_error)
{
	reset_harness();
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 20\r\n\r\n"
		    "{\"ais_own_mmsi\":-1}\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_equal(config_set_count, 0);
}

ZTEST(web_app_config_api, test_post_invalid_json_rejected)
{
	reset_harness();
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 9\r\n\r\nnot json!");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("invalid JSON"));
	zassert_equal(config_set_count, 0);
}

ZTEST(web_app_config_api, test_post_unknown_fields_only_rejected)
{
	reset_harness();
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 14\r\n\r\n"
		    "{\"other\":true}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_equal(config_set_count, 0);
}

ZTEST(web_app_config_api, test_post_without_content_length_rejected)
{
	reset_harness();
	set_request("POST /api/config HTTP/1.1\r\n\r\n{\"ais_own_mmsi\":1}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("Content-Length required"));
	zassert_equal(config_set_count, 0);
}

ZTEST(web_app_config_api, test_post_body_split_across_recv_chunks)
{
	reset_harness();
	sock.chunks[0] = (const uint8_t *)"POST /api/config HTTP/1.1\r\nContent-Length: 26\r\n\r\n{\"ais_own_";
	sock.chunk_lens[0] = strlen((const char *)sock.chunks[0]);
	sock.chunks[1] = (const uint8_t *)"mmsi\":211000000}";
	sock.chunk_lens[1] = strlen((const char *)sock.chunks[1]);
	sock.chunk_count = 2;

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(config_set_count, 1);
	zassert_equal(last_set_config.own_mmsi, 211000000U);
}

ZTEST(web_app_config_api, test_post_truncated_body_rejected)
{
	reset_harness();
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 26\r\n\r\n{\"ais_own_");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("body ended early"));
	zassert_equal(config_set_count, 0);
}

ZTEST(web_app_config_api, test_post_save_failure_reports_500)
{
	reset_harness();
	config_set_ret = -EIO;
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 26\r\n\r\n"
		    "{\"ais_own_mmsi\":211000000}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 500 Internal Server Error"));
	zassert_true(response_contains("saving AIS configuration failed"));
}

static char config_post_buf[512];

static void set_config_post(const char *body)
{
	snprintk(config_post_buf, sizeof(config_post_buf),
		 "POST /api/config HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s",
		 strlen(body), body);
	set_request(config_post_buf);
}

ZTEST(web_app_config_api, test_get_config_reports_sta_without_psk)
{
	reset_harness();
	set_request("GET /api/config HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_true(response_contains("\"sta_enabled\":true"));
	zassert_true(response_contains("\"sta_ssid\":\"BoatNet\""));
	zassert_true(response_contains("\"sta_psk_set\":true"));
	zassert_true(response_contains("\"sta_rotate_mac\":true"));
	zassert_true(response_contains("\"reboot_required\":false"));
	zassert_false(response_contains("anchor123"));
	zassert_false(response_contains("\"sta_psk\":"));
}

ZTEST(web_app_config_api, test_get_config_reports_unset_psk)
{
	reset_harness();
	fake_sta.psk[0] = '\0';
	set_request("GET /api/config HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("\"sta_psk_set\":false"));
}

ZTEST(web_app_config_api, test_post_sta_subset_changes_only_provided_fields)
{
	reset_harness();
	set_config_post("{\"sta_ssid\":\"Marina\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(sta_set_count, 1);
	zassert_str_equal(last_set_sta.ssid, "Marina");
	zassert_true(last_set_sta.enabled);
	zassert_true(last_set_sta.rotate_mac);
	zassert_str_equal(last_set_sta.psk, "anchor123");
	zassert_equal(config_set_count, 0);
	zassert_true(response_contains("\"sta_ssid\":\"Marina\""));
	zassert_true(response_contains("\"reboot_required\":true"));
}

ZTEST(web_app_config_api, test_post_blank_psk_keeps_stored_psk)
{
	reset_harness();
	set_config_post("{\"sta_enabled\":false,\"sta_psk\":\"\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(sta_set_count, 1);
	zassert_false(last_set_sta.enabled);
	zassert_str_equal(last_set_sta.psk, "anchor123");
}

ZTEST(web_app_config_api, test_post_new_psk_stored_but_never_echoed)
{
	reset_harness();
	set_config_post("{\"sta_psk\":\"newharbour1\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(sta_set_count, 1);
	zassert_str_equal(last_set_sta.psk, "newharbour1");
	zassert_false(response_contains("newharbour1"));
}

ZTEST(web_app_config_api, test_post_escaped_ssid_is_unescaped)
{
	reset_harness();
	set_config_post("{\"sta_ssid\":\"Boot \\\"Anna\\\"\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_str_equal(last_set_sta.ssid, "Boot \"Anna\"");
}

ZTEST(web_app_config_api, test_post_empty_ssid_rejected_with_field_error)
{
	reset_harness();
	set_config_post("{\"sta_ssid\":\"\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("\"sta_ssid\":"));
	zassert_equal(sta_set_count, 0);
}

ZTEST(web_app_config_api, test_post_overlong_ssid_rejected_with_field_error)
{
	reset_harness();
	set_config_post("{\"sta_ssid\":\"123456789012345678901234567890123\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("\"sta_ssid\":"));
	zassert_equal(sta_set_count, 0);
}

ZTEST(web_app_config_api, test_post_short_psk_rejected_with_field_error)
{
	reset_harness();
	set_config_post("{\"sta_psk\":\"short\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("\"sta_psk\":"));
	zassert_equal(sta_set_count, 0);
}

ZTEST(web_app_config_api, test_post_invalid_sta_field_saves_nothing)
{
	reset_harness();
	set_config_post("{\"ais_own_mmsi\":211000000,\"sta_psk\":\"short\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_equal(sta_set_count, 0);
	zassert_equal(config_set_count, 0);
}

ZTEST(web_app_config_api, test_post_ais_only_does_not_touch_sta)
{
	reset_harness();
	set_config_post("{\"ais_own_mmsi\":211000000}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(config_set_count, 1);
	zassert_equal(sta_set_count, 0);
	zassert_true(response_contains("\"reboot_required\":false"));
}

ZTEST(web_app_config_api, test_post_sta_save_failure_reports_500)
{
	reset_harness();
	sta_set_ret = -EIO;
	set_config_post("{\"sta_ssid\":\"Marina\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 500 Internal Server Error"));
	zassert_true(response_contains("saving WiFi configuration failed"));
}

ZTEST(web_app_config_api, test_post_psk_clear_removes_stored_psk)
{
	reset_harness();
	set_config_post("{\"sta_psk_clear\":true}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(sta_set_count, 1);
	zassert_str_equal(last_set_sta.psk, "");
	zassert_true(response_contains("\"sta_psk_set\":false"));
}

ZTEST(web_app_config_api, test_post_psk_clear_with_new_psk_rejected)
{
	reset_harness();
	set_config_post("{\"sta_psk_clear\":true,\"sta_psk\":\"newharbour1\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("\"sta_psk_clear\":"));
	zassert_equal(sta_set_count, 0);
}

ZTEST(web_app_config_api, test_post_enable_sta_with_empty_effective_ssid_rejected)
{
	reset_harness();
	fake_sta.enabled = false;
	fake_sta.ssid[0] = '\0';
	set_config_post("{\"sta_enabled\":true}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("required when enabling station mode"));
	zassert_equal(sta_set_count, 0);
}

ZTEST(web_app_config_api, test_post_rotate_mac_with_empty_ssid_still_saves)
{
	reset_harness();
	fake_sta.ssid[0] = '\0';
	set_config_post("{\"sta_rotate_mac\":false}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(sta_set_count, 1);
	zassert_false(last_set_sta.rotate_mac);
}

ZTEST(web_app_config_api, test_post_non_utf8_ssid_rejected)
{
	reset_harness();
	/* Raw 0xFF byte in the SSID; header built manually because the
	 * formatted %s path is not byte-transparent for non-ASCII input.
	 */
	set_request("POST /api/config HTTP/1.1\r\nContent-Length: 23\r\n\r\n"
		    "{\"sta_ssid\":\"Boat\xFFNet\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("must be valid UTF-8"));
	zassert_equal(sta_set_count, 0);
}

ZTEST(web_app_config_api, test_get_config_reports_tcp_client)
{
	reset_harness();
	set_request("GET /api/config HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_true(response_contains("\"tcp_client_enabled\":true"));
	zassert_true(response_contains("\"tcp_client_host\":\"192.168.1.50\""));
	zassert_true(response_contains("\"tcp_client_port\":10110"));
}

ZTEST(web_app_config_api, test_post_tcp_subset_changes_only_provided_fields)
{
	reset_harness();
	set_config_post("{\"tcp_client_port\":2000}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(tcp_set_count, 1);
	zassert_true(last_set_tcp.enabled);
	zassert_str_equal(last_set_tcp.host, "192.168.1.50");
	zassert_equal(last_set_tcp.port, 2000);
	zassert_equal(config_set_count, 0);
	zassert_equal(sta_set_count, 0);
	zassert_true(response_contains("\"tcp_client_port\":2000"));
	zassert_true(response_contains("\"reboot_required\":false"));
}

ZTEST(web_app_config_api, test_post_blank_tcp_host_means_gateway)
{
	reset_harness();
	set_config_post("{\"tcp_client_host\":\"\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(tcp_set_count, 1);
	zassert_str_equal(last_set_tcp.host, "");
	zassert_true(response_contains("\"tcp_client_host\":\"\""));
}

ZTEST(web_app_config_api, test_post_invalid_tcp_host_rejected_with_field_error)
{
	reset_harness();
	set_config_post("{\"tcp_client_host\":\"nmea.example.org\"}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("\"tcp_client_host\":\"must be an IPv4 address or blank\""));
	zassert_equal(tcp_set_count, 0);
}

ZTEST(web_app_config_api, test_post_out_of_range_tcp_port_rejected_with_field_error)
{
	reset_harness();
	set_config_post("{\"tcp_client_port\":65536}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_true(response_contains("\"tcp_client_port\":\"must be between 1 and 65535\""));
	zassert_equal(tcp_set_count, 0);
}

ZTEST(web_app_config_api, test_post_zero_tcp_port_rejected_with_field_error)
{
	reset_harness();
	set_config_post("{\"tcp_client_port\":0}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_equal(tcp_set_count, 0);
}

ZTEST(web_app_config_api, test_post_invalid_tcp_field_saves_nothing)
{
	reset_harness();
	set_config_post("{\"ais_own_mmsi\":211000000,\"sta_ssid\":\"Marina\","
			"\"tcp_client_port\":0}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 400 Bad Request"));
	zassert_equal(tcp_set_count, 0);
	zassert_equal(config_set_count, 0);
	zassert_equal(sta_set_count, 0);
}

ZTEST(web_app_config_api, test_post_tcp_enable_with_sta_disabled_still_saves)
{
	reset_harness();
	fake_sta.enabled = false;
	set_config_post("{\"tcp_client_enabled\":true}");

	web_app_test_handle_client(1);

	/* Soft dependency: stored anyway, the UI shows a hint. */
	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_equal(tcp_set_count, 1);
	zassert_true(last_set_tcp.enabled);
}

ZTEST(web_app_config_api, test_post_tcp_save_failure_reports_500)
{
	reset_harness();
	tcp_set_ret = -EIO;
	set_config_post("{\"tcp_client_port\":2000}");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 500 Internal Server Error"));
	zassert_true(response_contains("saving TCP client configuration failed"));
}

ZTEST_SUITE(web_app_config_api, NULL, NULL, NULL, NULL, NULL);

ZTEST(web_app_reboot, test_post_reboot_responds_before_reboot_request)
{
	reset_harness();
	set_request("POST /api/reboot HTTP/1.1\r\nContent-Length: 0\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 200 OK"));
	zassert_true(response_contains("{\"ok\":true}"));
	zassert_equal(reboot_request_count, 1);
	zassert_true(reboot_after_response);
}

ZTEST(web_app_reboot, test_get_reboot_is_not_found)
{
	reset_harness();
	set_request("GET /api/reboot HTTP/1.1\r\n\r\n");

	web_app_test_handle_client(1);

	zassert_true(response_contains("HTTP/1.1 404 Not Found"));
	zassert_equal(reboot_request_count, 0);
}

ZTEST_SUITE(web_app_reboot, NULL, NULL, NULL, NULL, NULL);
