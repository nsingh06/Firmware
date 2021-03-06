#include <unit_test/unit_test.h>

#include <systemlib/err.h>

#include <stdio.h>
#include <unistd.h>

#include <drivers/drv_hrt.h>

#define DSM_DEBUG
#include <lib/rc/sbus.h>
#include <lib/rc/dsm.h>
#include <lib/rc/st24.h>
#include <lib/rc/sumd.h>

#if !defined(CONFIG_ARCH_BOARD_SITL)
#define TEST_DATA_PATH "/fs/microsd"
#else
#define TEST_DATA_PATH "./test_data/"
#endif

extern "C" __EXPORT int rc_tests_main(int argc, char *argv[]);

class RCTest : public UnitTest
{
public:
	virtual bool run_tests(void);

private:
	bool dsmTest(const char *filepath, unsigned expected_chancount, unsigned expected_dropcount, unsigned chan0);
	bool dsmTest10Ch();
	bool dsmTest12Ch();
	bool sbus2Test();
	bool st24Test();
	bool sumdTest();
};

bool RCTest::run_tests(void)
{
	ut_run_test(dsmTest10Ch);
	ut_run_test(dsmTest12Ch);
	ut_run_test(sbus2Test);
	ut_run_test(st24Test);
	ut_run_test(sumdTest);

	return (_tests_failed == 0);
}

bool RCTest::dsmTest10Ch()
{
	return dsmTest(TEST_DATA_PATH "dsm_x_data.txt", 10, 6, 0);
}

bool RCTest::dsmTest12Ch()
{
	return dsmTest(TEST_DATA_PATH "dsm_x_dx9_data.txt", 12, 11, 1500);
}

bool RCTest::dsmTest(const char *filepath, unsigned expected_chancount, unsigned expected_dropcount, unsigned chan0)
{

	FILE *fp;
	fp = fopen(filepath, "rt");

	ut_test(fp != nullptr);
	//PX4_INFO("loading data from: %s", filepath);

	float f;
	unsigned x;
	int ret;

	// Trash the first 20 lines
	for (unsigned i = 0; i < 20; i++) {
		char buf[200];
		(void)fgets(buf, sizeof(buf), fp);
	}

	// Init the parser
	uint8_t frame[20];
	uint16_t rc_values[18];
	uint16_t num_values;
	bool dsm_11_bit;
	unsigned dsm_frame_drops = 0;
	uint16_t max_channels = sizeof(rc_values) / sizeof(rc_values[0]);

	int rate_limiter = 0;
	unsigned last_drop = 0;

	while (EOF != (ret = fscanf(fp, "%f,%x,,", &f, &x))) {

		ut_test(ret > 0);

		frame[0] = x;
		unsigned len = 1;

		// Pipe the data into the parser
		bool result = dsm_parse(f * 1e6f, &frame[0], len, rc_values, &num_values,
					&dsm_11_bit, &dsm_frame_drops, max_channels);

		if (result) {
			ut_test(num_values == expected_chancount);

			if (chan0 > 0) {
				ut_test((chan0 - rc_values[0]) < 3);
			}

			//PX4_INFO("decoded packet with %d channels and %s encoding:", num_values, (dsm_11_bit) ? "11 bit" : "10 bit");

			for (unsigned i = 0; i < num_values; i++) {
				//PX4_INFO("chan #%u:\t%d", i, (int)rc_values[i]);
			}
		}

		if (last_drop != (dsm_frame_drops)) {
			//PX4_INFO("frame dropped, now #%d", (dsm_frame_drops));
			last_drop = dsm_frame_drops;
		}

		rate_limiter++;
	}

	ut_test(ret == EOF);
	PX4_INFO("drop: %d", (int)last_drop);
	ut_test(last_drop == expected_dropcount);

	return true;
}

