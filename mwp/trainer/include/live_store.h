#ifndef LIVE_STORE_H
#define LIVE_STORE_H

#include "api_types.h"

#include <stddef.h>
#include <stdint.h>

#define LIVE_SYMBOL_MAX 32
#define LIVE_TIMEFRAME_MAX 16

typedef enum {
	LIVE_RECOMPUTE_EMPTY = 0,
	LIVE_RECOMPUTE_STALE = 1,
	LIVE_RECOMPUTE_FRESH = 2
} LiveRecomputeStatus;

typedef struct {
	char symbol[LIVE_SYMBOL_MAX];
	char timeframe[LIVE_TIMEFRAME_MAX];
	uint32_t dataset_id;
	uint32_t pip_scale;
	uint32_t candle_count;
	uint32_t wave_count;
	int32_t last_ts;
	int32_t last_recompute_ts;
	uint64_t last_update_ms;
	uint64_t last_recompute_ms;
	uint32_t last_batch_input;
	uint32_t last_batch_changed;
	int waves_stale;
	int recompute_status;
	uint32_t class_counts[WAVE_CLASS_COUNT];
} LiveStreamSummary;

int live_store_upsert_candle(const char *symbol,
	const char *timeframe,
	uint32_t pip_scale,
	const Candle *candle,
	LiveStreamSummary *out_summary,
	char *err,
	size_t err_sz);
int live_store_upsert_candle_ex(const char *symbol,
	const char *timeframe,
	uint32_t pip_scale,
	const Candle *candle,
	int recompute,
	LiveStreamSummary *out_summary,
	char *err,
	size_t err_sz);
int live_store_upsert_candles_ex(const char *symbol,
	const char *timeframe,
	uint32_t pip_scale,
	const Candle *candles,
	uint32_t candle_count,
	int recompute,
	uint32_t *out_changed_count,
	LiveStreamSummary *out_summary,
	char *err,
	size_t err_sz);
int live_store_recompute(const char *symbol,
	const char *timeframe,
	LiveStreamSummary *out_summary,
	char *err,
	size_t err_sz);
int live_store_get_summary(const char *symbol, const char *timeframe, LiveStreamSummary *out_summary);
int live_store_get_status(LiveStreamSummary *out_items, uint32_t max_items, uint32_t *out_count);
int live_store_get_dataset_id(const char *symbol, const char *timeframe, uint32_t *out_dataset_id);
int live_store_reset(const char *symbol, const char *timeframe);
void live_store_clear_runtime(void);

#endif
