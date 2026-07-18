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
int bridge_config_test_settings_load_subtree_direct(const char *subtree,
						    settings_load_direct_cb cb, void *param);
int bridge_config_test_settings_delete(const char *name);
#define cfg_settings_subsys_init bridge_config_test_settings_subsys_init
#define cfg_settings_load_subtree bridge_config_test_settings_load_subtree
#define cfg_settings_save_one bridge_config_test_settings_save_one
#define cfg_settings_load_subtree_direct bridge_config_test_settings_load_subtree_direct
#define cfg_settings_delete bridge_config_test_settings_delete
#else
#define cfg_settings_subsys_init settings_subsys_init
#define cfg_settings_load_subtree settings_load_subtree
#define cfg_settings_save_one settings_save_one
#define cfg_settings_load_subtree_direct settings_load_subtree_direct
#define cfg_settings_delete settings_delete
#endif

LOG_MODULE_REGISTER(bridge_config, LOG_LEVEL_INF);

#define BRIDGE_CONFIG_SUBTREE "bridge"
#define BRIDGE_CONFIG_KEY_AIS "bridge/ais"
#define BRIDGE_CONFIG_KEY_STA "bridge/sta"
#define BRIDGE_CONFIG_KEY_TCP "bridge/tcp"
#define BRIDGE_CONFIG_KEY_AP "bridge/ap"
#define BRIDGE_CONFIG_KEY_SYS "bridge/sys"

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

	if (ssid_len > BRIDGE_CONFIG_WIFI_SSID_MAX ||
	    (psk_len != 0U && (psk_len < BRIDGE_CONFIG_WIFI_PSK_MIN ||
			       psk_len > BRIDGE_CONFIG_WIFI_PSK_MAX))) {
		return false;
	}

	memset(dst, 0, sizeof(*dst));
	dst->enabled = record[0] != 0U;
	dst->rotate_mac = record[1] != 0U;
	memcpy(dst->ssid, &record[STA_RECORD_SSID_OFF], ssid_len);
	memcpy(dst->psk, &record[STA_RECORD_PSK_OFF], psk_len);
	/* Embedded NULs would silently shorten the credentials in use. */
	return strlen(dst->ssid) == ssid_len && strlen(dst->psk) == psk_len &&
	       bridge_config_text_valid(dst->ssid);
}

/*
 * Both AP fields travel in one fixed-size record for the same atomicity:
 * [0] ssid_len, [1..32] ssid, [33] psk_len, [34..96] psk.
 */
#define AP_RECORD_SSID_OFF 1U
#define AP_RECORD_PSK_LEN_OFF (AP_RECORD_SSID_OFF + BRIDGE_CONFIG_WIFI_SSID_MAX)
#define AP_RECORD_PSK_OFF (AP_RECORD_PSK_LEN_OFF + 1U)
#define AP_RECORD_LEN (AP_RECORD_PSK_OFF + BRIDGE_CONFIG_WIFI_PSK_MAX)

static void ap_record_pack(const struct bridge_config_ap *src, uint8_t record[AP_RECORD_LEN])
{
	memset(record, 0, AP_RECORD_LEN);
	record[0] = (uint8_t)strlen(src->ssid);
	memcpy(&record[AP_RECORD_SSID_OFF], src->ssid, record[0]);
	record[AP_RECORD_PSK_LEN_OFF] = (uint8_t)strlen(src->psk);
	memcpy(&record[AP_RECORD_PSK_OFF], src->psk, record[AP_RECORD_PSK_LEN_OFF]);
}

