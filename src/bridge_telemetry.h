#ifndef BRIDGE_TELEMETRY_H_
#define BRIDGE_TELEMETRY_H_

#include <stdbool.h>
#include <stdint.h>

#define BRIDGE_TELEMETRY_NMEA_INPUT_WINDOW_MS 5000U

enum bridge_telemetry_nmea_connection_state {
	BRIDGE_TELEMETRY_NMEA_DISCONNECTED,
	BRIDGE_TELEMETRY_NMEA_CONNECTED,
};

enum bridge_telemetry_nmea_input_state {
	BRIDGE_TELEMETRY_NMEA_INPUT_IDLE,
	BRIDGE_TELEMETRY_NMEA_INPUT_ACTIVE,
};

struct bridge_telemetry_counters {
	uint32_t uart_bytes_rx;
	uint32_t uart_frames_rx;
	uint32_t uart_overlong_frames;
	uint32_t uart_ais_self_mmsi_filtered;
	uint32_t bridge_frames_in;
	uint32_t bridge_ingest_dropped_oldest;
	uint32_t bridge_sink_dropped_oldest;
	uint32_t bridge_publish_no_sinks;
	uint32_t bridge_publish_invalid;
	uint32_t bridge_publish_oversize;
	uint32_t tcp_nmea_active_sessions;
	uint32_t tcp_server_active_peers;
	uint32_t tcp_server_max_peers;
};

struct bridge_telemetry_warnings {
	bool data_quality;
	bool frame_loss;
};

struct bridge_telemetry_snapshot {
	enum bridge_telemetry_nmea_connection_state connection_state;
	enum bridge_telemetry_nmea_input_state input_state;
	struct bridge_telemetry_warnings warnings;
	bool sta_ready;
	struct bridge_telemetry_counters counters;
};

struct bridge_telemetry_inputs {
	uint32_t uart_bytes_rx;
	uint32_t uart_frames_rx;
	uint32_t uart_overlong_frames;
	uint32_t uart_ais_self_mmsi_filtered;
	uint32_t bridge_frames_in;
	uint32_t bridge_ingest_dropped_oldest;
	uint32_t bridge_sink_dropped_oldest;
	uint32_t bridge_publish_no_sinks;
	uint32_t bridge_publish_invalid;
	uint32_t bridge_publish_oversize;
	uint32_t tcp_nmea_active_sessions;
	uint32_t tcp_server_active_peers;
	uint32_t tcp_server_max_peers;
	bool sta_ready;
};

struct bridge_telemetry_state {
	uint32_t last_bridge_frames_in;
	int64_t last_bridge_frame_ms;
	bool initialized;
};

void bridge_telemetry_get_snapshot(struct bridge_telemetry_snapshot *snapshot);
void bridge_telemetry_build_snapshot(struct bridge_telemetry_state *state,
				     const struct bridge_telemetry_inputs *inputs,
				     int64_t now_ms,
				     struct bridge_telemetry_snapshot *snapshot);

#endif /* BRIDGE_TELEMETRY_H_ */
