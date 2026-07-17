#ifndef OTA_UPDATE_H_
#define OTA_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_UPDATE_LAST_ERROR_LEN 96

enum ota_update_state {
	OTA_UPDATE_STATE_DISABLED,
	OTA_UPDATE_STATE_READY,
	OTA_UPDATE_STATE_UPLOADING,
	OTA_UPDATE_STATE_PENDING_REBOOT,
	OTA_UPDATE_STATE_CONFIRMED,
	OTA_UPDATE_STATE_ERROR,
};

struct ota_update_status {
	bool enabled;
	enum ota_update_state state;
	size_t expected_bytes;
	size_t uploaded_bytes;
	size_t max_upload_bytes;
	uint8_t slot;
	bool confirmed;
	char last_error[OTA_UPDATE_LAST_ERROR_LEN];
};

const char *ota_update_state_name(enum ota_update_state state);
int ota_update_validate_upload_size(size_t content_length, size_t max_upload_bytes);
void ota_update_get_status(struct ota_update_status *status);
int ota_update_begin(size_t content_length);
int ota_update_write(const uint8_t *data, size_t len);
int ota_update_finish(void);
void ota_update_schedule_reboot(void);
void ota_update_abort(const char *reason);
void ota_update_self_check_web_reachable(void);

#endif /* OTA_UPDATE_H_ */
