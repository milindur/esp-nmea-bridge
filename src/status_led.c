#include "status_led.h"
#include "status_led_policy.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

#define STATUS_LED_UPDATE_MS 100U
#define STATUS_LED_RETRY_INITIAL_MS 1000U
#define STATUS_LED_RETRY_MAX_MS 5000U

#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
#define STATUS_LED_HAS_STRIP 1
#define STATUS_LED_STRIP_NODE DT_ALIAS(led_strip)
static const struct device *const strip = DEVICE_DT_GET(STATUS_LED_STRIP_NODE);
static const struct device *const strip_bus = DEVICE_DT_GET(DT_BUS(STATUS_LED_STRIP_NODE));
#else
#define STATUS_LED_HAS_STRIP 0
static const struct device *const strip;
static const struct device *const strip_bus;
#endif

static bool status_led_running;
static int64_t status_led_started_ms;
static struct k_thread status_led_thread_data;
static struct status_led_policy_state status_led_state;
K_MUTEX_DEFINE(status_led_lock);
K_SEM_DEFINE(status_led_wake, 0, 1);
K_THREAD_STACK_DEFINE(status_led_stack, 1024);

static struct status_led_rgb status_led_render(uint32_t elapsed_ms)
{
	struct status_led_rgb rgb;

	k_mutex_lock(&status_led_lock, K_FOREVER);
	rgb = status_led_policy_render(&status_led_state, elapsed_ms);
	k_mutex_unlock(&status_led_lock);

	return rgb;
}

static bool status_led_rgb_equal(const struct status_led_rgb *a,
					const struct status_led_rgb *b)
{
	return a->r == b->r && a->g == b->g && a->b == b->b;
}

static void status_led_recover_bus(void)
{
#if STATUS_LED_HAS_STRIP
	int ret = i2s_trigger(strip_bus, I2S_DIR_TX, I2S_TRIGGER_DROP);

	if (ret < 0) {
		LOG_DBG("status LED I2S recovery trigger failed: %d", ret);
	}
#endif
}

static uint32_t status_led_retry_delay_ms(uint8_t failed_attempts)
{
	uint32_t delay_ms = STATUS_LED_RETRY_INITIAL_MS;

	while (failed_attempts > 1U && delay_ms < STATUS_LED_RETRY_MAX_MS) {
		delay_ms *= 2U;
		failed_attempts--;
	}

	if (delay_ms > STATUS_LED_RETRY_MAX_MS) {
		return STATUS_LED_RETRY_MAX_MS;
	}

	return delay_ms;
}

static void status_led_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int64_t next_retry_ms = 0;
	uint8_t failed_attempts = 0U;
	bool has_applied_rgb = false;
	struct status_led_rgb applied_rgb = { 0 };

	for (;;) {
		int64_t now_ms = k_uptime_get();
		uint32_t elapsed_ms = (uint32_t)(now_ms - status_led_started_ms);
		struct status_led_rgb rendered = status_led_render(elapsed_ms);

		if ((!has_applied_rgb || !status_led_rgb_equal(&rendered, &applied_rgb)) &&
		    now_ms >= next_retry_ms) {
			struct led_rgb pixel = {
				.r = rendered.r,
				.g = rendered.g,
				.b = rendered.b,
			};
			int ret = led_strip_update_rgb(strip, &pixel, 1);

			if (ret == 0) {
				applied_rgb = rendered;
				has_applied_rgb = true;
				failed_attempts = 0U;
				next_retry_ms = 0;
			} else {
				uint32_t retry_delay_ms;

				status_led_recover_bus();

				if (failed_attempts < UINT8_MAX) {
					failed_attempts++;
				}

				retry_delay_ms = status_led_retry_delay_ms(failed_attempts);
				next_retry_ms = now_ms + retry_delay_ms;
				LOG_WRN("status LED update failed: %d; retrying in %u ms", ret,
					retry_delay_ms);
			}
		}

		(void)k_sem_take(&status_led_wake, K_MSEC(STATUS_LED_UPDATE_MS));
	}
}

void status_led_tcp_nmea_session_started(void)
{
	k_mutex_lock(&status_led_lock, K_FOREVER);
	status_led_policy_tcp_nmea_session_started(&status_led_state);
	k_mutex_unlock(&status_led_lock);
}

void status_led_tcp_nmea_session_ended(void)
{
	k_mutex_lock(&status_led_lock, K_FOREVER);
	status_led_policy_tcp_nmea_session_ended(&status_led_state);
	k_mutex_unlock(&status_led_lock);
}

void status_led_tcp_nmea_client_connecting(bool connecting)
{
	k_mutex_lock(&status_led_lock, K_FOREVER);
	status_led_policy_tcp_nmea_client_connecting(&status_led_state, connecting);
	k_mutex_unlock(&status_led_lock);
}

void status_led_nmea_frame_received(void)
{
	uint32_t elapsed_ms = (uint32_t)(k_uptime_get() - status_led_started_ms);

	k_mutex_lock(&status_led_lock, K_FOREVER);
	status_led_policy_nmea_frame_received(&status_led_state, elapsed_ms);
	k_mutex_unlock(&status_led_lock);
	k_sem_give(&status_led_wake);
}

void status_led_nmea_frame_forwarded(void)
{
	uint32_t elapsed_ms = (uint32_t)(k_uptime_get() - status_led_started_ms);

	k_mutex_lock(&status_led_lock, K_FOREVER);
	status_led_policy_nmea_frame_forwarded(&status_led_state, elapsed_ms);
	k_mutex_unlock(&status_led_lock);
	k_sem_give(&status_led_wake);
}

void status_led_nmea_send_failed(void)
{
	uint32_t elapsed_ms = (uint32_t)(k_uptime_get() - status_led_started_ms);

	k_mutex_lock(&status_led_lock, K_FOREVER);
	status_led_policy_nmea_send_failed(&status_led_state, elapsed_ms);
	k_mutex_unlock(&status_led_lock);
	k_sem_give(&status_led_wake);
}

int status_led_start(void)
{
	if (status_led_running) {
		return 0;
	}

	if (!STATUS_LED_HAS_STRIP) {
		LOG_INF("status LED disabled: no led-strip devicetree alias");
		return 0;
	}

	if (!device_is_ready(strip)) {
		LOG_WRN("status LED disabled: led-strip device is not ready");
		return 0;
	}

	status_led_started_ms = k_uptime_get();
	status_led_running = true;
	k_thread_create(&status_led_thread_data, status_led_stack,
		       K_THREAD_STACK_SIZEOF(status_led_stack), status_led_thread,
		       NULL, NULL, NULL, 10, 0, K_NO_WAIT);
	LOG_INF("status LED started");

	return 0;
}
