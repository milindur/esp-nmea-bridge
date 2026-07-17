#include "ota_update.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(ota_update, LOG_LEVEL_INF);
#endif

struct ota_update_runtime {
	struct k_mutex lock;
	bool initialized;
	struct ota_update_status status;
#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
	struct flash_img_context flash_ctx;
	struct k_work_delayable reboot_work;
#endif
};

static struct ota_update_runtime ota_runtime;

const char *ota_update_state_name(enum ota_update_state state)
{
	switch (state) {
	case OTA_UPDATE_STATE_DISABLED:
		return "disabled";
	case OTA_UPDATE_STATE_READY:
		return "ready";
	case OTA_UPDATE_STATE_UPLOADING:
		return "uploading";
	case OTA_UPDATE_STATE_PENDING_REBOOT:
		return "pending_reboot";
	case OTA_UPDATE_STATE_CONFIRMED:
		return "confirmed";
	case OTA_UPDATE_STATE_ERROR:
		return "error";
	default:
		return "unknown";
	}
}

int ota_update_validate_upload_size(size_t content_length, size_t max_upload_bytes)
{
	if (content_length == 0U) {
		return -EINVAL;
	}

	if (max_upload_bytes > 0U && content_length > max_upload_bytes) {
		return -EFBIG;
	}

	return 0;
}

#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
static void set_error_locked(const char *reason, int err)
{
	ota_runtime.status.state = OTA_UPDATE_STATE_ERROR;
	(void)snprintk(ota_runtime.status.last_error,
		      sizeof(ota_runtime.status.last_error), "%s (%d)",
		      reason != NULL ? reason : "OTA update failed", err);
}

static size_t configured_limit_or_slot_size(const struct flash_area *area)
{
	if (CONFIG_ESP_NMEA_BRIDGE_OTA_MAX_UPLOAD_BYTES > 0) {
		return (size_t)CONFIG_ESP_NMEA_BRIDGE_OTA_MAX_UPLOAD_BYTES;
	}

	return area != NULL ? area->fa_size : 0U;
}

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("Rebooting into OTA test image");
	sys_reboot(SYS_REBOOT_COLD);
}
#endif

static void init_once(void)
{
	if (ota_runtime.initialized) {
		return;
	}

	k_mutex_init(&ota_runtime.lock);
#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
	k_work_init_delayable(&ota_runtime.reboot_work, reboot_work_handler);
	ota_runtime.status.enabled = true;
	ota_runtime.status.state = boot_is_img_confirmed() ?
		OTA_UPDATE_STATE_CONFIRMED : OTA_UPDATE_STATE_READY;
	ota_runtime.status.confirmed = boot_is_img_confirmed();
	ota_runtime.status.slot = flash_img_get_upload_slot();
#else
	ota_runtime.status.enabled = false;
	ota_runtime.status.state = OTA_UPDATE_STATE_DISABLED;
	ota_runtime.status.confirmed = true;
#endif
	ota_runtime.initialized = true;
}

void ota_update_get_status(struct ota_update_status *status)
{
	if (status == NULL) {
		return;
	}

	init_once();
	k_mutex_lock(&ota_runtime.lock, K_FOREVER);
	*status = ota_runtime.status;
	k_mutex_unlock(&ota_runtime.lock);
}

#ifdef CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE
int ota_update_begin(size_t content_length)
{
	const struct flash_area *area;
	uint8_t slot;
	int ret;

	init_once();
	slot = flash_img_get_upload_slot();

	ret = flash_area_open(slot, &area);
	if (ret != 0) {
		k_mutex_lock(&ota_runtime.lock, K_FOREVER);
		set_error_locked("opening OTA upload slot failed", ret);
		k_mutex_unlock(&ota_runtime.lock);
		return ret;
	}

	k_mutex_lock(&ota_runtime.lock, K_FOREVER);
	ota_runtime.status.max_upload_bytes = configured_limit_or_slot_size(area);
	ret = ota_update_validate_upload_size(content_length,
						ota_runtime.status.max_upload_bytes);
	if (ret != 0) {
		set_error_locked("invalid OTA upload size", ret);
		k_mutex_unlock(&ota_runtime.lock);
		flash_area_close(area);
		return ret;
	}

	ret = boot_erase_img_bank(slot);
	if (ret != 0) {
		set_error_locked("erasing OTA upload slot failed", ret);
		k_mutex_unlock(&ota_runtime.lock);
		flash_area_close(area);
		return ret;
	}

	ret = flash_img_init_id(&ota_runtime.flash_ctx, slot);
	if (ret != 0) {
		set_error_locked("initializing OTA flash writer failed", ret);
		k_mutex_unlock(&ota_runtime.lock);
		flash_area_close(area);
		return ret;
	}

	ota_runtime.status.enabled = true;
	ota_runtime.status.state = OTA_UPDATE_STATE_UPLOADING;
	ota_runtime.status.expected_bytes = content_length;
	ota_runtime.status.uploaded_bytes = 0U;
	ota_runtime.status.slot = slot;
	ota_runtime.status.last_error[0] = '\0';
	k_mutex_unlock(&ota_runtime.lock);
	flash_area_close(area);
	return 0;
}