static bool ap_record_unpack(const uint8_t record[AP_RECORD_LEN], struct bridge_config_ap *dst)
{
	uint8_t ssid_len = record[0];
	uint8_t psk_len = record[AP_RECORD_PSK_LEN_OFF];

	if (ssid_len == 0U || ssid_len > BRIDGE_CONFIG_WIFI_SSID_MAX ||
	    (psk_len != 0U && (psk_len < BRIDGE_CONFIG_WIFI_PSK_MIN ||
			       psk_len > BRIDGE_CONFIG_WIFI_PSK_MAX))) {
		return false;
	}

	memset(dst, 0, sizeof(*dst));
	memcpy(dst->ssid, &record[AP_RECORD_SSID_OFF], ssid_len);
	memcpy(dst->psk, &record[AP_RECORD_PSK_OFF], psk_len);
	/* An embedded NUL could silently turn the rescue AP into an open one. */
	return strlen(dst->ssid) == ssid_len && strlen(dst->psk) == psk_len &&
	       bridge_config_text_valid(dst->ssid);
}

/* The hostname travels as [0] hostname_len, [1..32] hostname. */
#define SYS_RECORD_HOSTNAME_OFF 1U
#define SYS_RECORD_LEN (SYS_RECORD_HOSTNAME_OFF + BRIDGE_CONFIG_HOSTNAME_MAX)

static void sys_record_pack(const struct bridge_config_system *src,
			    uint8_t record[SYS_RECORD_LEN])
{
	memset(record, 0, SYS_RECORD_LEN);
	record[0] = (uint8_t)strlen(src->hostname);
	memcpy(&record[SYS_RECORD_HOSTNAME_OFF], src->hostname, record[0]);
}

static bool sys_record_unpack(const uint8_t record[SYS_RECORD_LEN],
			      struct bridge_config_system *dst)
{
	uint8_t hostname_len = record[0];

	if (hostname_len > BRIDGE_CONFIG_HOSTNAME_MAX) {
		return false;
	}

	memset(dst, 0, sizeof(*dst));
	memcpy(dst->hostname, &record[SYS_RECORD_HOSTNAME_OFF], hostname_len);
	return strlen(dst->hostname) == hostname_len &&
	       bridge_config_hostname_valid(dst->hostname);
}

/*
 * All TCP client fields travel in one fixed-size record for the same
 * atomicity: [0] enabled, [1] host_len, [2..16] host, [17..18] port in
 * native byte order.
 */
#define TCP_RECORD_HOST_OFF 2U
#define TCP_RECORD_PORT_OFF (TCP_RECORD_HOST_OFF + BRIDGE_CONFIG_TCP_CLIENT_HOST_MAX)
#define TCP_RECORD_LEN (TCP_RECORD_PORT_OFF + 2U)

static void tcp_record_pack(const struct bridge_config_tcp_client *src,
			    uint8_t record[TCP_RECORD_LEN])
{
	memset(record, 0, TCP_RECORD_LEN);
	record[0] = src->enabled ? 1U : 0U;
	record[1] = (uint8_t)strlen(src->host);
	memcpy(&record[TCP_RECORD_HOST_OFF], src->host, record[1]);
	memcpy(&record[TCP_RECORD_PORT_OFF], &src->port, sizeof(src->port));
}

static bool tcp_record_unpack(const uint8_t record[TCP_RECORD_LEN],
			      struct bridge_config_tcp_client *dst)
{
	uint8_t host_len = record[1];

	if (host_len > BRIDGE_CONFIG_TCP_CLIENT_HOST_MAX) {
		return false;
	}

	memset(dst, 0, sizeof(*dst));
	dst->enabled = record[0] != 0U;
	memcpy(dst->host, &record[TCP_RECORD_HOST_OFF], host_len);
	memcpy(&dst->port, &record[TCP_RECORD_PORT_OFF], sizeof(dst->port));
	return bridge_config_tcp_client_host_valid(dst->host) && dst->port != 0U;
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
static struct bridge_config_ap ap = {
	.ssid = CONFIG_ESP_NMEA_BRIDGE_AP_SSID,
	.psk = CONFIG_ESP_NMEA_BRIDGE_AP_PSK,
};
static struct bridge_config_system system_cfg = {
	.hostname = CONFIG_NET_HOSTNAME,
};
static struct bridge_config_tcp_client tcp_client = {
	.enabled = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_ENABLE),
	.host = CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST,
	.port = CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_PORT,
};
static bool reboot_required;
/*
 * After a factory reset the stored overrides are gone but the in-RAM values
 * keep running until reboot. A later save that matches RAM would otherwise
 * be skipped as unchanged and silently revert at the next boot, so once a
 * reset happened every save persists until reboot.
 */
