#include "live_store.h"

#include "internal_store.h"
#include "logger.h"
#include "wave_dp.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define LIVE_STREAM_MAX 16u
#define LIVE_LINE_MAX 256u

typedef struct {
	int used;
	int loaded;
	char symbol[LIVE_SYMBOL_MAX];
	char timeframe[LIVE_TIMEFRAME_MAX];
	char safe_symbol[LIVE_SYMBOL_MAX];
	char safe_timeframe[LIVE_TIMEFRAME_MAX];
	char candle_path[768];
	char meta_path[768];
	uint32_t dataset_id;
	uint32_t pip_scale;
	int32_t last_recompute_ts;
	uint64_t last_update_ms;
	uint64_t last_recompute_ms;
	uint32_t last_batch_input;
	uint32_t last_batch_changed;
} LiveStream;

static LiveStream g_live_streams[LIVE_STREAM_MAX];

static void live_scan_existing_streams(void);

static uint64_t live_now_ms(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0) {
		return (uint64_t)time(NULL) * 1000ull;
	}
	return (uint64_t)tv.tv_sec * 1000ull + (uint64_t)(tv.tv_usec / 1000);
}

static void live_safe_name(const char *src, char *out, size_t out_sz)
{
	size_t i;
	size_t w = 0u;

	if (out == NULL || out_sz == 0u) {
		return;
	}
	if (src == NULL || src[0] == '\0') {
		snprintf(out, out_sz, "%s", "unknown");
		return;
	}
	for (i = 0u; src[i] != '\0' && w + 1u < out_sz; ++i) {
		unsigned char ch = (unsigned char)src[i];
		if (isalnum(ch) || ch == '_' || ch == '-') {
			out[w++] = (char)ch;
		} else {
			out[w++] = '_';
		}
	}
	out[w] = '\0';
	if (out[0] == '\0') {
		snprintf(out, out_sz, "%s", "unknown");
	}
}

static int live_mkdir_if_missing(const char *path)
{
	if (mkdir(path, 0775) == 0 || errno == EEXIST) {
		return 0;
	}
	return -1;
}

static int live_mkdirs_for_path(const char *file_path)
{
	char tmp[768];
	size_t i;

	if (file_path == NULL || snprintf(tmp, sizeof(tmp), "%s", file_path) <= 0) {
		return -1;
	}
	for (i = 1u; tmp[i] != '\0'; ++i) {
		if (tmp[i] == '/') {
			tmp[i] = '\0';
			if (live_mkdir_if_missing(tmp) != 0) {
				return -1;
			}
			tmp[i] = '/';
		}
	}
	return 0;
}

static int live_parse_timestamp_epoch(const char *d, const char *t, int32_t *out_epoch)
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

static void live_default_wave_params(WaveParams *params)
{
	memset(params, 0, sizeof(*params));
	params->wave_model = WAVE_MODEL_LAYERED;
	params->lmin_candles = 1u;
	params->lmax_candles = 60u;
	params->regime_context_window = 512u;
	params->regime_context_mode = WAVE_CONTEXT_TRAILING;
	params->min_wave_move_pips = 0;
	params->minimum_pips_for_trend = 7;
	params->norm_scale = 1000;
	params->context_range_floor = 1;
	params->epsilon = 1;
	params->segment_penalty_lambda = 220;
	params->weak_fit_threshold = 140;
	params->boundary_low_conf_threshold = 30;
	params->trend_priority_mode = 1;
	params->retrace_break_threshold = 200;
	params->trend_split_gain_min = 80;
	params->compression_min_duration = 4;
	params->expansion_min_duration = 4;
	params->v_reversal_min_trend_strength = 420;
	params->v_reversal_max_bridge_chop = 520;
	params->v_reversal_max_duration = 12;
	params->relative_trend_retracement_max = 300;
	params->relative_strong_trend_retracement_max = 180;
	params->relative_trend_der_step_min = 550;
	params->relative_trend_der_body_min = 500;
	params->relative_trend_body_eff_min = 450;
	params->relative_trend_overlap_max = 350;
	params->relative_trend_sign_flip_max = 450;
	params->relative_trend_wick_bias_min = 1050;
	params->relative_range_der_step_max = 500;
	params->relative_range_body_eff_max = 500;
	params->relative_range_overlap_min = 350;
	params->relative_range_sign_flip_min = 400;
}

