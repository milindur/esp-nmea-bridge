#include <errno.h>

#include <zephyr/ztest.h>

#include "ota_update.h"

ZTEST(ota_update, test_state_names_are_stable_for_status_json)
{
	zassert_str_equal(ota_update_state_name(OTA_UPDATE_STATE_DISABLED), "disabled");
	zassert_str_equal(ota_update_state_name(OTA_UPDATE_STATE_READY), "ready");
	zassert_str_equal(ota_update_state_name(OTA_UPDATE_STATE_UPLOADING), "uploading");
	zassert_str_equal(ota_update_state_name(OTA_UPDATE_STATE_PENDING_REBOOT), "pending_reboot");
	zassert_str_equal(ota_update_state_name(OTA_UPDATE_STATE_CONFIRMED), "confirmed");
	zassert_str_equal(ota_update_state_name(OTA_UPDATE_STATE_ERROR), "error");
	zassert_str_equal(ota_update_state_name((enum ota_update_state)99), "unknown");
}

ZTEST(ota_update, test_zero_length_upload_is_rejected)
{
	zassert_equal(ota_update_validate_upload_size(0U, 1024U), -EINVAL);
}

ZTEST(ota_update, test_upload_larger_than_limit_is_rejected)
{
	zassert_equal(ota_update_validate_upload_size(1025U, 1024U), -EFBIG);
}

ZTEST(ota_update, test_upload_at_limit_is_allowed)
{
	zassert_ok(ota_update_validate_upload_size(1024U, 1024U));
}

ZTEST(ota_update, test_zero_limit_means_runtime_slot_size_limit)
{
	zassert_ok(ota_update_validate_upload_size(4096U, 0U));
}

ZTEST(ota_update, test_disabled_status_is_safe)
{
	struct ota_update_status status;

	ota_update_get_status(&status);

	zassert_false(status.enabled);
	zassert_equal(status.state, OTA_UPDATE_STATE_DISABLED);
	zassert_true(status.confirmed);
}

ZTEST(ota_update, test_disabled_upload_apis_return_not_supported)
{
	uint8_t byte = 0xff;

	zassert_equal(ota_update_begin(1U), -ENOTSUP);
	zassert_equal(ota_update_write(&byte, sizeof(byte)), -ENOTSUP);
	zassert_equal(ota_update_finish(), -ENOTSUP);
}

ZTEST_SUITE(ota_update, NULL, NULL, NULL, NULL, NULL);
