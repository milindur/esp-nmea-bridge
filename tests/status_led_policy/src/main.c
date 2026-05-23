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

ZTEST(status_led_policy, test_active_session_drives_connected_state)
{
	struct status_led_policy_state state = { 0 };

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_DISCONNECTED);

	status_led_policy_tcp_nmea_session_started(&state);

	zassert_equal(status_led_policy_base_state(&state), STATUS_LED_BASE_CONNECTED);
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