static void live_clear_derived_buffers(DatasetRecord *rec)
{
	if (rec == NULL) {
		return;
	}
	free(rec->features);
	rec->features = NULL;
	rec->meta.feature_count = 0u;
	free(rec->chars.data);
	rec->chars.data = NULL;
	free(rec->chars.summaries);
	rec->chars.summaries = NULL;
	memset(&rec->chars, 0, sizeof(rec->chars));
	free(rec->clustering.cluster_ids);
	rec->clustering.cluster_ids = NULL;
	free(rec->clustering.summaries);
	rec->clustering.summaries = NULL;
	memset(&rec->clustering, 0, sizeof(rec->clustering));
	free(rec->symbol_stream);
	rec->symbol_stream = NULL;
	rec->symbol_count = 0u;
	free(rec->rle_tokens);
	rec->rle_tokens = NULL;
	rec->rle_count = 0u;
	free(rec->words);
	rec->words = NULL;
	rec->word_count = 0u;
	free(rec->phrases);
	rec->phrases = NULL;
	rec->phrase_count = 0u;
	free(rec->waves.tokens);
	rec->waves.tokens = NULL;
	free(rec->waves.layer1_tokens);
	rec->waves.layer1_tokens = NULL;
	memset(&rec->waves, 0, sizeof(rec->waves));
}

static int live_load_candles_from_file(const char *path, uint32_t pip_scale, Candle **out_candles, uint32_t *out_count)
{
	FILE *fp;
	char line[LIVE_LINE_MAX];
	uint32_t cap = 1024u;
	uint32_t n = 0u;
	Candle *candles;

	if (out_candles == NULL || out_count == NULL) {
		return -1;
	}
	*out_candles = NULL;
	*out_count = 0u;
	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			return 0;
		}
		return -2;
	}
	candles = (Candle *)malloc(cap * sizeof(Candle));
	if (candles == NULL) {
		fclose(fp);
		return -3;
	}
	while (fgets(line, sizeof(line), fp) != NULL) {
		char d[32];
		char t[32];
		double open_v;
		double high_v;
		double low_v;
		double close_v;
		int vol_v;
		int32_t ts_epoch;
		Candle c;

		if (sscanf(line, "%31[^,],%31[^,],%lf,%lf,%lf,%lf,%d", d, t, &open_v, &high_v, &low_v, &close_v, &vol_v) != 7) {
			continue;
		}
		if (live_parse_timestamp_epoch(d, t, &ts_epoch) != 0) {
			continue;
		}
		if (n == cap) {
			Candle *new_buf;
			cap *= 2u;
			new_buf = (Candle *)realloc(candles, cap * sizeof(Candle));
			if (new_buf == NULL) {
				free(candles);
				fclose(fp);
				return -4;
			}
			candles = new_buf;
		}
		c.ts_epoch = ts_epoch;
		c.open_pips = (int32_t)lround(open_v * (double)pip_scale);
		c.high_pips = (int32_t)lround(high_v * (double)pip_scale);
		c.low_pips = (int32_t)lround(low_v * (double)pip_scale);
		c.close_pips = (int32_t)lround(close_v * (double)pip_scale);
		c.volume = vol_v;
		candles[n++] = c;
	}
	fclose(fp);
	*out_candles = candles;
	*out_count = n;
	return 0;
}

static int live_write_candles_to_file(const char *path, const Candle *candles, uint32_t count, uint32_t pip_scale)
{
	FILE *fp;
	uint32_t i;
	double scale;

	if (path == NULL || candles == NULL || pip_scale == 0u) {
		return -1;
	}
	if (live_mkdirs_for_path(path) != 0) {
		return -2;
	}
	fp = fopen(path, "w");
	if (fp == NULL) {
		return -3;
	}
	scale = (double)pip_scale;
	for (i = 0u; i < count; ++i) {
		time_t ts = (time_t)candles[i].ts_epoch;
		struct tm *tm_val = localtime(&ts);

		if (tm_val == NULL) {
			fclose(fp);
			return -4;
		}
		fprintf(fp,
			"%04d.%02d.%02d,%02d:%02d,%.5f,%.5f,%.5f,%.5f,%d\n",
			tm_val->tm_year + 1900,
			tm_val->tm_mon + 1,
			tm_val->tm_mday,
			tm_val->tm_hour,
			tm_val->tm_min,
			(double)candles[i].open_pips / scale,
			(double)candles[i].high_pips / scale,
			(double)candles[i].low_pips / scale,
			(double)candles[i].close_pips / scale,
			candles[i].volume);
	}
	fclose(fp);
	return 0;
}

