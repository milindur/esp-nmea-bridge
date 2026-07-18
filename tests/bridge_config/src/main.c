#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "bridge_config.h"

int bridge_config_test_settings_set(const char *name, size_t len,
				    settings_read_cb read_cb, void *cb_arg);
void bridge_config_test_reset(void);

#define TEST_DEFAULT_MMSI 123456789U

struct saved_entry {
	char name[40];
	uint8_t value[128];
	size_t len;
};

static struct saved_entry saved[4];
static int save_count;
static int save_ret;
static int subsys_init_count;
static int load_subtree_count;
static char loaded_subtree[16];
static int listener_count;

/* Fake persistent keystore below "bridge/" for factory-reset enumeration. */
#define FAKE_STORE_MAX_KEYS 12
static char stored_keys[FAKE_STORE_MAX_KEYS][SETTINGS_MAX_NAME_LEN + 16];
static int stored_key_count;
static int delete_count;
/* Index of the delete call that fails with -EIO; -1 means none fails. */
static int delete_fail_at;
static int load_direct_count;

int bridge_config_test_settings_subsys_init(void)
{
	subsys_init_count++;
	return 0;
}

int bridge_config_test_settings_load_subtree(const char *subtree)
{
	load_subtree_count++;
	strncpy(loaded_subtree, subtree, sizeof(loaded_subtree) - 1U);
	return 0;
}

int bridge_config_test_settings_save_one(const char *name, const void *value, size_t val_len)
{
	if (save_ret != 0) {
		return save_ret;
	}
	if (save_count < ARRAY_SIZE(saved)) {
		strncpy(saved[save_count].name, name, sizeof(saved[save_count].name) - 1U);
		memcpy(saved[save_count].value, value, MIN(val_len, sizeof(saved[save_count].value)));
		saved[save_count].len = val_len;
	}
	save_count++;
	return 0;
}

/*
 * Faithful to Zephyr: a nonzero callback return stops the iteration (as the
 * NVS backend does), but the API itself always reports success.
 */
int bridge_config_test_settings_load_subtree_direct(const char *subtree,
						    settings_load_direct_cb cb, void *param)
{
	load_direct_count++;
	zassert_str_equal(subtree, "bridge");
	for (int i = 0; i < stored_key_count; i++) {
		if (cb(stored_keys[i], 0U, NULL, NULL, param) != 0) {
			break;
		}
	}
	return 0;
}

int bridge_config_test_settings_delete(const char *name)
{
	if (delete_count == delete_fail_at) {
		return -EIO;
	}
	zassert_true(strncmp(name, "bridge/", 7U) == 0, "delete outside subtree: %s", name);
	delete_count++;
	for (int i = 0; i < stored_key_count; i++) {
		if (strcmp(stored_keys[i], name + 7) == 0) {
			memmove(&stored_keys[i], &stored_keys[i + 1],
				(size_t)(stored_key_count - i - 1) * sizeof(stored_keys[0]));
			stored_key_count--;
			return 0;
		}
	}
	zassert_unreachable("deleted unknown key %s", name);
	return 0;
}

static void store_key(const char *name)
{
	zassert_true(stored_key_count < FAKE_STORE_MAX_KEYS);
	strcpy(stored_keys[stored_key_count++], name);
}

static ssize_t stored_read_cb(void *cb_arg, void *data, size_t len)
{
	memcpy(data, cb_arg, len);
	return (ssize_t)len;
}

static ssize_t failing_read_cb(void *cb_arg, void *data, size_t len)
{
	ARG_UNUSED(cb_arg);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	return -EIO;
}

/* Short read: fills only half the record and reports that. */
static ssize_t partial_read_cb(void *cb_arg, void *data, size_t len)
{
	memcpy(data, cb_arg, len / 2U);
	return (ssize_t)(len / 2U);
}

static void count_listener(void)
{
	listener_count++;
}

#define AIS_RECORD_LEN 5U

static void make_record(bool enabled, uint32_t mmsi, uint8_t record[AIS_RECORD_LEN])
{
	record[0] = enabled ? 1U : 0U;
	memcpy(&record[1], &mmsi, sizeof(mmsi));
}

static int store_record(bool enabled, uint32_t mmsi)
{
	uint8_t record[AIS_RECORD_LEN];

	make_record(enabled, mmsi, record);
	return bridge_config_test_settings_set("ais", sizeof(record), stored_read_cb, record);
}

static void reset_harness(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(saved, 0, sizeof(saved));
	save_count = 0;
	save_ret = 0;
	subsys_init_count = 0;
	load_subtree_count = 0;
	loaded_subtree[0] = '\0';
	listener_count = 0;
	memset(stored_keys, 0, sizeof(stored_keys));
	stored_key_count = 0;
	delete_count = 0;
	delete_fail_at = -1;
	load_direct_count = 0;
	bridge_config_test_reset();
}

ZTEST(bridge_config, test_defaults_come_from_kconfig)
{
	struct bridge_config_ais ais;

	bridge_config_get_ais(&ais);

	zassert_true(ais.filter_enabled);
	zassert_equal(ais.own_mmsi, TEST_DEFAULT_MMSI);
}

ZTEST(bridge_config, test_init_loads_bridge_subtree)
{
	zassert_equal(bridge_config_init(), 0);
	zassert_equal(subsys_init_count, 1);
	zassert_equal(load_subtree_count, 1);
	zassert_str_equal(loaded_subtree, "bridge");
}

ZTEST(bridge_config, test_stored_values_override_defaults)
{
	struct bridge_config_ais ais;

	zassert_equal(store_record(false, 211000000U), 0);

	bridge_config_get_ais(&ais);
	zassert_false(ais.filter_enabled);
	zassert_equal(ais.own_mmsi, 211000000U);
}

