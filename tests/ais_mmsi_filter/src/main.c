#include <string.h>

#include <zephyr/ztest.h>

#include "ais_mmsi_filter.h"

static bool should_drop(uint32_t own_mmsi, const char *frame)
{
	struct ais_mmsi_filter filter;

	ais_mmsi_filter_init(&filter, own_mmsi);
	return ais_mmsi_filter_should_drop(&filter, frame, strlen(frame));
}

ZTEST(ais_mmsi_filter, test_matching_self_mmsi_vdm_frame_is_dropped)
{
	zassert_true(should_drop(123456789U, "!AIVDM,1,1,,A,11mg=5@,0*64\r\n"));
}

ZTEST(ais_mmsi_filter, test_non_matching_vdm_frame_is_kept)
{
	zassert_false(should_drop(123456789U, "!AIVDM,1,1,,A,1>eq`d@,0*79\r\n"));
}

ZTEST(ais_mmsi_filter, test_vdm_from_any_talker_is_filtered)
{
	zassert_true(should_drop(123456789U, "!ABVDM,1,1,,A,11mg=5@,0*6F\r\n"));
}

ZTEST(ais_mmsi_filter, test_vdo_frame_is_kept)
{
	zassert_false(should_drop(123456789U, "!AIVDO,1,1,,A,11mg=5@,0*66\r\n"));
}

ZTEST(ais_mmsi_filter, test_missing_or_bad_checksum_is_kept)
{
	zassert_false(should_drop(123456789U, "!AIVDM,1,1,,A,11mg=5@,0\r\n"));
	zassert_false(should_drop(123456789U, "!AIVDM,1,1,,A,11mg=5@,0*00\r\n"));
}

ZTEST(ais_mmsi_filter, test_matching_multipart_sequence_with_sequence_id_is_dropped)
{
	struct ais_mmsi_filter filter;
	const char first[] = "!AIVDM,2,1,7,B,11mg=5@ABCDE,0*12\r\n";
	const char second[] = "!AIVDM,2,2,7,B,BBBBBB,0*12\r\n";

	ais_mmsi_filter_init(&filter, 123456789U);

	zassert_true(ais_mmsi_filter_should_drop(&filter, first, sizeof(first) - 1U));
	zassert_true(ais_mmsi_filter_should_drop(&filter, second, sizeof(second) - 1U));
}

ZTEST(ais_mmsi_filter, test_multipart_without_sequence_id_keeps_following_fragment)
{
	struct ais_mmsi_filter filter;
	const char first[] = "!AIVDM,2,1,,B,11mg=5@ABCDE,0*25\r\n";
	const char second[] = "!AIVDM,2,2,,B,BBBBBB,0*25\r\n";

	ais_mmsi_filter_init(&filter, 123456789U);

	zassert_true(ais_mmsi_filter_should_drop(&filter, first, sizeof(first) - 1U));
	zassert_false(ais_mmsi_filter_should_drop(&filter, second, sizeof(second) - 1U));
}

ZTEST_SUITE(ais_mmsi_filter, NULL, NULL, NULL, NULL, NULL);
