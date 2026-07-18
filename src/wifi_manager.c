#include "wifi_manager.h"

#include <string.h>

#include <esp_wifi.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define WIFI_EVENT_MASK (NET_EVENT_WIFI_CONNECT_RESULT | \
			 NET_EVENT_WIFI_DISCONNECT_RESULT | \
			 NET_EVENT_WIFI_AP_ENABLE_RESULT | \
			 NET_EVENT_WIFI_AP_DISABLE_RESULT | \
			 NET_EVENT_WIFI_AP_STA_CONNECTED | \
			 NET_EVENT_WIFI_AP_STA_DISCONNECTED)

#define IPV4_EVENT_MASK (NET_EVENT_IPV4_ADDR_ADD | \
			 NET_EVENT_IPV4_DHCP_BOUND | \
			 NET_EVENT_IPV4_DHCP_STOP)

static struct net_if *ap_iface;
static struct net_if *sta_iface;
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static bool sta_connected;
static bool sta_has_addr;
static bool sta_ipv4_logged;
static bool sta_connect_requested;
static bool sta_retry_enabled;
static uint32_t sta_retry_backoff_s = 5;

static struct net_in_addr *sta_dhcp_addr(void)
{
	if (sta_iface == NULL || sta_iface->config.ip.ipv4 == NULL) {
		return NULL;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		struct net_if_addr_ipv4 *unicast = &sta_iface->config.ip.ipv4->unicast[i];

		if (unicast->ipv4.addr_type != NET_ADDR_DHCP) {
			continue;
		}

		if (net_ipv4_is_addr_unspecified(&unicast->ipv4.address.in_addr)) {
			continue;
		}

		return &unicast->ipv4.address.in_addr;
	}

	return NULL;
}

