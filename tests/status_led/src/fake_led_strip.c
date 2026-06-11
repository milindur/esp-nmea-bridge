#define DT_DRV_COMPAT vnd_fake_led_strip

#include "fake_led_strip.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>

#define FAKE_LED_STRIP_MAX_UPDATES 32U

static int update_result;
static uint32_t update_count;
static struct led_rgb last_pixel;
static int64_t update_times_ms[FAKE_LED_STRIP_MAX_UPDATES];

void fake_led_strip_set_result(int result)
{
	update_result = result;
}

uint32_t fake_led_strip_update_count(void)
{
	return update_count;
}

struct led_rgb fake_led_strip_last_pixel(void)
{
	return last_pixel;
}

int64_t fake_led_strip_update_time_ms(uint32_t idx)
{
	if (idx >= MIN(update_count, FAKE_LED_STRIP_MAX_UPDATES)) {
		return -1;
	}

	return update_times_ms[idx];
}

static int fake_update_rgb(const struct device *dev, struct led_rgb *pixels, size_t num_pixels)
{
	ARG_UNUSED(dev);

	if (update_count < FAKE_LED_STRIP_MAX_UPDATES) {
		update_times_ms[update_count] = k_uptime_get();
	}
	update_count++;

	if (num_pixels > 0) {
		last_pixel = pixels[0];
	}

	return update_result;
}

static size_t fake_length(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 1;
}

static int fake_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (IS_ENABLED(CONFIG_TEST_FAKE_LED_STRIP_INIT_FAIL)) {
		return -EIO;
	}

	return 0;
}

static DEVICE_API(led_strip, fake_led_strip_api) = {
	.update_rgb = fake_update_rgb,
	.length = fake_length,
};

DEVICE_DT_INST_DEFINE(0, fake_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &fake_led_strip_api);
