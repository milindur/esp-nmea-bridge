#ifndef FAKE_LED_STRIP_H_
#define FAKE_LED_STRIP_H_

#include <stdint.h>

#include <zephyr/drivers/led_strip.h>

#ifdef __cplusplus
extern "C" {
#endif

void fake_led_strip_set_result(int result);
uint32_t fake_led_strip_update_count(void);
struct led_rgb fake_led_strip_last_pixel(void);
/* Uptime in ms when update attempt idx happened, or -1 if not recorded. */
int64_t fake_led_strip_update_time_ms(uint32_t idx);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_LED_STRIP_H_ */
