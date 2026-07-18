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
#define BRIDGE_CONFIG_HOSTNAME_MAX 32

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

struct bridge_config_ap {
	char ssid[BRIDGE_CONFIG_WIFI_SSID_MAX + 1];
	/* Empty means: open access point. */
	char psk[BRIDGE_CONFIG_WIFI_PSK_MAX + 1];
};

struct bridge_config_system {
	char hostname[BRIDGE_CONFIG_HOSTNAME_MAX + 1];
};

struct bridge_config_tcp_client {
	bool enabled;
	/* Empty means: connect to the STA gateway. */
	char host[BRIDGE_CONFIG_TCP_CLIENT_HOST_MAX + 1];
	uint16_t port;
};

typedef void (*bridge_config_listener_t)(void);

/*
 * True for an empty string or a dotted-quad IPv4 address (octets 0..255).
 * Leading zeros are rejected to match Zephyr's net_addr_pton(), which the
 * TCP NMEA client uses to parse the stored host.
 */
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
		bool leading_zero = host[0] == '0' && host[1] >= '0' && host[1] <= '9';
		int value = 0;
		int digits = 0;

		while (*host >= '0' && *host <= '9' && digits < 4) {
			value = value * 10 + (*host - '0');
			digits++;
			host++;
		}
		if (digits == 0 || digits > 3 || value > 255 || leading_zero) {
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

/*
 * True when the string is strictly valid UTF-8 (rejects overlongs,
 * surrogates, > U+10FFFF) and free of C0 control characters. Stored SSIDs
 * are echoed by the config web API, where anything else would corrupt the
 * JSON response or silently change on the next round trip.
 */
static inline bool bridge_config_text_valid(const char *s)
{
	const uint8_t *p = (const uint8_t *)s;

	while (*p != 0U) {
		uint8_t lead = *p;
		size_t cont;

		if (lead < 0x20U) {
			return false;
		} else if (lead < 0x80U) {
			cont = 0U;
		} else if ((lead & 0xE0U) == 0xC0U && lead >= 0xC2U) {
			cont = 1U;
		} else if ((lead & 0xF0U) == 0xE0U) {
			if ((lead == 0xE0U && (p[1] & 0xE0U) == 0x80U) ||
			    (lead == 0xEDU && (p[1] & 0xE0U) == 0xA0U)) {
				return false;
			}
			cont = 2U;
		} else if ((lead & 0xF8U) == 0xF0U && lead <= 0xF4U) {
			if ((lead == 0xF0U && (p[1] & 0xF0U) == 0x80U) ||
			    (lead == 0xF4U && p[1] > 0x8FU)) {
				return false;
			}
			cont = 3U;
		} else {
			return false;
		}
		p++;
		for (; cont > 0U; cont--, p++) {
			if ((*p & 0xC0U) != 0x80U) {
				return false;
			}
		}
	}
	return true;
}

/*
 * True for a DNS label usable as device hostname: 1..32 characters from
 * [a-z0-9-], neither starting nor ending with a hyphen.
 */
static inline bool bridge_config_hostname_valid(const char *hostname)
{
	size_t len = 0;

	if (hostname == NULL || hostname[0] == '\0' || hostname[0] == '-') {
		return false;
	}

	for (; hostname[len] != '\0'; len++) {
		char ch = hostname[len];

		if (len == BRIDGE_CONFIG_HOSTNAME_MAX) {
			return false;
		}
		if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) {
			return false;
		}
	}
	return hostname[len - 1] != '-';
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

void bridge_config_get_ap(struct bridge_config_ap *out);

/*
 * Validates (SSID 1..32 bytes, PSK empty = open AP or 8..63 chars), persists,
 * and marks a reboot as required when the effective configuration changed.
 * AP is reboot-scope: no listener notification, values are consumed at next
 * boot. AP enable stays Kconfig-only (rescue anchor, ADR 0002).
 */
int bridge_config_set_ap(const struct bridge_config_ap *ap);

void bridge_config_get_system(struct bridge_config_system *out);

/*
 * Validates (hostname a DNS label per bridge_config_hostname_valid()),
 * persists, and marks a reboot as required when the effective configuration
 * changed. Hostname is reboot-scope: applied once at boot.
 */
int bridge_config_set_system(const struct bridge_config_system *system);

/*
 * Deletes every stored override below the bridge settings namespace —
 * whatever the key, so future options are covered automatically — and marks
 * a reboot as required. The effective in-RAM configuration keeps running
 * until reboot; the next boot runs on the Kconfig defaults. Returns the
 * first delete or enumeration-callback error; backend read errors during
 * enumeration are invisible because settings_load_subtree_direct() always
 * reports success. A reboot stays marked if the failed reset already
 * deleted overrides.
 */
int bridge_config_factory_reset(void);

/* True once a reboot-scope value changed since boot. */
bool bridge_config_reboot_required(void);

void bridge_config_set_listener(bridge_config_listener_t listener);

#endif /* BRIDGE_CONFIG_H_ */
