#include "tcp_nmea_session.h"

#include "nmea_bridge.h"
#include "status_led.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(tcp_nmea_session, LOG_LEVEL_INF);

#define TCP_NMEA_SESSION_RX_DRAIN_BUF_SIZE 128
#define TCP_NMEA_SESSION_FRAME_WAIT K_MSEC(250)
#define TCP_NMEA_SESSION_SEND_RETRY_DELAY K_MSEC(10)

static atomic_t active_sessions;

static bool drain_socket_rx(int fd, const char *sink_name,
				    enum tcp_nmea_session_result *result)
{
	uint8_t buf[TCP_NMEA_SESSION_RX_DRAIN_BUF_SIZE];

	for (;;) {
		ssize_t received = zsock_recv(fd, buf, sizeof(buf), ZSOCK_MSG_DONTWAIT);

		if (received > 0) {
			continue;
		}

		if (received == 0) {
			LOG_INF("TCP NMEA session peer closed: %s", sink_name ? sink_name : "?");
			*result = TCP_NMEA_SESSION_ENDED_PEER_CLOSED;
			return true;
		}

		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return false;
		}

		LOG_WRN("TCP NMEA session RX failed: %s errno=%d",
			sink_name ? sink_name : "?", errno);
		*result = TCP_NMEA_SESSION_ENDED_RX_FAILED;
		return true;
	}
}

static bool send_all(int fd, const struct nmea_frame *frame, const char *sink_name,
		     enum tcp_nmea_session_result *result)
{
	size_t sent_total = 0;

	while (sent_total < frame->len) {
		ssize_t sent = zsock_send(fd, &frame->data[sent_total], frame->len - sent_total, 0);

		if (sent > 0) {
			sent_total += sent;
			continue;
		}

		if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
			k_sleep(TCP_NMEA_SESSION_SEND_RETRY_DELAY);
			continue;
		}

		LOG_WRN("TCP NMEA session send failed: %s sent=%zu len=%u errno=%d",
			sink_name ? sink_name : "?", sent_total, frame->len, errno);
		*result = TCP_NMEA_SESSION_ENDED_SEND_FAILED;
		return true;
	}

	return false;
}

enum tcp_nmea_session_result tcp_nmea_session_run(int fd, const char *sink_name)
{
	enum tcp_nmea_session_result result = TCP_NMEA_SESSION_ENDED_PEER_CLOSED;
	struct nmea_bridge_sink sink;
	struct nmea_frame frame;

	if (nmea_bridge_sink_open(&sink, sink_name) != 0) {
		LOG_ERR("Unable to register TCP NMEA session sink: %s", sink_name ? sink_name : "?");
		return TCP_NMEA_SESSION_ENDED_SINK_UNAVAILABLE;
	}

	atomic_inc(&active_sessions);
	status_led_tcp_nmea_session_started();

	for (;;) {
		if (drain_socket_rx(fd, sink_name, &result)) {
			break;
		}

		int ret = nmea_bridge_sink_take(&sink, &frame, TCP_NMEA_SESSION_FRAME_WAIT);
		if (ret == -ENOTCONN) {
			break;
		}
		if (ret != 0) {
			continue;
		}

		if (send_all(fd, &frame, sink_name, &result)) {
			status_led_nmea_send_failed();
			break;
		}

		status_led_nmea_frame_forwarded();

		if (drain_socket_rx(fd, sink_name, &result)) {
			break;
		}
	}

	status_led_tcp_nmea_session_ended();
	atomic_dec(&active_sessions);
	nmea_bridge_sink_close(&sink);
	return result;
}

void tcp_nmea_session_get_stats(struct tcp_nmea_session_stats *stats)
{
	if (stats != NULL) {
		stats->active_sessions = (uint32_t)atomic_get(&active_sessions);
	}
}
