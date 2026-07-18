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
#define BRIDGE_CONFIG_KEY_AIS "bridge/ais"
#define BRIDGE_CONFIG_KEY_STA "bridge/sta"

/*
 * Both AIS fields travel in one stored record so a save is atomic at the
 * NVS level: [0] enabled flag, [1..4] own MMSI in native byte order.
 */
#define AIS_RECORD_LEN 5U

/*
 * All STA fields travel in one fixed-size record for the same atomicity:
 * [0] enabled, [1] rotate_mac, [2] ssid_len, [3..34] ssid,
 * [35] psk_len, [36..98] psk.
 */
#define STA_RECORD_SSID_OFF 3U
#define STA_RECORD_PSK_LEN_OFF (STA_RECORD_SSID_OFF + BRIDGE_CONFIG_WIFI_SSID_MAX)
#define STA_RECORD_PSK_OFF (STA_RECORD_PSK_LEN_OFF + 1U)
#define STA_RECORD_LEN (STA_RECORD_PSK_OFF + BRIDGE_CONFIG_WIFI_PSK_MAX)

static void ais_record_pack(const struct bridge_config_ais *src, uint8_t record[AIS_RECORD_LEN])
{
	record[0] = src->filter_enabled ? 1U : 0U;
	memcpy(&record[1], &src->own_mmsi, sizeof(src->own_mmsi));
}

static void ais_record_unpack(const uint8_t record[AIS_RECORD_LEN], struct bridge_config_ais *dst)
{
	dst->filter_enabled = record[0] != 0U;
	memcpy(&dst->own_mmsi, &record[1], sizeof(dst->own_mmsi));
}

static void sta_record_pack(const struct bridge_config_sta *src, uint8_t record[STA_RECORD_LEN])
{
	memset(record, 0, STA_RECORD_LEN);
	record[0] = src->enabled ? 1U : 0U;
	record[1] = src->rotate_mac ? 1U : 0U;
	record[2] = (uint8_t)strlen(src->ssid);
	memcpy(&record[STA_RECORD_SSID_OFF], src->ssid, record[2]);
	record[STA_RECORD_PSK_LEN_OFF] = (uint8_t)strlen(src->psk);
	memcpy(&record[STA_RECORD_PSK_OFF], src->psk, record[STA_RECORD_PSK_LEN_OFF]);
}

static bool sta_record_unpack(const uint8_t record[STA_RECORD_LEN], struct bridge_config_sta *dst)
{
	uint8_t ssid_len = record[2];
	uint8_t psk_len = record[STA_RECORD_PSK_LEN_OFF];

	if (ssid_len > BRIDGE_CONFIG_WIFI_SSID_MAX || psk_len > BRIDGE_CONFIG_WIFI_PSK_MAX) {
		return false;
	}

	memset(dst, 0, sizeof(*dst));
	dst->enabled = record[0] != 0U;
	dst->rotate_mac = record[1] != 0U;
	memcpy(dst->ssid, &record[STA_RECORD_SSID_OFF], ssid_len);
	memcpy(dst->psk, &record[STA_RECORD_PSK_OFF], psk_len);
	return true;
}

static struct bridge_config_ais ais = {
	.filter_enabled = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_ENABLE),
	.own_mmsi = CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_MMSI,
};
static struct bridge_config_sta sta = {
	.enabled = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_STA_ENABLE),
	.rotate_mac = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_STA_ROTATE_MAC),
	.ssid = CONFIG_ESP_NMEA_BRIDGE_STA_SSID,
	.psk = CONFIG_ESP_NMEA_BRIDGE_STA_PSK,
};
static bool reboot_required;
static K_MUTEX_DEFINE(config_lock);
static bridge_config_listener_t listener;

