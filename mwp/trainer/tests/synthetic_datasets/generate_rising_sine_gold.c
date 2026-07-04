#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_OUT_PATH "/home/mf/tdata/XAUUSD5_sine_rising.csv"
#define DEFAULT_ROWS 50000
#define BAR_SECONDS (5 * 60)
#define PI_VALUE 3.14159265358979323846

static void write_dataset(const char *out_path, int rows)
{
	FILE *out;
	time_t start_ts;
	struct tm start_tm;
	const double base_price = 3300.0;
	const double trend_angle_deg = 10.0;
	const double slope_per_bar = tan(trend_angle_deg * PI_VALUE / 180.0) * 3.20;
	const double sine_period = 36.0;
	const double sine_amplitude = 12.0;
	const double body_period = 12.0;
	const double body_amplitude = 1.6;
	const double upper_period = 9.0;
	const double lower_period = 10.0;
	const double upper_base = 0.55;
	const double upper_amplitude = 0.85;
	const double lower_base = 0.50;
	const double lower_amplitude = 0.95;
	const double volume_base = 850.0;
	const double volume_amplitude = 280.0;
	const int range_extension_candles = 5;
	const double range_fluctuation_amplitude = 0.45;
	const int minimum_phase_index = (int)(sine_period * 0.75);
	double prev_close = base_price;
	double range_center = base_price;
	int logical_index = 0;
	int plateau_remaining = 0;
	int plateau_step = 0;
	int last_plateau_cycle = -1;
	int i;

	out = fopen(out_path, "w");
	if (!out) {
		perror(out_path);
		exit(1);
	}

	start_tm = (struct tm){0};
	start_tm.tm_year = 2025 - 1900;
	start_tm.tm_mon = 0;
	start_tm.tm_mday = 1;
	start_tm.tm_hour = 0;
	start_tm.tm_min = 0;
	start_tm.tm_sec = 0;
	start_tm.tm_isdst = -1;
	start_ts = mktime(&start_tm);
	if (start_ts == (time_t)-1) {
		fprintf(stderr, "failed to initialize start timestamp\n");
		fclose(out);
		exit(1);
	}

	for (i = 0; i < rows; ++i) {
		time_t ts = start_ts + (time_t)i * BAR_SECONDS;
		struct tm tm_value;
		const int cycle_index = (int)(logical_index / (int)sine_period);
		const int phase_index = logical_index % (int)sine_period;
		const double phase = (2.0 * PI_VALUE * (double)logical_index);
		double close_signal;
		double upper_wick;
		double lower_wick;
		int volume;

		if (plateau_remaining > 0) {
			const double range_phase =
				(2.0 * PI_VALUE * (double)plateau_step) / (double)range_extension_candles;

			close_signal =
				range_center +
				range_fluctuation_amplitude * cos(range_phase);
			upper_wick =
				upper_base +
				upper_amplitude * (0.5 + 0.5 * cos(range_phase));
			lower_wick =
				lower_base +
				lower_amplitude * (0.5 + 0.5 * cos(range_phase + (PI_VALUE / 3.0)));
			volume = (int)lround(
				volume_base +
				volume_amplitude * (0.5 + 0.5 * cos(range_phase + (PI_VALUE / 5.0))));
			plateau_remaining--;
			plateau_step++;
		} else {
			close_signal =
				base_price +
				slope_per_bar * (double)logical_index +
				sine_amplitude * sin(phase / sine_period) +
				body_amplitude * cos(phase / body_period);
			upper_wick =
				upper_base +
				upper_amplitude * (0.5 + 0.5 * cos(phase / upper_period));
			lower_wick =
				lower_base +
				lower_amplitude * (0.5 + 0.5 * cos((phase / lower_period) + (PI_VALUE / 3.0)));
			volume = (int)lround(
				volume_base +
				volume_amplitude * (0.5 + 0.5 * cos((phase / 72.0) + (PI_VALUE / 5.0))));
			if (phase_index == minimum_phase_index && last_plateau_cycle != cycle_index) {
				range_center = close_signal;
				plateau_remaining = range_extension_candles;
				plateau_step = 0;
				last_plateau_cycle = cycle_index;
			}
			logical_index++;
		}

		const double open_price = (i == 0) ? base_price : prev_close;
		const double close_price = close_signal;
		const double high_price =
			(open_price > close_price ? open_price : close_price) + upper_wick;
		const double low_price =
			(open_price < close_price ? open_price : close_price) - lower_wick;

		if (volume < 1) {
			volume = 1;
		}

		localtime_r(&ts, &tm_value);
		fprintf(
			out,
			"%04d.%02d.%02d,%02d:%02d,%.5f,%.5f,%.5f,%.5f,%d\n",
			tm_value.tm_year + 1900,
			tm_value.tm_mon + 1,
			tm_value.tm_mday,
			tm_value.tm_hour,
			tm_value.tm_min,
			open_price,
			high_price,
			low_price,
			close_price,
			volume);
		prev_close = close_price;
	}

	fclose(out);
}

int main(int argc, char **argv)
{
	const char *out_path = DEFAULT_OUT_PATH;
	int rows = DEFAULT_ROWS;

	if (argc >= 2) {
		out_path = argv[1];
	}
	if (argc >= 3) {
		rows = atoi(argv[2]);
		if (rows <= 0) {
			fprintf(stderr, "invalid row count: %s\n", argv[2]);
			return 1;
		}
	}

	write_dataset(out_path, rows);
	printf("wrote %d rows to %s\n", rows, out_path);
	return 0;
}
