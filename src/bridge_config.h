#ifndef BRIDGE_CONFIG_H_
#define BRIDGE_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#define BRIDGE_CONFIG_AIS_MMSI_MAX 999999999U

struct bridge_config_ais {
	bool filter_enabled;
	uint32_t own_mmsi;
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

void bridge_config_set_listener(bridge_config_listener_t listener);

#endif /* BRIDGE_CONFIG_H_ */