ZTEST(bridge_config, test_malformed_stored_values_keep_defaults)
{
	struct bridge_config_ais ais;
	uint8_t short_value = 1U;
	uint8_t record[AIS_RECORD_LEN];

	zassert_equal(store_record(true, BRIDGE_CONFIG_AIS_MMSI_MAX + 1U), 0);
	zassert_equal(bridge_config_test_settings_set("ais", sizeof(short_value),
						      stored_read_cb, &short_value), 0);
	make_record(true, 1U, record);
	zassert_equal(bridge_config_test_settings_set("ais", sizeof(record),
						      failing_read_cb, record), 0);

	bridge_config_get_ais(&ais);
	zassert_true(ais.filter_enabled);
	zassert_equal(ais.own_mmsi, TEST_DEFAULT_MMSI);
}

ZTEST(bridge_config, test_unknown_key_is_rejected)
{
	uint32_t value = 1U;

	zassert_equal(bridge_config_test_settings_set("unknown", sizeof(value),
						      stored_read_cb, &value), -ENOENT);
}

ZTEST(bridge_config, test_set_persists_single_atomic_record)
{
	struct bridge_config_ais next = {
		.filter_enabled = false,
		.own_mmsi = 211000000U,
	};
	struct bridge_config_ais ais;
	uint8_t expected[AIS_RECORD_LEN];

	zassert_equal(bridge_config_set_ais(&next), 0);

	make_record(false, 211000000U, expected);
	zassert_equal(save_count, 1);
	zassert_str_equal(saved[0].name, "bridge/ais");
	zassert_equal(saved[0].len, sizeof(expected));
	zassert_mem_equal(saved[0].value, expected, sizeof(expected));

	bridge_config_get_ais(&ais);
	zassert_false(ais.filter_enabled);
	zassert_equal(ais.own_mmsi, 211000000U);
}

ZTEST(bridge_config, test_set_rejects_out_of_range_mmsi_without_saving)
{
	struct bridge_config_ais next = {
		.filter_enabled = true,
		.own_mmsi = BRIDGE_CONFIG_AIS_MMSI_MAX + 1U,
	};
	struct bridge_config_ais ais;

	zassert_equal(bridge_config_set_ais(&next), -EINVAL);
	zassert_equal(save_count, 0);

	bridge_config_get_ais(&ais);
	zassert_equal(ais.own_mmsi, TEST_DEFAULT_MMSI);
}

ZTEST(bridge_config, test_set_unchanged_config_is_a_no_op)
{
	struct bridge_config_ais next = {
		.filter_enabled = true,
		.own_mmsi = TEST_DEFAULT_MMSI,
	};

	bridge_config_set_listener(count_listener);
	zassert_equal(bridge_config_set_ais(&next), 0);
	zassert_equal(save_count, 0);
	zassert_equal(listener_count, 0);
}

ZTEST(bridge_config, test_listener_fires_after_change)
{
	struct bridge_config_ais next = {
		.filter_enabled = false,
		.own_mmsi = 211000000U,
	};

	bridge_config_set_listener(count_listener);
	zassert_equal(bridge_config_set_ais(&next), 0);
	zassert_equal(listener_count, 1);
}

ZTEST(bridge_config, test_save_failure_keeps_previous_config)
{
	struct bridge_config_ais next = {
		.filter_enabled = false,
		.own_mmsi = 211000000U,
	};
	struct bridge_config_ais ais;

	save_ret = -EIO;
	bridge_config_set_listener(count_listener);

	zassert_equal(bridge_config_set_ais(&next), -EIO);
	zassert_equal(listener_count, 0);

	bridge_config_get_ais(&ais);
	zassert_true(ais.filter_enabled);
	zassert_equal(ais.own_mmsi, TEST_DEFAULT_MMSI);
}

/* Mirrors the packed layout in bridge_config.c: flags, ssid_len, ssid, psk_len, psk. */
#define STA_RECORD_LEN 99U

static void make_sta_record(bool enabled, bool rotate_mac, const char *ssid, const char *psk,
			    uint8_t record[STA_RECORD_LEN])
{
	memset(record, 0, STA_RECORD_LEN);
	record[0] = enabled ? 1U : 0U;
	record[1] = rotate_mac ? 1U : 0U;
	record[2] = (uint8_t)strlen(ssid);
	memcpy(&record[3], ssid, strlen(ssid));
	record[35] = (uint8_t)strlen(psk);
	memcpy(&record[36], psk, strlen(psk));
}

static int store_sta_record(bool enabled, bool rotate_mac, const char *ssid, const char *psk)
{
	uint8_t record[STA_RECORD_LEN];

	make_sta_record(enabled, rotate_mac, ssid, psk, record);
	return bridge_config_test_settings_set("sta", sizeof(record), stored_read_cb, record);
}

static void fill_sta(struct bridge_config_sta *sta, bool enabled, bool rotate_mac,
		     const char *ssid, const char *psk)
{
	memset(sta, 0, sizeof(*sta));
	sta->enabled = enabled;
	sta->rotate_mac = rotate_mac;
	strcpy(sta->ssid, ssid);
	strcpy(sta->psk, psk);
}