static int bridge_config_settings_set(const char *name, size_t len,
				      settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(name, "ais") == 0) {
		uint8_t record[AIS_RECORD_LEN];
		struct bridge_config_ais stored;

		if (len != sizeof(record) || read_cb(cb_arg, record, sizeof(record)) < 0) {
			LOG_WRN("Ignoring malformed stored %s", name);
			return 0;
		}
		ais_record_unpack(record, &stored);
		if (stored.own_mmsi > BRIDGE_CONFIG_AIS_MMSI_MAX) {
			LOG_WRN("Ignoring stored %s with out-of-range MMSI", name);
			return 0;
		}
		k_mutex_lock(&config_lock, K_FOREVER);
		ais = stored;
		k_mutex_unlock(&config_lock);
		return 0;
	}

	if (strcmp(name, "sta") == 0) {
		uint8_t record[STA_RECORD_LEN];
		struct bridge_config_sta stored;

		if (len != sizeof(record) || read_cb(cb_arg, record, sizeof(record)) < 0 ||
		    !sta_record_unpack(record, &stored)) {
			LOG_WRN("Ignoring malformed stored %s", name);
			return 0;
		}
		k_mutex_lock(&config_lock, K_FOREVER);
		sta = stored;
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

void bridge_config_get_sta(struct bridge_config_sta *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	*out = sta;
	k_mutex_unlock(&config_lock);
}

int bridge_config_set_sta(const struct bridge_config_sta *next)
{
	uint8_t record[STA_RECORD_LEN];
	size_t psk_len;
	bool changed;
	int ret;

	/* Unterminated arrays would mean over-long fields; the terminated
	 * capacity already caps the SSID at 32 bytes.
	 */
	if (next == NULL || memchr(next->ssid, '\0', sizeof(next->ssid)) == NULL ||
	    memchr(next->psk, '\0', sizeof(next->psk)) == NULL) {
		return -EINVAL;
	}
	psk_len = strlen(next->psk);
	if (psk_len != 0U &&
	    (psk_len < BRIDGE_CONFIG_WIFI_PSK_MIN || psk_len > BRIDGE_CONFIG_WIFI_PSK_MAX)) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	changed = sta.enabled != next->enabled || sta.rotate_mac != next->rotate_mac ||
		  strcmp(sta.ssid, next->ssid) != 0 || strcmp(sta.psk, next->psk) != 0;
	k_mutex_unlock(&config_lock);

	if (!changed) {
		return 0;
	}

	sta_record_pack(next, record);
	ret = cfg_settings_save_one(BRIDGE_CONFIG_KEY_STA, record, sizeof(record));
	if (ret != 0) {
		LOG_ERR("Persisting %s failed: %d", BRIDGE_CONFIG_KEY_STA, ret);
		return ret;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	sta = *next;
	reboot_required = true;
	k_mutex_unlock(&config_lock);
	return 0;
}

bool bridge_config_reboot_required(void)
{
	return reboot_required;
}

void bridge_config_set_listener(bridge_config_listener_t new_listener)
{
	listener = new_listener;
}

int bridge_config_set_ais(const struct bridge_config_ais *next)
{
	uint8_t record[AIS_RECORD_LEN];
	bool changed;
	int ret;

	if (next == NULL || next->own_mmsi > BRIDGE_CONFIG_AIS_MMSI_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	changed = ais.filter_enabled != next->filter_enabled || ais.own_mmsi != next->own_mmsi;
	k_mutex_unlock(&config_lock);

	if (!changed) {
		return 0;
	}

	ais_record_pack(next, record);
	ret = cfg_settings_save_one(BRIDGE_CONFIG_KEY_AIS, record, sizeof(record));
	if (ret != 0) {
		LOG_ERR("Persisting %s failed: %d", BRIDGE_CONFIG_KEY_AIS, ret);
		return ret;
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
	memset(&sta, 0, sizeof(sta));
	sta.enabled = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_STA_ENABLE);
	sta.rotate_mac = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_STA_ROTATE_MAC);
	strcpy(sta.ssid, CONFIG_ESP_NMEA_BRIDGE_STA_SSID);
	strcpy(sta.psk, CONFIG_ESP_NMEA_BRIDGE_STA_PSK);
	reboot_required = false;
	listener = NULL;
}
#endif
