#ifndef AIS_MMSI_FILTER_H_
#define AIS_MMSI_FILTER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIS_MMSI_FILTER_SEQ_ID_MAX_LEN 8U

struct ais_mmsi_filter_sequence {
	bool active;
	uint8_t total_fragments;
	uint8_t next_fragment;
	char sequence_id[AIS_MMSI_FILTER_SEQ_ID_MAX_LEN + 1U];
	char channel;
};

struct ais_mmsi_filter {
	uint32_t own_mmsi;
	struct ais_mmsi_filter_sequence sequence;
};

void ais_mmsi_filter_init(struct ais_mmsi_filter *filter, uint32_t own_mmsi);
bool ais_mmsi_filter_should_drop(struct ais_mmsi_filter *filter, const void *frame, size_t len);

#endif /* AIS_MMSI_FILTER_H_ */