ZTEST(bridge_config, test_sta_defaults_come_from_kconfig)
{
	struct bridge_config_sta sta;

	bridge_config_get_sta(&sta);

	zassert_true(sta.enabled);
	zassert_true(sta.rotate_mac);
	zassert_str_equal(sta.ssid, "BoatNet");
	zassert_str_equal(sta.psk, "anchor123");
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_stored_sta_overrides_defaults_without_reboot_flag)
{
	struct bridge_config_sta sta;

	zassert_equal(store_sta_record(false, false, "Marina", "harbour99"), 0);

	bridge_config_get_sta(&sta);
	zassert_false(sta.enabled);
	zassert_false(sta.rotate_mac);
	zassert_str_equal(sta.ssid, "Marina");
	zassert_str_equal(sta.psk, "harbour99");
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_malformed_stored_sta_keeps_defaults)
{
	struct bridge_config_sta sta;
	uint8_t record[STA_RECORD_LEN];
	uint8_t short_value = 1U;

	zassert_equal(bridge_config_test_settings_set("sta", sizeof(short_value),
						      stored_read_cb, &short_value), 0);

	make_sta_record(true, true, "Marina", "harbour99", record);
	record[2] = 33U; /* ssid_len out of range */
	zassert_equal(bridge_config_test_settings_set("sta", sizeof(record),
						      stored_read_cb, record), 0);

	make_sta_record(true, true, "Marina", "harbour99", record);
	record[35] = 64U; /* psk_len out of range */
	zassert_equal(bridge_config_test_settings_set("sta", sizeof(record),
						      stored_read_cb, record), 0);

	make_sta_record(true, true, "Marina", "harbour99", record);
	record[35] = 3U; /* psk_len below the WPA2 minimum */
	zassert_equal(bridge_config_test_settings_set("sta", sizeof(record),
						      stored_read_cb, record), 0);

	make_sta_record(true, true, "Marina", "harbour99", record);
	zassert_equal(bridge_config_test_settings_set("sta", sizeof(record),
						      failing_read_cb, record), 0);

	bridge_config_get_sta(&sta);
	zassert_str_equal(sta.ssid, "BoatNet");
	zassert_str_equal(sta.psk, "anchor123");
}

