#ifndef STATUS_LED_POLICY_H_
#define STATUS_LED_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_LED_DISCONNECTED_PERIOD_MS 2000U
#define STATUS_LED_DISCONNECTED_ON_MS 1000U
#define STATUS_LED_DISCONNECTED_RED 16U
#define STATUS_LED_CONNECTING_PERIOD_MS 1000U
#define STATUS_LED_CONNECTING_RED_MIN 4U
#define STATUS_LED_CONNECTING_GREEN_MIN 2U
#define STATUS_LED_CONNECTING_RED_MAX 16U
#define STATUS_LED_CONNECTING_GREEN_MAX 8U
#define STATUS_LED_CONNECTED_GREEN 16U
#define STATUS_LED_NMEA_ACTIVITY_FLASH_MS 250U
#define STATUS_LED_NMEA_ACTIVITY_BLUE 16U

struct status_led_rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

enum status_led_base_state {
	STATUS_LED_BASE_DISCONNECTED,
	STATUS_LED_BASE_CONNECTING,
	STATUS_LED_BASE_CONNECTED,
};

struct status_led_policy_state {
	uint16_t active_tcp_nmea_sessions;
	bool outbound_tcp_nmea_client_connecting;
	uint32_t nmea_activity_flash_until_ms;
};

void status_led_policy_tcp_nmea_session_started(struct status_led_policy_state *state);
void status_led_policy_tcp_nmea_session_ended(struct status_led_policy_state *state);
void status_led_policy_tcp_nmea_client_connecting(struct status_led_policy_state *state,
						 bool connecting);
void status_led_policy_nmea_frame_received(struct status_led_policy_state *state,
					  uint32_t now_ms);
enum status_led_base_state
status_led_policy_base_state(const struct status_led_policy_state *state);

/**
 * Render the current NMEA connection state base pattern.
 *
 * The disconnected base state is a dim red slow blink, the connecting base state
 * is a dim yellow/orange pulse, and the connected base state is steady dim
 * green. These policy functions are independent from Zephyr devices so they can
 * be unit tested on a host.
 */
struct status_led_rgb status_led_policy_render(const struct status_led_policy_state *state,
						 uint32_t elapsed_ms);
struct status_led_rgb status_led_policy_render_base(enum status_led_base_state state,
						    uint32_t elapsed_ms);
struct status_led_rgb status_led_policy_render_disconnected(uint32_t elapsed_ms);
struct status_led_rgb status_led_policy_render_connecting(uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_LED_POLICY_H_ */
