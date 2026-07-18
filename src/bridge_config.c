#include "bridge_config.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#ifdef CONFIG_ZTEST
int bridge_config_test_settings_subsys_init(void);
int bridge_config_test_settings_load_subtree(const char *subtree);
int bridge_config_test_settings_save_one(const char *name, const void *value, size_t val_len);
#define cfg_settings_subsys_init bridge_config_test_settings_subsys_init
#define cfg_settings_load_subtree bridge_config_test_settings_load_subtree
#define cfg_settings_save_one bridge_config_test_settings_save_one
#else
#define cfg_settings_subsys_init settings_subsys_init
#define cfg_settings_load_subtree settings_load_subtree
#define cfg_settings_save_one settings_save_one
#endif

LOG_MODULE_REGISTER(bridge_config, LOG_LEVEL_INF);

#define BRIDGE_CONFIG_SUBTREE "bridge"
#define BRIDGE_CONFIG_KEY_AIS_ENABLED "bridge/ais_filter_enabled"
#define BRIDGE_CONFIG_KEY_AIS_MMSI "bridge/ais_own_mmsi"

static struct bridge_config_ais ais = {
	.filter_enabled = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_ENABLE),
	.own_mmsi = CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_MMSI,
};
static K_MUTEX_DEFINE(config_lock);
static bridge_config_listener_t listener;

static int bridge_config_settings_set(const char *name, size_t len,
				      settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(name, "ais_filter_enabled") == 0) {
		uint8_t enabled;

		if (len != sizeof(enabled) || read_cb(cb_arg, &enabled, sizeof(enabled)) < 0) {
			LOG_WRN("Ignoring malformed stored %s", name);
			return 0;
		}
		k_mutex_lock(&config_lock, K_FOREVER);
		ais.filter_enabled = enabled != 0U;
		k_mutex_unlock(&config_lock);
		return 0;
	}

	if (strcmp(name, "ais_own_mmsi") == 0) {
		uint32_t mmsi;

		if (len != sizeof(mmsi) || read_cb(cb_arg, &mmsi, sizeof(mmsi)) < 0 ||
		    mmsi > BRIDGE_CONFIG_AIS_MMSI_MAX) {
			LOG_WRN("Ignoring malformed stored %s", name);
			return 0;
		}
		k_mutex_lock(&config_lock, K_FOREVER);
		ais.own_mmsi = mmsi;
		k_mutex_unlock(&config_lock);
		return 0;
	}

	return -ENOENT;
}

#if defined(CONFIG_SETTINGS) && !defined(CONFIG_ZTEST)
SETTINGS_STATIC_HANDLER_DEFINE(bridge_config, BRIDGE_CONFIG_SUBTREE, NULL,
			       bridge_config_settings_set, NULL, NULL);
#endif

int bridge_config_init(void)
{
	int ret = cfg_settings_subsys_init();

	if (ret != 0) {
		LOG_WRN("Settings init failed: %d; using build-time defaults", ret);
		return ret;
	}

	ret = cfg_settings_load_subtree(BRIDGE_CONFIG_SUBTREE);
	if (ret != 0) {
		LOG_WRN("Loading stored configuration failed: %d", ret);
	}
	return ret;
}

void bridge_config_get_ais(struct bridge_config_ais *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	*out = ais;
	k_mutex_unlock(&config_lock);
}

void bridge_config_set_listener(bridge_config_listener_t new_listener)
{
	listener = new_listener;
}

int bridge_config_set_ais(const struct bridge_config_ais *next)
{
	bool enabled_changed;
	bool mmsi_changed;
	int ret;

	if (next == NULL || next->own_mmsi > BRIDGE_CONFIG_AIS_MMSI_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	enabled_changed = ais.filter_enabled != next->filter_enabled;
	mmsi_changed = ais.own_mmsi != next->own_mmsi;
	k_mutex_unlock(&config_lock);

	if (enabled_changed) {
		uint8_t enabled = next->filter_enabled ? 1U : 0U;

		ret = cfg_settings_save_one(BRIDGE_CONFIG_KEY_AIS_ENABLED, &enabled,
					    sizeof(enabled));
		if (ret != 0) {
			LOG_ERR("Persisting %s failed: %d", BRIDGE_CONFIG_KEY_AIS_ENABLED, ret);
			return ret;
		}
	}
	if (mmsi_changed) {
		ret = cfg_settings_save_one(BRIDGE_CONFIG_KEY_AIS_MMSI, &next->own_mmsi,
					    sizeof(next->own_mmsi));
		if (ret != 0) {
			LOG_ERR("Persisting %s failed: %d", BRIDGE_CONFIG_KEY_AIS_MMSI, ret);
			return ret;
		}
	}

	if (!enabled_changed && !mmsi_changed) {
		return 0;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	ais = *next;
	k_mutex_unlock(&config_lock);

	if (listener != NULL) {
		listener();
	}
	return 0;
}

#ifdef CONFIG_ZTEST
int bridge_config_test_settings_set(const char *name, size_t len,
				    settings_read_cb read_cb, void *cb_arg)
{
	return bridge_config_settings_set(name, len, read_cb, cb_arg);
}

void bridge_config_test_reset(void)
{
	ais.filter_enabled = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_ENABLE);
	ais.own_mmsi = CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_MMSI;
	listener = NULL;
}
#endif