static void live_read_meta(const char *path,
	const char *fallback_symbol,
	const char *fallback_timeframe,
	uint32_t fallback_pip_scale,
	char *out_symbol,
	size_t out_symbol_sz,
	char *out_timeframe,
	size_t out_timeframe_sz,
	uint32_t *out_pip_scale)
{
	FILE *fp;
	char line[LIVE_LINE_MAX];

	if (out_symbol != NULL && out_symbol_sz > 0u) {
		snprintf(out_symbol, out_symbol_sz, "%s", fallback_symbol != NULL ? fallback_symbol : "");
	}
	if (out_timeframe != NULL && out_timeframe_sz > 0u) {
		snprintf(out_timeframe, out_timeframe_sz, "%s", fallback_timeframe != NULL ? fallback_timeframe : "");
	}
	if (out_pip_scale != NULL) {
		*out_pip_scale = fallback_pip_scale;
	}
	fp = fopen(path, "r");
	if (fp == NULL) {
		return;
	}
	while (fgets(line, sizeof(line), fp) != NULL) {
		char key[64];
		char value[128];
		unsigned int pip_scale;

		if (sscanf(line, "%63[^=]=%127[^\r\n]", key, value) != 2) {
			continue;
		}
		if (strcmp(key, "symbol") == 0 && out_symbol != NULL && out_symbol_sz > 0u) {
			snprintf(out_symbol, out_symbol_sz, "%s", value);
		} else if (strcmp(key, "timeframe") == 0 && out_timeframe != NULL && out_timeframe_sz > 0u) {
			snprintf(out_timeframe, out_timeframe_sz, "%s", value);
		} else if (strcmp(key, "pip_scale") == 0 &&
			out_pip_scale != NULL &&
			sscanf(value, "%u", &pip_scale) == 1 &&
			pip_scale > 0u) {
			*out_pip_scale = (uint32_t)pip_scale;
		}
	}
	fclose(fp);
}

static int live_write_meta(const char *path, const char *symbol, const char *timeframe, uint32_t pip_scale)
{
	FILE *fp;

	if (live_mkdirs_for_path(path) != 0) {
		return -1;
	}
	fp = fopen(path, "w");
	if (fp == NULL) {
		return -2;
	}
	fprintf(fp, "symbol=%s\n", symbol != NULL ? symbol : "");
	fprintf(fp, "timeframe=%s\n", timeframe != NULL ? timeframe : "");
	fprintf(fp, "pip_scale=%u\n", pip_scale);
	fclose(fp);
	return 0;
}

static int live_recompute_stream(LiveStream *stream, char *err, size_t err_sz)
{
	WaveParams params;
	DatasetRecord *rec;
	uint64_t started_ms;
	int rc;

	if (stream == NULL) {
		return -1;
	}
	rec = store_find_dataset(stream->dataset_id);
	if (rec == NULL) {
		snprintf(err, err_sz, "live dataset not found");
		return -2;
	}
	if (rec->meta.candle_count == 0u) {
		return 0;
	}
	live_clear_derived_buffers(rec);
	live_default_wave_params(&params);
	started_ms = live_now_ms();
	trainer_log_info("live recompute start symbol=%s timeframe=%s dataset_id=%u candles=%u",
		stream->symbol,
		stream->timeframe,
		stream->dataset_id,
		rec->meta.candle_count);
	rc = wave_dp_run(stream->dataset_id, &params, NULL, NULL, err, err_sz);
	stream->last_recompute_ms = live_now_ms() - started_ms;
	if (rc != 0) {
		trainer_log_error("live recompute failed symbol=%s timeframe=%s dataset_id=%u rc=%d err=%s",
			stream->symbol,
			stream->timeframe,
			stream->dataset_id,
			rc,
			(err != NULL) ? err : "");
		return rc;
	}
	stream->last_recompute_ts = rec->candles[rec->meta.candle_count - 1u].ts_epoch;
	trainer_log_info("live recompute complete symbol=%s timeframe=%s dataset_id=%u waves=%u elapsed_ms=%llu",
		stream->symbol,
		stream->timeframe,
		stream->dataset_id,
		rec->waves.token_count,
		(unsigned long long)stream->last_recompute_ms);
	return 0;
}

