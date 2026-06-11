#include "status_led.h"
#include "status_led_policy.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#ifdef CONFIG_TEST_FAKE_LED_STRIP
#include "fake_led_strip.h"
#endif

static void call_all_reporters(void)
{
	status_led_tcp_nmea_session_started();
	status_led_tcp_nmea_session_ended();
	status_led_tcp_nmea_client_connecting(true);
	status_led_tcp_nmea_client_connecting(false);
	status_led_nmea_frame_received();
	status_led_nmea_frame_forwarded();
	status_led_nmea_send_failed();
}

#ifdef CONFIG_TEST_STATUS_LED_SCENARIO_NO_DEVICE

ZTEST(status_led_no_device, test_start_and_reporters_are_noops)
{
	zassert_equal(status_led_start(), 0);
	call_all_reporters();
	k_sleep(K_MSEC(300));
	call_all_reporters();
}

ZTEST_SUITE(status_led_no_device, NULL, NULL, NULL, NULL, NULL);

#endif /* CONFIG_TEST_STATUS_LED_SCENARIO_NO_DEVICE */

#ifdef CONFIG_TEST_STATUS_LED_SCENARIO_NOT_READY

ZTEST(status_led_not_ready, test_start_skips_thread_when_device_not_ready)
{
	zassert_equal(status_led_start(), 0);
	k_sleep(K_MSEC(500));
	zassert_equal(fake_led_strip_update_count(), 0U);

	call_all_reporters();
	k_sleep(K_MSEC(300));
	zassert_equal(fake_led_strip_update_count(), 0U);
}

ZTEST_SUITE(status_led_not_ready, NULL, NULL, NULL, NULL, NULL);

#endif /* CONFIG_TEST_STATUS_LED_SCENARIO_NOT_READY */

#ifdef CONFIG_TEST_STATUS_LED_SCENARIO_UPDATE_FAILURE

/* Update attempts happen on the next 100 ms observer wake after a retry
 * window elapses, so allow that granularity plus scheduling slack.
 */
#define BACKOFF_SLACK_MS 300

static uint32_t wait_for_update_count(uint32_t target, uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (fake_led_strip_update_count() < target && k_uptime_get() < deadline) {
		k_sleep(K_MSEC(10));
	}

	return fake_led_strip_update_count();
}

static void assert_gap_in_window(uint32_t idx, int64_t expected_ms)
{
	int64_t gap = fake_led_strip_update_time_ms(idx + 1U) -
		      fake_led_strip_update_time_ms(idx);

	zassert_true(gap >= expected_ms && gap <= expected_ms + BACKOFF_SLACK_MS,
		     "attempt %u->%u gap %lld ms, expected ~%lld ms", idx, idx + 1U,
		     gap, expected_ms);
}

/* Single sequential test: status_led module state is static, so the
 * failure, recovery, and backoff-reset phases must run in order.
 */
ZTEST(status_led_update_failure, test_backoff_and_recovery)
{
	struct led_rgb pixel;

	fake_led_strip_set_result(-EIO);
	zassert_equal(status_led_start(), 0);

	/* First attempt happens promptly and renders the dim red disconnected base. */
	zassert_equal(wait_for_update_count(1U, 500U), 1U);
	pixel = fake_led_strip_last_pixel();
	zassert_equal(pixel.r, STATUS_LED_DISCONNECTED_RED);
	zassert_equal(pixel.g, 0U);
	zassert_equal(pixel.b, 0U);

	/* Exactly one attempt within the first retry window. */
	k_sleep(K_MSEC(500));
	zassert_equal(fake_led_strip_update_count(), 1U);

	/* Reporters stay callable while the strip keeps failing. */
	call_all_reporters();

	/* Backoff doubles 1000 -> 2000 -> 4000 and caps at 5000 ms. */
	zassert_true(wait_for_update_count(5U, 15000U) >= 5U);
	assert_gap_in_window(0U, 1000);
	assert_gap_in_window(1U, 2000);
	assert_gap_in_window(2U, 4000);
	assert_gap_in_window(3U, 5000);

	/* Recovery: the next retry succeeds and the pixel reaches the strip. */
	uint32_t failed_count = fake_led_strip_update_count();

	fake_led_strip_set_result(0);
	zassert_true(wait_for_update_count(failed_count + 1U, 6000U) > failed_count);
	pixel = fake_led_strip_last_pixel();
	zassert_equal(pixel.r, STATUS_LED_DISCONNECTED_RED);
	zassert_equal(pixel.g, 0U);
	zassert_equal(pixel.b, 0U);

	/* Steady state: unchanged color means no further update attempts. */
	uint32_t settled_count = fake_led_strip_update_count();

	k_sleep(K_MSEC(1000));
	zassert_equal(fake_led_strip_update_count(), settled_count);

	/* A fresh failure restarts the backoff at 1000 ms. */
	fake_led_strip_set_result(-EIO);
	status_led_tcp_nmea_session_started();
	zassert_true(wait_for_update_count(settled_count + 2U, 2000U) >=
		     settled_count + 2U);
	assert_gap_in_window(settled_count, 1000);
}

ZTEST_SUITE(status_led_update_failure, NULL, NULL, NULL, NULL, NULL);

#endif /* CONFIG_TEST_STATUS_LED_SCENARIO_UPDATE_FAILURE */