static bool mark_sta_ipv4_ready(void)
{
	struct net_in_addr *addr = sta_dhcp_addr();
	struct net_in_addr gw;
	char addr_buf[NET_IPV4_ADDR_LEN];
	char gw_buf[NET_IPV4_ADDR_LEN];

	if (addr == NULL) {
		return false;
	}

	sta_connected = true;
	sta_has_addr = true;

	if (sta_ipv4_logged) {
		return true;
	}

	sta_ipv4_logged = true;
	net_addr_ntop(AF_INET, addr, addr_buf, sizeof(addr_buf));

	gw = net_if_ipv4_get_gw(sta_iface);
	if (net_ipv4_is_addr_unspecified(&gw)) {
		LOG_INF("STA IPv4 ready: addr %s gateway unavailable", addr_buf);
		return true;
	}

	net_addr_ntop(AF_INET, &gw, gw_buf, sizeof(gw_buf));
	LOG_INF("STA IPv4 ready: addr %s gateway %s", addr_buf, gw_buf);
	return true;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		struct wifi_status *status = (struct wifi_status *)cb->info;

		if (iface != sta_iface) {
			break;
		}

		if (status != NULL && status->status != 0) {
			sta_connected = false;
			sta_has_addr = false;
			sta_ipv4_logged = false;
			sta_connect_requested = false;
			LOG_WRN("STA connect to %s failed: status=%d (%s); retry in %us",
				CONFIG_ESP_NMEA_BRIDGE_STA_SSID, status->status,
				wifi_conn_status_txt(status->conn_status), sta_retry_backoff_s);
			break;
		}

		sta_connected = true;
		sta_connect_requested = false;
		sta_retry_backoff_s = 5;
		LOG_INF("STA connected to %s", CONFIG_ESP_NMEA_BRIDGE_STA_SSID);
		(void)mark_sta_ipv4_ready();
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		if (iface != sta_iface) {
			break;
		}
		sta_connected = false;
		sta_has_addr = false;
		sta_ipv4_logged = false;
		sta_connect_requested = false;
		LOG_INF("STA disconnected from %s", CONFIG_ESP_NMEA_BRIDGE_STA_SSID);
		break;
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
		LOG_INF("SoftAP enabled");
		break;
	case NET_EVENT_WIFI_AP_DISABLE_RESULT:
		LOG_INF("SoftAP disabled");
		break;
	case NET_EVENT_WIFI_AP_STA_CONNECTED: {
		struct wifi_ap_sta_info *sta_info = (struct wifi_ap_sta_info *)cb->info;

		LOG_INF("AP client joined: " MACSTR, sta_info->mac[0], sta_info->mac[1],
			sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		break;
	}
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
		struct wifi_ap_sta_info *sta_info = (struct wifi_ap_sta_info *)cb->info;

		LOG_INF("AP client left: " MACSTR, sta_info->mac[0], sta_info->mac[1],
			sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		break;
	}
	default:
		break;
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	ARG_UNUSED(cb);

	switch (mgmt_event) {
	case NET_EVENT_IPV4_ADDR_ADD:
	case NET_EVENT_IPV4_DHCP_BOUND:
		/* Zephyr's DHCP samples react to the IPv4 event and inspect the
		 * interface address table. Do the same for the STA interface instead
		 * of depending on the Wi-Fi connect event carrying IP readiness.
		 */
		(void)mark_sta_ipv4_ready();
		break;
	case NET_EVENT_IPV4_DHCP_STOP:
		if (iface == sta_iface) {
			sta_has_addr = false;
			sta_ipv4_logged = false;
		}
		break;
	default:
		break;
	}
}

static int start_dhcp_server(void)
{
	static struct net_in_addr addr;
	static struct net_in_addr netmask;
	struct net_in_addr pool_start;
	int ret;

	if (net_addr_pton(AF_INET, CONFIG_ESP_NMEA_BRIDGE_AP_IP_ADDRESS, &addr) != 0) {
		LOG_ERR("Invalid AP IP: %s", CONFIG_ESP_NMEA_BRIDGE_AP_IP_ADDRESS);
		return -EINVAL;
	}

	if (net_addr_pton(AF_INET, CONFIG_ESP_NMEA_BRIDGE_AP_NETMASK, &netmask) != 0) {
		LOG_ERR("Invalid AP netmask: %s", CONFIG_ESP_NMEA_BRIDGE_AP_NETMASK);
		return -EINVAL;
	}

	net_if_ipv4_set_gw(ap_iface, &addr);
	if (net_if_ipv4_addr_add(ap_iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		LOG_WRN("Unable to add AP IPv4 address; it may already be configured");
	}

	if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &addr, &netmask)) {
		LOG_ERR("Unable to set AP netmask");
		return -EIO;
	}

	pool_start = addr;
	pool_start.s4_addr[3] += 10;

	ret = net_dhcpv4_server_start(ap_iface, &pool_start);
	if (ret != 0) {
		LOG_ERR("DHCPv4 server start failed: %d", ret);
		return ret;
	}

	LOG_INF("DHCPv4 server started on %s", CONFIG_ESP_NMEA_BRIDGE_AP_IP_ADDRESS);
	return 0;
}

static int enable_ap(void)
{
	static struct wifi_connect_req_params ap_config;
	int ret;

	if (ap_iface == NULL) {
		return -ENODEV;
	}

	memset(&ap_config, 0, sizeof(ap_config));
	ap_config.ssid = (const uint8_t *)CONFIG_ESP_NMEA_BRIDGE_AP_SSID;
	ap_config.ssid_length = strlen(CONFIG_ESP_NMEA_BRIDGE_AP_SSID);
	ap_config.psk = (const uint8_t *)CONFIG_ESP_NMEA_BRIDGE_AP_PSK;
	ap_config.psk_length = strlen(CONFIG_ESP_NMEA_BRIDGE_AP_PSK);
	ap_config.channel = WIFI_CHANNEL_ANY;
	ap_config.band = WIFI_FREQ_BAND_2_4_GHZ;
	ap_config.security = ap_config.psk_length == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;

	ret = start_dhcp_server();
	if (ret != 0) {
		return ret;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &ap_config, sizeof(ap_config));
	if (ret != 0) {
		LOG_ERR("SoftAP enable failed: %d", ret);
		return ret;
	}

	LOG_INF("SoftAP enable requested: ssid=%s security=%s", CONFIG_ESP_NMEA_BRIDGE_AP_SSID,
		ap_config.security == WIFI_SECURITY_TYPE_NONE ? "open" : "WPA2-PSK");
	return 0;
}