int ota_update_write(const uint8_t *data, size_t len)
{
	int ret;

	if (data == NULL && len > 0U) {
		return -EINVAL;
	}

	init_once();
	k_mutex_lock(&ota_runtime.lock, K_FOREVER);
	if (ota_runtime.status.state != OTA_UPDATE_STATE_UPLOADING) {
		k_mutex_unlock(&ota_runtime.lock);
		return -EALREADY;
	}
	if (ota_runtime.status.uploaded_bytes + len > ota_runtime.status.expected_bytes) {
		set_error_locked("OTA upload exceeded Content-Length", -EFBIG);
		k_mutex_unlock(&ota_runtime.lock);
		return -EFBIG;
	}

	ret = flash_img_buffered_write(&ota_runtime.flash_ctx, data, len, false);
	if (ret == 0) {
		ota_runtime.status.uploaded_bytes += len;
	} else {
		set_error_locked("writing OTA image failed", ret);
	}
	k_mutex_unlock(&ota_runtime.lock);
	return ret;
}

int ota_update_finish(void)
{
	int ret;

	init_once();
	k_mutex_lock(&ota_runtime.lock, K_FOREVER);
	if (ota_runtime.status.state != OTA_UPDATE_STATE_UPLOADING) {
		k_mutex_unlock(&ota_runtime.lock);
		return -EALREADY;
	}
	if (ota_runtime.status.uploaded_bytes != ota_runtime.status.expected_bytes) {
		set_error_locked("OTA upload ended before Content-Length", -EMSGSIZE);
		k_mutex_unlock(&ota_runtime.lock);
		return -EMSGSIZE;
	}

	ret = flash_img_buffered_write(&ota_runtime.flash_ctx, NULL, 0U, true);
	if (ret != 0) {
		set_error_locked("flushing OTA image failed", ret);
		k_mutex_unlock(&ota_runtime.lock);
		return ret;
	}

	ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
	if (ret != 0) {
		set_error_locked("requesting OTA test boot failed", ret);
		k_mutex_unlock(&ota_runtime.lock);
		return ret;
	}

	ota_runtime.status.state = OTA_UPDATE_STATE_PENDING_REBOOT;
	ota_runtime.status.last_error[0] = '\0';
	k_mutex_unlock(&ota_runtime.lock);
	return 0;
}

void ota_update_schedule_reboot(void)
{
	init_once();
	(void)k_work_schedule(&ota_runtime.reboot_work, K_MSEC(750));
}

void ota_update_abort(const char *reason)
{
	init_once();
	k_mutex_lock(&ota_runtime.lock, K_FOREVER);
	set_error_locked(reason != NULL ? reason : "OTA upload aborted", -ECANCELED);
	k_mutex_unlock(&ota_runtime.lock);
}

void ota_update_self_check_web_reachable(void)
{
	int ret;

	init_once();
	k_mutex_lock(&ota_runtime.lock, K_FOREVER);
	if (boot_is_img_confirmed()) {
		ota_runtime.status.confirmed = true;
		ota_runtime.status.state = OTA_UPDATE_STATE_CONFIRMED;
		k_mutex_unlock(&ota_runtime.lock);
		return;
	}

	ret = boot_write_img_confirmed();
	if (ret == 0) {
		ota_runtime.status.confirmed = true;
		ota_runtime.status.state = OTA_UPDATE_STATE_CONFIRMED;
		ota_runtime.status.last_error[0] = '\0';
	} else {
		set_error_locked("confirming OTA image failed", ret);
	}
	k_mutex_unlock(&ota_runtime.lock);
}
#else
int ota_update_begin(size_t content_length)
{
	ARG_UNUSED(content_length);
	init_once();
	return -ENOTSUP;
}

int ota_update_write(const uint8_t *data, size_t len)
{
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	init_once();
	return -ENOTSUP;
}

int ota_update_finish(void)
{
	init_once();
	return -ENOTSUP;
}

void ota_update_schedule_reboot(void)
{
	init_once();
}

void ota_update_abort(const char *reason)
{
	ARG_UNUSED(reason);
	init_once();
}

void ota_update_self_check_web_reachable(void)
{
	init_once();
}
#endif
