#include "web_app.h"

#include <zephyr/net/dns_sd.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_DNS_SD_ENABLE) && \
	IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_ENABLE)

/* DNS-SD TXT records are length-prefixed strings as specified by RFC 6763.
 * This stream is a read-only MAIANA RX passthrough containing GPS and AIS RX
 * NMEA-0183 sentences.
 */
static const char nmea_txt[] = {
	"\x09" "txtvers=1"
	"\x0d" "talkers=GP,AI"
	"\x12" "content=gps,ais-rx"
	"\x0d" "source=maiana"
	"\x04" "ro=1"
};

DNS_SD_REGISTER_TCP_SERVICE(nmea0183,
			    CONFIG_NET_HOSTNAME,
			    "_nmea-0183",
			    "local",
			    nmea_txt,
			    CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_PORT);

#endif

#if IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_DNS_SD_ENABLE) && \
	IS_ENABLED(CONFIG_ESP_NMEA_BRIDGE_WEB_APP_ENABLE)

/* Advertise the built-in status web app for browsers and generic service
 * discovery clients. The HTTP service uses the same instance name as the
 * device hostname so it appears as <hostname>._http._tcp.local.
 */
static const char http_txt[] = {
	"\x09" "txtvers=1"
	"\x06" "path=/"
};

DNS_SD_REGISTER_TCP_SERVICE(http,
			    CONFIG_NET_HOSTNAME,
			    "_http",
			    "local",
			    http_txt,
			    WEB_APP_HTTP_PORT);

#endif
