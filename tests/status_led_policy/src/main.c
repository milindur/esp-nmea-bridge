#include <zephyr/ztest.h>

#include "status_led_policy.h"

ZTEST(status_led_policy, test_disconnected_is_dim_red_during_on_phase)
{
	struct status_led_rgb rgb = status_led_policy_render_disconnected(0U);

	zassert_equal(rgb.r, STATUS_LED_DISCONNECTED_RED);
	zassert_equal(rgb.g, 0U);
	zassert_equal(rgb.b, 0U);
}

ZTEST(status_led_policy, test_disconnected_is_off_during_off_phase)
{
	struct status_led_rgb rgb =
		status_led_policy_render_disconnected(STATUS_LED_DISCONNECTED_ON_MS);

	zassert_equal(rgb.r, 0U);
	zassert_equal(rgb.g, 0U);
	zassert_equal(rgb.b, 0U);
}

ZTEST(status_led_policy, test_disconnected_repeats_after_period)
{
	struct status_led_rgb rgb =
		status_led_policy_render_disconnected(STATUS_LED_DISCONNECTED_PERIOD_MS);

	zassert_equal(rgb.r, STATUS_LED_DISCONNECTED_RED);
	zassert_equal(rgb.g, 0U);
	zassert_equal(rgb.b, 0U);
}

ZTEST(status_led_policy, test_connecting_is_dim_yellow_orange_pulse)
{
	struct status_led_rgb rgb =
		status_led_policy_render_base(STATUS_LED_BASE_CONNECTING,
						      STATUS_LED_CONNECTING_PERIOD_MS / 2U);

	zassert_equal(rgb.r, STATUS_LED_CONNECTING_RED_MAX);
	zassert_equal(rgb.g, STATUS_LED_CONNECTING_GREEN_MAX);
	zassert_equal(rgb.b, 0U);

	rgb = status_led_policy_render_base(STATUS_LED_BASE_CONNECTING, 0U);

	zassert_equal(rgb.r, STATUS_LED_CONNECTING_RED_MIN);
	zassert_equal(rgb.g, STATUS_LED_CONNECTING_GREEN_MIN);
	zassert_equal(rgb.b, 0U);
}

ZTEST(status_led_policy, test_connected_is_dim_steady_green)
{
	struct status_led_rgb rgb =
		status_led_policy_render_base(STATUS_LED_BASE_CONNECTED, 0U);

	zassert_equal(rgb.r, 0U);
	zassert_equal(rgb.g, STATUS_LED_CONNECTED_GREEN);
	zassert_equal(rgb.b, 0U);

	rgb = status_led_policy_render_base(STATUS_LED_BASE_CONNECTED,
					    STATUS_LED_DISCONNECTED_ON_MS);

	zassert_equal(rgb.r, 0U);
	zassert_equal(rgb.g, STATUS_LED_CONNECTED_GREEN);
	zassert_equal(rgb.b, 0U);
}

ZTEST(status_led_policy, test_nmea_frame_receipt_flashes_blue)
{
	struct status_led_policy_state state = { 0 };
	struct status_led_rgb rgb;

	status_led_policy_nmea_frame_received(&state, 1000U);
	rgb = status_led_policy_render(&state, 1000U);

	zassert_equal(rgb.r, 0U);
	zassert_equal(rgb.g, 0U);
	zassert_equal(rgb.b, STATUS_LED_NMEA_ACTIVITY_BLUE);
}

ZTEST(status_led_policy, test_nmea_frame_flash_expires_to_disconnected_base)
{
	struct status_led_policy_state state = { 0 };
	uint32_t expired_ms = STATUS_LED_DISCONNECTED_PERIOD_MS;
	struct status_led_rgb rgb;

	status_led_policy_nmea_frame_received(&state,
					       expired_ms - STATUS_LED_NMEA_ACTIVITY_FLASH_MS);
	rgb = status_led_policy_render(&state, expired_ms);

	zassert_equal(rgb.r, STATUS_LED_DISCONNECTED_RED);
	zassert_equal(rgb.g, 0U);
	zassert_equal(rgb.b, 0U);
}

ZTEST(status_led_policy, test_nmea_frame_flash_expires_to_connecting_base)
{
	struct status_led_policy_state state = { 0 };
	uint32_t expired_ms = STATUS_LED_CONNECTING_PERIOD_MS;
	struct status_led_rgb rgb;

	status_led_policy_tcp_nmea_client_connecting(&state, true);
	status_led_policy_nmea_frame_received(&state,
					       expired_ms - STATUS_LED_NMEA_ACTIVITY_FLASH_MS);
	rgb = status_led_policy_render(&state, expired_ms);

	zassert_equal(rgb.r, STATUS_LED_CONNECTING_RED_MIN);
	zassert_equal(rgb.g, STATUS_LED_CONNECTING_GREEN_MIN);
	zassert_equal(rgb.b, 0U);
}

ZTEST(status_led_policy, test_nmea_frame_flash_expires_to_connected_base)
{
	struct status_led_policy_state state = { 0 };
	uint32_t expired_ms = 1000U + STATUS_LED_NMEA_ACTIVITY_FLASH_MS;
	struct status_led_rgb rgb;

	status_led_policy_tcp_nmea_session_started(&state);
	status_led_policy_nmea_frame_received(&state, 1000U);
	rgb = status_led_policy_render(&state, expired_ms);

	zassert_equal(rgb.r, 0U);
	zassert_equal(rgb.g, STATUS_LED_CONNECTED_GREEN);
	zassert_equal(rgb.b, 0U);
}

ZTEST(status_led_policy, test_active_session_drives_connected_state)
{
	struct status_led_policy_state state = { 0 };

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_DISCONNECTED);

	status_led_policy_tcp_nmea_session_started(&state);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_CONNECTED);
}

ZTEST(status_led_policy, test_outbound_client_retry_drives_connecting_state)
{
	struct status_led_policy_state state = { 0 };

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_DISCONNECTED);

	status_led_policy_tcp_nmea_client_connecting(&state, true);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_CONNECTING);

	status_led_policy_tcp_nmea_client_connecting(&state, false);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_DISCONNECTED);
}

ZTEST(status_led_policy, test_connected_state_wins_over_connecting_state)
{
	struct status_led_policy_state state = { 0 };

	status_led_policy_tcp_nmea_client_connecting(&state, true);
	status_led_policy_tcp_nmea_session_started(&state);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_CONNECTED);

	status_led_policy_tcp_nmea_session_ended(&state);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_CONNECTING);
}

ZTEST(status_led_policy, test_overlapping_sessions_disconnect_after_last_end)
{
	struct status_led_policy_state state = { 0 };

	status_led_policy_tcp_nmea_session_started(&state);
	status_led_policy_tcp_nmea_session_started(&state);
	status_led_policy_tcp_nmea_session_ended(&state);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_CONNECTED);

	status_led_policy_tcp_nmea_session_ended(&state);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_DISCONNECTED);
}

ZTEST(status_led_policy, test_session_end_without_active_session_stays_disconnected)
{
	struct status_led_policy_state state = { 0 };

	status_led_policy_tcp_nmea_session_ended(&state);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_DISCONNECTED);
}

ZTEST_SUITE(status_led_policy, NULL, NULL, NULL, NULL, NULL);