static int live_init_stream(LiveStream *stream, const char *symbol, const char *timeframe, uint32_t pip_scale, char *err, size_t err_sz)
{
	GlobalStore *store;
	DatasetRecord *rec;
	Candle *candles = NULL;
	uint32_t candle_count = 0u;
	char meta_symbol[LIVE_SYMBOL_MAX];
	char meta_timeframe[LIVE_TIMEFRAME_MAX];
	int rc;

	if (stream == NULL || symbol == NULL || timeframe == NULL) {
		return -1;
	}
	store = store_get();
	snprintf(stream->symbol, sizeof(stream->symbol), "%s", symbol);
	snprintf(stream->timeframe, sizeof(stream->timeframe), "%s", timeframe);
	live_safe_name(symbol, stream->safe_symbol, sizeof(stream->safe_symbol));
	live_safe_name(timeframe, stream->safe_timeframe, sizeof(stream->safe_timeframe));
	stream->pip_scale = (pip_scale == 0u) ? 10000u : pip_scale;
	snprintf(stream->candle_path,
		sizeof(stream->candle_path),
		"%s/live/%s/%s/candles.csv",
		store->storage_root,
		stream->safe_symbol,
		stream->safe_timeframe);
	snprintf(stream->meta_path,
		sizeof(stream->meta_path),
		"%s/live/%s/%s/meta.txt",
		store->storage_root,
		stream->safe_symbol,
		stream->safe_timeframe);
	live_read_meta(stream->meta_path,
		stream->symbol,
		stream->timeframe,
		stream->pip_scale,
		meta_symbol,
		sizeof(meta_symbol),
		meta_timeframe,
		sizeof(meta_timeframe),
		&stream->pip_scale);
	if (strcmp(meta_symbol, stream->symbol) != 0 || strcmp(meta_timeframe, stream->timeframe) != 0) {
		snprintf(stream->symbol, sizeof(stream->symbol), "%s", meta_symbol);
		snprintf(stream->timeframe, sizeof(stream->timeframe), "%s", meta_timeframe);
		live_safe_name(stream->symbol, stream->safe_symbol, sizeof(stream->safe_symbol));
		live_safe_name(stream->timeframe, stream->safe_timeframe, sizeof(stream->safe_timeframe));
		snprintf(stream->candle_path,
			sizeof(stream->candle_path),
			"%s/live/%s/%s/candles.csv",
			store->storage_root,
			stream->safe_symbol,
			stream->safe_timeframe);
		snprintf(stream->meta_path,
			sizeof(stream->meta_path),
			"%s/live/%s/%s/meta.txt",
			store->storage_root,
			stream->safe_symbol,
			stream->safe_timeframe);
	}
	rec = store_alloc_dataset(&stream->dataset_id);
	if (rec == NULL) {
		snprintf(err, err_sz, "no dataset slots available");
		return -2;
	}
	rc = live_load_candles_from_file(stream->candle_path, stream->pip_scale, &candles, &candle_count);
	if (rc != 0) {
		snprintf(err, err_sz, "failed to load live candles rc=%d", rc);
		return -3;
	}
	rec->candles = candles;
	rec->meta.pip_scale = stream->pip_scale;
	rec->meta.candle_count = candle_count;
	rec->meta.feature_count = 0u;
	snprintf(rec->meta.name, sizeof(rec->meta.name), "live_%s_%s", stream->symbol, stream->timeframe);
	snprintf(rec->meta.source_path, sizeof(rec->meta.source_path), "%s", stream->candle_path);
	stream->used = 1;
	stream->loaded = 1;
	stream->last_update_ms = live_now_ms();
	trainer_log_info("live stream loaded symbol=%s timeframe=%s dataset_id=%u candles=%u path=%s",
		stream->symbol,
		stream->timeframe,
		stream->dataset_id,
		candle_count,
		stream->candle_path);
	if (candle_count > 0u) {
		(void)live_recompute_stream(stream, err, err_sz);
	}
	return 0;
}