ZTEST(bridge_config, test_set_sta_persists_record_and_flags_reboot)
{
	struct bridge_config_sta next;
	struct bridge_config_sta sta;
	uint8_t expected[STA_RECORD_LEN];

	fill_sta(&next, false, false, "Marina", "harbour99");
	zassert_equal(bridge_config_set_sta(&next), 0);

	make_sta_record(false, false, "Marina", "harbour99", expected);
	zassert_equal(save_count, 1);
	zassert_str_equal(saved[0].name, "bridge/sta");
	zassert_equal(saved[0].len, sizeof(expected));
	zassert_mem_equal(saved[0].value, expected, sizeof(expected));

	bridge_config_get_sta(&sta);
	zassert_false(sta.enabled);
	zassert_str_equal(sta.ssid, "Marina");
	zassert_str_equal(sta.psk, "harbour99");
	zassert_true(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_set_sta_accepts_empty_ssid_and_psk)
{
	struct bridge_config_sta next;

	fill_sta(&next, false, true, "", "");
	zassert_equal(bridge_config_set_sta(&next), 0);
	zassert_equal(save_count, 1);
}

ZTEST(bridge_config, test_set_sta_rejects_invalid_fields_without_saving)
{
	struct bridge_config_sta next;
	struct bridge_config_sta sta;

	/* SSID filling the whole array without terminator = longer than 32 bytes */
	fill_sta(&next, true, true, "", "harbour99");
	memset(next.ssid, 'a', sizeof(next.ssid));
	zassert_equal(bridge_config_set_sta(&next), -EINVAL);

	/* PSK shorter than 8 characters */
	fill_sta(&next, true, true, "Marina", "short");
	zassert_equal(bridge_config_set_sta(&next), -EINVAL);

	zassert_equal(save_count, 0);
	zassert_false(bridge_config_reboot_required());

	bridge_config_get_sta(&sta);
	zassert_str_equal(sta.ssid, "BoatNet");
}

ZTEST(bridge_config, test_set_sta_unchanged_is_a_no_op)
{
	struct bridge_config_sta next;

	fill_sta(&next, true, true, "BoatNet", "anchor123");
	zassert_equal(bridge_config_set_sta(&next), 0);
	zassert_equal(save_count, 0);
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_set_sta_save_failure_keeps_previous_config)
{
	struct bridge_config_sta next;
	struct bridge_config_sta sta;

	save_ret = -EIO;
	fill_sta(&next, false, false, "Marina", "harbour99");

	zassert_equal(bridge_config_set_sta(&next), -EIO);
	zassert_false(bridge_config_reboot_required());

	bridge_config_get_sta(&sta);
	zassert_true(sta.enabled);
	zassert_str_equal(sta.ssid, "BoatNet");
}

ZTEST(bridge_config, test_set_sta_does_not_notify_live_listener)
{
	struct bridge_config_sta next;

	bridge_config_set_listener(count_listener);
	fill_sta(&next, false, false, "Marina", "harbour99");
	zassert_equal(bridge_config_set_sta(&next), 0);
	zassert_equal(listener_count, 0);
}

/* Mirrors the packed layout in bridge_config.c: enabled, host_len, host, port. */
#define TCP_RECORD_LEN 19U

static void make_tcp_record(bool enabled, const char *host, uint16_t port,
			    uint8_t record[TCP_RECORD_LEN])
{
	memset(record, 0, TCP_RECORD_LEN);
	record[0] = enabled ? 1U : 0U;
	record[1] = (uint8_t)strlen(host);
	memcpy(&record[2], host, strlen(host));
	memcpy(&record[17], &port, sizeof(port));
}

static int store_tcp_record(bool enabled, const char *host, uint16_t port)
{
	uint8_t record[TCP_RECORD_LEN];

	make_tcp_record(enabled, host, port, record);
	return bridge_config_test_settings_set("tcp", sizeof(record), stored_read_cb, record);
}

static void fill_tcp(struct bridge_config_tcp_client *tcp, bool enabled, const char *host,
		     uint16_t port)
{
	memset(tcp, 0, sizeof(*tcp));
	tcp->enabled = enabled;
	strcpy(tcp->host, host);
	tcp->port = port;
}

ZTEST(bridge_config, test_tcp_defaults_come_from_kconfig)
{
	struct bridge_config_tcp_client tcp;

	bridge_config_get_tcp_client(&tcp);

	zassert_true(tcp.enabled);
	zassert_str_equal(tcp.host, "");
	zassert_equal(tcp.port, 10110);
}

ZTEST(bridge_config, test_stored_tcp_overrides_defaults_without_reboot_flag)
{
	struct bridge_config_tcp_client tcp;

	zassert_equal(store_tcp_record(false, "192.168.1.50", 2000), 0);

	bridge_config_get_tcp_client(&tcp);
	zassert_false(tcp.enabled);
	zassert_str_equal(tcp.host, "192.168.1.50");
	zassert_equal(tcp.port, 2000);
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_malformed_stored_tcp_keeps_defaults)
{
	struct bridge_config_tcp_client tcp;
	uint8_t record[TCP_RECORD_LEN];
	uint8_t short_value = 1U;

	zassert_equal(bridge_config_test_settings_set("tcp", sizeof(short_value),
						      stored_read_cb, &short_value), 0);

	make_tcp_record(true, "192.168.1.50", 2000, record);
	record[1] = 16U; /* host_len out of range */
	zassert_equal(bridge_config_test_settings_set("tcp", sizeof(record),
						      stored_read_cb, record), 0);

	make_tcp_record(true, "192.168.1.50", 0, record); /* port 0 invalid */
	zassert_equal(bridge_config_test_settings_set("tcp", sizeof(record),
						      stored_read_cb, record), 0);

	make_tcp_record(true, "not-an-ip", 2000, record);
	zassert_equal(bridge_config_test_settings_set("tcp", sizeof(record),
						      stored_read_cb, record), 0);

	make_tcp_record(true, "192.168.1.50", 2000, record);
	zassert_equal(bridge_config_test_settings_set("tcp", sizeof(record),
						      failing_read_cb, record), 0);

	bridge_config_get_tcp_client(&tcp);
	zassert_true(tcp.enabled);
	zassert_str_equal(tcp.host, "");
	zassert_equal(tcp.port, 10110);
}

ZTEST(bridge_config, test_set_tcp_persists_record_and_notifies_listener)
{
	struct bridge_config_tcp_client next;
	struct bridge_config_tcp_client tcp;
	uint8_t expected[TCP_RECORD_LEN];

	bridge_config_set_listener(count_listener);
	fill_tcp(&next, false, "10.0.0.2", 2000);
	zassert_equal(bridge_config_set_tcp_client(&next), 0);

	make_tcp_record(false, "10.0.0.2", 2000, expected);
	zassert_equal(save_count, 1);
	zassert_str_equal(saved[0].name, "bridge/tcp");
	zassert_equal(saved[0].len, sizeof(expected));
	zassert_mem_equal(saved[0].value, expected, sizeof(expected));
	zassert_equal(listener_count, 1);
	zassert_false(bridge_config_reboot_required());

	bridge_config_get_tcp_client(&tcp);
	zassert_false(tcp.enabled);
	zassert_str_equal(tcp.host, "10.0.0.2");
	zassert_equal(tcp.port, 2000);
}

ZTEST(bridge_config, test_tcp_max_length_host_round_trips)
{
	struct bridge_config_tcp_client next;
	struct bridge_config_tcp_client tcp;

	/* 15 characters: exactly fills the packed record's host field. */
	fill_tcp(&next, true, "255.255.255.255", 65535);
	zassert_equal(bridge_config_set_tcp_client(&next), 0);
	zassert_equal(save_count, 1);

	zassert_equal(bridge_config_test_settings_set("tcp", saved[0].len,
						      stored_read_cb, saved[0].value), 0);
	bridge_config_get_tcp_client(&tcp);
	zassert_str_equal(tcp.host, "255.255.255.255");
	zassert_equal(tcp.port, 65535);
}

ZTEST(bridge_config, test_set_tcp_accepts_empty_host_meaning_gateway)
{
	struct bridge_config_tcp_client next;

	fill_tcp(&next, true, "", 4000);
	zassert_equal(bridge_config_set_tcp_client(&next), 0);
	zassert_equal(save_count, 1);
}

ZTEST(bridge_config, test_set_tcp_rejects_invalid_fields_without_saving)
{
	static const char *const bad_hosts[] = {
		"not-an-ip", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.4 ", "1..2.3", "1.2.3.",
		/* Leading zeros: net_addr_pton would reject the stored host. */
		"010.0.0.1", "1.2.3.04",
	};
	struct bridge_config_tcp_client next;
	struct bridge_config_tcp_client tcp;

	for (size_t i = 0; i < ARRAY_SIZE(bad_hosts); i++) {
		fill_tcp(&next, true, bad_hosts[i], 2000);
		zassert_equal(bridge_config_set_tcp_client(&next), -EINVAL,
			      "host %s must be rejected", bad_hosts[i]);
	}

	fill_tcp(&next, true, "10.0.0.2", 0); /* port 0 */
	zassert_equal(bridge_config_set_tcp_client(&next), -EINVAL);

	/* Unterminated host array */
	fill_tcp(&next, true, "", 2000);
	memset(next.host, '1', sizeof(next.host));
	zassert_equal(bridge_config_set_tcp_client(&next), -EINVAL);

	zassert_equal(save_count, 0);
	bridge_config_get_tcp_client(&tcp);
	zassert_str_equal(tcp.host, "");
	zassert_equal(tcp.port, 10110);
}

ZTEST(bridge_config, test_set_tcp_unchanged_is_a_no_op)
{
	struct bridge_config_tcp_client next;

	bridge_config_set_listener(count_listener);
	fill_tcp(&next, true, "", 10110);
	zassert_equal(bridge_config_set_tcp_client(&next), 0);
	zassert_equal(save_count, 0);
	zassert_equal(listener_count, 0);
}

ZTEST(bridge_config, test_set_tcp_save_failure_keeps_previous_config)
{
	struct bridge_config_tcp_client next;
	struct bridge_config_tcp_client tcp;

	save_ret = -EIO;
	bridge_config_set_listener(count_listener);
	fill_tcp(&next, false, "10.0.0.2", 2000);

	zassert_equal(bridge_config_set_tcp_client(&next), -EIO);
	zassert_equal(listener_count, 0);

	bridge_config_get_tcp_client(&tcp);
	zassert_true(tcp.enabled);
	zassert_str_equal(tcp.host, "");
	zassert_equal(tcp.port, 10110);
}

ZTEST(bridge_config, test_ais_change_does_not_require_reboot)
{
	struct bridge_config_ais next = {
		.filter_enabled = false,
		.own_mmsi = 211000000U,
	};

	zassert_equal(bridge_config_set_ais(&next), 0);
	zassert_false(bridge_config_reboot_required());
}

/* Mirrors the packed layout in bridge_config.c: ssid_len, ssid, psk_len, psk. */
#define AP_RECORD_LEN 97U

static void make_ap_record(const char *ssid, const char *psk, uint8_t record[AP_RECORD_LEN])
{
	memset(record, 0, AP_RECORD_LEN);
	record[0] = (uint8_t)strlen(ssid);
	memcpy(&record[1], ssid, strlen(ssid));
	record[33] = (uint8_t)strlen(psk);
	memcpy(&record[34], psk, strlen(psk));
}

static int store_ap_record(const char *ssid, const char *psk)
{
	uint8_t record[AP_RECORD_LEN];

	make_ap_record(ssid, psk, record);
	return bridge_config_test_settings_set("ap", sizeof(record), stored_read_cb, record);
}

static void fill_ap(struct bridge_config_ap *ap, const char *ssid, const char *psk)
{
	memset(ap, 0, sizeof(*ap));
	strcpy(ap->ssid, ssid);
	strcpy(ap->psk, psk);
}

ZTEST(bridge_config, test_ap_defaults_come_from_kconfig)
{
	struct bridge_config_ap ap;

	bridge_config_get_ap(&ap);

	zassert_str_equal(ap.ssid, "ESP-NMEA0183");
	zassert_str_equal(ap.psk, "ChangeMe1234");
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_stored_ap_overrides_defaults_without_reboot_flag)
{
	struct bridge_config_ap ap;

	zassert_equal(store_ap_record("SY Anna", "harbour99"), 0);

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, "SY Anna");
	zassert_str_equal(ap.psk, "harbour99");
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_stored_open_ap_overrides_default_psk)
{
	struct bridge_config_ap ap;

	zassert_equal(store_ap_record("SY Anna", ""), 0);

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.psk, "");
}

