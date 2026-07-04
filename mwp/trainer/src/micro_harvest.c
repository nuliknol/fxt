#include "micro_harvest.h"
#include "internal_store.h"
#include "wave_dp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MH_MAX_ISLAND_SPAN_WAVES 16
#define MH_MAX_BRIDGE_WAVES 2

typedef struct {
	int32_t lookback_start_index;
	int32_t lookback_end_index;
} MicroHarvestWindow;

typedef struct {
	int32_t total_profit_pips;
	int32_t total_closed_trade_count;
	int32_t total_ignored_open_trade_count;
	int32_t total_max_concurrent_open_trade_count;
	int32_t selected_island_count;
} MicroHarvestScore;

typedef struct {
	int32_t end_candle_index;
	int32_t original_index;
} MicroHarvestOrderKey;

typedef struct {
	MicroHarvestScore score;
	int32_t prev_pos;
	int took;
} MicroHarvestDpCell;

typedef struct {
	int32_t feature_id;
	int32_t compare_op;
	int32_t threshold;
} MicroHarvestSignalRule;

static int max_i32_mh(int a, int b)
{
	return (a > b) ? a : b;
}

static int min_i32_mh(int a, int b)
{
	return (a < b) ? a : b;
}

static int abs_i32_mh(int a)
{
	return (a < 0) ? -a : a;
}

static int compare_i32_mh(const void *lhs, const void *rhs)
{
	const int32_t *a = (const int32_t *)lhs;
	const int32_t *b = (const int32_t *)rhs;

	if (*a < *b) {
		return -1;
	}
	if (*a > *b) {
		return 1;
	}
	return 0;
}

static void micro_harvest_result_reset(MicroHarvestResult *result)
{
	if (result == NULL) {
		return;
	}
	memset(result, 0, sizeof(*result));
}

void micro_harvest_result_free(MicroHarvestResult *result)
{
	if (result == NULL) {
		return;
	}
	free(result->islands);
	free(result->trades);
	micro_harvest_result_reset(result);
}

static int result_reserve_islands(MicroHarvestResult *result, int32_t needed)
{
	MicroHarvestIsland *grown;
	int32_t new_cap;

	if (result == NULL) {
		return -1;
	}
	if (needed <= result->island_capacity) {
		return 0;
	}
	new_cap = max_i32_mh(needed, max_i32_mh(8, result->island_capacity * 2));
	grown = (MicroHarvestIsland *)realloc(result->islands, (size_t)new_cap * sizeof(*grown));
	if (grown == NULL) {
		return -1;
	}
	result->islands = grown;
	result->island_capacity = new_cap;
	return 0;
}

static int result_reserve_trades(MicroHarvestResult *result, int32_t needed)
{
	MicroHarvestTrade *grown;
	int32_t new_cap;

	if (result == NULL) {
		return -1;
	}
	if (needed <= result->trade_capacity) {
		return 0;
	}
	new_cap = max_i32_mh(needed, max_i32_mh(16, result->trade_capacity * 2));
	grown = (MicroHarvestTrade *)realloc(result->trades, (size_t)new_cap * sizeof(*grown));
	if (grown == NULL) {
		return -1;
	}
	result->trades = grown;
	result->trade_capacity = new_cap;
	return 0;
}

static int append_trade(MicroHarvestResult *result, const MicroHarvestTrade *trade)
{
	if (result_reserve_trades(result, result->trade_count + 1) != 0) {
		return -1;
	}
	result->trades[result->trade_count++] = *trade;
	return 0;
}

static int append_island(MicroHarvestResult *result, const MicroHarvestIsland *island)
{
	if (result_reserve_islands(result, result->island_count + 1) != 0) {
		return -1;
	}
	result->islands[result->island_count++] = *island;
	return 0;
}

static int bias_is_valid(int32_t bias)
{
	return bias == MH_BIAS_BUY || bias == MH_BIAS_SELL;
}

static int compute_lookback_window(const Candle *candles, int32_t candle_count, int32_t lookback_days, MicroHarvestWindow *out_window)
{
	int32_t end_ts;
	int32_t start_ts;
	int32_t start_index = 0;

	if (candles == NULL || candle_count <= 0 || out_window == NULL) {
		return -1;
	}
	end_ts = candles[candle_count - 1].ts_epoch;
	start_ts = end_ts - max_i32_mh(1, lookback_days) * 86400;
	while (start_index < candle_count - 1 && candles[start_index].ts_epoch < start_ts) {
		start_index += 1;
	}
	out_window->lookback_start_index = start_index;
	out_window->lookback_end_index = candle_count - 1;
	return 0;
}

static int island_height_ok(int32_t height_pips, const MicroHarvestParams *params)
{
	if (height_pips < params->min_island_height_pips) {
		return 0;
	}
	if (params->max_island_height_pips > 0 && height_pips > params->max_island_height_pips) {
		return 0;
	}
	return 1;
}

static int open_order_count_active(const MicroHarvestSimState *state)
{
	int32_t i;
	int32_t count = 0;

	for (i = 0; i < MH_MAX_OPEN_ORDERS; ++i) {
		if (state->open_orders[i].active) {
			count += 1;
		}
	}
	return count;
}

static void refresh_open_count(MicroHarvestSimState *state)
{
	if (state == NULL) {
		return;
	}
	state->open_count = open_order_count_active(state);
	if (state->open_count > state->max_open_count) {
		state->max_open_count = state->open_count;
	}
}

static int push_open_order(MicroHarvestSimState *state, int32_t direction, int32_t candle_index, int32_t open_price)
{
	int32_t i;

	if (state == NULL) {
		return -1;
	}
	for (i = 0; i < MH_MAX_OPEN_ORDERS; ++i) {
		if (!state->open_orders[i].active) {
			state->open_orders[i].active = 1;
			state->open_orders[i].direction = direction;
			state->open_orders[i].open_candle_index = candle_index;
			state->open_orders[i].open_price_pips = open_price;
			refresh_open_count(state);
			return 0;
		}
	}
	return -1;
}

static int32_t lowest_open_price(const MicroHarvestSimState *state, int32_t direction)
{
	int32_t i;
	int32_t found = 0;
	int32_t out = 0;

	for (i = 0; i < MH_MAX_OPEN_ORDERS; ++i) {
		if (!state->open_orders[i].active || state->open_orders[i].direction != direction) {
			continue;
		}
		if (!found || state->open_orders[i].open_price_pips < out) {
			out = state->open_orders[i].open_price_pips;
			found = 1;
		}
	}
	return found ? out : 0;
}