static LiveStream *live_find_stream(const char *symbol, const char *timeframe)
{
	uint32_t i;

	for (i = 0u; i < LIVE_STREAM_MAX; ++i) {
		if (g_live_streams[i].used &&
			strcmp(g_live_streams[i].symbol, symbol) == 0 &&
			strcmp(g_live_streams[i].timeframe, timeframe) == 0) {
			return &g_live_streams[i];
		}
	}
	return NULL;
}

static LiveStream *live_get_or_create_stream(const char *symbol, const char *timeframe, uint32_t pip_scale, char *err, size_t err_sz)
{
	uint32_t i;
	LiveStream *stream;

	stream = live_find_stream(symbol, timeframe);
	if (stream != NULL) {
		if (pip_scale != 0u && stream->pip_scale != pip_scale) {
			DatasetRecord *rec = store_find_dataset(stream->dataset_id);
			Candle *candles = NULL;
			uint32_t candle_count = 0u;

			trainer_log_info("live stream pip_scale changed symbol=%s timeframe=%s old=%u new=%u",
				symbol,
				timeframe,
				stream->pip_scale,
				pip_scale);
			stream->pip_scale = pip_scale;
			(void)live_write_meta(stream->meta_path, stream->symbol, stream->timeframe, stream->pip_scale);
			if (rec != NULL && live_load_candles_from_file(stream->candle_path, stream->pip_scale, &candles, &candle_count) == 0) {
				live_clear_derived_buffers(rec);
				free(rec->candles);
				rec->candles = candles;
				rec->meta.candle_count = candle_count;
				rec->meta.pip_scale = stream->pip_scale;
			}
		}
		return stream;
	}
	for (i = 0u; i < LIVE_STREAM_MAX; ++i) {
		if (!g_live_streams[i].used) {
			if (live_init_stream(&g_live_streams[i], symbol, timeframe, pip_scale, err, err_sz) != 0) {
				return NULL;
			}
			return &g_live_streams[i];
		}
	}
	snprintf(err, err_sz, "no live stream slots available");
	return NULL;
}

static void live_fill_summary(const LiveStream *stream, LiveStreamSummary *out)
{
	DatasetRecord *rec;

	if (stream == NULL || out == NULL) {
		return;
	}
	memset(out, 0, sizeof(*out));
	snprintf(out->symbol, sizeof(out->symbol), "%s", stream->symbol);
	snprintf(out->timeframe, sizeof(out->timeframe), "%s", stream->timeframe);
	out->dataset_id = stream->dataset_id;
	out->pip_scale = stream->pip_scale;
	out->last_update_ms = stream->last_update_ms;
	out->last_recompute_ms = stream->last_recompute_ms;
	out->last_batch_input = stream->last_batch_input;
	out->last_batch_changed = stream->last_batch_changed;
	rec = store_find_dataset(stream->dataset_id);
	if (rec != NULL) {
		out->candle_count = rec->meta.candle_count;
		out->wave_count = rec->waves.token_count;
		if (rec->meta.candle_count > 0u) {
			out->last_ts = rec->candles[rec->meta.candle_count - 1u].ts_epoch;
		}
		out->last_recompute_ts = stream->last_recompute_ts;
		out->waves_stale = (out->candle_count > 0u && out->last_recompute_ts != out->last_ts) ? 1 : 0;
		if (out->candle_count == 0u) {
			out->recompute_status = LIVE_RECOMPUTE_EMPTY;
		} else if (out->waves_stale) {
			out->recompute_status = LIVE_RECOMPUTE_STALE;
		} else {
			out->recompute_status = LIVE_RECOMPUTE_FRESH;
		}
		memcpy(out->class_counts, rec->waves.class_counts, sizeof(out->class_counts));
	}
}

