#ifndef TCP_NMEA_SESSION_H_
#define TCP_NMEA_SESSION_H_

#include <stdint.h>

enum tcp_nmea_session_result {
	TCP_NMEA_SESSION_ENDED_PEER_CLOSED,
	TCP_NMEA_SESSION_ENDED_SEND_FAILED,
	TCP_NMEA_SESSION_ENDED_SINK_UNAVAILABLE,
	TCP_NMEA_SESSION_ENDED_RX_FAILED,
};

struct tcp_nmea_session_stats {
	uint32_t active_sessions;
};

/* Runs until the session ends. The caller keeps ownership of fd and closes it. */
enum tcp_nmea_session_result tcp_nmea_session_run(int fd, const char *sink_name);
void tcp_nmea_session_get_stats(struct tcp_nmea_session_stats *stats);

#endif /* TCP_NMEA_SESSION_H_ */
