#ifndef STATUS_LED_H_
#define STATUS_LED_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the optional status LED observer.
 *
 * Returns 0 when the observer started or status LED support is disabled or not
 * available. Hardware failures are non-fatal by design.
 */
#ifdef CONFIG_ESP_SERIAL_BRIDGE_STATUS_LED_ENABLE
int status_led_start(void);
void status_led_tcp_nmea_session_started(void);
void status_led_tcp_nmea_session_ended(void);
void status_led_tcp_nmea_client_connecting(bool connecting);
void status_led_nmea_frame_received(void);
#else
static inline int status_led_start(void)
{
	return 0;
}

static inline void status_led_tcp_nmea_session_started(void)
{
}

static inline void status_led_tcp_nmea_session_ended(void)
{
}

static inline void status_led_tcp_nmea_client_connecting(bool connecting)
{
	(void)connecting;
}

static inline void status_led_nmea_frame_received(void)
{
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* STATUS_LED_H_ */
