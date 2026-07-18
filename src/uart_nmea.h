#ifndef UART_NMEA_H_
#define UART_NMEA_H_

#include <stdbool.h>
#include <stdint.h>

struct uart_nmea_stats {
	uint32_t bytes_rx;
	uint32_t frames_rx;
	uint32_t overlong_frames;
	uint32_t ais_self_mmsi_filtered;
};

int uart_nmea_start(void);
void uart_nmea_get_stats(struct uart_nmea_stats *stats);

/* Applies live: the RX thread re-initialises the AIS self-MMSI filter between frames. */
void uart_nmea_set_ais_config(bool filter_enabled, uint32_t own_mmsi);

#endif /* UART_NMEA_H_ */
