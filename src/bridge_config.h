#ifndef BRIDGE_CONFIG_H_
#define BRIDGE_CONFIG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BRIDGE_CONFIG_AIS_MMSI_MAX 999999999U
#define BRIDGE_CONFIG_WIFI_SSID_MAX 32
#define BRIDGE_CONFIG_WIFI_PSK_MIN 8
#define BRIDGE_CONFIG_WIFI_PSK_MAX 63
#define BRIDGE_CONFIG_TCP_CLIENT_HOST_MAX 15

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

struct bridge_config_tcp_client {
	bool enabled;
	/* Empty means: connect to the STA gateway. */
	char host[BRIDGE_CONFIG_TCP_CLIENT_HOST_MAX + 1];
	uint16_t port;
};

typedef void (*bridge_config_listener_t)(void);

/* True for an empty string or a dotted-quad IPv4 address (octets 0..255). */
static inline bool bridge_config_tcp_client_host_valid(const char *host)
{
	int octets = 0;

	if (host == NULL) {
		return false;
	}
	if (host[0] == '\0') {
		return true;
	}

	while (*host != '\0') {
		int value = 0;
		int digits = 0;

		while (*host >= '0' && *host <= '9' && digits < 4) {
			value = value * 10 + (*host - '0');
			digits++;
			host++;
		}
		if (digits == 0 || digits > 3 || value > 255) {
			return false;
		}
		octets++;
		if (*host == '.') {
			if (octets == 4) {
				return false;
			}
			host++;
		} else if (*host != '\0') {
			return false;
		}
	}
	return octets == 4;
}

/* Loads persistent overrides; without them the Kconfig defaults stay effective. */
int bridge_config_init(void);

void bridge_config_get_ais(struct bridge_config_ais *out);

/*
 * Validates, persists changed fields, and notifies the listener when the
 * effective configuration changed. Returns -EINVAL without saving anything
 * when a field is out of range.
 */
int bridge_config_set_ais(const struct bridge_config_ais *ais);

void bridge_config_get_tcp_client(struct bridge_config_tcp_client *out);

/*
 * Validates (host empty or IPv4, port 1..65535), persists, and notifies the
 * listener when the effective configuration changed. TCP client is
 * live-scope: consumers pick the new values up immediately.
 */
int bridge_config_set_tcp_client(const struct bridge_config_tcp_client *tcp);

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