ZTEST(bridge_config, test_malformed_stored_ap_keeps_defaults)
{
	struct bridge_config_ap ap;
	uint8_t record[AP_RECORD_LEN];
	uint8_t short_value = 1U;

	zassert_equal(bridge_config_test_settings_set("ap", sizeof(short_value),
						      stored_read_cb, &short_value), 0);

	make_ap_record("SY Anna", "harbour99", record);
	record[0] = 0U; /* AP SSID must not be empty */
	zassert_equal(bridge_config_test_settings_set("ap", sizeof(record),
						      stored_read_cb, record), 0);

	make_ap_record("SY Anna", "harbour99", record);
	record[0] = 33U; /* ssid_len out of range */
	zassert_equal(bridge_config_test_settings_set("ap", sizeof(record),
						      stored_read_cb, record), 0);

	make_ap_record("SY Anna", "harbour99", record);
	record[33] = 64U; /* psk_len out of range */
	zassert_equal(bridge_config_test_settings_set("ap", sizeof(record),
						      stored_read_cb, record), 0);

	make_ap_record("SY Anna", "harbour99", record);
	record[33] = 3U; /* psk_len below the WPA2 minimum */
	zassert_equal(bridge_config_test_settings_set("ap", sizeof(record),
						      stored_read_cb, record), 0);

	make_ap_record("SY Anna", "harbour99", record);
	zassert_equal(bridge_config_test_settings_set("ap", sizeof(record),
						      failing_read_cb, record), 0);

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, "ESP-NMEA0183");
	zassert_str_equal(ap.psk, "ChangeMe1234");
}

ZTEST(bridge_config, test_set_ap_persists_record_and_flags_reboot)
{
	struct bridge_config_ap next;
	struct bridge_config_ap ap;
	uint8_t expected[AP_RECORD_LEN];

	fill_ap(&next, "SY Anna", "harbour99");
	zassert_equal(bridge_config_set_ap(&next), 0);

	make_ap_record("SY Anna", "harbour99", expected);
	zassert_equal(save_count, 1);
	zassert_str_equal(saved[0].name, "bridge/ap");
	zassert_equal(saved[0].len, sizeof(expected));
	zassert_mem_equal(saved[0].value, expected, sizeof(expected));

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, "SY Anna");
	zassert_str_equal(ap.psk, "harbour99");
	zassert_true(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_set_ap_accepts_empty_psk_meaning_open_ap)
{
	struct bridge_config_ap next;

	fill_ap(&next, "SY Anna", "");
	zassert_equal(bridge_config_set_ap(&next), 0);
	zassert_equal(save_count, 1);
}