static int live_insert_or_replace_candle(DatasetRecord *rec, const Candle *candle, int *out_changed)
{
	uint32_t i;
	Candle *new_buf;

	if (rec == NULL || candle == NULL || out_changed == NULL) {
		return -1;
	}
	*out_changed = 0;
	for (i = 0u; i < rec->meta.candle_count; ++i) {
		if (rec->candles[i].ts_epoch == candle->ts_epoch) {
			if (memcmp(&rec->candles[i], candle, sizeof(*candle)) != 0) {
				rec->candles[i] = *candle;
				*out_changed = 1;
			}
			return 0;
		}
		if (rec->candles[i].ts_epoch > candle->ts_epoch) {
			break;
		}
	}
	new_buf = (Candle *)realloc(rec->candles, (rec->meta.candle_count + 1u) * sizeof(Candle));
	if (new_buf == NULL) {
		return -2;
	}
	rec->candles = new_buf;
	if (i < rec->meta.candle_count) {
		memmove(rec->candles + i + 1u,
			rec->candles + i,
			(rec->meta.candle_count - i) * sizeof(Candle));
	}
	rec->candles[i] = *candle;
	rec->meta.candle_count += 1u;
	*out_changed = 1;
	return 0;
}

int live_store_upsert_candle_ex(const char *symbol,
	const char *timeframe,
	uint32_t pip_scale,
	const Candle *candle,
	int recompute,
	LiveStreamSummary *out_summary,
	char *err,
	size_t err_sz)
{
	LiveStream *stream;
	DatasetRecord *rec;
	int changed = 0;
	int rc;

	if (err != NULL && err_sz > 0u) {
		err[0] = '\0';
	}
	if (symbol == NULL || symbol[0] == '\0' || timeframe == NULL || timeframe[0] == '\0' || candle == NULL) {
		snprintf(err, err_sz, "missing live candle fields");
		return -1;
	}
	if (pip_scale == 0u) {
		pip_scale = 10000u;
	}
	stream = live_get_or_create_stream(symbol, timeframe, pip_scale, err, err_sz);
	if (stream == NULL) {
		return -2;
	}
	rec = store_find_dataset(stream->dataset_id);
	if (rec == NULL) {
		snprintf(err, err_sz, "live dataset not found");
		return -3;
	}
	rc = live_insert_or_replace_candle(rec, candle, &changed);
	if (rc != 0) {
		snprintf(err, err_sz, "failed to update candle rc=%d", rc);
		return -4;
	}
	if (changed) {
		rec->meta.pip_scale = pip_scale;
		stream->pip_scale = pip_scale;
		stream->last_update_ms = live_now_ms();
		if (live_write_candles_to_file(stream->candle_path, rec->candles, rec->meta.candle_count, stream->pip_scale) != 0) {
			snprintf(err, err_sz, "failed to persist live candles");
			return -5;
		}
		(void)live_write_meta(stream->meta_path, stream->symbol, stream->timeframe, stream->pip_scale);
		if (recompute) {
			rc = live_recompute_stream(stream, err, err_sz);
			if (rc != 0) {
				return -6;
			}
		}
	}
	stream->last_batch_input = 1u;
	stream->last_batch_changed = changed ? 1u : 0u;
	live_fill_summary(stream, out_summary);
	trainer_log_info("live candle accepted symbol=%s timeframe=%s ts=%d changed=%d recompute=%d candles=%u waves=%u",
		symbol,
		timeframe,
		candle->ts_epoch,
		changed,
		recompute ? 1 : 0,
		(out_summary != NULL) ? out_summary->candle_count : 0u,
		(out_summary != NULL) ? out_summary->wave_count : 0u);
	return 0;
}

int live_store_upsert_candle(const char *symbol,
	const char *timeframe,
	uint32_t pip_scale,
	const Candle *candle,
	LiveStreamSummary *out_summary,
	char *err,
	size_t err_sz)
{
	return live_store_upsert_candle_ex(symbol, timeframe, pip_scale, candle, 1, out_summary, err, err_sz);
}

