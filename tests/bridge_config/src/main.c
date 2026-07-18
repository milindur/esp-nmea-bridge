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
	uint8_t value[8];
	size_t len;
};

static struct saved_entry saved[4];
static int save_count;
static int save_ret;
static int subsys_init_count;
static int load_subtree_count;
static char loaded_subtree[16];
static int listener_count;

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

ZTEST_SUITE(bridge_config, NULL, NULL, reset_harness, NULL, NULL);
