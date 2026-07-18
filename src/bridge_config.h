#ifndef BRIDGE_CONFIG_H_
#define BRIDGE_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#define BRIDGE_CONFIG_AIS_MMSI_MAX 999999999U
#define BRIDGE_CONFIG_WIFI_SSID_MAX 32
#define BRIDGE_CONFIG_WIFI_PSK_MIN 8
#define BRIDGE_CONFIG_WIFI_PSK_MAX 63

struct bridge_config_ais {
	bool filter_enabled;
	uint32_t own_mmsi;
};

struct bridge_config_sta {
	bool enabled;
	bool rotate_mac;
	char ssid[BRIDGE_CONFIG_WIFI_SSID_MAX + 1];
	char psk[BRIDGE_CONFIG_WIFI_PSK_MAX + 1];
};

typedef void (*bridge_config_listener_t)(void);

/* Loads persistent overrides; without them the Kconfig defaults stay effective. */
int bridge_config_init(void);

void bridge_config_get_ais(struct bridge_config_ais *out);

/*
 * Validates, persists changed fields, and notifies the listener when the
 * effective configuration changed. Returns -EINVAL without saving anything
 * when a field is out of range.
 */
int bridge_config_set_ais(const struct bridge_config_ais *ais);

void bridge_config_get_sta(struct bridge_config_sta *out);

/*
 * Validates (SSID <= 32 bytes, PSK empty or 8..63 chars), persists, and marks
 * a reboot as required when the effective configuration changed. STA is
 * reboot-scope: no listener notification, values are consumed at next boot.
 */
int bridge_config_set_sta(const struct bridge_config_sta *sta);

/* True once a reboot-scope value changed since boot. */
bool bridge_config_reboot_required(void);

void bridge_config_set_listener(bridge_config_listener_t listener);

#endif /* BRIDGE_CONFIG_H_ */