static bool factory_reset_pending;
static K_MUTEX_DEFINE(config_lock);
static bridge_config_listener_t listener;

/* Over-long Kconfig defaults would silently truncate or lose termination. */
BUILD_ASSERT(sizeof(CONFIG_ESP_NMEA_BRIDGE_STA_SSID) <= BRIDGE_CONFIG_WIFI_SSID_MAX + 1,
	     "STA SSID default exceeds 32 bytes");
BUILD_ASSERT(sizeof(CONFIG_ESP_NMEA_BRIDGE_STA_PSK) <= BRIDGE_CONFIG_WIFI_PSK_MAX + 1,
	     "STA PSK default exceeds 63 characters");
BUILD_ASSERT(sizeof(CONFIG_ESP_NMEA_BRIDGE_AP_SSID) <= BRIDGE_CONFIG_WIFI_SSID_MAX + 1,
	     "AP SSID default exceeds 32 bytes");
BUILD_ASSERT(sizeof(CONFIG_ESP_NMEA_BRIDGE_AP_PSK) <= BRIDGE_CONFIG_WIFI_PSK_MAX + 1,
	     "AP PSK default exceeds 63 characters");
BUILD_ASSERT(sizeof(CONFIG_NET_HOSTNAME) <= BRIDGE_CONFIG_HOSTNAME_MAX + 1,
	     "hostname default exceeds 32 characters");
BUILD_ASSERT(sizeof(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST) <=
	     BRIDGE_CONFIG_TCP_CLIENT_HOST_MAX + 1,
	     "TCP client host default exceeds 15 characters");

