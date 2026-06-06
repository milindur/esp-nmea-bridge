#ifndef UART_NMEA_H_
#define UART_NMEA_H_

#include <stdint.h>

struct uart_nmea_stats {
	uint32_t bytes_rx;
	uint32_t lines_rx;
	uint32_t overlong_lines;
	uint32_t ais_self_mmsi_filtered;
};

int uart_nmea_start(void);
void uart_nmea_get_stats(struct uart_nmea_stats *stats);

#endif /* UART_NMEA_H_ */