ZTEST(bridge_config, test_set_ap_rejects_invalid_fields_without_saving)
{
	struct bridge_config_ap next;
	struct bridge_config_ap ap;

	/* AP SSID must never be empty: the rescue anchor stays findable. */
	fill_ap(&next, "", "harbour99");
	zassert_equal(bridge_config_set_ap(&next), -EINVAL);

	/* SSID filling the whole array without terminator = longer than 32 bytes */
	fill_ap(&next, "", "harbour99");
	memset(next.ssid, 'a', sizeof(next.ssid));
	zassert_equal(bridge_config_set_ap(&next), -EINVAL);

	/* PSK shorter than 8 characters */
	fill_ap(&next, "SY Anna", "short");
	zassert_equal(bridge_config_set_ap(&next), -EINVAL);

	zassert_equal(save_count, 0);
	zassert_false(bridge_config_reboot_required());

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, "ESP-NMEA0183");
}

ZTEST(bridge_config, test_stored_ap_with_embedded_nul_keeps_defaults)
{
	struct bridge_config_ap ap;
	uint8_t record[AP_RECORD_LEN];

	/* NUL as first PSK byte: would silently open the rescue AP. */
	make_ap_record("SY Anna", "harbour99", record);
	record[34] = 0U;
	zassert_equal(bridge_config_test_settings_set("ap", sizeof(record),
						      stored_read_cb, record), 0);

	/* NUL as first SSID byte: SSID would become effectively empty. */
	make_ap_record("SY Anna", "harbour99", record);
	record[1] = 0U;
	zassert_equal(bridge_config_test_settings_set("ap", sizeof(record),
						      stored_read_cb, record), 0);

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, "ESP-NMEA0183");
	zassert_str_equal(ap.psk, "ChangeMe1234");
}

ZTEST(bridge_config, test_stored_ap_with_invalid_utf8_ssid_keeps_defaults)
{
	struct bridge_config_ap ap;
	uint8_t record[AP_RECORD_LEN];

	make_ap_record("Boat\xFFNet", "harbour99", record);
	zassert_equal(bridge_config_test_settings_set("ap", sizeof(record),
						      stored_read_cb, record), 0);

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, "ESP-NMEA0183");
}

ZTEST(bridge_config, test_short_settings_read_keeps_defaults)
{
	struct bridge_config_ap ap;
	struct bridge_config_sta sta;
	uint8_t record[STA_RECORD_LEN];

	make_ap_record("SY Anna", "harbour99", record);
	zassert_equal(bridge_config_test_settings_set("ap", AP_RECORD_LEN,
						      partial_read_cb, record), 0);

	make_sta_record(true, true, "Marina", "harbour99", record);
	zassert_equal(bridge_config_test_settings_set("sta", sizeof(record),
						      partial_read_cb, record), 0);

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, "ESP-NMEA0183");
	bridge_config_get_sta(&sta);
	zassert_str_equal(sta.ssid, "BoatNet");
}

ZTEST(bridge_config, test_set_ap_rejects_non_printable_ssid)
{
	struct bridge_config_ap next;

	fill_ap(&next, "Boat\xFFNet", "harbour99");
	zassert_equal(bridge_config_set_ap(&next), -EINVAL);

	fill_ap(&next, "Boat\aNet", "harbour99");
	zassert_equal(bridge_config_set_ap(&next), -EINVAL);

	zassert_equal(save_count, 0);
}

ZTEST(bridge_config, test_set_sta_rejects_non_printable_ssid)
{
	struct bridge_config_sta next;

	fill_sta(&next, true, true, "Boat\xFFNet", "harbour99");
	zassert_equal(bridge_config_set_sta(&next), -EINVAL);
	zassert_equal(save_count, 0);
}

ZTEST(bridge_config, test_ap_max_length_fields_round_trip)
{
	static const char ssid_32[] = "12345678901234567890123456789012";
	static const char psk_63[] =
		"123456789012345678901234567890123456789012345678901234567890123";
	struct bridge_config_ap next;
	struct bridge_config_ap ap;

	fill_ap(&next, ssid_32, psk_63);
	zassert_equal(bridge_config_set_ap(&next), 0);
	zassert_equal(save_count, 1);

	zassert_equal(bridge_config_test_settings_set("ap", saved[0].len,
						      stored_read_cb, saved[0].value), 0);
	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, ssid_32);
	zassert_str_equal(ap.psk, psk_63);
}

ZTEST(bridge_config, test_set_ap_unchanged_is_a_no_op)
{
	struct bridge_config_ap next;

	fill_ap(&next, "ESP-NMEA0183", "ChangeMe1234");
	zassert_equal(bridge_config_set_ap(&next), 0);
	zassert_equal(save_count, 0);
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_set_ap_save_failure_keeps_previous_config)
{
	struct bridge_config_ap next;
	struct bridge_config_ap ap;

	save_ret = -EIO;
	fill_ap(&next, "SY Anna", "harbour99");

	zassert_equal(bridge_config_set_ap(&next), -EIO);
	zassert_false(bridge_config_reboot_required());

	bridge_config_get_ap(&ap);
	zassert_str_equal(ap.ssid, "ESP-NMEA0183");
}

