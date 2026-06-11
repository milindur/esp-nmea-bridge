#ifndef TCP_NMEA_SERVER_H_
#define TCP_NMEA_SERVER_H_

#include <stdint.h>

struct tcp_nmea_server_stats {
	uint32_t active_peers;
	uint32_t max_peers;
};

int tcp_nmea_server_start(void);
void tcp_nmea_server_get_stats(struct tcp_nmea_server_stats *stats);

#endif /* TCP_NMEA_SERVER_H_ */