int live_store_upsert_candles_ex(const char *symbol,
	const char *timeframe,
	uint32_t pip_scale,
	const Candle *candles,
	uint32_t candle_count,
	int recompute,
	uint32_t *out_changed_count,
	LiveStreamSummary *out_summary,
	char *err,
	size_t err_sz)
{
	LiveStream *stream;
	DatasetRecord *rec;
	uint32_t i;
	uint32_t changed_count = 0u;
	int rc;

	if (err != NULL && err_sz > 0u) {
		err[0] = '\0';
	}
	if (out_changed_count != NULL) {
		*out_changed_count = 0u;
	}
	if (symbol == NULL || symbol[0] == '\0' || timeframe == NULL || timeframe[0] == '\0' || candles == NULL) {
		snprintf(err, err_sz, "missing live candle batch fields");
		return -1;
	}
	if (pip_scale == 0u) {
		pip_scale = 10000u;
	}
	stream = live_get_or_create_stream(symbol, timeframe, pip_scale, err, err_sz);
	if (stream == NULL) {
		return -2;
	}
	rec = store_find_dataset(stream->dataset_id);
	if (rec == NULL) {
		snprintf(err, err_sz, "live dataset not found");
		return -3;
	}
	for (i = 0u; i < candle_count; ++i) {
		int changed = 0;

		rc = live_insert_or_replace_candle(rec, &candles[i], &changed);
		if (rc != 0) {
			snprintf(err, err_sz, "failed to update batch candle index=%u rc=%d", i, rc);
			return -4;
		}
		if (changed) {
			changed_count += 1u;
		}
	}
	if (changed_count > 0u) {
		rec->meta.pip_scale = pip_scale;
		stream->pip_scale = pip_scale;
		stream->last_update_ms = live_now_ms();
		if (live_write_candles_to_file(stream->candle_path, rec->candles, rec->meta.candle_count, stream->pip_scale) != 0) {
			snprintf(err, err_sz, "failed to persist live candle batch");
			return -5;
		}
		(void)live_write_meta(stream->meta_path, stream->symbol, stream->timeframe, stream->pip_scale);
		if (recompute) {
			rc = live_recompute_stream(stream, err, err_sz);
			if (rc != 0) {
				return -6;
			}
		}
	}
	stream->last_batch_input = candle_count;
	stream->last_batch_changed = changed_count;
	if (out_changed_count != NULL) {
		*out_changed_count = changed_count;
	}
	live_fill_summary(stream, out_summary);
	trainer_log_info("live candle batch accepted symbol=%s timeframe=%s input=%u changed=%u recompute=%d candles=%u waves=%u",
		symbol,
		timeframe,
		candle_count,
		changed_count,
		recompute ? 1 : 0,
		(out_summary != NULL) ? out_summary->candle_count : 0u,
		(out_summary != NULL) ? out_summary->wave_count : 0u);
	return 0;
}

int live_store_recompute(const char *symbol,
	const char *timeframe,
	LiveStreamSummary *out_summary,
	char *err,
	size_t err_sz)
{
	LiveStream *stream;
	int rc;

	if (err != NULL && err_sz > 0u) {
		err[0] = '\0';
	}
	if (symbol == NULL || symbol[0] == '\0' || timeframe == NULL || timeframe[0] == '\0') {
		snprintf(err, err_sz, "missing symbol or timeframe");
		return -1;
	}
	live_scan_existing_streams();
	stream = live_find_stream(symbol, timeframe);
	if (stream == NULL) {
		snprintf(err, err_sz, "live stream not found");
		return -2;
	}
	rc = live_recompute_stream(stream, err, err_sz);
	if (rc != 0) {
		return -3;
	}
	live_fill_summary(stream, out_summary);
	return 0;
}

