#include "ais_mmsi_filter.h"

#include <ctype.h>
#include <string.h>

struct field_view {
	const char *ptr;
	size_t len;
};

static int hex_value(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'A' && ch <= 'F') {
		return ch - 'A' + 10;
	}
	if (ch >= 'a' && ch <= 'f') {
		return ch - 'a' + 10;
	}
	return -1;
}

static bool checksum_is_valid(const char *frame, size_t len, size_t *body_start,
				      size_t *body_len)
{
	if (len < 5U || (frame[0] != '!' && frame[0] != '$')) {
		return false;
	}

	size_t star = 0U;
	for (size_t i = 1U; i < len; i++) {
		if (frame[i] == '*') {
			star = i;
			break;
		}
	}
	if (star == 0U || star + 2U >= len) {
		return false;
	}

	int hi = hex_value(frame[star + 1U]);
	int lo = hex_value(frame[star + 2U]);
	if (hi < 0 || lo < 0) {
		return false;
	}

	uint8_t actual = 0U;
	for (size_t i = 1U; i < star; i++) {
		actual ^= (uint8_t)frame[i];
	}

	if (actual != (uint8_t)((hi << 4) | lo)) {
		return false;
	}

	*body_start = 1U;
	*body_len = star - 1U;
	return true;
}

static bool split_fields(const char *body, size_t body_len, struct field_view *fields,
				 size_t field_count)
{
	size_t field = 0U;
	size_t start = 0U;

	for (size_t i = 0U; i <= body_len; i++) {
		if (i != body_len && body[i] != ',') {
			continue;
		}

		if (field >= field_count) {
			return true;
		}

		fields[field].ptr = &body[start];
		fields[field].len = i - start;
		field++;
		start = i + 1U;
	}

	return field >= field_count;
}

static bool parse_uint_field(const struct field_view *field, uint8_t *value)
{
	if (field->len == 0U || field->len > 3U) {
		return false;
	}

	uint16_t parsed = 0U;
	for (size_t i = 0U; i < field->len; i++) {
		if (!isdigit((unsigned char)field->ptr[i])) {
			return false;
		}
		parsed = (uint16_t)(parsed * 10U + (uint16_t)(field->ptr[i] - '0'));
		if (parsed > UINT8_MAX) {
			return false;
		}
	}

	*value = (uint8_t)parsed;
	return true;
}

static int ais_sixbit_value(char ch)
{
	unsigned char c = (unsigned char)ch;

	if (c < 48U || c > 119U) {
		return -1;
	}

	int value = (int)c - 48;
	if (value > 40) {
		value -= 8;
	}
	if (value < 0 || value > 63) {
		return -1;
	}
	return value;
}

static bool decode_mmsi(const struct field_view *payload, uint32_t *mmsi)
{
	if (payload->len < 7U) {
		return false;
	}

	uint64_t bits = 0U;
	for (size_t i = 0U; i < 7U; i++) {
		int value = ais_sixbit_value(payload->ptr[i]);
		if (value < 0) {
			return false;
		}
		bits = (bits << 6) | (uint64_t)value;
	}

	*mmsi = (uint32_t)((bits >> 4) & 0x3fffffffU);
	return true;
}

static bool formatter_is_vdm(const struct field_view *formatter)
{
	return formatter->len == 5U && formatter->ptr[2] == 'V' && formatter->ptr[3] == 'D' &&
	       formatter->ptr[4] == 'M';
}

static char channel_value(const struct field_view *channel)
{
	return channel->len == 1U ? channel->ptr[0] : '\0';
}

static bool sequence_matches(const struct ais_mmsi_filter_sequence *sequence,
			     const struct field_view *seq_id, char channel, uint8_t total,
			     uint8_t fragment)
{
	return sequence->active && sequence->total_fragments == total &&
	       sequence->next_fragment == fragment && sequence->channel == channel &&
	       strlen(sequence->sequence_id) == seq_id->len &&
	       strncmp(sequence->sequence_id, seq_id->ptr, seq_id->len) == 0;
}

static void remember_sequence(struct ais_mmsi_filter *filter, const struct field_view *seq_id,
			      char channel, uint8_t total)
{
	if (seq_id->len == 0U || seq_id->len > AIS_MMSI_FILTER_SEQ_ID_MAX_LEN || total < 2U) {
		filter->sequence.active = false;
		return;
	}

	filter->sequence.active = true;
	filter->sequence.total_fragments = total;
	filter->sequence.next_fragment = 2U;
	filter->sequence.channel = channel;
	memcpy(filter->sequence.sequence_id, seq_id->ptr, seq_id->len);
	filter->sequence.sequence_id[seq_id->len] = '\0';
}

void ais_mmsi_filter_init(struct ais_mmsi_filter *filter, uint32_t own_mmsi)
{
	if (filter == NULL) {
		return;
	}

	memset(filter, 0, sizeof(*filter));
	filter->own_mmsi = own_mmsi;
}

bool ais_mmsi_filter_should_drop(struct ais_mmsi_filter *filter, const void *frame, size_t len)
{
	if (filter == NULL || frame == NULL || len == 0U || filter->own_mmsi == 0U) {
		return false;
	}

	const char *text = frame;
	size_t body_start;
	size_t body_len;
	if (!checksum_is_valid(text, len, &body_start, &body_len)) {
		return false;
	}

	struct field_view fields[7] = { 0 };
	if (!split_fields(&text[body_start], body_len, fields, 7U) || !formatter_is_vdm(&fields[0])) {
		return false;
	}

	uint8_t total;
	uint8_t fragment;
	if (!parse_uint_field(&fields[1], &total) || !parse_uint_field(&fields[2], &fragment) ||
	    total == 0U || fragment == 0U || fragment > total) {
		return false;
	}

	char channel = channel_value(&fields[4]);

	if (fragment > 1U) {
		if (!sequence_matches(&filter->sequence, &fields[3], channel, total, fragment)) {
			return false;
		}
		filter->sequence.next_fragment++;
		if (filter->sequence.next_fragment > filter->sequence.total_fragments) {
			filter->sequence.active = false;
		}
		return true;
	}

	uint32_t mmsi;
	if (!decode_mmsi(&fields[5], &mmsi) || mmsi != filter->own_mmsi) {
		return false;
	}

	if (total > 1U) {
		remember_sequence(filter, &fields[3], channel, total);
	} else {
		filter->sequence.active = false;
	}

	return true;
}