static void disable_sta_power_save(void)
{
	struct wifi_ps_params params = {
		.enabled = WIFI_PS_DISABLED,
		.type = WIFI_PS_PARAM_STATE,
	};
	int ret;

	if (sta_iface == NULL) {
		return;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_PS, sta_iface, &params, sizeof(params));
	if (ret != 0) {
		LOG_WRN("Unable to disable STA Wi-Fi power save via Zephyr: %d", ret);
	}
}

static void rotate_sta_mac(void)
{
	struct ethernet_req_params params = { 0 };
	struct net_linkaddr *link_addr;
	bool was_up;
	int ret;

	if (!IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_STA_ROTATE_MAC) || sta_iface == NULL) {
		return;
	}

	/* Zephyr's ESP32 driver forwards MAC changes to esp_wifi_set_mac().
	 * That ESP-IDF API rejects STA MAC changes while Wi-Fi mode is NULL
	 * (ESP_ERR_WIFI_MODE). Select STA mode before the Zephyr net_mgmt
	 * request so the STA interface exists but is still not connected.
	 */
	ret = esp_wifi_set_mode(ESP32_WIFI_MODE_STA);
	if (ret != 0) {
		LOG_WRN("STA MAC rotation skipped: set STA mode failed: %d", ret);
		return;
	}

	link_addr = net_if_get_link_addr(sta_iface);
	if (link_addr == NULL || link_addr->len != sizeof(params.mac_address.addr)) {
		LOG_WRN("STA MAC rotation skipped: link address unavailable");
		return;
	}

	memcpy(params.mac_address.addr, link_addr->addr, sizeof(params.mac_address.addr));
	sys_rand_get(&params.mac_address.addr[3], 3);
	params.mac_address.addr[0] = (params.mac_address.addr[0] & 0xFC) | 0x02;

	was_up = net_if_is_admin_up(sta_iface);
	if (was_up) {
		ret = net_if_down(sta_iface);
		if (ret != 0) {
			LOG_WRN("STA MAC rotation skipped: iface down failed: %d", ret);
			return;
		}
	}

	ret = net_mgmt(NET_REQUEST_ETHERNET_SET_MAC_ADDRESS, sta_iface,
		       &params, sizeof(params));
	if (was_up) {
		int up_ret = net_if_up(sta_iface);

		if (up_ret != 0) {
			LOG_WRN("STA iface up after MAC rotation failed: %d", up_ret);
		}
	}

	if (ret != 0) {
		LOG_WRN("STA MAC rotation failed: %d", ret);
		return;
	}

	LOG_INF("STA MAC rotated to %02x:%02x:%02x:%02x:%02x:%02x",
		params.mac_address.addr[0], params.mac_address.addr[1],
		params.mac_address.addr[2], params.mac_address.addr[3],
		params.mac_address.addr[4], params.mac_address.addr[5]);
}

static void disconnect_sta(void)
{
	int ret;

	if (sta_iface == NULL) {
		return;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);
	if (ret != 0) {
		LOG_DBG("STA disconnect request returned %d", ret);
	}
}

static int connect_sta(void)
{
	static struct wifi_connect_req_params sta_config;
	int ret;

	if (sta_iface == NULL) {
		return -ENODEV;
	}

	if (strlen(CONFIG_ESP_NMEA_BRIDGE_STA_SSID) == 0) {
		LOG_WRN("STA SSID empty; STA connect skipped");
		return 0;
	}

	memset(&sta_config, 0, sizeof(sta_config));
	sta_config.ssid = (const uint8_t *)CONFIG_ESP_NMEA_BRIDGE_STA_SSID;
	sta_config.ssid_length = strlen(CONFIG_ESP_NMEA_BRIDGE_STA_SSID);
	sta_config.psk = (const uint8_t *)CONFIG_ESP_NMEA_BRIDGE_STA_PSK;
	sta_config.psk_length = strlen(CONFIG_ESP_NMEA_BRIDGE_STA_PSK);
	sta_config.security = sta_config.psk_length == 0 ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
	sta_config.channel = WIFI_CHANNEL_ANY;
	sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;

	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_config, sizeof(sta_config));
	if (ret != 0) {
		sta_connect_requested = false;
		LOG_ERR("STA connect request failed: %d", ret);
		return ret;
	}

	sta_connect_requested = true;
	disable_sta_power_save();
	LOG_INF("STA connect requested: ssid=%s security=%s channel=any",
		CONFIG_ESP_NMEA_BRIDGE_STA_SSID, wifi_security_txt(sta_config.security));
	return 0;
}