ZTEST(bridge_config, test_set_ap_does_not_notify_live_listener)
{
	struct bridge_config_ap next;

	bridge_config_set_listener(count_listener);
	fill_ap(&next, "SY Anna", "harbour99");
	zassert_equal(bridge_config_set_ap(&next), 0);
	zassert_equal(listener_count, 0);
}

/* Mirrors the packed layout in bridge_config.c: hostname_len, hostname. */
#define SYS_RECORD_LEN 33U

static void make_sys_record(const char *hostname, uint8_t record[SYS_RECORD_LEN])
{
	memset(record, 0, SYS_RECORD_LEN);
	record[0] = (uint8_t)strlen(hostname);
	memcpy(&record[1], hostname, strlen(hostname));
}

static int store_sys_record(const char *hostname)
{
	uint8_t record[SYS_RECORD_LEN];

	make_sys_record(hostname, record);
	return bridge_config_test_settings_set("sys", sizeof(record), stored_read_cb, record);
}

static void fill_system(struct bridge_config_system *system, const char *hostname)
{
	memset(system, 0, sizeof(*system));
	strcpy(system->hostname, hostname);
}

ZTEST(bridge_config, test_hostname_defaults_to_net_hostname)
{
	struct bridge_config_system system;

	bridge_config_get_system(&system);

	zassert_str_equal(system.hostname, "esp-nmea-bridge");
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_stored_hostname_overrides_default_without_reboot_flag)
{
	struct bridge_config_system system;

	zassert_equal(store_sys_record("sy-anna"), 0);

	bridge_config_get_system(&system);
	zassert_str_equal(system.hostname, "sy-anna");
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_malformed_stored_hostname_keeps_default)
{
	struct bridge_config_system system;
	uint8_t record[SYS_RECORD_LEN];
	uint8_t short_value = 1U;

	zassert_equal(bridge_config_test_settings_set("sys", sizeof(short_value),
						      stored_read_cb, &short_value), 0);

	make_sys_record("", record); /* empty hostname invalid */
	zassert_equal(bridge_config_test_settings_set("sys", sizeof(record),
						      stored_read_cb, record), 0);

	make_sys_record("sy-anna", record);
	record[0] = 33U; /* hostname_len out of range */
	zassert_equal(bridge_config_test_settings_set("sys", sizeof(record),
						      stored_read_cb, record), 0);

	make_sys_record("Bad_Name", record); /* invalid DNS label characters */
	zassert_equal(bridge_config_test_settings_set("sys", sizeof(record),
						      stored_read_cb, record), 0);

	make_sys_record("sy-anna", record);
	zassert_equal(bridge_config_test_settings_set("sys", sizeof(record),
						      failing_read_cb, record), 0);

	bridge_config_get_system(&system);
	zassert_str_equal(system.hostname, "esp-nmea-bridge");
}

ZTEST(bridge_config, test_stored_hostname_with_embedded_nul_keeps_default)
{
	struct bridge_config_system system;
	uint8_t record[SYS_RECORD_LEN];

	make_sys_record("sy-anna", record);
	record[3] = 0U; /* declared length 7, effective string "sy" */
	zassert_equal(bridge_config_test_settings_set("sys", sizeof(record),
						      stored_read_cb, record), 0);

	bridge_config_get_system(&system);
	zassert_str_equal(system.hostname, "esp-nmea-bridge");
}