static int bridge_config_settings_set(const char *name, size_t len,
				      settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(name, "ais") == 0) {
		uint8_t record[AIS_RECORD_LEN];
		struct bridge_config_ais stored;

		if (len != sizeof(record) || read_cb(cb_arg, record, sizeof(record)) != (ssize_t)sizeof(record)) {
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

	if (strcmp(name, "tcp") == 0) {
		uint8_t record[TCP_RECORD_LEN];
		struct bridge_config_tcp_client stored;

		if (len != sizeof(record) || read_cb(cb_arg, record, sizeof(record)) != (ssize_t)sizeof(record) ||
		    !tcp_record_unpack(record, &stored)) {
			LOG_WRN("Ignoring malformed stored %s", name);
			return 0;
		}
		k_mutex_lock(&config_lock, K_FOREVER);
		tcp_client = stored;
		k_mutex_unlock(&config_lock);
		return 0;
	}

	if (strcmp(name, "ap") == 0) {
		uint8_t record[AP_RECORD_LEN];
		struct bridge_config_ap stored;

		if (len != sizeof(record) || read_cb(cb_arg, record, sizeof(record)) != (ssize_t)sizeof(record) ||
		    !ap_record_unpack(record, &stored)) {
			LOG_WRN("Ignoring malformed stored %s", name);
			return 0;
		}
		k_mutex_lock(&config_lock, K_FOREVER);
		ap = stored;
		k_mutex_unlock(&config_lock);
		return 0;
	}

	if (strcmp(name, "sys") == 0) {
		uint8_t record[SYS_RECORD_LEN];
		struct bridge_config_system stored;

		if (len != sizeof(record) || read_cb(cb_arg, record, sizeof(record)) != (ssize_t)sizeof(record) ||
		    !sys_record_unpack(record, &stored)) {
			LOG_WRN("Ignoring malformed stored %s", name);
			return 0;
		}
		k_mutex_lock(&config_lock, K_FOREVER);
		system_cfg = stored;
		k_mutex_unlock(&config_lock);
		return 0;
	}

	if (strcmp(name, "sta") == 0) {
		uint8_t record[STA_RECORD_LEN];
		struct bridge_config_sta stored;

		if (len != sizeof(record) || read_cb(cb_arg, record, sizeof(record)) != (ssize_t)sizeof(record) ||
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

void bridge_config_get_tcp_client(struct bridge_config_tcp_client *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	*out = tcp_client;
	k_mutex_unlock(&config_lock);
}

int bridge_config_set_tcp_client(const struct bridge_config_tcp_client *next)
{
	uint8_t record[TCP_RECORD_LEN];
	bool changed;
	int ret;

	if (next == NULL || memchr(next->host, '\0', sizeof(next->host)) == NULL ||
	    !bridge_config_tcp_client_host_valid(next->host) || next->port == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	changed = factory_reset_pending || tcp_client.enabled != next->enabled ||
		  tcp_client.port != next->port || strcmp(tcp_client.host, next->host) != 0;
	k_mutex_unlock(&config_lock);

	if (!changed) {
		return 0;
	}

	tcp_record_pack(next, record);
	ret = cfg_settings_save_one(BRIDGE_CONFIG_KEY_TCP, record, sizeof(record));
	if (ret != 0) {
		LOG_ERR("Persisting %s failed: %d", BRIDGE_CONFIG_KEY_TCP, ret);
		return ret;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	tcp_client = *next;
	k_mutex_unlock(&config_lock);

	if (listener != NULL) {
		listener();
	}
	return 0;
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
	    memchr(next->psk, '\0', sizeof(next->psk)) == NULL ||
	    !bridge_config_text_valid(next->ssid)) {
		return -EINVAL;
	}
	psk_len = strlen(next->psk);
	if (psk_len != 0U &&
	    (psk_len < BRIDGE_CONFIG_WIFI_PSK_MIN || psk_len > BRIDGE_CONFIG_WIFI_PSK_MAX)) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	changed = factory_reset_pending || sta.enabled != next->enabled ||
		  sta.rotate_mac != next->rotate_mac ||
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

void bridge_config_get_ap(struct bridge_config_ap *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	*out = ap;
	k_mutex_unlock(&config_lock);
}

int bridge_config_set_ap(const struct bridge_config_ap *next)
{
	uint8_t record[AP_RECORD_LEN];
	size_t ssid_len;
	size_t psk_len;
	bool changed;
	int ret;

	if (next == NULL || memchr(next->ssid, '\0', sizeof(next->ssid)) == NULL ||
	    memchr(next->psk, '\0', sizeof(next->psk)) == NULL ||
	    !bridge_config_text_valid(next->ssid)) {
		return -EINVAL;
	}
	ssid_len = strlen(next->ssid);
	psk_len = strlen(next->psk);
	if (ssid_len == 0U ||
	    (psk_len != 0U &&
	     (psk_len < BRIDGE_CONFIG_WIFI_PSK_MIN || psk_len > BRIDGE_CONFIG_WIFI_PSK_MAX))) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	changed = factory_reset_pending || strcmp(ap.ssid, next->ssid) != 0 ||
		  strcmp(ap.psk, next->psk) != 0;
	k_mutex_unlock(&config_lock);

	if (!changed) {
		return 0;
	}

	ap_record_pack(next, record);
	ret = cfg_settings_save_one(BRIDGE_CONFIG_KEY_AP, record, sizeof(record));
	if (ret != 0) {
		LOG_ERR("Persisting %s failed: %d", BRIDGE_CONFIG_KEY_AP, ret);
		return ret;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	ap = *next;
	reboot_required = true;
	k_mutex_unlock(&config_lock);
	return 0;
}

void bridge_config_get_system(struct bridge_config_system *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	*out = system_cfg;
	k_mutex_unlock(&config_lock);
}

int bridge_config_set_system(const struct bridge_config_system *next)
{
	uint8_t record[SYS_RECORD_LEN];
	bool changed;
	int ret;

	if (next == NULL || memchr(next->hostname, '\0', sizeof(next->hostname)) == NULL ||
	    !bridge_config_hostname_valid(next->hostname)) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	changed = factory_reset_pending || strcmp(system_cfg.hostname, next->hostname) != 0;
	k_mutex_unlock(&config_lock);

	if (!changed) {
		return 0;
	}

	sys_record_pack(next, record);
	ret = cfg_settings_save_one(BRIDGE_CONFIG_KEY_SYS, record, sizeof(record));
	if (ret != 0) {
		LOG_ERR("Persisting %s failed: %d", BRIDGE_CONFIG_KEY_SYS, ret);
		return ret;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	system_cfg = *next;
	reboot_required = true;
	k_mutex_unlock(&config_lock);
	return 0;
}

/*
 * One enumeration pass collects at most this many key names; more stored
 * keys than that just take another delete-and-rescan pass.
 */
#define RESET_KEYS_PER_PASS 8U
#define RESET_KEY_NAME_MAX 32U

struct reset_key_batch {
	char names[RESET_KEYS_PER_PASS][RESET_KEY_NAME_MAX + 1];
	size_t count;
	bool more;
};

static int reset_collect_cb(const char *key, size_t len, settings_read_cb read_cb,
			    void *cb_arg, void *param)
{
	struct reset_key_batch *batch = param;

	ARG_UNUSED(len);
	ARG_UNUSED(read_cb);
	ARG_UNUSED(cb_arg);

	if (strlen(key) > RESET_KEY_NAME_MAX) {
		return -ENAMETOOLONG;
	}
	if (batch->count == ARRAY_SIZE(batch->names)) {
		/* Stops this pass; the caller deletes the batch and rescans. */
		batch->more = true;
		return 1;
	}
	strcpy(batch->names[batch->count++], key);
	return 0;
}

int bridge_config_factory_reset(void)
{
	struct reset_key_batch batch;
	int ret;

	do {
		batch.count = 0;
		batch.more = false;
		ret = cfg_settings_load_subtree_direct(BRIDGE_CONFIG_SUBTREE,
						       reset_collect_cb, &batch);
		if (ret != 0 && !batch.more) {
			LOG_ERR("Enumerating stored configuration failed: %d", ret);
			return ret;
		}
		for (size_t i = 0; i < batch.count; i++) {
			char name[sizeof(BRIDGE_CONFIG_SUBTREE) + RESET_KEY_NAME_MAX + 1];

			(void)snprintk(name, sizeof(name), "%s/%s", BRIDGE_CONFIG_SUBTREE,
				       batch.names[i]);
			ret = cfg_settings_delete(name);
			if (ret != 0) {
				LOG_ERR("Deleting %s failed: %d", name, ret);
				return ret;
			}
		}
	} while (batch.more);

	LOG_INF("Factory reset: stored configuration overrides deleted");
	k_mutex_lock(&config_lock, K_FOREVER);
	reboot_required = true;
	factory_reset_pending = true;
	k_mutex_unlock(&config_lock);
	return 0;
}

bool bridge_config_reboot_required(void)
{
	bool required;

	k_mutex_lock(&config_lock, K_FOREVER);
	required = reboot_required;
	k_mutex_unlock(&config_lock);
	return required;
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
	changed = factory_reset_pending || ais.filter_enabled != next->filter_enabled ||
		  ais.own_mmsi != next->own_mmsi;
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
	memset(&ap, 0, sizeof(ap));
	strcpy(ap.ssid, CONFIG_ESP_NMEA_BRIDGE_AP_SSID);
	strcpy(ap.psk, CONFIG_ESP_NMEA_BRIDGE_AP_PSK);
	memset(&system_cfg, 0, sizeof(system_cfg));
	strcpy(system_cfg.hostname, CONFIG_NET_HOSTNAME);
	memset(&tcp_client, 0, sizeof(tcp_client));
	tcp_client.enabled = IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_ENABLE);
	strcpy(tcp_client.host, CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST);
	tcp_client.port = CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_PORT;
	reboot_required = false;
	factory_reset_pending = false;
	listener = NULL;
}
#endif
