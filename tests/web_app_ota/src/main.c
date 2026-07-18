#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <zephyr/net/socket.h>
#include <zephyr/ztest.h>

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