ZTEST(bridge_config, test_set_system_persists_record_and_flags_reboot)
{
	struct bridge_config_system next;
	struct bridge_config_system system;
	uint8_t expected[SYS_RECORD_LEN];

	fill_system(&next, "sy-anna");
	zassert_equal(bridge_config_set_system(&next), 0);

	make_sys_record("sy-anna", expected);
	zassert_equal(save_count, 1);
	zassert_str_equal(saved[0].name, "bridge/sys");
	zassert_equal(saved[0].len, sizeof(expected));
	zassert_mem_equal(saved[0].value, expected, sizeof(expected));

	bridge_config_get_system(&system);
	zassert_str_equal(system.hostname, "sy-anna");
	zassert_true(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_set_system_rejects_invalid_hostnames_without_saving)
{
	static const char *const bad_hostnames[] = {
		"", "-anna", "anna-", "sy_anna", "SY-Anna", "sy.anna", "sy anna",
		"123456789012345678901234567890123", /* 33 characters */
	};
	struct bridge_config_system next;
	struct bridge_config_system system;

	for (size_t i = 0; i < ARRAY_SIZE(bad_hostnames); i++) {
		fill_system(&next, bad_hostnames[i]);
		zassert_equal(bridge_config_set_system(&next), -EINVAL,
			      "hostname %s must be rejected", bad_hostnames[i]);
	}

	/* Unterminated hostname array */
	memset(next.hostname, 'a', sizeof(next.hostname));
	zassert_equal(bridge_config_set_system(&next), -EINVAL);

	zassert_equal(save_count, 0);
	zassert_false(bridge_config_reboot_required());

	bridge_config_get_system(&system);
	zassert_str_equal(system.hostname, "esp-nmea-bridge");
}

ZTEST(bridge_config, test_set_system_accepts_edge_hostnames)
{
	static const char *const good_hostnames[] = {
		"a", "7seas", "sy-anna-2", "12345678901234567890123456789012", /* 32 chars */
	};
	struct bridge_config_system next;

	for (size_t i = 0; i < ARRAY_SIZE(good_hostnames); i++) {
		fill_system(&next, good_hostnames[i]);
		zassert_equal(bridge_config_set_system(&next), 0,
			      "hostname %s must be accepted", good_hostnames[i]);
	}
}

ZTEST(bridge_config, test_set_system_unchanged_is_a_no_op)
{
	struct bridge_config_system next;

	fill_system(&next, "esp-nmea-bridge");
	zassert_equal(bridge_config_set_system(&next), 0);
	zassert_equal(save_count, 0);
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_set_system_save_failure_keeps_previous_config)
{
	struct bridge_config_system next;
	struct bridge_config_system system;

	save_ret = -EIO;
	fill_system(&next, "sy-anna");

	zassert_equal(bridge_config_set_system(&next), -EIO);
	zassert_false(bridge_config_reboot_required());

	bridge_config_get_system(&system);
	zassert_str_equal(system.hostname, "esp-nmea-bridge");
}

ZTEST(bridge_config, test_set_system_does_not_notify_live_listener)
{
	struct bridge_config_system next;

	bridge_config_set_listener(count_listener);
	fill_system(&next, "sy-anna");
	zassert_equal(bridge_config_set_system(&next), 0);
	zassert_equal(listener_count, 0);
}

ZTEST(bridge_config, test_factory_reset_deletes_every_stored_key_generically)
{
	store_key("ais");
	store_key("sta");
	store_key("tcp");
	store_key("ap");
	store_key("sys");
	store_key("future-option");

	zassert_equal(bridge_config_factory_reset(), 0);
	zassert_equal(stored_key_count, 0);
	zassert_equal(delete_count, 6);
	zassert_true(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_factory_reset_without_stored_keys_still_flags_reboot)
{
	zassert_equal(bridge_config_factory_reset(), 0);
	zassert_equal(delete_count, 0);
	zassert_true(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_factory_reset_keeps_effective_values_until_reboot)
{
	struct bridge_config_ais ais;

	zassert_equal(store_record(false, 211000000U), 0);
	store_key("ais");

	zassert_equal(bridge_config_factory_reset(), 0);

	bridge_config_get_ais(&ais);
	zassert_false(ais.filter_enabled);
	zassert_equal(ais.own_mmsi, 211000000U);
}

ZTEST(bridge_config, test_factory_reset_deletes_more_keys_than_one_pass_holds)
{
	char name[3] = "k0";

	for (int i = 0; i < 10; i++) {
		name[1] = (char)('0' + i);
		store_key(name);
	}

	zassert_equal(bridge_config_factory_reset(), 0);
	zassert_equal(stored_key_count, 0);
	zassert_equal(delete_count, 10);
	zassert_true(load_direct_count >= 2);
}

ZTEST(bridge_config, test_factory_reset_delete_failure_propagates)
{
	store_key("ais");
	delete_fail_at = 0;

	zassert_equal(bridge_config_factory_reset(), -EIO);
	zassert_equal(delete_count, 0);
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_factory_reset_partial_delete_still_flags_reboot)
{
	struct bridge_config_ais ais;

	store_key("ais");
	store_key("sta");
	delete_fail_at = 1;

	zassert_equal(bridge_config_factory_reset(), -EIO);
	zassert_equal(delete_count, 1);
	/* One override is already gone, so a reboot changes behaviour... */
	zassert_true(bridge_config_reboot_required());

	/* ...and later saves must persist even when they match RAM. */
	bridge_config_get_ais(&ais);
	zassert_equal(bridge_config_set_ais(&ais), 0);
	zassert_equal(save_count, 1);
}

ZTEST(bridge_config, test_factory_reset_reports_corrupt_overlong_key)
{
	char long_key[SETTINGS_MAX_NAME_LEN + 2];

	memset(long_key, 'x', sizeof(long_key) - 1U);
	long_key[sizeof(long_key) - 1U] = '\0';
	store_key(long_key);

	zassert_equal(bridge_config_factory_reset(), -ENAMETOOLONG);
	zassert_equal(delete_count, 0);
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_factory_reset_then_reboot_runs_on_kconfig_defaults)
{
	struct bridge_config_ais ais;

	zassert_equal(store_record(false, 211000000U), 0);
	store_key("ais");
	zassert_equal(bridge_config_factory_reset(), 0);

	/* Reboot: fresh RAM state, loading from the now-empty store. */
	bridge_config_test_reset();
	zassert_equal(bridge_config_init(), 0);

	bridge_config_get_ais(&ais);
	zassert_true(ais.filter_enabled);
	zassert_equal(ais.own_mmsi, TEST_DEFAULT_MMSI);
	zassert_false(bridge_config_reboot_required());
}

ZTEST(bridge_config, test_forced_save_after_reset_does_not_notify_listener)
{
	struct bridge_config_tcp_client tcp;

	zassert_equal(bridge_config_factory_reset(), 0);
	bridge_config_set_listener(count_listener);

	bridge_config_get_tcp_client(&tcp);
	zassert_equal(bridge_config_set_tcp_client(&tcp), 0);
	zassert_equal(save_count, 1);
	zassert_equal(listener_count, 0);
}

ZTEST(bridge_config, test_save_after_factory_reset_persists_unchanged_values)
{
	struct bridge_config_ais ais = {
		.filter_enabled = true,
		.own_mmsi = TEST_DEFAULT_MMSI,
	};
	struct bridge_config_system system;

	zassert_equal(bridge_config_factory_reset(), 0);

	/* Values equal the effective ones, but the stored overrides are gone:
	 * skipping the save would silently revert them at the next boot.
	 */
	zassert_equal(bridge_config_set_ais(&ais), 0);
	zassert_equal(save_count, 1);

	bridge_config_get_system(&system);
	zassert_equal(bridge_config_set_system(&system), 0);
	zassert_equal(save_count, 2);
}

ZTEST_SUITE(bridge_config, NULL, NULL, reset_harness, NULL, NULL);
