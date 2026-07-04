#include "core_api.h"
#include "internal_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LINE_BUF_SZ 1024

static int parse_timestamp_epoch(const char *d, const char *t, int32_t *out_epoch)
{
	struct tm tm_val;
	int yy;
	int mm;
	int dd;
	int hh;
	int min;
	time_t ts;

	if (d == NULL || t == NULL || out_epoch == NULL) {
		return -1;
	}
	if (sscanf(d, "%d.%d.%d", &yy, &mm, &dd) != 3) {
		return -1;
	}
	if (sscanf(t, "%d:%d", &hh, &min) != 2) {
		return -1;
	}
	memset(&tm_val, 0, sizeof(tm_val));
	tm_val.tm_year = yy - 1900;
	tm_val.tm_mon = mm - 1;
	tm_val.tm_mday = dd;
	tm_val.tm_hour = hh;
	tm_val.tm_min = min;
	tm_val.tm_isdst = -1;
	ts = mktime(&tm_val);
	if (ts == (time_t)-1) {
		return -1;
	}
	*out_epoch = (int32_t)ts;
	return 0;
}

static int cmp_candle_ts(const void *a, const void *b)
{
	const Candle *ca = (const Candle *)a;
	const Candle *cb = (const Candle *)b;
	if (ca->ts_epoch < cb->ts_epoch) {
		return -1;
	}
	if (ca->ts_epoch > cb->ts_epoch) {
		return 1;
	}
	return 0;
}

static uint32_t dedupe_by_ts(Candle *items, uint32_t n)
{
	uint32_t r;
	uint32_t w;

	if (n == 0) {
		return 0;
	}
	w = 1;
	for (r = 1; r < n; ++r) {
		if (items[r].ts_epoch != items[w - 1].ts_epoch) {
			items[w++] = items[r];
		}
	}
	return w;
}

int core_import_mt4_csv(const char *path, const LoadOptions *opt, uint32_t *out_dataset_id)
{
	FILE *fp;
	char line[LINE_BUF_SZ];
	uint32_t cap;
	uint32_t n;
	Candle *candles;
	DatasetRecord *rec;
	LoadOptions local_opt;

	char d[32];
	char t[32];
	double open_v;
	double high_v;
	double low_v;
	double close_v;
	int vol_v;
	int fields;
	int32_t ts_epoch;

	if (path == NULL || out_dataset_id == NULL) {
		return -1;
	}
	local_opt.pip_scale = 10000;
	local_opt.sort_on_load = 1;
	local_opt.dedupe_timestamps = 1;
	if (opt != NULL) {
		local_opt = *opt;
		if (local_opt.pip_scale <= 0) {
			local_opt.pip_scale = 10000;
		}
	}

	fp = fopen(path, "r");
	if (fp == NULL) {
		return -2;
	}
	cap = 4096;
	n = 0;
	candles = (Candle *)malloc(cap * sizeof(Candle));
	if (candles == NULL) {
		fclose(fp);
		return -3;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		Candle c;

		fields = sscanf(line,
			"%31[^,],%31[^,],%lf,%lf,%lf,%lf,%d",
			d,
			t,
			&open_v,
			&high_v,
			&low_v,
			&close_v,
			&vol_v);
		if (fields != 7) {
			continue;
		}
		if (parse_timestamp_epoch(d, t, &ts_epoch) != 0) {
			continue;
		}
		if (n == cap) {
			Candle *new_buf;
			cap *= 2;
			new_buf = (Candle *)realloc(candles, cap * sizeof(Candle));
			if (new_buf == NULL) {
				free(candles);
				fclose(fp);
				return -4;
			}
			candles = new_buf;
		}
		c.ts_epoch = ts_epoch;
		c.open_pips = (int32_t)(open_v * local_opt.pip_scale);
		c.high_pips = (int32_t)(high_v * local_opt.pip_scale);
		c.low_pips = (int32_t)(low_v * local_opt.pip_scale);
		c.close_pips = (int32_t)(close_v * local_opt.pip_scale);
		c.volume = vol_v;
		candles[n++] = c;
	}
	fclose(fp);

	if (n == 0) {
		free(candles);
		return -5;
	}

	if (local_opt.sort_on_load) {
		qsort(candles, n, sizeof(Candle), cmp_candle_ts);
	}
	if (local_opt.dedupe_timestamps) {
		n = dedupe_by_ts(candles, n);
	}

	rec = store_alloc_dataset(out_dataset_id);
	if (rec == NULL) {
		free(candles);
		return -6;
	}

	rec->candles = candles;
	rec->meta.pip_scale = (uint32_t)local_opt.pip_scale;
	rec->meta.candle_count = n;
	rec->meta.feature_count = 0;
	rec->meta.char_block_count = 0;
	rec->meta.q = 0;
	snprintf(rec->meta.source_path, sizeof(rec->meta.source_path), "%s", path);
	store_name_from_path(path, rec->meta.name, sizeof(rec->meta.name));

	return 0;
}