static int32_t highest_open_price(const MicroHarvestSimState *state, int32_t direction)
{
	int32_t i;
	int32_t found = 0;
	int32_t out = 0;

	for (i = 0; i < MH_MAX_OPEN_ORDERS; ++i) {
		if (!state->open_orders[i].active || state->open_orders[i].direction != direction) {
			continue;
		}
		if (!found || state->open_orders[i].open_price_pips > out) {
			out = state->open_orders[i].open_price_pips;
			found = 1;
		}
	}
	return found ? out : 0;
}

static int close_eligible_orders(MicroHarvestSimState *state,
	const MicroHarvestParams *params,
	int32_t island_index,
	int32_t candle_index,
	const Candle *candle,
	MicroHarvestResult *result)
{
	int32_t i;

	if (state == NULL || params == NULL || candle == NULL) {
		return -1;
	}
	for (i = 0; i < MH_MAX_OPEN_ORDERS; ++i) {
		int32_t close_price;
		MicroHarvestTrade trade;

		if (!state->open_orders[i].active) {
			continue;
		}
		if (state->open_orders[i].direction == MH_BIAS_BUY) {
			close_price = state->open_orders[i].open_price_pips + params->spread_pips + params->take_profit_pips;
			if (candle->high_pips < close_price) {
				continue;
			}
		} else {
			close_price = state->open_orders[i].open_price_pips - params->spread_pips - params->take_profit_pips;
			if (candle->low_pips > close_price) {
				continue;
			}
		}
		memset(&trade, 0, sizeof(trade));
		trade.island_index = island_index;
		trade.open_candle_index = state->open_orders[i].open_candle_index;
		trade.close_candle_index = candle_index;
		trade.direction = state->open_orders[i].direction;
		trade.open_price_pips = state->open_orders[i].open_price_pips;
		trade.close_price_pips = close_price;
		trade.take_profit_pips = params->take_profit_pips;
		trade.profit_pips = params->take_profit_pips;
		trade.closed = 1;
		if (result != NULL && append_trade(result, &trade) != 0) {
			return -1;
		}
		state->closed_profit_pips += params->take_profit_pips;
		state->closed_trade_count += 1;
		state->open_orders[i].active = 0;
	}
	refresh_open_count(state);
	return 0;
}

static int record_ignored_orders(MicroHarvestSimState *state,
	const MicroHarvestParams *params,
	int32_t island_index,
	MicroHarvestResult *result)
{
	int32_t i;

	if (state == NULL || params == NULL) {
		return -1;
	}
	for (i = 0; i < MH_MAX_OPEN_ORDERS; ++i) {
		MicroHarvestTrade trade;

		if (!state->open_orders[i].active) {
			continue;
		}
		memset(&trade, 0, sizeof(trade));
		trade.island_index = island_index;
		trade.open_candle_index = state->open_orders[i].open_candle_index;
		trade.close_candle_index = -1;
		trade.direction = state->open_orders[i].direction;
		trade.open_price_pips = state->open_orders[i].open_price_pips;
		trade.close_price_pips = 0;
		trade.take_profit_pips = params->take_profit_pips;
		trade.profit_pips = 0;
		trade.closed = 0;
		if (result != NULL && append_trade(result, &trade) != 0) {
			return -1;
		}
		state->ignored_open_trade_count += 1;
		state->open_orders[i].active = 0;
	}
	refresh_open_count(state);
	return 0;
}

static int open_orders_for_candle(MicroHarvestSimState *state,
	const MicroHarvestParams *params,
	const MicroHarvestIsland *island,
	int32_t candle_index,
	const Candle *candle,
	int32_t *last_open_candle)
{
	int32_t island_height;
	int32_t entry_threshold;
	int opened = 0;

	if (state == NULL || params == NULL || island == NULL || candle == NULL || last_open_candle == NULL) {
		return -1;
	}
	if (params->max_orders <= 0 || params->max_orders > MH_MAX_OPEN_ORDERS) {
		return -1;
	}
	if (*last_open_candle >= 0 && candle_index - *last_open_candle < params->cooldown_candles) {
		return 0;
	}
	island_height = max_i32_mh(1, island->height_pips);
	if (params->bias == MH_BIAS_BUY) {
		int32_t next_open_price;

		entry_threshold = island->low_price_pips + (int32_t)(((long long)island_height * params->entry_edge_fraction) / 1000ll);
		if (candle->low_pips > entry_threshold) {
			return 0;
		}
		next_open_price = (state->open_count == 0)
			? entry_threshold
			: lowest_open_price(state, MH_BIAS_BUY) - params->min_spacing_pips;
		while (state->open_count < params->max_orders && candle->low_pips <= next_open_price) {
			if (push_open_order(state, MH_BIAS_BUY, candle_index, next_open_price) != 0) {
				return -1;
			}
			*last_open_candle = candle_index;
			opened += 1;
			if (params->cooldown_candles > 0) {
				break;
			}
			next_open_price = lowest_open_price(state, MH_BIAS_BUY) - params->min_spacing_pips;
		}
		return opened;
	}
	entry_threshold = island->high_price_pips - (int32_t)(((long long)island_height * params->entry_edge_fraction) / 1000ll);
	if (candle->high_pips < entry_threshold) {
		return 0;
	}
	{
		int32_t next_open_price = (state->open_count == 0)
			? entry_threshold
			: highest_open_price(state, MH_BIAS_SELL) + params->min_spacing_pips;

		while (state->open_count < params->max_orders && candle->high_pips >= next_open_price) {
			if (push_open_order(state, MH_BIAS_SELL, candle_index, next_open_price) != 0) {
				return -1;
			}
			*last_open_candle = candle_index;
			opened += 1;
			if (params->cooldown_candles > 0) {
				break;
			}
			next_open_price = highest_open_price(state, MH_BIAS_SELL) + params->min_spacing_pips;
		}
	}
	return opened;
}

