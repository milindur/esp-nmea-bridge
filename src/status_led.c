#include "status_led.h"
#include "status_led_policy.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

#define STATUS_LED_UPDATE_MS 100U

#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
#define STATUS_LED_HAS_STRIP 1
#define STATUS_LED_STRIP_NODE DT_ALIAS(led_strip)
static const struct device *const strip = DEVICE_DT_GET(STATUS_LED_STRIP_NODE);
#else
#define STATUS_LED_HAS_STRIP 0
static const struct device *const strip;
#endif

static bool status_led_running;
static struct k_thread status_led_thread_data;
static struct status_led_policy_state status_led_state;
K_MUTEX_DEFINE(status_led_lock);
K_THREAD_STACK_DEFINE(status_led_stack, 1024);

static enum status_led_base_state status_led_current_base_state(void)
{
	enum status_led_base_state base_state;

	k_mutex_lock(&status_led_lock, K_FOREVER);
	base_state = status_led_policy_base_state(&status_led_state);
	k_mutex_unlock(&status_led_lock);

	return base_state;
}

static void status_led_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int64_t started_ms = k_uptime_get();

	for (;;) {
		uint32_t elapsed_ms = (uint32_t)(k_uptime_get() - started_ms);
		struct status_led_rgb rendered =
			status_led_policy_render_base(status_led_current_base_state(), elapsed_ms);
		struct led_rgb pixel = {
			.r = rendered.r,
			.g = rendered.g,
			.b = rendered.b,
		};
		int ret = led_strip_update_rgb(strip, &pixel, 1);

		if (ret != 0) {
			LOG_WRN("status LED update failed: %d", ret);
		}

		k_sleep(K_MSEC(STATUS_LED_UPDATE_MS));
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

	status_led_running = true;
	k_thread_create(&status_led_thread_data, status_led_stack,
		       K_THREAD_STACK_SIZEOF(status_led_stack), status_led_thread,
		       NULL, NULL, NULL, 10, 0, K_NO_WAIT);
	LOG_INF("status LED started");

	return 0;
}