static void live_scan_existing_streams(void)
{
	GlobalStore *store = store_get();
	char live_root[640];
	DIR *root_dir;
	struct dirent *sym_ent;

	snprintf(live_root, sizeof(live_root), "%s/live", store->storage_root);
	root_dir = opendir(live_root);
	if (root_dir == NULL) {
		return;
	}
	while ((sym_ent = readdir(root_dir)) != NULL) {
		char sym_path[768];
		DIR *sym_dir;
		struct dirent *tf_ent;

		if (sym_ent->d_name[0] == '.') {
			continue;
		}
		snprintf(sym_path, sizeof(sym_path), "%s/%s", live_root, sym_ent->d_name);
		sym_dir = opendir(sym_path);
		if (sym_dir == NULL) {
			continue;
		}
		while ((tf_ent = readdir(sym_dir)) != NULL) {
			char err[256];
			char meta_path[768];
			char stream_symbol[LIVE_SYMBOL_MAX];
			char stream_timeframe[LIVE_TIMEFRAME_MAX];
			uint32_t pip_scale = 10000u;

			if (tf_ent->d_name[0] == '.') {
				continue;
			}
			snprintf(meta_path,
				sizeof(meta_path),
				"%s/%s/meta.txt",
				sym_path,
				tf_ent->d_name);
			live_read_meta(meta_path,
				sym_ent->d_name,
				tf_ent->d_name,
				10000u,
				stream_symbol,
				sizeof(stream_symbol),
				stream_timeframe,
				sizeof(stream_timeframe),
				&pip_scale);
			if (live_find_stream(stream_symbol, stream_timeframe) != NULL) {
				continue;
			}
			err[0] = '\0';
			(void)live_get_or_create_stream(stream_symbol, stream_timeframe, pip_scale, err, sizeof(err));
		}
		closedir(sym_dir);
	}
	closedir(root_dir);
}

int live_store_get_status(LiveStreamSummary *out_items, uint32_t max_items, uint32_t *out_count)
{
	uint32_t i;
	uint32_t w = 0u;

	if (out_count == NULL) {
		return -1;
	}
	live_scan_existing_streams();
	for (i = 0u; i < LIVE_STREAM_MAX; ++i) {
		if (!g_live_streams[i].used) {
			continue;
		}
		if (out_items != NULL && w < max_items) {
			live_fill_summary(&g_live_streams[i], &out_items[w]);
		}
		w += 1u;
	}
	*out_count = w;
	return 0;
}

int live_store_get_dataset_id(const char *symbol, const char *timeframe, uint32_t *out_dataset_id)
{
	LiveStream *stream;

	if (out_dataset_id == NULL || symbol == NULL || timeframe == NULL) {
		return -1;
	}
	live_scan_existing_streams();
	stream = live_find_stream(symbol, timeframe);
	if (stream == NULL) {
		return -2;
	}
	*out_dataset_id = stream->dataset_id;
	return 0;
}

int live_store_get_summary(const char *symbol, const char *timeframe, LiveStreamSummary *out_summary)
{
	LiveStream *stream;

	if (out_summary == NULL || symbol == NULL || timeframe == NULL) {
		return -1;
	}
	live_scan_existing_streams();
	stream = live_find_stream(symbol, timeframe);
	if (stream == NULL) {
		return -2;
	}
	live_fill_summary(stream, out_summary);
	return 0;
}

int live_store_reset(const char *symbol, const char *timeframe)
{
	GlobalStore *store;
	LiveStream *stream;
	DatasetRecord *rec;
	char sym_dir[768];
	char tf_dir[768];

	if (symbol == NULL || timeframe == NULL) {
		return -1;
	}
	live_scan_existing_streams();
	stream = live_find_stream(symbol, timeframe);
	if (stream == NULL) {
		return 0;
	}
	store = store_get();
	snprintf(sym_dir,
		sizeof(sym_dir),
		"%s/live/%s",
		store->storage_root,
		stream->safe_symbol);
	snprintf(tf_dir,
		sizeof(tf_dir),
		"%s/live/%s/%s",
		store->storage_root,
		stream->safe_symbol,
		stream->safe_timeframe);
	rec = store_find_dataset(stream->dataset_id);
	if (rec != NULL) {
		store_free_dataset_buffers(rec);
		rec->used = 0;
	}
	unlink(stream->candle_path);
	unlink(stream->meta_path);
	(void)rmdir(tf_dir);
	(void)rmdir(sym_dir);
	memset(stream, 0, sizeof(*stream));
	trainer_log_info("live stream reset symbol=%s timeframe=%s", symbol, timeframe);
	return 0;
}

void live_store_clear_runtime(void)
{
	memset(g_live_streams, 0, sizeof(g_live_streams));
}