static int simulate_island(const Candle *candles,
	const MicroHarvestIsland *island,
	const MicroHarvestParams *params,
	MicroHarvestIsland *out_island,
	MicroHarvestResult *result)
{
	MicroHarvestSimState state;
	int32_t candle_index;
	int32_t last_open_candle = -1000000;

	memset(&state, 0, sizeof(state));
	for (candle_index = island->start_candle_index; candle_index <= island->end_candle_index; ++candle_index) {
		if (open_orders_for_candle(&state, params, island, candle_index, &candles[candle_index], &last_open_candle) < 0) {
			return -1;
		}
		if (close_eligible_orders(&state, params, island->index, candle_index, &candles[candle_index], result) != 0) {
			return -1;
		}
	}
	if (record_ignored_orders(&state, params, island->index, result) != 0) {
		return -1;
	}
	if (out_island != NULL) {
		*out_island = *island;
		out_island->profit_pips = state.closed_profit_pips;
		out_island->closed_trade_count = state.closed_trade_count;
		out_island->ignored_open_trade_count = state.ignored_open_trade_count;
		out_island->max_concurrent_open_trade_count = state.max_open_count;
		out_island->selected = 0;
	}
	return 0;
}

static int wave_overlaps_window(const WaveToken *wave, const MicroHarvestWindow *window, int32_t *out_start, int32_t *out_end)
{
	int32_t start_candle;
	int32_t end_candle;

	if (wave == NULL || window == NULL || out_start == NULL || out_end == NULL) {
		return 0;
	}
	start_candle = max_i32_mh((int32_t)wave->start_candle, window->lookback_start_index);
	end_candle = min_i32_mh((int32_t)wave->end_candle, window->lookback_end_index);
	if (start_candle > end_candle) {
		return 0;
	}
	*out_start = start_candle;
	*out_end = end_candle;
	return 1;
}

static void extend_island_bounds_from_candles(const Candle *candles, int32_t start_candle, int32_t end_candle, int32_t *io_low, int32_t *io_high)
{
	int32_t c;

	for (c = start_candle; c <= end_candle; ++c) {
		if (candles[c].low_pips < *io_low) {
			*io_low = candles[c].low_pips;
		}
		if (candles[c].high_pips > *io_high) {
			*io_high = candles[c].high_pips;
		}
	}
}

static int wave_is_bridgeable(const WaveToken *wave, int32_t reference_height_pips)
{
	int32_t abs_move;
	int32_t max_bridge_move;

	if (wave == NULL) {
		return 0;
	}
	abs_move = abs_i32_mh(wave->net_move_raw);
	max_bridge_move = max_i32_mh(40, (reference_height_pips * 3) / 5);
	if ((int32_t)wave->duration_candles > 24) {
		return 0;
	}
	if (abs_move > max_bridge_move) {
		return 0;
	}
	if (wave->relative_directional_efficiency_step > 750) {
		return 0;
	}
	return 1;
}

static int wave_is_bias_aligned_trend(const WaveToken *wave, int32_t bias)
{
	if (wave == NULL || (int32_t)wave->class_id != RELATIVE_CLASS_TREND) {
		return 0;
	}
	if (wave->dominant_direction != bias) {
		return 0;
	}
	if (abs_i32_mh(wave->net_move_raw) < 20) {
		return 0;
	}
	if (wave->relative_directional_efficiency_step < 450 &&
		wave->relative_trend_quality_raw < 0) {
		return 0;
	}
	return 1;
}

static int wave_is_primary_harvestable(const WaveToken *wave, int32_t bias)
{
	if (wave == NULL) {
		return 0;
	}
	if ((int32_t)wave->class_id == RELATIVE_CLASS_RANGE) {
		return 1;
	}
	return wave_is_bias_aligned_trend(wave, bias);
}

static int32_t micro_harvest_feature_value(const WaveToken *wave, int32_t feature_id)
{
	if (wave == NULL) {
		return 0;
	}
	switch (feature_id) {
	case 1:
		return wave->retracement_ratio;
	case 2:
		return wave->second_best_error;
	case 3:
		return wave->relative_overlap_ratio;
	case 4:
		return wave->relative_directional_efficiency_step;
	case 5:
		return wave->boundary_confidence;
	case 6:
		return wave->weak_fit;
	case 7:
		return abs_i32_mh(wave->net_move_raw);
	case 8:
		return (int32_t)wave->duration_candles;
	case 9:
		return wave->relative_wick_to_body_ratio;
	case 10:
		return wave->ambiguity_margin;
	case 11:
		return wave->fit_error;
	case 12:
		return wave->relative_retracement_ratio;
	case 13:
		return wave->relative_trend_quality_raw;
	case 14:
		return wave->knn_margin;
	case 15:
		return wave->sign_change_density;
	case 16:
		return wave->primitive_alternating_ratio;
	case 17:
		return wave->directional_purity_body;
	default:
		return 0;
	}
}

static int signal_gate_passes(const WaveToken *wave, const MicroHarvestParams *params)
{
	int32_t value;

	if (params == NULL || params->signal_feature_id <= 0 || params->signal_compare_op == 0) {
		return 1;
	}
	value = micro_harvest_feature_value(wave, params->signal_feature_id);
	if (params->signal_compare_op < 0) {
		return value <= params->signal_threshold;
	}
	return value >= params->signal_threshold;
}

static int build_signal_thresholds(const MicroHarvestResult *candidate_islands,
	const WaveToken *waves,
	int32_t feature_id,
	int32_t *out_values,
	int32_t max_values)
{
	int32_t values[256];
	int32_t raw_count = 0;
	int32_t unique_count = 0;
	int32_t i;
	static const int32_t percentiles[] = { 10, 25, 40, 50, 60, 75, 90 };

	if (candidate_islands == NULL || waves == NULL || out_values == NULL || max_values <= 0) {
		return 0;
	}
	for (i = 0; i < candidate_islands->island_count && raw_count < (int32_t)(sizeof(values) / sizeof(values[0])); ++i) {
		const MicroHarvestIsland *island = &candidate_islands->islands[i];
		values[raw_count++] = micro_harvest_feature_value(&waves[island->start_wave_index], feature_id);
		if (raw_count < (int32_t)(sizeof(values) / sizeof(values[0])) &&
			island->end_wave_index != island->start_wave_index) {
			values[raw_count++] = micro_harvest_feature_value(&waves[island->end_wave_index], feature_id);
		}
	}
	if (raw_count <= 0) {
		return 0;
	}
	qsort(values, (size_t)raw_count, sizeof(values[0]), compare_i32_mh);
	for (i = 0; i < (int32_t)(sizeof(percentiles) / sizeof(percentiles[0])) && unique_count < max_values; ++i) {
		int32_t idx = ((raw_count - 1) * percentiles[i]) / 100;
		int32_t value = values[idx];

		if (unique_count == 0 || out_values[unique_count - 1] != value) {
			out_values[unique_count++] = value;
		}
	}
	if (unique_count == 0 && max_values > 0) {
		out_values[unique_count++] = values[raw_count / 2];
	}
	return unique_count;
}