bool RCTest::sbus2Test(void)
{
	const char *filepath = TEST_DATA_PATH "sbus2_r7008SB.txt";

	FILE *fp;
	fp = fopen(filepath, "rt");

	ut_test(fp != nullptr);
	//warnx("loading data from: %s", filepath);

	// if (argc < 2)
	// 	errx(1, "Need a filename for the input file");

	//int byte_offset = 7;

	// if (argc > 2) {
	// 	char* end;
	// 	byte_offset = strtol(argv[2],&end,10);
	// }

	//warnx("RUNNING TEST WITH BYTE OFFSET OF: %d", byte_offset);

	float f;
	unsigned x;
	int ret;

	// Trash the first 20 lines
	for (unsigned i = 0; i < 20; i++) {
		char buf[200];
		(void)fgets(buf, sizeof(buf), fp);
	}

	// Init the parser
	uint8_t frame[SBUS_BUFFER_SIZE];
	uint16_t rc_values[18];
	uint16_t num_values;
	unsigned sbus_frame_drops = 0;
	unsigned sbus_frame_resets = 0;
	bool sbus_failsafe;
	bool sbus_frame_drop;
	uint16_t max_channels = sizeof(rc_values) / sizeof(rc_values[0]);

	int rate_limiter = 0;
	unsigned last_drop = 0;

	while (EOF != (ret = fscanf(fp, "%f,%x,,", &f, &x))) {

		ut_test(ret > 0);

		frame[0] = x;
		unsigned len = 1;

		// Pipe the data into the parser
		hrt_abstime now = hrt_absolute_time();

		// if (rate_limiter % byte_offset == 0) {
		bool result = sbus_parse(now, &frame[0], len, rc_values, &num_values,
					 &sbus_failsafe, &sbus_frame_drop, &sbus_frame_drops, max_channels);

		if (result) {
			//warnx("decoded packet");
		}

		// }

		if (last_drop != (sbus_frame_drops + sbus_frame_resets)) {
			PX4_WARN("frame dropped, now #%d", (sbus_frame_drops + sbus_frame_resets));
			last_drop = sbus_frame_drops + sbus_frame_resets;
		}

		rate_limiter++;
	}

	ut_test(ret == EOF);

	return true;
}

bool RCTest::st24Test(void)
{
	const char *filepath = TEST_DATA_PATH "st24_data.txt";

	//warnx("loading data from: %s", filepath);

	FILE *fp;

	fp = fopen(filepath, "rt");
	ut_test(fp != nullptr);

	float f;
	unsigned x;
	int ret;

	// Trash the first 20 lines
	for (unsigned i = 0; i < 20; i++) {
		char buf[200];
		(void)fgets(buf, sizeof(buf), fp);
	}

	float last_time = 0;

	while (EOF != (ret = fscanf(fp, "%f,%x,,", &f, &x))) {
		ut_test(ret > 0);

		if (((f - last_time) * 1000 * 1000) > 3000) {
			// warnx("FRAME RESET\n\n");
		}

		uint8_t b = static_cast<uint8_t>(x);

		last_time = f;

		// Pipe the data into the parser
		//hrt_abstime now = hrt_absolute_time();

		uint8_t rssi;
		uint8_t rx_count;
		uint16_t channel_count;
		uint16_t channels[20];

		if (!st24_decode(b, &rssi, &rx_count, &channel_count, channels, sizeof(channels) / sizeof(channels[0]))) {
			//warnx("decoded: %u channels (converted to PPM range)", (unsigned)channel_count);

			for (unsigned i = 0; i < channel_count; i++) {
				//int16_t val = channels[i];
				//warnx("channel %u: %d 0x%03X", i, static_cast<int>(val), static_cast<int>(val));
			}
		}
	}

	ut_test(ret == EOF);

	return true;
}

bool RCTest::sumdTest(void)
{
	const char *filepath = TEST_DATA_PATH "sumd_data.txt";

	//warnx("loading data from: %s", filepath);

	FILE *fp;

	fp = fopen(filepath, "rt");
	ut_test(fp != nullptr);

	float f;
	unsigned x;
	int ret;

	// Trash the first 20 lines
	for (unsigned i = 0; i < 20; i++) {
		char buf[200];
		(void)fgets(buf, sizeof(buf), fp);
	}

	float last_time = 0;

	while (EOF != (ret = fscanf(fp, "%f,%x,,", &f, &x))) {
		ut_test(ret > 0);

		if (((f - last_time) * 1000 * 1000) > 3000) {
			// warnx("FRAME RESET\n\n");
		}

		uint8_t b = static_cast<uint8_t>(x);

		last_time = f;

		// Pipe the data into the parser
		//hrt_abstime now = hrt_absolute_time();

		uint8_t rssi;
		uint8_t rx_count;
		uint16_t channel_count;
		uint16_t channels[32];


		if (!sumd_decode(b, &rssi, &rx_count, &channel_count, channels, 32)) {
			//PX4_INFO("decoded: %u channels (converted to PPM range)", (unsigned)channel_count);

			for (unsigned i = 0; i < channel_count; i++) {
				//int16_t val = channels[i];
				//PX4_INFO("channel %u: %d 0x%03X", i, static_cast<int>(val), static_cast<int>(val));
			}
		}
	}

	ut_test(ret == EOF);

	return true;
}



ut_declare_test_c(rc_tests_main, RCTest)

