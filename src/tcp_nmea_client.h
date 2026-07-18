#ifndef TCP_NMEA_CLIENT_H_
#define TCP_NMEA_CLIENT_H_

int tcp_nmea_client_start(void);

/*
 * Live apply for the TCP NMEA client bridge configuration: closes the
 * current TCP NMEA session (if any) and wakes the reconnect loop so it
 * picks up the new enable/host/port values immediately.
 */
void tcp_nmea_client_config_changed(void);

#endif /* TCP_NMEA_CLIENT_H_ */
