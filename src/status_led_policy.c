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

void status_led_policy_tcp_nmea_client_connecting(struct status_led_policy_state *state,
						 bool connecting)
{
	state->outbound_tcp_nmea_client_connecting = connecting;
}

void status_led_policy_nmea_frame_received(struct status_led_policy_state *state,
					  uint32_t now_ms)
{
	state->nmea_activity_flash_until_ms = now_ms + STATUS_LED_NMEA_ACTIVITY_FLASH_MS;
}

void status_led_policy_nmea_frame_forwarded(struct status_led_policy_state *state,
					   uint32_t now_ms)
{
	state->nmea_forwarded_flash_until_ms = now_ms + STATUS_LED_NMEA_ACTIVITY_FLASH_MS;
}

void status_led_policy_nmea_send_failed(struct status_led_policy_state *state,
					uint32_t now_ms)
{
	state->nmea_error_flash_until_ms = now_ms + STATUS_LED_NMEA_ACTIVITY_FLASH_MS;
}

enum status_led_base_state
status_led_policy_base_state(const struct status_led_policy_state *state)
{
	if (state->active_tcp_nmea_sessions > 0U) {
		return STATUS_LED_BASE_CONNECTED;
	}

	if (state->outbound_tcp_nmea_client_connecting) {
		return STATUS_LED_BASE_CONNECTING;
	}

	return STATUS_LED_BASE_DISCONNECTED;
}

struct status_led_rgb status_led_policy_render(const struct status_led_policy_state *state,
						 uint32_t elapsed_ms)
{
	if (elapsed_ms < state->nmea_error_flash_until_ms) {
		return (struct status_led_rgb){ STATUS_LED_NMEA_ERROR_RED, 0, 0 };
	}

	if (elapsed_ms < state->nmea_forwarded_flash_until_ms) {
		return (struct status_led_rgb){ STATUS_LED_NMEA_FORWARDED_WHITE,
					       STATUS_LED_NMEA_FORWARDED_WHITE,
					       STATUS_LED_NMEA_FORWARDED_WHITE };
	}

	if (elapsed_ms < state->nmea_activity_flash_until_ms) {
		return (struct status_led_rgb){ 0, 0, STATUS_LED_NMEA_ACTIVITY_BLUE };
	}

	return status_led_policy_render_base(status_led_policy_base_state(state), elapsed_ms);
}

struct status_led_rgb status_led_policy_render_base(enum status_led_base_state state,
						    uint32_t elapsed_ms)
{
	if (state == STATUS_LED_BASE_CONNECTED) {
		return (struct status_led_rgb){ 0, STATUS_LED_CONNECTED_GREEN, 0 };
	}

	if (state == STATUS_LED_BASE_CONNECTING) {
		return status_led_policy_render_connecting(elapsed_ms);
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

struct status_led_rgb status_led_policy_render_connecting(uint32_t elapsed_ms)
{
	uint32_t half_period_ms = STATUS_LED_CONNECTING_PERIOD_MS / 2U;
	uint32_t phase_ms = elapsed_ms % STATUS_LED_CONNECTING_PERIOD_MS;
	uint32_t ramp_ms = phase_ms;
	uint32_t red_range = STATUS_LED_CONNECTING_RED_MAX - STATUS_LED_CONNECTING_RED_MIN;
	uint32_t green_range = STATUS_LED_CONNECTING_GREEN_MAX - STATUS_LED_CONNECTING_GREEN_MIN;

	if (ramp_ms > half_period_ms) {
		ramp_ms = STATUS_LED_CONNECTING_PERIOD_MS - ramp_ms;
	}

	return (struct status_led_rgb){
		STATUS_LED_CONNECTING_RED_MIN + (uint8_t)((red_range * ramp_ms) / half_period_ms),
		STATUS_LED_CONNECTING_GREEN_MIN + (uint8_t)((green_range * ramp_ms) / half_period_ms),
		0,
	};
}