static void sta_retry_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	if (!IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_STA_ENABLE) ||
	    strlen(CONFIG_ESP_NMEA_BRIDGE_STA_SSID) == 0) {
		return;
	}

	while (!sta_retry_enabled) {
		k_sleep(K_SECONDS(1));
	}

	for (;;) {
		if (sta_connected || sta_connect_requested) {
			k_sleep(K_SECONDS(1));
			continue;
		}

		k_sleep(K_SECONDS(sta_retry_backoff_s));

		if (sta_connected || sta_connect_requested) {
			continue;
		}

		LOG_INF("STA reconnecting to %s after %us backoff",
			CONFIG_ESP_NMEA_BRIDGE_STA_SSID, sta_retry_backoff_s);
		disconnect_sta();
		k_sleep(K_MSEC(500));

		if (connect_sta() == 0) {
			sta_retry_backoff_s = MIN(sta_retry_backoff_s * 2U, 60U);
		} else {
			sta_retry_backoff_s = MIN(sta_retry_backoff_s * 2U, 60U);
		}
	}
}

K_THREAD_DEFINE(sta_retry_tid, 2048, sta_retry_thread, NULL, NULL, NULL,
	       10, 0, 0);

int wifi_manager_start(void)
{
	int ret = 0;

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, IPV4_EVENT_MASK);
	net_mgmt_add_event_callback(&ipv4_cb);

	ap_iface = net_if_get_wifi_sap();
	sta_iface = net_if_get_wifi_sta();

	LOG_INF("Wi-Fi interfaces: ap=%p sta=%p", ap_iface, sta_iface);
	rotate_sta_mac();

	/* Follow Zephyr's AP+STA sample: start SoftAP first, then request STA connect.
	 * The ESP32 driver is expected to manage AP+STA channel coexistence.
	 */
	if (IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_AP_ENABLE)) {
		ret = enable_ap();
		if (ret != 0) {
			LOG_ERR("AP startup failed: %d", ret);
		}
	}

	if (IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_STA_ENABLE)) {
		disconnect_sta();
		k_sleep(K_MSEC(500));

		int sta_ret = connect_sta();

		if (sta_ret != 0 && ret == 0) {
			ret = sta_ret;
		}
	}

	sta_retry_enabled = true;
	return ret;
}

struct net_if *wifi_manager_sta_iface(void)
{
	return sta_iface;
}

bool wifi_manager_sta_ready(void)
{
	if (sta_connected && !sta_has_addr) {
		(void)mark_sta_ipv4_ready();
	}

	return sta_connected && sta_has_addr;
}

bool wifi_manager_get_sta_ipv4(char *buf, size_t buf_len)
{
	struct net_in_addr *addr;

	if (buf == NULL || buf_len < NET_IPV4_ADDR_LEN || !wifi_manager_sta_ready()) {
		return false;
	}

	addr = sta_dhcp_addr();
	if (addr == NULL) {
		return false;
	}

	return net_addr_ntop(AF_INET, addr, buf, buf_len) != NULL;
}

bool wifi_manager_get_sta_rssi(int *rssi_dbm)
{
	struct wifi_iface_status status = { 0 };

	if (rssi_dbm == NULL || sta_iface == NULL || !sta_connected) {
		return false;
	}

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, sta_iface, &status, sizeof(status)) != 0) {
		return false;
	}

	if (status.state != WIFI_STATE_COMPLETED) {
		return false;
	}

	*rssi_dbm = status.rssi;
	return true;
}

bool wifi_manager_get_sta_gateway(struct net_in_addr *gw)
{
	if (gw == NULL || !wifi_manager_sta_ready()) {
		return false;
	}

	*gw = net_if_ipv4_get_gw(sta_iface);
	return !net_ipv4_is_addr_unspecified(gw);
}