static int append_candidate_island(MicroHarvestResult *result,
	int32_t start_wave_index,
	int32_t end_wave_index,
	int32_t start_candle_index,
	int32_t end_candle_index,
	int32_t low_price_pips,
	int32_t high_price_pips,
	const MicroHarvestParams *params)
{
	MicroHarvestIsland island;

	memset(&island, 0, sizeof(island));
	island.index = result->island_count;
	island.start_wave_index = start_wave_index;
	island.end_wave_index = end_wave_index;
	island.start_candle_index = start_candle_index;
	island.end_candle_index = end_candle_index;
	island.low_price_pips = low_price_pips;
	island.high_price_pips = high_price_pips;
	island.height_pips = high_price_pips - low_price_pips;
	if (!island_height_ok(island.height_pips, params)) {
		return 0;
	}
	return append_island(result, &island);
}

static int build_candidate_islands(const Candle *candles,
	const WaveToken *waves,
	int32_t wave_count,
	const MicroHarvestWindow *window,
	const MicroHarvestParams *params,
	MicroHarvestResult *result)
{
	int32_t start_wave;

	for (start_wave = 0; start_wave < wave_count; ++start_wave) {
		int32_t span_start_candle;
		int32_t span_end_candle;
		int32_t span_low;
		int32_t span_high;
		int32_t end_wave;
		int32_t bridge_run = 0;

		if (!wave_is_primary_harvestable(&waves[start_wave], params->bias)) {
			continue;
		}
		if (!wave_overlaps_window(&waves[start_wave], window, &span_start_candle, &span_end_candle)) {
			continue;
		}
		span_low = candles[span_start_candle].low_pips;
		span_high = candles[span_start_candle].high_pips;
		extend_island_bounds_from_candles(candles, span_start_candle, span_end_candle, &span_low, &span_high);
		if (append_candidate_island(result,
			start_wave,
			start_wave,
			span_start_candle,
			span_end_candle,
			span_low,
			span_high,
			params) != 0) {
			return -1;
		}
		for (end_wave = start_wave + 1; end_wave < wave_count && end_wave - start_wave + 1 <= MH_MAX_ISLAND_SPAN_WAVES; ++end_wave) {
			int32_t wave_start_candle;
			int32_t wave_end_candle;
			int32_t reference_height = max_i32_mh(1, span_high - span_low);

			if ((int32_t)waves[end_wave].start_candle > window->lookback_end_index) {
				break;
			}
			if (!wave_overlaps_window(&waves[end_wave], window, &wave_start_candle, &wave_end_candle)) {
				continue;
			}
			if (wave_is_primary_harvestable(&waves[end_wave], params->bias)) {
				bridge_run = 0;
			} else {
				if (bridge_run >= MH_MAX_BRIDGE_WAVES || !wave_is_bridgeable(&waves[end_wave], reference_height)) {
					break;
				}
				bridge_run += 1;
			}
			span_end_candle = wave_end_candle;
			extend_island_bounds_from_candles(candles, wave_start_candle, wave_end_candle, &span_low, &span_high);
			if (!wave_is_primary_harvestable(&waves[end_wave], params->bias)) {
				continue;
			}
			if (append_candidate_island(result,
				start_wave,
				end_wave,
				span_start_candle,
				span_end_candle,
				span_low,
				span_high,
				params) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

static int compare_order_key(const void *lhs, const void *rhs)
{
	const MicroHarvestOrderKey *a = (const MicroHarvestOrderKey *)lhs;
	const MicroHarvestOrderKey *b = (const MicroHarvestOrderKey *)rhs;

	if (a->end_candle_index != b->end_candle_index) {
		return (a->end_candle_index < b->end_candle_index) ? -1 : 1;
	}
	if (a->original_index != b->original_index) {
		return (a->original_index < b->original_index) ? -1 : 1;
	}
	return 0;
}

static int score_is_better(const MicroHarvestScore *cand, const MicroHarvestScore *best)
{
	if (cand->total_profit_pips != best->total_profit_pips) {
		return cand->total_profit_pips > best->total_profit_pips;
	}
	if (cand->total_closed_trade_count != best->total_closed_trade_count) {
		return cand->total_closed_trade_count > best->total_closed_trade_count;
	}
	if (cand->total_ignored_open_trade_count != best->total_ignored_open_trade_count) {
		return cand->total_ignored_open_trade_count < best->total_ignored_open_trade_count;
	}
	if (cand->total_max_concurrent_open_trade_count != best->total_max_concurrent_open_trade_count) {
		return cand->total_max_concurrent_open_trade_count < best->total_max_concurrent_open_trade_count;
	}
	if (cand->selected_island_count != best->selected_island_count) {
		return cand->selected_island_count > best->selected_island_count;
	}
	return 0;
}

static int score_parameter_better(const MicroHarvestScore *cand,
	const MicroHarvestScore *best,
	const MicroHarvestParams *cand_params,
	const MicroHarvestParams *best_params)
{
	if (score_is_better(cand, best)) {
		return 1;
	}
	if (score_is_better(best, cand)) {
		return 0;
	}
	if (cand_params->min_spacing_pips != best_params->min_spacing_pips) {
		return cand_params->min_spacing_pips < best_params->min_spacing_pips;
	}
	if (cand_params->max_orders != best_params->max_orders) {
		return cand_params->max_orders > best_params->max_orders;
	}
	if (cand_params->entry_edge_fraction != best_params->entry_edge_fraction) {
		return cand_params->entry_edge_fraction < best_params->entry_edge_fraction;
	}
	if (cand_params->take_profit_pips != best_params->take_profit_pips) {
		return cand_params->take_profit_pips > best_params->take_profit_pips;
	}
	if (cand_params->signal_feature_id != best_params->signal_feature_id) {
		return cand_params->signal_feature_id != 0 && best_params->signal_feature_id == 0;
	}
	return cand_params->signal_threshold > best_params->signal_threshold;
}

static int find_prev_non_overlapping(const MicroHarvestOrderKey *order, int32_t pos, const MicroHarvestIsland *scored_islands)
{
	int32_t lo = 0;
	int32_t hi = pos - 1;
	int32_t best = -1;
	int32_t start_candle = scored_islands[order[pos].original_index].start_candle_index;

	while (lo <= hi) {
		int32_t mid = lo + ((hi - lo) / 2);
		int32_t end_candle = scored_islands[order[mid].original_index].end_candle_index;

		if (end_candle < start_candle) {
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return best;
}

static int select_best_island_path(const MicroHarvestIsland *scored_islands,
	int32_t island_count,
	MicroHarvestScore *out_score,
	uint8_t *selected_mask)
{
	MicroHarvestOrderKey *order;
	MicroHarvestDpCell *dp;
	int32_t *prev;
	int32_t pos;

	if (out_score == NULL) {
		return -1;
	}
	memset(out_score, 0, sizeof(*out_score));
	if (island_count <= 0) {
		return 0;
	}
	order = (MicroHarvestOrderKey *)calloc((size_t)island_count, sizeof(*order));
	dp = (MicroHarvestDpCell *)calloc((size_t)island_count, sizeof(*dp));
	prev = (int32_t *)calloc((size_t)island_count, sizeof(*prev));
	if (order == NULL || dp == NULL || prev == NULL) {
		free(order);
		free(dp);
		free(prev);
		return -1;
	}
	for (pos = 0; pos < island_count; ++pos) {
		order[pos].end_candle_index = scored_islands[pos].end_candle_index;
		order[pos].original_index = pos;
	}
	qsort(order, (size_t)island_count, sizeof(*order), compare_order_key);
	for (pos = 0; pos < island_count; ++pos) {
		prev[pos] = find_prev_non_overlapping(order, pos, scored_islands);
	}
	for (pos = 0; pos < island_count; ++pos) {
		MicroHarvestScore skip_score;
		MicroHarvestScore take_score;
		const MicroHarvestIsland *island = &scored_islands[order[pos].original_index];

		memset(&skip_score, 0, sizeof(skip_score));
		memset(&take_score, 0, sizeof(take_score));
		if (pos > 0) {
			skip_score = dp[pos - 1].score;
		}
		if (island->profit_pips > 0) {
			if (prev[pos] >= 0) {
				take_score = dp[prev[pos]].score;
			}
			take_score.total_profit_pips += island->profit_pips;
			take_score.total_closed_trade_count += island->closed_trade_count;
			take_score.total_ignored_open_trade_count += island->ignored_open_trade_count;
			take_score.total_max_concurrent_open_trade_count = max_i32_mh(
				take_score.total_max_concurrent_open_trade_count,
				island->max_concurrent_open_trade_count);
			take_score.selected_island_count += 1;
		}
		if (island->profit_pips > 0 && score_is_better(&take_score, &skip_score)) {
			dp[pos].score = take_score;
			dp[pos].prev_pos = prev[pos];
			dp[pos].took = 1;
		} else {
			dp[pos].score = skip_score;
			dp[pos].prev_pos = pos - 1;
			dp[pos].took = 0;
		}
	}
	*out_score = dp[island_count - 1].score;
	if (selected_mask != NULL) {
		memset(selected_mask, 0, (size_t)island_count * sizeof(*selected_mask));
		pos = island_count - 1;
		while (pos >= 0) {
			if (dp[pos].took) {
				selected_mask[order[pos].original_index] = 1;
				pos = dp[pos].prev_pos;
			} else {
				pos -= 1;
			}
		}
	}
	free(order);
	free(dp);
	free(prev);
	return 0;
}

static int score_parameter_set(const Candle *candles,
	const WaveToken *waves,
	const MicroHarvestResult *candidate_islands,
	const MicroHarvestParams *params,
	MicroHarvestScore *out_score,
	MicroHarvestIsland *out_scored_islands,
	uint8_t *out_selected_mask)
{
	MicroHarvestIsland *scored_islands;
	int32_t i;
	int owns_scored = 0;

	if (out_score == NULL || candidate_islands == NULL || params == NULL) {
		return -1;
	}
	scored_islands = out_scored_islands;
	if (scored_islands == NULL) {
		scored_islands = (MicroHarvestIsland *)calloc((size_t)candidate_islands->island_count, sizeof(*scored_islands));
		if (scored_islands == NULL) {
			return -1;
		}
		owns_scored = 1;
	}
	for (i = 0; i < candidate_islands->island_count; ++i) {
		memset(&scored_islands[i], 0, sizeof(scored_islands[i]));
		scored_islands[i] = candidate_islands->islands[i];
		if (!signal_gate_passes(&waves[candidate_islands->islands[i].start_wave_index], params)) {
			continue;
		}
		if (simulate_island(candles, &candidate_islands->islands[i], params, &scored_islands[i], NULL) != 0) {
			if (owns_scored) {
				free(scored_islands);
			}
			return -1;
		}
	}
	if (select_best_island_path(scored_islands, candidate_islands->island_count, out_score, out_selected_mask) != 0) {
		if (owns_scored) {
			free(scored_islands);
		}
		return -1;
	}
	if (owns_scored) {
		free(scored_islands);
	}
	return 0;
}

int micro_harvest_run(
	const Candle *candles,
	int32_t candle_count,
	const WaveToken *waves,
	int32_t wave_count,
	const MicroHarvestSearchConfig *config,
	MicroHarvestResult *out_result)
{
	MicroHarvestWindow window;
	MicroHarvestResult candidates;
	MicroHarvestParams best_params;
	MicroHarvestScore best_score;
	MicroHarvestIsland *final_scored_islands = NULL;
	uint8_t *selected_mask = NULL;
	int32_t max_orders;
	int32_t spacing;
	int32_t take_profit;
	int32_t entry_edge;
	int32_t feature_id;
	int have_best = 0;
	int32_t i;

	if (candles == NULL || waves == NULL || config == NULL || out_result == NULL || candle_count <= 0 || wave_count <= 0) {
		return -1;
	}
	if (!bias_is_valid(config->bias) || config->max_orders_min <= 0 || config->max_orders_max < config->max_orders_min) {
		return -2;
	}
	if (compute_lookback_window(candles, candle_count, config->lookback_days, &window) != 0) {
		return -3;
	}
	micro_harvest_result_reset(out_result);
	micro_harvest_result_reset(&candidates);
	memset(&best_params, 0, sizeof(best_params));
	memset(&best_score, 0, sizeof(best_score));
	best_params.bias = config->bias;
	best_params.spread_pips = config->spread_pips;
	best_params.min_island_height_pips = config->min_island_height_pips;
	best_params.max_island_height_pips = config->max_island_height_pips;
	if (build_candidate_islands(candles, waves, wave_count, &window, &best_params, &candidates) != 0) {
		micro_harvest_result_free(&candidates);
		return -4;
	}
	if (candidates.island_count == 0) {
		micro_harvest_result_free(&candidates);
		return -5;
	}
	for (feature_id = 0; feature_id <= 17; ++feature_id) {
		int32_t thresholds[8];
		int32_t threshold_count = 1;
		int32_t compare_start = 0;
		int32_t compare_end = 0;
		int32_t compare_op;

		thresholds[0] = 0;
		if (feature_id > 0) {
			threshold_count = build_signal_thresholds(&candidates, waves, feature_id, thresholds, 8);
			if (threshold_count <= 0) {
				continue;
			}
			compare_start = -1;
			compare_end = 1;
		}
		for (compare_op = compare_start; compare_op <= compare_end; compare_op += (feature_id > 0 ? 2 : 1)) {
			int32_t threshold_index;

			for (threshold_index = 0; threshold_index < threshold_count; ++threshold_index) {
				for (max_orders = config->max_orders_min; max_orders <= config->max_orders_max; ++max_orders) {
					for (spacing = config->spacing_min_pips; spacing <= config->spacing_max_pips; spacing += max_i32_mh(1, config->spacing_step_pips)) {
						for (take_profit = config->take_profit_min_pips; take_profit <= config->take_profit_max_pips; take_profit += max_i32_mh(1, config->take_profit_step_pips)) {
							for (entry_edge = config->entry_edge_min; entry_edge <= config->entry_edge_max; entry_edge += max_i32_mh(1, config->entry_edge_step)) {
					MicroHarvestParams cand_params;
					MicroHarvestScore cand_score;

					memset(&cand_params, 0, sizeof(cand_params));
					cand_params.bias = config->bias;
					cand_params.max_orders = max_orders;
					cand_params.min_spacing_pips = spacing;
					cand_params.take_profit_pips = take_profit;
					cand_params.spread_pips = config->spread_pips;
					cand_params.min_island_height_pips = config->min_island_height_pips;
					cand_params.max_island_height_pips = config->max_island_height_pips;
					cand_params.entry_edge_fraction = entry_edge;
					cand_params.signal_feature_id = feature_id;
					cand_params.signal_compare_op = compare_op;
					cand_params.signal_threshold = thresholds[threshold_index];
					if (score_parameter_set(candles, waves, &candidates, &cand_params, &cand_score, NULL, NULL) != 0) {
						micro_harvest_result_free(&candidates);
						return -6;
					}
					if (!have_best || score_parameter_better(&cand_score, &best_score, &cand_params, &best_params)) {
						best_score = cand_score;
						best_params = cand_params;
						have_best = 1;
					}
							}
						}
					}
				}
			}
		}
	}
	if (!have_best) {
		micro_harvest_result_free(&candidates);
		return -7;
	}
	final_scored_islands = (MicroHarvestIsland *)calloc((size_t)candidates.island_count, sizeof(*final_scored_islands));
	selected_mask = (uint8_t *)calloc((size_t)candidates.island_count, sizeof(*selected_mask));
	if (final_scored_islands == NULL || selected_mask == NULL) {
		free(final_scored_islands);
		free(selected_mask);
		micro_harvest_result_free(&candidates);
		return -8;
	}
	if (score_parameter_set(candles, waves, &candidates, &best_params, &best_score, final_scored_islands, selected_mask) != 0) {
		free(final_scored_islands);
		free(selected_mask);
		micro_harvest_result_free(&candidates);
		return -9;
	}
	out_result->recommended_params = best_params;
	out_result->total_profit_pips = best_score.total_profit_pips;
	out_result->total_closed_trade_count = best_score.total_closed_trade_count;
	out_result->total_ignored_open_trade_count = best_score.total_ignored_open_trade_count;
	out_result->total_max_concurrent_open_trade_count = best_score.total_max_concurrent_open_trade_count;
	out_result->selected_island_count = best_score.selected_island_count;
	for (i = 0; i < candidates.island_count; ++i) {
		final_scored_islands[i].selected = selected_mask[i] ? 1 : 0;
		if (append_island(out_result, &final_scored_islands[i]) != 0) {
			free(final_scored_islands);
			free(selected_mask);
			micro_harvest_result_free(&candidates);
			micro_harvest_result_free(out_result);
			return -10;
		}
		if (!selected_mask[i]) {
			continue;
		}
		if (!signal_gate_passes(&waves[candidates.islands[i].start_wave_index], &best_params)) {
			continue;
		}
		if (simulate_island(candles, &candidates.islands[i], &best_params, NULL, out_result) != 0) {
			free(final_scored_islands);
			free(selected_mask);
			micro_harvest_result_free(&candidates);
			micro_harvest_result_free(out_result);
			return -11;
		}
	}
	free(final_scored_islands);
	free(selected_mask);
	micro_harvest_result_free(&candidates);
	return 0;
}

typedef struct {
	int valid;
	int32_t candle_index;
	int32_t mode;
	WaveToken frontier;
	int32_t low_price_pips;
	int32_t high_price_pips;
	int32_t height_pips;
} MicroHarvestReplaySnapshot;

enum {
	MH_REPLAY_MODE_WAIT = 0,
	MH_REPLAY_MODE_RANGE = 1,
	MH_REPLAY_MODE_TREND = 2
};

static int replay_snapshot_mode(const WaveToken *tok, int32_t bias)
{
	if (tok == NULL) {
		return MH_REPLAY_MODE_WAIT;
	}
	if ((int32_t)tok->class_id == RELATIVE_CLASS_RANGE) {
		return MH_REPLAY_MODE_RANGE;
	}
	if (wave_is_bias_aligned_trend(tok, bias)) {
		return MH_REPLAY_MODE_TREND;
	}
	return MH_REPLAY_MODE_WAIT;
}

static int close_all_orders_at_price(MicroHarvestSimState *state,
	const MicroHarvestParams *params,
	int32_t island_index,
	int32_t candle_index,
	int32_t close_price,
	MicroHarvestResult *result)
{
	int32_t i;

	if (state == NULL || params == NULL) {
		return -1;
	}
	for (i = 0; i < MH_MAX_OPEN_ORDERS; ++i) {
		int32_t profit_pips;
		MicroHarvestTrade trade;

		if (!state->open_orders[i].active) {
			continue;
		}
		if (state->open_orders[i].direction == MH_BIAS_BUY) {
			profit_pips = close_price - state->open_orders[i].open_price_pips - params->spread_pips;
		} else {
			profit_pips = state->open_orders[i].open_price_pips - close_price - params->spread_pips;
		}
		memset(&trade, 0, sizeof(trade));
		trade.island_index = island_index;
		trade.open_candle_index = state->open_orders[i].open_candle_index;
		trade.close_candle_index = candle_index;
		trade.direction = state->open_orders[i].direction;
		trade.open_price_pips = state->open_orders[i].open_price_pips;
		trade.close_price_pips = close_price;
		trade.take_profit_pips = params->take_profit_pips;
		trade.profit_pips = profit_pips;
		trade.closed = 1;
		if (result != NULL && append_trade(result, &trade) != 0) {
			return -1;
		}
		state->closed_profit_pips += profit_pips;
		state->closed_trade_count += 1;
		state->open_orders[i].active = 0;
	}
	refresh_open_count(state);
	return 0;
}

static int build_replay_snapshots(uint32_t replay_dataset_id,
	const WaveParams *wave_params,
	int32_t start_index,
	MicroHarvestReplaySnapshot *snapshots,
	int32_t snapshot_count,
	volatile float *progress)
{
	DatasetRecord *rec;
	char err[128];
	uint32_t full_count;
	int32_t i;

	rec = store_find_dataset(replay_dataset_id);
	if (rec == NULL || snapshots == NULL || snapshot_count <= 0) {
		return -1;
	}
	full_count = rec->meta.candle_count;
	for (i = 0; i < snapshot_count; ++i) {
		WaveToken *tok;
		int32_t prefix_count = start_index + i + 1;
		int32_t c;

		memset(&snapshots[i], 0, sizeof(snapshots[i]));
		rec->meta.candle_count = (uint32_t)prefix_count;
		rec->meta.feature_count = (uint32_t)prefix_count;
		err[0] = '\0';
		if (wave_dp_run(replay_dataset_id, wave_params, NULL, NULL, err, sizeof(err)) != 0) {
			rec->meta.candle_count = full_count;
			rec->meta.feature_count = full_count;
			return -2;
		}
		if (rec->waves.token_count == 0) {
			continue;
		}
		tok = &rec->waves.tokens[rec->waves.token_count - 1u];
		snapshots[i].valid = 1;
		snapshots[i].candle_index = prefix_count - 1;
		snapshots[i].frontier = *tok;
		snapshots[i].low_price_pips = rec->candles[tok->start_candle].low_pips;
		snapshots[i].high_price_pips = rec->candles[tok->start_candle].high_pips;
		for (c = (int32_t)tok->start_candle + 1; c <= (int32_t)tok->end_candle; ++c) {
			if (rec->candles[c].low_pips < snapshots[i].low_price_pips) {
				snapshots[i].low_price_pips = rec->candles[c].low_pips;
			}
			if (rec->candles[c].high_pips > snapshots[i].high_price_pips) {
				snapshots[i].high_price_pips = rec->candles[c].high_pips;
			}
		}
		snapshots[i].height_pips = snapshots[i].high_price_pips - snapshots[i].low_price_pips;
		if (progress != NULL) {
			*progress = 0.05f + 0.45f * ((float)(i + 1) / (float)snapshot_count);
		}
	}
	rec->meta.candle_count = full_count;
	rec->meta.feature_count = full_count;
	return 0;
}

static int build_replay_signal_thresholds(const MicroHarvestReplaySnapshot *snapshots,
	int32_t snapshot_count,
	int32_t feature_id,
	int32_t *out_values,
	int32_t max_values)
{
	int32_t values[512];
	int32_t raw_count = 0;
	int32_t unique_count = 0;
	int32_t i;
	static const int32_t percentiles[] = { 10, 25, 40, 50, 60, 75, 90 };

	for (i = 0; i < snapshot_count && raw_count < (int32_t)(sizeof(values) / sizeof(values[0])); ++i) {
		if (!snapshots[i].valid) {
			continue;
		}
		values[raw_count++] = micro_harvest_feature_value(&snapshots[i].frontier, feature_id);
	}
	if (raw_count <= 0) {
		return 0;
	}
	qsort(values, (size_t)raw_count, sizeof(values[0]), compare_i32_mh);
	for (i = 0; i < (int32_t)(sizeof(percentiles) / sizeof(percentiles[0])) && unique_count < max_values; ++i) {
		int32_t idx = ((raw_count - 1) * percentiles[i]) / 100;
		int32_t value = values[idx];

		if (unique_count == 0 || out_values[unique_count - 1] != value) {
			out_values[unique_count++] = value;
		}
	}
	return unique_count;
}

static int score_replay_parameter_set(const Candle *candles,
	const MicroHarvestReplaySnapshot *snapshots,
	int32_t snapshot_count,
	const MicroHarvestParams *params,
	MicroHarvestScore *out_score,
	MicroHarvestResult *result)
{
	MicroHarvestSimState state;
	int32_t i;
	int32_t current_mode = MH_REPLAY_MODE_WAIT;
	int32_t last_open_candle = -1000000;

	memset(&state, 0, sizeof(state));
	memset(out_score, 0, sizeof(*out_score));
	for (i = 0; i < snapshot_count - 1; ++i) {
		MicroHarvestIsland island;
		int32_t mode;
		int32_t trade_candle_index;

		if (!snapshots[i].valid) {
			continue;
		}
		trade_candle_index = snapshots[i].candle_index + 1;
		if (trade_candle_index >= snapshots[snapshot_count - 1].candle_index + 1) {
			break;
		}
		mode = replay_snapshot_mode(&snapshots[i].frontier, params->bias);
		if (!signal_gate_passes(&snapshots[i].frontier, params)) {
			mode = MH_REPLAY_MODE_WAIT;
		}
		if (mode != current_mode && state.open_count > 0) {
			if (close_all_orders_at_price(&state,
				params,
				i,
				trade_candle_index,
				candles[trade_candle_index].open_pips,
				result) != 0) {
				return -1;
			}
		}
		current_mode = mode;
		if (current_mode == MH_REPLAY_MODE_WAIT) {
			continue;
		}
		memset(&island, 0, sizeof(island));
		island.index = i;
		island.start_candle_index = (int32_t)snapshots[i].frontier.start_candle;
		island.end_candle_index = (int32_t)snapshots[i].frontier.end_candle;
		island.low_price_pips = snapshots[i].low_price_pips;
		island.high_price_pips = snapshots[i].high_price_pips;
		island.height_pips = max_i32_mh(1, snapshots[i].height_pips);
		if (open_orders_for_candle(&state,
			params,
			&island,
			trade_candle_index,
			&candles[trade_candle_index],
			&last_open_candle) < 0) {
			return -1;
		}
		if (close_eligible_orders(&state,
			params,
			i,
			trade_candle_index,
			&candles[trade_candle_index],
			result) != 0) {
			return -1;
		}
	}
	if (state.open_count > 0) {
		int32_t last_candle_index = snapshots[snapshot_count - 1].candle_index;

		if (close_all_orders_at_price(&state,
			params,
			snapshot_count,
			last_candle_index,
			candles[last_candle_index].close_pips,
			result) != 0) {
			return -1;
		}
	}
	out_score->total_profit_pips = state.closed_profit_pips;
	out_score->total_closed_trade_count = state.closed_trade_count;
	out_score->total_ignored_open_trade_count = state.ignored_open_trade_count;
	out_score->total_max_concurrent_open_trade_count = state.max_open_count;
	return 0;
}

int micro_harvest_run_replay(
	uint32_t replay_dataset_id,
	const WaveParams *wave_params,
	const MicroHarvestSearchConfig *config,
	MicroHarvestResult *out_result,
	volatile float *progress)
{
	DatasetRecord *rec;
	MicroHarvestReplaySnapshot *snapshots;
	MicroHarvestParams best_params;
	MicroHarvestScore best_score;
	int32_t start_index;
	int32_t snapshot_count;
	int32_t max_orders;
	int32_t spacing;
	int32_t take_profit;
	int32_t entry_edge;
	int32_t feature_id;
	int have_best = 0;

	rec = store_find_dataset(replay_dataset_id);
	if (rec == NULL || wave_params == NULL || config == NULL || out_result == NULL) {
		return -1;
	}
	if (rec->meta.candle_count < wave_params->lmin_candles + 2u) {
		return -2;
	}
	start_index = max_i32_mh((int32_t)wave_params->lmin_candles, 8);
	snapshot_count = (int32_t)rec->meta.candle_count - start_index;
	if (snapshot_count <= 2) {
		return -3;
	}
	snapshots = (MicroHarvestReplaySnapshot *)calloc((size_t)snapshot_count, sizeof(*snapshots));
	if (snapshots == NULL) {
		return -4;
	}
	if (build_replay_snapshots(replay_dataset_id, wave_params, start_index, snapshots, snapshot_count, progress) != 0) {
		free(snapshots);
		return -5;
	}
	micro_harvest_result_reset(out_result);
	memset(&best_params, 0, sizeof(best_params));
	memset(&best_score, 0, sizeof(best_score));
	best_params.bias = config->bias;
	best_params.spread_pips = config->spread_pips;
	best_params.min_island_height_pips = config->min_island_height_pips;
	best_params.max_island_height_pips = config->max_island_height_pips;
	for (feature_id = 0; feature_id <= 17; ++feature_id) {
		int32_t thresholds[8];
		int32_t threshold_count = 1;
		int32_t compare_start = 0;
		int32_t compare_end = 0;
		int32_t compare_op;

		thresholds[0] = 0;
		if (feature_id > 0) {
			threshold_count = build_replay_signal_thresholds(snapshots, snapshot_count, feature_id, thresholds, 8);
			if (threshold_count <= 0) {
				continue;
			}
			compare_start = -1;
			compare_end = 1;
		}
		for (compare_op = compare_start; compare_op <= compare_end; compare_op += (feature_id > 0 ? 2 : 1)) {
			int32_t threshold_index;

			for (threshold_index = 0; threshold_index < threshold_count; ++threshold_index) {
				for (max_orders = config->max_orders_min; max_orders <= config->max_orders_max; ++max_orders) {
					for (spacing = config->spacing_min_pips; spacing <= config->spacing_max_pips; spacing += max_i32_mh(1, config->spacing_step_pips)) {
						for (take_profit = config->take_profit_min_pips; take_profit <= config->take_profit_max_pips; take_profit += max_i32_mh(1, config->take_profit_step_pips)) {
							for (entry_edge = config->entry_edge_min; entry_edge <= config->entry_edge_max; entry_edge += max_i32_mh(1, config->entry_edge_step)) {
								MicroHarvestParams cand_params;
								MicroHarvestScore cand_score;

								memset(&cand_params, 0, sizeof(cand_params));
								cand_params.bias = config->bias;
								cand_params.max_orders = max_orders;
								cand_params.min_spacing_pips = spacing;
								cand_params.take_profit_pips = take_profit;
								cand_params.spread_pips = config->spread_pips;
								cand_params.min_island_height_pips = config->min_island_height_pips;
								cand_params.max_island_height_pips = config->max_island_height_pips;
								cand_params.entry_edge_fraction = entry_edge;
								cand_params.signal_feature_id = feature_id;
								cand_params.signal_compare_op = compare_op;
								cand_params.signal_threshold = thresholds[threshold_index];
								if (score_replay_parameter_set(rec->candles, snapshots, snapshot_count, &cand_params, &cand_score, NULL) != 0) {
									free(snapshots);
									return -6;
								}
								if (!have_best || score_parameter_better(&cand_score, &best_score, &cand_params, &best_params)) {
									best_score = cand_score;
									best_params = cand_params;
									have_best = 1;
								}
							}
						}
					}
				}
			}
		}
		if (progress != NULL) {
			*progress = 0.50f + 0.45f * ((float)(feature_id + 1) / 18.0f);
		}
	}
	if (!have_best) {
		free(snapshots);
		return -7;
	}
	out_result->recommended_params = best_params;
	out_result->replay_mode = 1;
	if (score_replay_parameter_set(rec->candles, snapshots, snapshot_count, &best_params, &best_score, out_result) != 0) {
		micro_harvest_result_free(out_result);
		free(snapshots);
		return -8;
	}
	out_result->total_profit_pips = best_score.total_profit_pips;
	out_result->total_closed_trade_count = best_score.total_closed_trade_count;
	out_result->total_ignored_open_trade_count = best_score.total_ignored_open_trade_count;
	out_result->total_max_concurrent_open_trade_count = best_score.total_max_concurrent_open_trade_count;
	out_result->selected_island_count = best_score.selected_island_count;
	free(snapshots);
	if (progress != NULL) {
		*progress = 0.98f;
	}
	return 0;
}
