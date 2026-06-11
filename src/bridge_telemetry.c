#include "bridge_telemetry.h"

#include "nmea_bridge.h"
#include "tcp_nmea_server.h"
#include "tcp_nmea_session.h"
#include "uart_nmea.h"
#include "wifi_manager.h"

#include <stddef.h>

#include <zephyr/kernel.h>

static struct bridge_telemetry_state telemetry_state;

static void copy_counters(const struct bridge_telemetry_inputs *inputs,
			 struct bridge_telemetry_counters *counters)
{
	counters->uart_bytes_rx = inputs->uart_bytes_rx;
	counters->uart_frames_rx = inputs->uart_frames_rx;
	counters->uart_overlong_frames = inputs->uart_overlong_frames;
	counters->uart_ais_self_mmsi_filtered = inputs->uart_ais_self_mmsi_filtered;
	counters->bridge_frames_in = inputs->bridge_frames_in;
	counters->bridge_ingest_dropped_oldest = inputs->bridge_ingest_dropped_oldest;
	counters->bridge_sink_dropped_oldest = inputs->bridge_sink_dropped_oldest;
	counters->bridge_publish_no_sinks = inputs->bridge_publish_no_sinks;
	counters->bridge_publish_invalid = inputs->bridge_publish_invalid;
	counters->bridge_publish_oversize = inputs->bridge_publish_oversize;
	counters->tcp_nmea_active_sessions = inputs->tcp_nmea_active_sessions;
	counters->tcp_server_active_peers = inputs->tcp_server_active_peers;
	counters->tcp_server_max_peers = inputs->tcp_server_max_peers;
}

static void collect_inputs(struct bridge_telemetry_inputs *inputs)
{
	struct uart_nmea_stats uart_stats;
	struct nmea_bridge_stats bridge_stats;
	struct tcp_nmea_server_stats server_stats;
	struct tcp_nmea_session_stats session_stats;

	uart_nmea_get_stats(&uart_stats);
	nmea_bridge_get_stats(&bridge_stats);
	tcp_nmea_server_get_stats(&server_stats);
	tcp_nmea_session_get_stats(&session_stats);

	inputs->uart_bytes_rx = uart_stats.bytes_rx;
	inputs->uart_frames_rx = uart_stats.frames_rx;
	inputs->uart_overlong_frames = uart_stats.overlong_frames;
	inputs->uart_ais_self_mmsi_filtered = uart_stats.ais_self_mmsi_filtered;
	inputs->bridge_frames_in = bridge_stats.frames_in;
	inputs->bridge_ingest_dropped_oldest = bridge_stats.ingest_dropped_oldest;
	inputs->bridge_sink_dropped_oldest = bridge_stats.sink_dropped_oldest;
	inputs->bridge_publish_no_sinks = bridge_stats.publish_no_sinks;
	inputs->bridge_publish_invalid = bridge_stats.publish_invalid;
	inputs->bridge_publish_oversize = bridge_stats.publish_oversize;
	inputs->tcp_nmea_active_sessions = session_stats.active_sessions;
	inputs->tcp_server_active_peers = server_stats.active_peers;
	inputs->tcp_server_max_peers = server_stats.max_peers;
	inputs->sta_ready = wifi_manager_sta_ready();
}

void bridge_telemetry_build_snapshot(struct bridge_telemetry_state *state,
				     const struct bridge_telemetry_inputs *inputs,
				     int64_t now_ms,
				     struct bridge_telemetry_snapshot *snapshot)
{
	if (state == NULL || inputs == NULL || snapshot == NULL) {
		return;
	}

	if (!state->initialized) {
		state->last_bridge_frames_in = inputs->bridge_frames_in;
		state->last_bridge_frame_ms = inputs->bridge_frames_in > 0U ? now_ms : INT64_MIN;
		state->initialized = true;
	} else if (inputs->bridge_frames_in != state->last_bridge_frames_in) {
		state->last_bridge_frames_in = inputs->bridge_frames_in;
		state->last_bridge_frame_ms = now_ms;
	}

	snapshot->connection_state = inputs->tcp_nmea_active_sessions > 0U ?
		BRIDGE_TELEMETRY_NMEA_CONNECTED : BRIDGE_TELEMETRY_NMEA_DISCONNECTED;
	if (state->last_bridge_frame_ms == INT64_MIN) {
		snapshot->input_state = BRIDGE_TELEMETRY_NMEA_INPUT_IDLE;
	} else {
		snapshot->input_state = now_ms - state->last_bridge_frame_ms <=
			BRIDGE_TELEMETRY_NMEA_INPUT_WINDOW_MS ?
			BRIDGE_TELEMETRY_NMEA_INPUT_ACTIVE : BRIDGE_TELEMETRY_NMEA_INPUT_IDLE;
	}
	snapshot->warnings.data_quality = inputs->uart_overlong_frames > 0U ||
		inputs->bridge_publish_invalid > 0U || inputs->bridge_publish_oversize > 0U;
	snapshot->warnings.frame_loss = inputs->bridge_ingest_dropped_oldest > 0U ||
		inputs->bridge_sink_dropped_oldest > 0U;
	snapshot->sta_ready = inputs->sta_ready;
	copy_counters(inputs, &snapshot->counters);
}

void bridge_telemetry_get_snapshot(struct bridge_telemetry_snapshot *snapshot)
{
	struct bridge_telemetry_inputs inputs;

	if (snapshot == NULL) {
		return;
	}

	collect_inputs(&inputs);
	bridge_telemetry_build_snapshot(&telemetry_state, &inputs, k_uptime_get(), snapshot);
}
