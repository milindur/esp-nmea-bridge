#include <zephyr/ztest.h>

#include "bridge_telemetry.h"
#include "nmea_bridge.h"
#include "tcp_nmea_server.h"
#include "tcp_nmea_session.h"
#include "uart_nmea.h"

void uart_nmea_get_stats(struct uart_nmea_stats *stats)
{
	ARG_UNUSED(stats);
}

void nmea_bridge_get_stats(struct nmea_bridge_stats *stats)
{
	ARG_UNUSED(stats);
}

void tcp_nmea_server_get_stats(struct tcp_nmea_server_stats *stats)
{
	ARG_UNUSED(stats);
}

void tcp_nmea_session_get_stats(struct tcp_nmea_session_stats *stats)
{
	ARG_UNUSED(stats);
}

bool wifi_manager_sta_ready(void)
{
	return false;
}

static struct bridge_telemetry_inputs base_inputs(void)
{
	return (struct bridge_telemetry_inputs){
		.tcp_server_max_peers = 3U,
	};
}

ZTEST(bridge_telemetry, test_active_tcp_session_drives_connected_state)
{
	struct bridge_telemetry_state state = { 0 };
	struct bridge_telemetry_inputs inputs = base_inputs();
	struct bridge_telemetry_snapshot snapshot;

	inputs.tcp_nmea_active_sessions = 1U;
	bridge_telemetry_build_snapshot(&state, &inputs, 0, &snapshot);

	zassert_equal(snapshot.connection_state, BRIDGE_TELEMETRY_NMEA_CONNECTED);
}

ZTEST(bridge_telemetry, test_no_tcp_session_drives_disconnected_state)
{
	struct bridge_telemetry_state state = { 0 };
	struct bridge_telemetry_inputs inputs = base_inputs();
	struct bridge_telemetry_snapshot snapshot;

	bridge_telemetry_build_snapshot(&state, &inputs, 0, &snapshot);

	zassert_equal(snapshot.connection_state, BRIDGE_TELEMETRY_NMEA_DISCONNECTED);
}

ZTEST(bridge_telemetry, test_input_stays_active_inside_frame_window)
{
	struct bridge_telemetry_state state = { 0 };
	struct bridge_telemetry_inputs inputs = base_inputs();
	struct bridge_telemetry_snapshot snapshot;

	inputs.bridge_frames_in = 1U;
	bridge_telemetry_build_snapshot(&state, &inputs, 1000, &snapshot);
	bridge_telemetry_build_snapshot(&state, &inputs,
		1000 + BRIDGE_TELEMETRY_NMEA_INPUT_WINDOW_MS, &snapshot);

	zassert_equal(snapshot.input_state, BRIDGE_TELEMETRY_NMEA_INPUT_ACTIVE);
}

ZTEST(bridge_telemetry, test_input_goes_idle_after_frame_window)
{
	struct bridge_telemetry_state state = { 0 };
	struct bridge_telemetry_inputs inputs = base_inputs();
	struct bridge_telemetry_snapshot snapshot;

	inputs.bridge_frames_in = 1U;
	bridge_telemetry_build_snapshot(&state, &inputs, 1000, &snapshot);
	bridge_telemetry_build_snapshot(&state, &inputs,
		1001 + BRIDGE_TELEMETRY_NMEA_INPUT_WINDOW_MS, &snapshot);

	zassert_equal(snapshot.input_state, BRIDGE_TELEMETRY_NMEA_INPUT_IDLE);
}

ZTEST(bridge_telemetry, test_new_frame_reactivates_input_state)
{
	struct bridge_telemetry_state state = { 0 };
	struct bridge_telemetry_inputs inputs = base_inputs();
	struct bridge_telemetry_snapshot snapshot;

	bridge_telemetry_build_snapshot(&state, &inputs, 0, &snapshot);
	inputs.bridge_frames_in = 1U;
	bridge_telemetry_build_snapshot(&state, &inputs, 9000, &snapshot);

	zassert_equal(snapshot.input_state, BRIDGE_TELEMETRY_NMEA_INPUT_ACTIVE);
}

ZTEST(bridge_telemetry, test_warnings_exclude_publish_no_sinks)
{
	struct bridge_telemetry_state state = { 0 };
	struct bridge_telemetry_inputs inputs = base_inputs();
	struct bridge_telemetry_snapshot snapshot;

	inputs.bridge_publish_no_sinks = 10U;
	bridge_telemetry_build_snapshot(&state, &inputs, 0, &snapshot);

	zassert_false(snapshot.warnings.data_quality);
	zassert_false(snapshot.warnings.frame_loss);
}

ZTEST(bridge_telemetry, test_data_quality_warning)
{
	struct bridge_telemetry_state state = { 0 };
	struct bridge_telemetry_inputs inputs = base_inputs();
	struct bridge_telemetry_snapshot snapshot;

	inputs.bridge_publish_invalid = 1U;
	bridge_telemetry_build_snapshot(&state, &inputs, 0, &snapshot);

	zassert_true(snapshot.warnings.data_quality);
	zassert_false(snapshot.warnings.frame_loss);
}

ZTEST(bridge_telemetry, test_frame_loss_warning)
{
	struct bridge_telemetry_state state = { 0 };
	struct bridge_telemetry_inputs inputs = base_inputs();
	struct bridge_telemetry_snapshot snapshot;

	inputs.bridge_sink_dropped_oldest = 1U;
	bridge_telemetry_build_snapshot(&state, &inputs, 0, &snapshot);

	zassert_false(snapshot.warnings.data_quality);
	zassert_true(snapshot.warnings.frame_loss);
}

ZTEST_SUITE(bridge_telemetry, NULL, NULL, NULL, NULL, NULL);
