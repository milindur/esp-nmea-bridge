#include "status_led_policy.h"

void status_led_policy_tcp_nmea_session_started(struct status_led_policy_state *state)
{
	state->active_tcp_nmea_sessions++;
}

void status_led_policy_tcp_nmea_session_ended(struct status_led_policy_state *state)
{
	if (state->active_tcp_nmea_sessions > 0U) {
		state->active_tcp_nmea_sessions--;
	}
}

enum status_led_base_state
status_led_policy_base_state(const struct status_led_policy_state *state)
{
	if (state->active_tcp_nmea_sessions > 0U) {
		return STATUS_LED_BASE_CONNECTED;
	}

	return STATUS_LED_BASE_DISCONNECTED;
}

struct status_led_rgb status_led_policy_render_base(enum status_led_base_state state,
						    uint32_t elapsed_ms)
{
	if (state == STATUS_LED_BASE_CONNECTED) {
		return (struct status_led_rgb){ 0, STATUS_LED_CONNECTED_GREEN, 0 };
	}

	return status_led_policy_render_disconnected(elapsed_ms);
}

struct status_led_rgb status_led_policy_render_disconnected(uint32_t elapsed_ms)
{
	uint32_t phase_ms = elapsed_ms % STATUS_LED_DISCONNECTED_PERIOD_MS;

	if (phase_ms >= STATUS_LED_DISCONNECTED_ON_MS) {
		return (struct status_led_rgb){ 0, 0, 0 };
	}

	return (struct status_led_rgb){ STATUS_LED_DISCONNECTED_RED, 0, 0 };
}
