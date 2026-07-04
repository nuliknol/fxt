#include "api_types.h"

#include <hip/hip_runtime.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#define WAVE_DP_THREADS 64

static Candle *g_seed_scan_candles = nullptr;
static CandleFeat *g_seed_scan_features = nullptr;
static uint32_t *g_seed_scan_best_end = nullptr;
static int32_t *g_seed_scan_quality_raw = nullptr;
static int32_t *g_seed_scan_net_move_raw = nullptr;
static int32_t *g_seed_scan_price_amplitude_raw = nullptr;
static int32_t *g_seed_scan_retracement_ratio = nullptr;
static int32_t *g_seed_scan_overlap_ratio = nullptr;
static int32_t *g_seed_scan_wick_to_body_ratio = nullptr;
static int32_t *g_seed_scan_strongest_leg_raw = nullptr;
static uint32_t *g_seed_scan_duration = nullptr;
static uint32_t g_seed_scan_candle_capacity = 0u;
static uint32_t g_seed_scan_range_capacity = 0u;
static const Candle *g_seed_scan_host_candles = nullptr;
static const CandleFeat *g_seed_scan_host_features = nullptr;
static uint32_t g_seed_scan_host_candle_count = 0u;

typedef struct {
	int64_t score;
	int32_t start;
	uint32_t class_id;
	int32_t fit_error;
	int32_t second_best_error;
	int32_t ambiguity_margin;
	int32_t net_move_raw;
	int32_t relative_net_move_raw;
	int32_t retracement_ratio;
	int32_t sign_change_density;
	int32_t directional_purity_body;
	int32_t range_growth_ratio;
	int32_t context_range;
	int32_t relative_directional_efficiency_body;
	int32_t relative_directional_efficiency_step;
	int32_t relative_avg_body_efficiency;
	int32_t relative_body_direction_share;
	int32_t relative_wick_to_body_ratio;
	int32_t relative_retracement_ratio;
	int32_t relative_overlap_ratio;
	int32_t relative_trend_quality_raw;
	int32_t relative_impulse_leg_raw;
	int32_t price_amplitude_raw;
	int32_t dominant_direction;
	uint32_t duration;
} WaveCandidateBest;

__device__ static int32_t abs_i32_device(int32_t v)
{
	return (v >= 0) ? v : -v;
}

__device__ static int32_t max_i32_device(int32_t a, int32_t b)
{
	return (a > b) ? a : b;
}

__device__ static int32_t min_i32_device(int32_t a, int32_t b)
{
	return (a < b) ? a : b;
}

__device__ static int32_t sign_i32_device(int32_t v)
{
	return (v > 0) ? 1 : ((v < 0) ? -1 : 0);
}

__device__ static int32_t penalty_low_device(int32_t x, int32_t a)
{
	return (x < a) ? (a - x) : 0;
}

__device__ static int32_t penalty_high_device(int32_t x, int32_t b)
{
	return (x > b) ? (x - b) : 0;
}

__device__ static int32_t penalty_band_device(int32_t x, int32_t a, int32_t b)
{
	if (x < a) {
		return a - x;
	}
	if (x > b) {
		return x - b;
	}
	return 0;
}

__device__ static uint32_t wave_output_class_count_device(const WaveParams *params)
{
	if (params != nullptr &&
		(params->wave_model == WAVE_MODEL_RELATIVE ||
		 params->wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
		 params->wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		 params->wave_model == WAVE_MODEL_RELATIVE_ITERATIVE)) {
		return RELATIVE_CLASS_COUNT;
	}
	if (params != nullptr && params->wave_model == WAVE_MODEL_PRIMITIVE) {
		return PRIMITIVE_CLASS_COUNT;
	}
	return WAVE_CLASS_COUNT;
}

__device__ static int close_step_direction_device(const Candle *candles, uint32_t prev, uint32_t next)
{
	int32_t delta;

	if (candles == nullptr || next <= prev) {
		return 0;
	}
	delta = candles[next].close_pips - candles[prev].close_pips;
	if (delta > 0) {
		return 1;
	}
	if (delta < 0) {
		return -1;
	}
	return 0;
}

__device__ static int is_inside_candle_device(const Candle *candles, uint32_t index)
{
	if (candles == nullptr || index == 0u) {
		return 0;
	}
	return candles[index].high_pips <= candles[index - 1u].high_pips &&
		candles[index].low_pips >= candles[index - 1u].low_pips;
}

__device__ static int is_tolerated_counter_trend_candle_device(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	uint32_t index,
	int32_t canonical_sign)
{
	uint32_t next_index;

	if (candles == nullptr || features == nullptr || index <= start || index >= end) {
		return 0;
	}
	next_index = index + 1u;
	while (next_index <= end && features[next_index].b == 0) {
		next_index += 1u;
	}
	if (next_index > end) {
		return 0;
	}
	if (canonical_sign > 0) {
		return features[index].b < 0 &&
			features[next_index].b > 0 &&
			candles[index].close_pips >= candles[index - 1u].low_pips &&
			candles[next_index].close_pips > candles[index - 1u].close_pips;
	}
	if (canonical_sign < 0) {
		return features[index].b > 0 &&
			features[next_index].b < 0 &&
			candles[index].close_pips <= candles[index - 1u].high_pips &&
			candles[next_index].close_pips < candles[index - 1u].close_pips;
	}
	return 0;
}

__device__ static void compute_context_bounds(uint32_t candle_count,
	const WaveParams *params,
	uint32_t start,
	uint32_t end,
	uint32_t *out_ctx_start,
	uint32_t *out_ctx_end)
{
	uint32_t duration = end - start + 1u;
	uint32_t ctx_len = params->regime_context_window;

	if (ctx_len < duration) {
		ctx_len = duration;
	}
	if (params->regime_context_mode == WAVE_CONTEXT_TRAILING) {
		uint32_t ctx_end = end;
		uint32_t ctx_start = (ctx_len > end + 1u) ? 0u : (end + 1u - ctx_len);

		*out_ctx_start = ctx_start;
		*out_ctx_end = ctx_end;
		return;
	}
	{
		int32_t center = (int32_t)((start + end) / 2u);
		int32_t ctx_start = center - (int32_t)(ctx_len / 2u);
		int32_t ctx_end = ctx_start + (int32_t)ctx_len - 1;

		if (ctx_start < 0) {
			ctx_end -= ctx_start;
			ctx_start = 0;
		}
		if (ctx_end >= (int32_t)candle_count) {
			int32_t overshoot = ctx_end - (int32_t)candle_count + 1;
			ctx_start -= overshoot;
			ctx_end -= overshoot;
			if (ctx_start < 0) {
				ctx_start = 0;
			}
		}
		if ((uint32_t)ctx_start > start) {
			ctx_start = (int32_t)start;
			ctx_end = min_i32_device((int32_t)candle_count - 1, ctx_start + (int32_t)ctx_len - 1);
		}
		if ((uint32_t)ctx_end < end) {
			ctx_end = (int32_t)end;
			ctx_start = max_i32_device(0, ctx_end - (int32_t)ctx_len + 1);
		}
		*out_ctx_start = (uint32_t)ctx_start;
		*out_ctx_end = (uint32_t)ctx_end;
	}
}

__device__ static int relative_span_has_consistent_edges_device(const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction)
{
	int first_sign = 0;
	int last_sign = 0;

	if (features == nullptr || end < start || dominant_direction == 0) {
		return 0;
	}
	first_sign = sign_i32_device(features[start].b);
	last_sign = sign_i32_device(features[end].b);
	if (first_sign != 0 && first_sign != dominant_direction) {
		return 0;
	}
	if (last_sign != 0 && last_sign != dominant_direction) {
		return 0;
	}
	if (first_sign == 0 && last_sign == 0) {
		return 0;
	}
	return 1;
}

__device__ static uint32_t find_relative_trend_terminal_index_device(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction)
{
	uint32_t best_index = start;
	int32_t best_price;
	uint32_t c;

	if (candles == nullptr || features == nullptr || end < start || dominant_direction == 0) {
		return start;
	}
	best_price = (dominant_direction > 0) ?
		max_i32_device(candles[start].open_pips, candles[start].close_pips) :
		min_i32_device(candles[start].open_pips, candles[start].close_pips);
	for (c = start; c <= end; ++c) {
		int sign = sign_i32_device(features[c].b);
		int32_t price = (dominant_direction > 0) ?
			max_i32_device(candles[c].open_pips, candles[c].close_pips) :
			min_i32_device(candles[c].open_pips, candles[c].close_pips);

		if (sign != dominant_direction) {
			continue;
		}
		if ((dominant_direction > 0 && price > best_price) ||
			(dominant_direction < 0 && price < best_price)) {
			best_price = price;
			best_index = c;
		}
	}
	return best_index;
}

__device__ static uint32_t find_relative_trend_start_index_device(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction)
{
	uint32_t best_index = start;
	int32_t best_price;
	uint32_t c;

	if (candles == nullptr || features == nullptr || end < start || dominant_direction == 0) {
		return start;
	}
	best_price = (dominant_direction > 0) ?
		min_i32_device(candles[start].open_pips, candles[start].close_pips) :
		max_i32_device(candles[start].open_pips, candles[start].close_pips);
	for (c = start; c <= end; ++c) {
		int sign = sign_i32_device(features[c].b);
		int32_t price = (dominant_direction > 0) ?
			min_i32_device(candles[c].open_pips, candles[c].close_pips) :
			max_i32_device(candles[c].open_pips, candles[c].close_pips);

		if (sign != dominant_direction) {
			continue;
		}
		if ((dominant_direction > 0 && price <= best_price) ||
			(dominant_direction < 0 && price >= best_price)) {
			best_price = price;
			best_index = c;
		}
	}
	return best_index;
}

__device__ static int32_t compute_relative_trend_price_amplitude_device(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction)
{
	uint32_t actual_start = start;
	uint32_t actual_end = end;

	if (candles == nullptr || features == nullptr || end < start || dominant_direction == 0) {
		return 0;
	}
	while (actual_start <= end && sign_i32_device(features[actual_start].b) != dominant_direction) {
		actual_start += 1u;
	}
	if (actual_start > end) {
		return 0;
	}
	while (actual_end > actual_start && sign_i32_device(features[actual_end].b) != dominant_direction) {
		actual_end -= 1u;
	}
	actual_end = find_relative_trend_terminal_index_device(candles, features, actual_start, actual_end, dominant_direction);
	actual_start = find_relative_trend_start_index_device(candles, features, actual_start, actual_end, dominant_direction);
	if (dominant_direction > 0) {
		return max_i32_device(candles[actual_end].open_pips, candles[actual_end].close_pips) -
			min_i32_device(candles[actual_start].open_pips, candles[actual_start].close_pips);
	}
	return max_i32_device(candles[actual_start].open_pips, candles[actual_start].close_pips) -
		min_i32_device(candles[actual_end].open_pips, candles[actual_end].close_pips);
}

__device__ static WaveCandidateBest build_candidate(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const int64_t *dp,
	uint32_t end,
	uint32_t start)
{
	WaveCandidateBest out;
	uint32_t duration = end - start + 1u;
	uint32_t ctx_start;
	uint32_t ctx_end;
	int32_t ctx_high;
	int32_t ctx_low;
	int32_t context_range;
	int32_t context_denom;
	int32_t raw_net = candles[end].close_pips - candles[start].open_pips;
	int32_t canonical_sign = (raw_net < 0) ? -1 : 1;
	int32_t last_sign = 0;
	int32_t sign_changes = 0;
	int32_t trend_body_count = 0;
	int32_t counter_body_count = 0;
	int32_t trend_body = 0;
	int32_t retrace_body = 0;
	int32_t gross_move_raw = 0;
	int32_t gross_range_raw = 0;
	int32_t relative_abs_body_sum_raw = 0;
	int32_t relative_abs_step_sum_raw = 0;
	int32_t relative_body_efficiency_sum = 0;
	int32_t relative_aligned_body_sum = 0;
	int32_t relative_counter_body_sum = 0;
	int32_t relative_upper_wick_sum_raw = 0;
	int32_t relative_lower_wick_sum_raw = 0;
	int32_t relative_max_pullback_raw = 0;
	int32_t relative_impulse_leg_raw = 0;
	int32_t relative_body_overlap_sum_raw = 0;
	int32_t active_body_count = 0;
	int32_t step_nonzero_count = 0;
	int32_t step_consecutive_count = 0;
	int32_t step_alternating_count = 0;
	int32_t step_same_run = 0;
	int32_t step_alt_run = 1;
	int32_t step_max_same_run = 0;
	int32_t step_max_alt_run = 0;
	int32_t prev_step_dir = 0;
	int32_t first_half_sum = 0;
	int32_t second_half_sum = 0;
	uint32_t first_half_len;
	uint32_t second_half_len;
	int32_t first_half_body_sum = 0;
	int32_t second_half_body_sum = 0;
	int32_t range_growth_ratio;
	int32_t net_move_raw;
	int32_t net_move_norm;
	int32_t gross_range_norm;
	int32_t net_move_ratio;
	int32_t retracement_ratio;
	int32_t sign_change_density;
	int32_t directional_purity_body;
	int32_t advance_per_candle_norm;
	int32_t active_body_ratio;
	int32_t primitive_consecutive_ratio;
	int32_t primitive_alternating_ratio;
	int32_t primitive_max_consecutive_run;
	int32_t primitive_max_alternating_run;
	int32_t primitive_counter_body_ratio;
	int32_t primitive_minority_body_ratio;
	int32_t relative_directional_efficiency_body;
	int32_t relative_directional_efficiency_step;
	int32_t relative_avg_body_efficiency;
	int32_t relative_total_wick_sum_raw;
	int32_t relative_wick_to_body_ratio;
	int32_t relative_wick_bias_aligned;
	int32_t relative_overlap_ratio;
	int32_t relative_retracement_ratio;
	int32_t trend_quality_penalty;
	int32_t range_quality_penalty;
	int32_t move_penalty;
	int32_t v_strength = 0;
	int32_t relative_strong_trend_error;
	int32_t relative_weak_trend_error;
	int32_t relative_trend_error;
	int32_t relative_range_error;
	int32_t class_error[WAVE_CLASS_COUNT];
	int32_t strong_trend_error;
	int32_t weak_trend_error;
	int32_t retracement_error;
	int32_t alternating_chop_error;
	int32_t mixed_compression_error;
	int32_t volatile_expansion_error;
	int32_t v_shaped_reversal_error;
	int32_t separator_error;
	int edge_direction_mismatch = 0;
	uint32_t tolerated_counter_index = candle_count;
	uint32_t first_effective_index = candle_count;
	uint32_t last_effective_index = candle_count;
	uint32_t first_body_index = candle_count;
	uint32_t last_body_index = candle_count;
	int32_t relative_running_extreme = candles[start].close_pips;
	uint32_t best_class = 0u;
	uint32_t second_best_class = 0u;
	int32_t best_fit;
	int32_t second_best_error;
	uint32_t class_count_limit;
	uint32_t t;

	compute_context_bounds(candle_count, params, start, end, &ctx_start, &ctx_end);
	ctx_high = candles[ctx_start].high_pips;
	ctx_low = candles[ctx_start].low_pips;
	for (t = ctx_start + 1u; t <= ctx_end; ++t) {
		if (candles[t].high_pips > ctx_high) {
			ctx_high = candles[t].high_pips;
		}
		if (candles[t].low_pips < ctx_low) {
			ctx_low = candles[t].low_pips;
		}
	}
	context_range = ctx_high - ctx_low;
	context_denom = max_i32_device(context_range, params->context_range_floor);
	net_move_raw = abs_i32_device(raw_net);
	first_half_len = duration / 2u;
	if (first_half_len == 0u) {
		first_half_len = 1u;
	}
	second_half_len = duration - first_half_len;
	if (second_half_len == 0u) {
		second_half_len = 1u;
	}

	for (t = start; t <= end; ++t) {
		int32_t canonical_body = features[t].b * canonical_sign;
		int32_t body_abs = abs_i32_device(features[t].b);
		int32_t range_abs = max_i32_device(features[t].r, 0);
		int32_t upper_wick = candles[t].high_pips - max_i32_device(candles[t].open_pips, candles[t].close_pips);
		int32_t lower_wick = min_i32_device(candles[t].open_pips, candles[t].close_pips) - candles[t].low_pips;
		int tolerated_counter = 0;
		int32_t sign = 0;
		int ignore_inside = is_inside_candle_device(candles, t);
		int ignore_doji = (features[t].b == 0);
		uint32_t offset = t - start;

		if (tolerated_counter_index == candle_count &&
			is_tolerated_counter_trend_candle_device(candles, features, start, end, t, canonical_sign)) {
			tolerated_counter = 1;
			tolerated_counter_index = t;
			canonical_body = 0;
		}

		if (canonical_body > 0) {
			sign = 1;
			trend_body_count += 1;
			trend_body += canonical_body;
			relative_aligned_body_sum += canonical_body;
		} else if (canonical_body < 0) {
			sign = -1;
			counter_body_count += 1;
			retrace_body += -canonical_body;
			relative_counter_body_sum += -canonical_body;
		}
		if (!ignore_inside && !ignore_doji && sign != 0 && last_sign != 0 && sign != last_sign) {
			sign_changes += 1;
		}
		if (!ignore_inside && !ignore_doji && sign != 0) {
			last_sign = sign;
		}
		if (canonical_body != 0 || tolerated_counter) {
			active_body_count += 1;
		}
		if (features[t].b != 0) {
			if (first_body_index == candle_count) {
				first_body_index = t;
			}
			last_body_index = t;
		}
		if (!ignore_inside && !ignore_doji && canonical_body != 0) {
			if (first_effective_index == candle_count) {
				first_effective_index = t;
			}
			last_effective_index = t;
		}
		gross_move_raw += abs_i32_device(canonical_body);
		gross_range_raw += features[t].r;
		relative_abs_body_sum_raw += body_abs;
		relative_body_efficiency_sum += (int32_t)(((int64_t)body_abs * params->norm_scale) /
			(int64_t)max_i32_device(range_abs, params->epsilon));
		relative_upper_wick_sum_raw += upper_wick;
		relative_lower_wick_sum_raw += lower_wick;
		if (offset < first_half_len) {
			first_half_sum += features[t].r;
			first_half_body_sum += body_abs;
		} else {
			second_half_sum += features[t].r;
			second_half_body_sum += body_abs;
		}
		if (t > start) {
			int32_t step_abs = abs_i32_device(candles[t].close_pips - candles[t - 1u].close_pips);
			int32_t prev_body_lo = min_i32_device(candles[t - 1u].open_pips, candles[t - 1u].close_pips);
			int32_t prev_body_hi = max_i32_device(candles[t - 1u].open_pips, candles[t - 1u].close_pips);
			int32_t body_lo = min_i32_device(candles[t].open_pips, candles[t].close_pips);
			int32_t body_hi = max_i32_device(candles[t].open_pips, candles[t].close_pips);
			int32_t overlap = min_i32_device(body_hi, prev_body_hi) - max_i32_device(body_lo, prev_body_lo);

			relative_abs_step_sum_raw += step_abs;
			if (overlap > 0) {
				relative_body_overlap_sum_raw += overlap;
			}
		}
		if (raw_net > 0) {
			if (candles[t].close_pips > relative_running_extreme) {
				relative_running_extreme = candles[t].close_pips;
			}
			if (relative_running_extreme - candles[t].close_pips > relative_max_pullback_raw) {
				relative_max_pullback_raw = relative_running_extreme - candles[t].close_pips;
			}
			if (relative_running_extreme - candles[start].close_pips > relative_impulse_leg_raw) {
				relative_impulse_leg_raw = relative_running_extreme - candles[start].close_pips;
			}
		} else if (raw_net < 0) {
			if (candles[t].close_pips < relative_running_extreme) {
				relative_running_extreme = candles[t].close_pips;
			}
			if (candles[t].close_pips - relative_running_extreme > relative_max_pullback_raw) {
				relative_max_pullback_raw = candles[t].close_pips - relative_running_extreme;
			}
			if (candles[start].close_pips - relative_running_extreme > relative_impulse_leg_raw) {
				relative_impulse_leg_raw = candles[start].close_pips - relative_running_extreme;
			}
		}
	}
	for (t = start; t < end; ++t) {
		int32_t left_raw = candles[t].close_pips - candles[start].open_pips;
		int32_t right_raw = candles[end].close_pips - candles[t + 1u].open_pips;

		if ((left_raw > 0 && right_raw < 0) || (left_raw < 0 && right_raw > 0)) {
			int32_t left_strength = (int32_t)(((int64_t)abs_i32_device(left_raw) * params->norm_scale) / (int64_t)context_denom);
			int32_t right_strength = (int32_t)(((int64_t)abs_i32_device(right_raw) * params->norm_scale) / (int64_t)context_denom);
			int32_t min_strength = min_i32_device(left_strength, right_strength);

			if (min_strength > v_strength) {
				v_strength = min_strength;
			}
		}
	}
	for (t = start + 1u; t <= end; ++t) {
		int32_t step_dir = close_step_direction_device(candles, t - 1u, t);

		if (t == tolerated_counter_index) {
			continue;
		}
		if (features[t].b == 0) {
			continue;
		}
		if (is_inside_candle_device(candles, t)) {
			continue;
		}
		if (step_dir == 0) {
			continue;
		}
		step_nonzero_count += 1;
		if (step_dir == canonical_sign) {
			step_consecutive_count += 1;
		}
		if (prev_step_dir == 0) {
			step_same_run = 1;
			step_alt_run = 1;
		} else if (step_dir == prev_step_dir) {
			step_same_run += 1;
			step_alt_run = 1;
		} else {
			step_same_run = 1;
			step_alt_run += 1;
			step_alternating_count += 1;
		}
		if (step_same_run > step_max_same_run) {
			step_max_same_run = step_same_run;
		}
		if (step_alt_run > step_max_alt_run) {
			step_max_alt_run = step_alt_run;
		}
		prev_step_dir = step_dir;
	}

	range_growth_ratio = (duration < 2u)
		? params->norm_scale
		: (int32_t)(((int64_t)(second_half_sum / (int32_t)second_half_len) * params->norm_scale) /
			(int64_t)max_i32_device(first_half_sum / (int32_t)first_half_len, params->epsilon));
	{
		int32_t relative_wick_bias_up;
		int32_t relative_wick_bias_down;

		net_move_norm = (int32_t)(((int64_t)net_move_raw * params->norm_scale) / (int64_t)context_denom);
		gross_range_norm = (int32_t)(((int64_t)gross_range_raw * params->norm_scale) / (int64_t)context_denom);
		net_move_ratio = (int32_t)(((int64_t)net_move_raw * params->norm_scale) /
			(int64_t)max_i32_device(gross_range_raw, params->epsilon));
		retracement_ratio = (int32_t)(((int64_t)retrace_body * params->norm_scale) /
			(int64_t)max_i32_device(trend_body, params->epsilon));
		sign_change_density = (int32_t)(((int64_t)sign_changes * params->norm_scale) /
			(int64_t)max_i32_device((int32_t)duration - 1, 1));
		active_body_ratio = (int32_t)(((int64_t)active_body_count * params->norm_scale) /
			(int64_t)max_i32_device((int32_t)duration, 1));
		primitive_consecutive_ratio = (int32_t)(((int64_t)step_consecutive_count * params->norm_scale) /
			(int64_t)max_i32_device(step_nonzero_count, 1));
		primitive_alternating_ratio = (int32_t)(((int64_t)step_alternating_count * params->norm_scale) /
			(int64_t)max_i32_device(step_nonzero_count, 1));
		primitive_max_consecutive_run = step_max_same_run + ((step_max_same_run > 0) ? 1 : 0);
		primitive_max_alternating_run = step_max_alt_run + ((step_max_alt_run > 0) ? 1 : 0);
		primitive_counter_body_ratio = (int32_t)(((int64_t)counter_body_count * params->norm_scale) /
			(int64_t)max_i32_device(active_body_count, 1));
		primitive_minority_body_ratio = (int32_t)(((int64_t)min_i32_device(trend_body_count, counter_body_count) * params->norm_scale) /
			(int64_t)max_i32_device(active_body_count, 1));
		relative_directional_efficiency_body = (int32_t)(((int64_t)net_move_raw * params->norm_scale) /
			(int64_t)max_i32_device(relative_abs_body_sum_raw, params->epsilon));
		relative_directional_efficiency_step = (int32_t)(((int64_t)net_move_raw * params->norm_scale) /
			(int64_t)max_i32_device(relative_abs_step_sum_raw, params->epsilon));
		relative_avg_body_efficiency = relative_body_efficiency_sum / (int32_t)max_i32_device((int32_t)duration, 1);
		relative_wick_bias_up = (int32_t)(((int64_t)(relative_lower_wick_sum_raw + params->epsilon) * params->norm_scale) /
			(int64_t)max_i32_device(relative_upper_wick_sum_raw + params->epsilon, 1));
		relative_wick_bias_down = (int32_t)(((int64_t)(relative_upper_wick_sum_raw + params->epsilon) * params->norm_scale) /
			(int64_t)max_i32_device(relative_lower_wick_sum_raw + params->epsilon, 1));
		relative_total_wick_sum_raw = relative_upper_wick_sum_raw + relative_lower_wick_sum_raw;
		relative_wick_to_body_ratio = (int32_t)(((int64_t)relative_total_wick_sum_raw * params->norm_scale) /
			(int64_t)max_i32_device(relative_abs_body_sum_raw, params->epsilon));
		relative_wick_bias_aligned = (raw_net > 0) ? relative_wick_bias_up : ((raw_net < 0) ? relative_wick_bias_down : params->norm_scale);
		relative_overlap_ratio = (int32_t)(((int64_t)relative_body_overlap_sum_raw * params->norm_scale) /
			(int64_t)max_i32_device(relative_abs_body_sum_raw, params->epsilon));
		relative_retracement_ratio = (raw_net == 0 || relative_impulse_leg_raw <= 0)
			? params->norm_scale
			: (int32_t)(((int64_t)relative_max_pullback_raw * params->norm_scale) /
				(int64_t)max_i32_device(relative_impulse_leg_raw, params->epsilon));
	}
	trend_quality_penalty =
		((params->norm_scale - min_i32_device(relative_directional_efficiency_step, params->norm_scale)) >> 2) +
		((params->norm_scale - min_i32_device(relative_directional_efficiency_body, params->norm_scale)) >> 3) +
		(relative_retracement_ratio >> 3) +
		(relative_overlap_ratio >> 3) +
		(relative_wick_to_body_ratio >> 3) +
		((params->norm_scale - min_i32_device(relative_avg_body_efficiency, params->norm_scale)) >> 3) +
		(sign_change_density >> 4) +
		(penalty_low_device(relative_wick_bias_aligned, params->norm_scale) >> 3) +
		(penalty_low_device(net_move_norm, max_i32_device(params->norm_scale / 4, 1)) >> 1);
	range_quality_penalty =
		(relative_directional_efficiency_step >> 3) +
		(relative_directional_efficiency_body >> 4) +
		(relative_avg_body_efficiency >> 4) +
		(penalty_low_device(relative_overlap_ratio, params->relative_range_overlap_min) >> 1) +
		(penalty_low_device(sign_change_density, params->relative_range_sign_flip_min) >> 2) +
		(penalty_low_device(relative_retracement_ratio, params->relative_trend_retracement_max) >> 2) +
		(penalty_low_device(relative_wick_to_body_ratio, params->norm_scale / 4) >> 2) +
		(penalty_high_device(net_move_norm, max_i32_device(params->norm_scale / 5, 1)) >> 1);
	directional_purity_body = (int32_t)(((int64_t)trend_body * params->norm_scale) /
		(int64_t)max_i32_device(gross_move_raw, params->epsilon));
	advance_per_candle_norm = net_move_norm / (int32_t)max_i32_device((int32_t)duration, 1);
	move_penalty = penalty_low_device(net_move_raw, params->min_wave_move_pips);
	const int trend_move_too_small = (net_move_raw < params->minimum_pips_for_trend);
	if (first_body_index != candle_count &&
		features[first_body_index].b * canonical_sign < 0) {
		edge_direction_mismatch = 1;
	}
	if (last_body_index != candle_count &&
		features[last_body_index].b * canonical_sign < 0) {
		edge_direction_mismatch = 1;
	}

	strong_trend_error =
		5 * penalty_low_device(primitive_consecutive_ratio, 840) +
		5 * penalty_high_device(primitive_alternating_ratio, 180) +
		4 * penalty_low_device(primitive_max_consecutive_run, 5) +
		8 * penalty_high_device(primitive_counter_body_ratio, 0) +
		3 * penalty_low_device(net_move_ratio, max_i32_device(params->trend_net_min - 40, 360)) +
		3 * penalty_low_device(directional_purity_body, max_i32_device(params->trend_purity_min - 40, 680)) +
		2 * penalty_high_device(sign_change_density, 220);
	weak_trend_error =
		5 * penalty_low_device(primitive_consecutive_ratio, 680) +
		4 * penalty_high_device(primitive_alternating_ratio, 320) +
		3 * penalty_low_device(primitive_max_consecutive_run, 4) +
		6 * penalty_high_device(primitive_counter_body_ratio, 0) +
		3 * penalty_low_device(net_move_ratio, max_i32_device(params->trend_net_min - 120, 280)) +
		3 * penalty_low_device(directional_purity_body, max_i32_device(params->trend_purity_min - 140, 560)) +
		2 * penalty_high_device(sign_change_density, 380);
	retracement_error =
		3 * penalty_low_device(retracement_ratio, max_i32_device(params->retrace_retrace_min, params->retrace_break_threshold)) +
		3 * penalty_high_device(primitive_consecutive_ratio, 460) +
		2 * penalty_high_device(primitive_max_consecutive_run, 3) +
		2 * penalty_band_device(net_move_ratio, params->retrace_net_lo, params->retrace_net_hi);
	alternating_chop_error =
		4 * penalty_low_device(primitive_alternating_ratio, 680) +
		4 * penalty_band_device(primitive_minority_body_ratio, 300, 500) +
		3 * penalty_high_device(net_move_ratio, params->chop_net_max) +
		2 * penalty_high_device(primitive_max_consecutive_run, 2);
	mixed_compression_error =
		3 * penalty_low_device(primitive_alternating_ratio, 420) +
		2 * penalty_band_device(primitive_minority_body_ratio, 300, 500) +
		2 * penalty_high_device(range_growth_ratio, params->compress_growth_max) +
		2 * penalty_band_device(net_move_ratio, params->compress_net_lo, params->compress_net_hi) +
		penalty_low_device((int32_t)duration, params->compression_min_duration);
	volatile_expansion_error =
		3 * penalty_low_device(primitive_alternating_ratio, 420) +
		2 * penalty_band_device(primitive_minority_body_ratio, 300, 500) +
		params->expand_w_growth * penalty_low_device(range_growth_ratio, params->expand_growth_min) +
		params->expand_w_gross * penalty_low_device(gross_range_norm, params->expand_gross_min) +
		penalty_low_device((int32_t)duration, params->expansion_min_duration);
	v_shaped_reversal_error =
		params->v_reversal_w_strength * penalty_low_device(v_strength, params->v_reversal_min_trend_strength) +
		params->v_reversal_w_bridge * penalty_high_device(sign_change_density, params->v_reversal_max_bridge_chop) +
		params->v_reversal_w_duration * penalty_high_device((int32_t)duration, params->v_reversal_max_duration);
	separator_error = 0x3fffffff;
	if (duration == 1u) {
		separator_error =
			120 +
			penalty_low_device(active_body_ratio, 300) +
			penalty_low_device(gross_range_norm, max_i32_device(params->expand_gross_min / 4, 60));
	}

	if (params->wave_model == WAVE_MODEL_PRIMITIVE) {
		for (t = 0u; t < WAVE_CLASS_COUNT; ++t) {
			class_error[t] = 0x3fffffff;
		}
		class_error[PRIMITIVE_CLASS_CONSECUTIVE] =
			(edge_direction_mismatch || trend_move_too_small)
				? 0x3fffffff
				: (min_i32_device(strong_trend_error, weak_trend_error) + move_penalty);
		class_error[PRIMITIVE_CLASS_ALTERNATING] =
			min_i32_device(retracement_error,
			min_i32_device(alternating_chop_error,
			min_i32_device(mixed_compression_error,
			min_i32_device(volatile_expansion_error, v_shaped_reversal_error)))) + move_penalty;
		class_error[PRIMITIVE_CLASS_SEPARATOR] = separator_error;
	} else if (params->wave_model == WAVE_MODEL_RELATIVE ||
		params->wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
		params->wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		params->wave_model == WAVE_MODEL_RELATIVE_ITERATIVE) {
		for (t = 0u; t < WAVE_CLASS_COUNT; ++t) {
			class_error[t] = 0x3fffffff;
		}
		relative_strong_trend_error =
			4 * penalty_low_device(relative_directional_efficiency_step, params->relative_trend_der_step_min + 120) +
			3 * penalty_low_device(relative_directional_efficiency_body, params->relative_trend_der_body_min + 80) +
			4 * penalty_high_device(relative_retracement_ratio, params->relative_strong_trend_retracement_max) +
			3 * penalty_low_device(relative_avg_body_efficiency, params->relative_trend_body_eff_min + 60) +
			3 * penalty_high_device(relative_overlap_ratio, max_i32_device(params->relative_trend_overlap_max - 100, 0)) +
			2 * penalty_high_device(sign_change_density, max_i32_device(params->relative_trend_sign_flip_max - 120, 0)) +
			penalty_low_device(relative_wick_bias_aligned, params->relative_trend_wick_bias_min) +
			trend_quality_penalty;
		relative_weak_trend_error =
			4 * penalty_low_device(relative_directional_efficiency_step, params->relative_trend_der_step_min) +
			3 * penalty_low_device(relative_directional_efficiency_body, params->relative_trend_der_body_min) +
			4 * penalty_high_device(relative_retracement_ratio, params->relative_trend_retracement_max) +
			3 * penalty_low_device(relative_avg_body_efficiency, params->relative_trend_body_eff_min) +
			3 * penalty_high_device(relative_overlap_ratio, params->relative_trend_overlap_max) +
			2 * penalty_high_device(sign_change_density, params->relative_trend_sign_flip_max) +
			penalty_low_device(relative_wick_bias_aligned, params->relative_trend_wick_bias_min) +
			trend_quality_penalty;
		relative_trend_error = min_i32_device(relative_strong_trend_error, relative_weak_trend_error) + move_penalty;
		if (raw_net == 0 || trend_move_too_small) {
			relative_trend_error = 0x3fffffff;
		}
		relative_range_error =
			4 * penalty_high_device(relative_directional_efficiency_step, params->relative_range_der_step_max) +
			3 * penalty_high_device(relative_avg_body_efficiency, params->relative_range_body_eff_max) +
			3 * penalty_low_device(relative_overlap_ratio, params->relative_range_overlap_min) +
			2 * penalty_low_device(sign_change_density, params->relative_range_sign_flip_min) +
			2 * penalty_low_device(relative_retracement_ratio, params->relative_trend_retracement_max) +
			penalty_low_device(params->norm_scale - relative_wick_bias_aligned, 0) +
			range_quality_penalty +
			move_penalty;
		class_error[RELATIVE_CLASS_TREND] = relative_trend_error;
		class_error[RELATIVE_CLASS_RANGE] = relative_range_error;
	} else {
		class_error[WAVE_CLASS_STRONG_TREND] =
			params->trend_w_net * penalty_low_device(net_move_ratio, params->trend_net_min) +
			params->trend_w_purity * penalty_low_device(directional_purity_body, params->trend_purity_min) +
			params->trend_w_retrace * penalty_high_device(retracement_ratio, params->trend_retrace_max) +
			params->trend_w_sign * penalty_high_device(sign_change_density, params->trend_sign_max) +
			params->trend_w_adv * penalty_low_device(advance_per_candle_norm, params->trend_adv_min) +
			4 * penalty_low_device(active_body_ratio, 350);
		class_error[WAVE_CLASS_WEAK_TREND] =
			params->trend_w_net * penalty_low_device(net_move_ratio, max_i32_device(params->trend_net_min - 140, 220)) +
			params->trend_w_purity * penalty_low_device(directional_purity_body, max_i32_device(params->trend_purity_min - 180, 420)) +
			params->trend_w_retrace * penalty_band_device(retracement_ratio, 120, params->trend_retrace_max + 220) +
			params->trend_w_sign * penalty_band_device(sign_change_density, 120, params->trend_sign_max + 260) +
			params->trend_w_adv * penalty_low_device(advance_per_candle_norm, max_i32_device(params->trend_adv_min - 8, 4)) +
			2 * penalty_low_device(active_body_ratio, 220);
		if (trend_move_too_small) {
			class_error[WAVE_CLASS_STRONG_TREND] = 0x3fffffff;
			class_error[WAVE_CLASS_WEAK_TREND] = 0x3fffffff;
		}
		class_error[WAVE_CLASS_RETRACEMENT] =
			params->retrace_w_retrace * penalty_low_device(retracement_ratio, params->retrace_retrace_min) +
			params->retrace_w_net * penalty_band_device(net_move_ratio, params->retrace_net_lo, params->retrace_net_hi) +
			params->retrace_w_sign * penalty_band_device(sign_change_density, params->retrace_sign_lo, params->retrace_sign_hi) +
			params->retrace_w_duration * penalty_high_device((int32_t)duration, params->retrace_duration_max) +
			params->retrace_w_purity * penalty_band_device(directional_purity_body, params->retrace_purity_lo, params->retrace_purity_hi);
		class_error[WAVE_CLASS_ALTERNATING_CHOP] =
			params->chop_w_sign * penalty_low_device(sign_change_density, params->chop_sign_min) +
			params->chop_w_net * penalty_high_device(net_move_ratio, params->chop_net_max) +
			params->chop_w_purity * penalty_high_device(directional_purity_body, params->chop_purity_max) +
			params->chop_w_adv * penalty_high_device(advance_per_candle_norm, params->chop_adv_max);
		class_error[WAVE_CLASS_MIXED_COMPRESSION] =
			params->compress_w_growth * penalty_high_device(range_growth_ratio, params->compress_growth_max) +
			params->compress_w_net * penalty_band_device(net_move_ratio, params->compress_net_lo, params->compress_net_hi) +
			params->compress_w_sign * penalty_band_device(sign_change_density, params->compress_sign_lo, params->compress_sign_hi) +
			penalty_low_device((int32_t)duration, params->compression_min_duration);
		class_error[WAVE_CLASS_VOLATILE_EXPANSION] =
			params->expand_w_growth * penalty_low_device(range_growth_ratio, params->expand_growth_min) +
			params->expand_w_gross * penalty_low_device(gross_range_norm, params->expand_gross_min) +
			penalty_low_device((int32_t)duration, params->expansion_min_duration);
		class_error[WAVE_CLASS_V_SHAPED_REVERSAL] = v_shaped_reversal_error;
		for (t = 0u; t < WAVE_CLASS_COUNT; ++t) {
			class_error[t] += move_penalty;
		}
	}

	class_count_limit = wave_output_class_count_device(params);
	best_fit = class_error[0];
	second_best_error = 0x7fffffff;
	for (t = 1u; t < class_count_limit; ++t) {
		if (class_error[t] < best_fit) {
			second_best_error = best_fit;
			second_best_class = best_class;
			best_fit = class_error[t];
			best_class = t;
		} else if (class_error[t] < second_best_error) {
			second_best_error = class_error[t];
			second_best_class = t;
		}
	}
	if (second_best_error == 0x7fffffff) {
		second_best_error = best_fit;
		second_best_class = best_class;
	}

	out.score = ((dp != nullptr && start > 0u) ? dp[start - 1u] : 0ll) + (int64_t)best_fit + (int64_t)params->segment_penalty_lambda;
	out.start = (int32_t)start;
	out.class_id = best_class;
	out.fit_error = best_fit;
	out.second_best_error = second_best_error;
	out.ambiguity_margin = second_best_error - best_fit;
	out.net_move_raw = net_move_raw;
	out.relative_net_move_raw = raw_net;
	out.retracement_ratio = retracement_ratio;
	out.sign_change_density = sign_change_density;
	out.directional_purity_body = directional_purity_body;
	out.range_growth_ratio = range_growth_ratio;
	out.context_range = context_range;
	out.relative_directional_efficiency_body = relative_directional_efficiency_body;
	out.relative_directional_efficiency_step = relative_directional_efficiency_step;
	out.relative_avg_body_efficiency = relative_avg_body_efficiency;
	out.relative_body_direction_share = (int32_t)(((int64_t)relative_aligned_body_sum * params->norm_scale) /
		(int64_t)max_i32_device(relative_abs_body_sum_raw, params->epsilon));
	out.relative_wick_to_body_ratio = relative_wick_to_body_ratio;
	out.relative_retracement_ratio = relative_retracement_ratio;
	out.relative_overlap_ratio = relative_overlap_ratio;
	out.relative_trend_quality_raw = trend_quality_penalty + move_penalty;
	out.relative_impulse_leg_raw = relative_impulse_leg_raw;
	out.dominant_direction = (raw_net > 0) ? 1 : ((raw_net < 0) ? -1 : 0);
	out.price_amplitude_raw = compute_relative_trend_price_amplitude_device(candles,
		features,
		start,
		end,
		out.dominant_direction);
	out.duration = duration;
	(void)second_best_class;
	return out;
}

__device__ static WaveCandidateBest candidate_worse(void)
{
	WaveCandidateBest out;

	out.score = (int64_t)0x3fffffffffffffffLL;
	out.start = 0;
	out.class_id = 0u;
	out.fit_error = 0;
	out.second_best_error = 0;
	out.ambiguity_margin = 0;
	out.net_move_raw = 0;
	out.relative_net_move_raw = 0;
	out.retracement_ratio = 0;
	out.sign_change_density = 0;
	out.directional_purity_body = 0;
	out.range_growth_ratio = 0;
	out.context_range = 0;
	out.relative_directional_efficiency_body = 0;
	out.relative_directional_efficiency_step = 0;
	out.relative_avg_body_efficiency = 0;
	out.relative_body_direction_share = 0;
	out.relative_wick_to_body_ratio = 0;
	out.relative_retracement_ratio = 0;
	out.relative_overlap_ratio = 0;
	out.relative_trend_quality_raw = 0x3fffffff;
	out.relative_impulse_leg_raw = 0;
	out.price_amplitude_raw = 0;
	out.dominant_direction = 0;
	out.duration = 0u;
	return out;
}

__device__ static int candidate_is_better(const WaveCandidateBest *a, const WaveCandidateBest *b)
{
	if (a->score != b->score) {
		return a->score < b->score;
	}
	if (a->start != b->start) {
		return a->start < b->start;
	}
	return a->class_id < b->class_id;
}

__device__ static int seed_candidate_is_valid(const WaveCandidateBest *cand,
	const CandleFeat *features,
	const WaveParams *params,
	uint32_t start,
	uint32_t end,
	uint32_t seed_pass)
{
	int32_t min_der_step;
	int32_t min_der_body;
	int32_t max_retrace;
	int32_t max_overlap;
	int32_t max_wick_to_body;
	int32_t max_sign_density;
	int32_t min_body_share;
	int32_t min_ambiguity;
	int32_t max_quality;
	int32_t min_duration;
	int32_t min_impulse_leg;
	int32_t min_price_amplitude;

	if (cand == nullptr || features == nullptr || params == nullptr ||
		(params->wave_model != WAVE_MODEL_RELATIVE_SEEDED &&
		 params->wave_model != WAVE_MODEL_RELATIVE_ITERATIVE)) {
		return 0;
	}
	if (seed_pass <= 1u) {
		min_der_step = min_i32_device(params->norm_scale, params->relative_trend_der_step_min + 140);
		min_der_body = min_i32_device(params->norm_scale, params->relative_trend_der_body_min + 120);
		max_retrace = min_i32_device(params->relative_strong_trend_retracement_max, 180);
		max_overlap = min_i32_device(params->relative_trend_overlap_max, 180);
		max_wick_to_body = (params->norm_scale * 3) / 4;
		max_sign_density = 160;
		min_body_share = 820;
		min_ambiguity = 240;
		max_quality = 260;
		min_impulse_leg = max_i32_device(params->minimum_pips_for_trend * 2, 14);
		min_price_amplitude = max_i32_device(params->minimum_pips_for_trend * 3, 20);
		if (params->trend_priority_mode >= 2) {
			min_der_step = min_i32_device(params->norm_scale, min_der_step + 40);
			min_der_body = min_i32_device(params->norm_scale, min_der_body + 40);
			max_retrace = min_i32_device(max_retrace, 140);
			max_overlap = min_i32_device(max_overlap, 140);
			max_wick_to_body = min_i32_device(max_wick_to_body, (params->norm_scale * 2) / 3);
			max_sign_density = 120;
			min_body_share = 860;
			min_ambiguity = 320;
			max_quality = 220;
			min_impulse_leg = max_i32_device(min_impulse_leg, 18);
			min_price_amplitude = max_i32_device(min_price_amplitude, 24);
		}
	} else {
		min_der_step = min_i32_device(params->norm_scale, params->relative_trend_der_step_min + 60);
		min_der_body = min_i32_device(params->norm_scale, params->relative_trend_der_body_min + 50);
		max_retrace = min_i32_device(params->relative_trend_retracement_max, 240);
		max_overlap = min_i32_device(params->relative_trend_overlap_max, 240);
		max_wick_to_body = (params->norm_scale * 9) / 10;
		max_sign_density = 240;
		min_body_share = 740;
		min_ambiguity = 160;
		max_quality = 420;
		min_impulse_leg = params->minimum_pips_for_trend;
		min_price_amplitude = max_i32_device(params->minimum_pips_for_trend + 3, 10);
		if (params->trend_priority_mode >= 2) {
			min_der_step = min_i32_device(params->norm_scale, min_der_step + 30);
			min_der_body = min_i32_device(params->norm_scale, min_der_body + 20);
			max_retrace = min_i32_device(max_retrace, 210);
			max_overlap = min_i32_device(max_overlap, 210);
			max_wick_to_body = min_i32_device(max_wick_to_body, (params->norm_scale * 17) / 20);
			max_sign_density = 200;
			min_body_share = 780;
			min_ambiguity = 220;
			max_quality = 360;
			min_impulse_leg = max_i32_device(min_impulse_leg, 10);
			min_price_amplitude = max_i32_device(min_price_amplitude, 12);
		}
	}
	min_duration = max_i32_device((int32_t)params->lmin_candles, 3);
	return cand->class_id == RELATIVE_CLASS_TREND &&
		cand->duration >= (uint32_t)min_duration &&
		cand->net_move_raw >= params->minimum_pips_for_trend &&
		cand->relative_impulse_leg_raw >= min_impulse_leg &&
		abs_i32_device(cand->relative_net_move_raw) >= min_price_amplitude &&
		cand->dominant_direction != 0 &&
		cand->ambiguity_margin >= min_ambiguity &&
		cand->relative_trend_quality_raw <= max_quality &&
		cand->relative_directional_efficiency_step >= min_der_step &&
		cand->relative_directional_efficiency_body >= min_der_body &&
		cand->relative_retracement_ratio <= max_retrace &&
		cand->relative_overlap_ratio <= max_overlap &&
		cand->relative_wick_to_body_ratio <= max_wick_to_body &&
		cand->sign_change_density <= max_sign_density &&
		cand->relative_body_direction_share >= min_body_share &&
		relative_span_has_consistent_edges_device(features, start, end, cand->dominant_direction);
}

__device__ static int seed_priority_is_better(const WaveCandidateBest *a, const WaveCandidateBest *b)
{
	int32_t a_move;
	int32_t b_move;

	if (a == nullptr || b == nullptr) {
		return 0;
	}
	a_move = (a->price_amplitude_raw > 0) ? a->price_amplitude_raw :
		((a->net_move_raw >= 0) ? a->net_move_raw : -a->net_move_raw);
	b_move = (b->price_amplitude_raw > 0) ? b->price_amplitude_raw :
		((b->net_move_raw >= 0) ? b->net_move_raw : -b->net_move_raw);
	if (a->relative_trend_quality_raw != b->relative_trend_quality_raw) {
		return a->relative_trend_quality_raw < b->relative_trend_quality_raw;
	}
	if (a_move != b_move) {
		return a_move > b_move;
	}
	if (a->relative_retracement_ratio != b->relative_retracement_ratio) {
		return a->relative_retracement_ratio < b->relative_retracement_ratio;
	}
	if (a->relative_overlap_ratio != b->relative_overlap_ratio) {
		return a->relative_overlap_ratio < b->relative_overlap_ratio;
	}
	if (a->relative_wick_to_body_ratio != b->relative_wick_to_body_ratio) {
		return a->relative_wick_to_body_ratio < b->relative_wick_to_body_ratio;
	}
	if (a->duration != b->duration) {
		return a->duration < b->duration;
	}
	if (a->start != b->start) {
		return a->start < b->start;
	}
	return a->fit_error < b->fit_error;
}

__global__ static void wave_dp_step_kernel(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	WaveParams params,
	const int64_t *dp,
	uint32_t end,
	int64_t *out_dp,
	int32_t *out_prev_start,
	uint32_t *out_class_id,
	int32_t *out_fit_error,
	int32_t *out_net_move_raw,
	int32_t *out_retracement_ratio,
	int32_t *out_sign_change_density,
	int32_t *out_directional_purity_body,
	int32_t *out_range_growth_ratio,
	int32_t *out_context_range)
{
	__shared__ WaveCandidateBest shared_best[WAVE_DP_THREADS];
	WaveCandidateBest local_best = candidate_worse();
	uint32_t tid = (uint32_t)threadIdx.x;
	uint32_t max_len = params.lmax_candles;
	uint32_t min_len = params.lmin_candles;
	uint32_t candidate_count = (end + 1u < max_len) ? (end + 1u) : max_len;
	uint32_t len;

	for (len = tid + 1u; len <= candidate_count; len += (uint32_t)blockDim.x) {
		uint32_t start;
		WaveCandidateBest cand;

		if (len < min_len) {
			continue;
		}
		start = end + 1u - len;
		cand = build_candidate(candles, features, candle_count, &params, dp, end, start);
		if (candidate_is_better(&cand, &local_best)) {
			local_best = cand;
		}
	}
	shared_best[tid] = local_best;
	__syncthreads();

	for (uint32_t stride = (uint32_t)blockDim.x / 2u; stride > 0u; stride >>= 1u) {
		if (tid < stride && candidate_is_better(&shared_best[tid + stride], &shared_best[tid])) {
			shared_best[tid] = shared_best[tid + stride];
		}
		__syncthreads();
	}

	if (tid == 0u) {
		WaveCandidateBest best = shared_best[0];

		out_dp[end] = best.score;
		out_prev_start[end] = best.start;
		out_class_id[end] = best.class_id;
		out_fit_error[end] = best.fit_error;
		out_net_move_raw[end] = best.net_move_raw;
		out_retracement_ratio[end] = best.retracement_ratio;
		out_sign_change_density[end] = best.sign_change_density;
		out_directional_purity_body[end] = best.directional_purity_body;
		out_range_growth_ratio[end] = best.range_growth_ratio;
		out_context_range[end] = best.context_range;
	}
}

__global__ static void wave_seed_scan_kernel(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	WaveParams params,
	uint32_t range_start,
	uint32_t range_end,
	uint32_t seed_pass,
	uint32_t *out_best_end,
	int32_t *out_quality_raw,
	int32_t *out_net_move_raw,
	int32_t *out_price_amplitude_raw,
	int32_t *out_retracement_ratio,
	int32_t *out_overlap_ratio,
	int32_t *out_wick_to_body_ratio,
	int32_t *out_strongest_leg_raw,
	uint32_t *out_duration)
{
	__shared__ WaveCandidateBest shared_best[WAVE_DP_THREADS];
	uint32_t local_index = (uint32_t)blockIdx.x;
	uint32_t start = range_start + local_index;
	uint32_t tid = (uint32_t)threadIdx.x;
	WaveCandidateBest local_best = candidate_worse();

	if (start > range_end) {
		return;
	}
	for (uint32_t len = tid + 1u; len <= params.lmax_candles && start + len - 1u <= range_end; len += (uint32_t)blockDim.x) {
		uint32_t end = start + len - 1u;
		WaveCandidateBest cand = build_candidate(candles, features, candle_count, &params, nullptr, end, start);

		if (!seed_candidate_is_valid(&cand, features, &params, start, end, seed_pass)) {
			continue;
		}
		if (!seed_priority_is_better(&cand, &local_best)) {
			continue;
		}
		local_best = cand;
	}
	shared_best[tid] = local_best;
	__syncthreads();

	for (uint32_t stride = (uint32_t)blockDim.x / 2u; stride > 0u; stride >>= 1u) {
		if (tid < stride && seed_priority_is_better(&shared_best[tid + stride], &shared_best[tid])) {
			shared_best[tid] = shared_best[tid + stride];
		}
		__syncthreads();
	}
	if (tid == 0u) {
		WaveCandidateBest best = shared_best[0];

		out_best_end[local_index] = (best.duration > 0u) ? ((uint32_t)best.start + best.duration - 1u) : 0xffffffffu;
		out_quality_raw[local_index] = best.relative_trend_quality_raw;
		out_net_move_raw[local_index] = best.relative_net_move_raw;
		out_price_amplitude_raw[local_index] = best.price_amplitude_raw;
		out_retracement_ratio[local_index] = best.relative_retracement_ratio;
		out_overlap_ratio[local_index] = best.relative_overlap_ratio;
		out_wick_to_body_ratio[local_index] = best.relative_wick_to_body_ratio;
		out_strongest_leg_raw[local_index] = best.relative_impulse_leg_raw;
		out_duration[local_index] = best.duration;
	}
}

extern "C" int hip_wave_dp_impl(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	int32_t *out_prev_start,
	uint32_t *out_class_id,
	int32_t *out_fit_error,
	int32_t *out_net_move_raw,
	int32_t *out_retracement_ratio,
	int32_t *out_sign_change_density,
	int32_t *out_directional_purity_body,
	int32_t *out_range_growth_ratio,
	int32_t *out_context_range,
	int64_t *out_total_fit_error)
{
	Candle *d_candles = nullptr;
	CandleFeat *d_features = nullptr;
	int64_t *d_dp = nullptr;
	int32_t *d_prev_start = nullptr;
	uint32_t *d_class_id = nullptr;
	int32_t *d_fit_error = nullptr;
	int32_t *d_net_move_raw = nullptr;
	int32_t *d_retracement_ratio = nullptr;
	int32_t *d_sign_change_density = nullptr;
	int32_t *d_directional_purity_body = nullptr;
	int32_t *d_range_growth_ratio = nullptr;
	int32_t *d_context_range = nullptr;
	int64_t total_fit_error = 0;
	int rc = -1;
	uint32_t end;

	if (candles == nullptr || features == nullptr || params == nullptr || candle_count == 0u ||
		out_prev_start == nullptr || out_class_id == nullptr || out_fit_error == nullptr ||
		out_net_move_raw == nullptr || out_retracement_ratio == nullptr || out_sign_change_density == nullptr ||
		out_directional_purity_body == nullptr || out_range_growth_ratio == nullptr || out_context_range == nullptr ||
		out_total_fit_error == nullptr) {
		return -1;
	}

	if (hipMalloc((void **)&d_candles, (size_t)candle_count * sizeof(Candle)) != hipSuccess ||
		hipMalloc((void **)&d_features, (size_t)candle_count * sizeof(CandleFeat)) != hipSuccess ||
		hipMalloc((void **)&d_dp, (size_t)candle_count * sizeof(int64_t)) != hipSuccess ||
		hipMalloc((void **)&d_prev_start, (size_t)candle_count * sizeof(int32_t)) != hipSuccess ||
		hipMalloc((void **)&d_class_id, (size_t)candle_count * sizeof(uint32_t)) != hipSuccess ||
		hipMalloc((void **)&d_fit_error, (size_t)candle_count * sizeof(int32_t)) != hipSuccess ||
		hipMalloc((void **)&d_net_move_raw, (size_t)candle_count * sizeof(int32_t)) != hipSuccess ||
		hipMalloc((void **)&d_retracement_ratio, (size_t)candle_count * sizeof(int32_t)) != hipSuccess ||
		hipMalloc((void **)&d_sign_change_density, (size_t)candle_count * sizeof(int32_t)) != hipSuccess ||
		hipMalloc((void **)&d_directional_purity_body, (size_t)candle_count * sizeof(int32_t)) != hipSuccess ||
		hipMalloc((void **)&d_range_growth_ratio, (size_t)candle_count * sizeof(int32_t)) != hipSuccess ||
		hipMalloc((void **)&d_context_range, (size_t)candle_count * sizeof(int32_t)) != hipSuccess) {
		goto cleanup;
	}
	if (hipMemcpy(d_candles, candles, (size_t)candle_count * sizeof(Candle), hipMemcpyHostToDevice) != hipSuccess ||
		hipMemcpy(d_features, features, (size_t)candle_count * sizeof(CandleFeat), hipMemcpyHostToDevice) != hipSuccess) {
		goto cleanup;
	}

	for (end = 0u; end < candle_count; ++end) {
		hipLaunchKernelGGL(wave_dp_step_kernel,
			dim3(1),
			dim3(WAVE_DP_THREADS),
			0,
			0,
			d_candles,
			d_features,
			candle_count,
			*params,
			d_dp,
			end,
			d_dp,
			d_prev_start,
			d_class_id,
			d_fit_error,
			d_net_move_raw,
			d_retracement_ratio,
			d_sign_change_density,
			d_directional_purity_body,
			d_range_growth_ratio,
			d_context_range);
		if (hipGetLastError() != hipSuccess) {
			goto cleanup;
		}
	}
	if (hipDeviceSynchronize() != hipSuccess) {
		goto cleanup;
	}
	if (hipMemcpy(out_prev_start, d_prev_start, (size_t)candle_count * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_class_id, d_class_id, (size_t)candle_count * sizeof(uint32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_fit_error, d_fit_error, (size_t)candle_count * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_net_move_raw, d_net_move_raw, (size_t)candle_count * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_retracement_ratio, d_retracement_ratio, (size_t)candle_count * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_sign_change_density, d_sign_change_density, (size_t)candle_count * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_directional_purity_body, d_directional_purity_body, (size_t)candle_count * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_range_growth_ratio, d_range_growth_ratio, (size_t)candle_count * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_context_range, d_context_range, (size_t)candle_count * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(&total_fit_error, d_dp + (candle_count - 1u), sizeof(int64_t), hipMemcpyDeviceToHost) != hipSuccess) {
		goto cleanup;
	}
	*out_total_fit_error = total_fit_error;
	rc = 0;

cleanup:
	if (d_context_range != nullptr) {
		(void)hipFree(d_context_range);
	}
	if (d_range_growth_ratio != nullptr) {
		(void)hipFree(d_range_growth_ratio);
	}
	if (d_directional_purity_body != nullptr) {
		(void)hipFree(d_directional_purity_body);
	}
	if (d_sign_change_density != nullptr) {
		(void)hipFree(d_sign_change_density);
	}
	if (d_retracement_ratio != nullptr) {
		(void)hipFree(d_retracement_ratio);
	}
	if (d_net_move_raw != nullptr) {
		(void)hipFree(d_net_move_raw);
	}
	if (d_fit_error != nullptr) {
		(void)hipFree(d_fit_error);
	}
	if (d_class_id != nullptr) {
		(void)hipFree(d_class_id);
	}
	if (d_prev_start != nullptr) {
		(void)hipFree(d_prev_start);
	}
	if (d_dp != nullptr) {
		(void)hipFree(d_dp);
	}
	if (d_features != nullptr) {
		(void)hipFree(d_features);
	}
	if (d_candles != nullptr) {
		(void)hipFree(d_candles);
	}
	return rc;
}

extern "C" int hip_wave_seed_scan_impl(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t range_start,
	uint32_t range_end,
	uint32_t seed_pass,
	uint32_t *out_best_end,
	int32_t *out_quality_raw,
	int32_t *out_net_move_raw,
	int32_t *out_price_amplitude_raw,
	int32_t *out_retracement_ratio,
	int32_t *out_overlap_ratio,
	int32_t *out_wick_to_body_ratio,
	int32_t *out_strongest_leg_raw,
	uint32_t *out_duration)
{
	uint32_t range_len;
	int rc = -1;

	if (candles == nullptr || features == nullptr || params == nullptr ||
		out_best_end == nullptr || out_quality_raw == nullptr || out_net_move_raw == nullptr ||
		out_price_amplitude_raw == nullptr || out_retracement_ratio == nullptr || out_overlap_ratio == nullptr ||
		out_wick_to_body_ratio == nullptr || out_strongest_leg_raw == nullptr || out_duration == nullptr ||
		range_end < range_start || range_end >= candle_count) {
		return -1;
	}
	range_len = range_end - range_start + 1u;
	if (g_seed_scan_candle_capacity < candle_count) {
		if (g_seed_scan_candles != nullptr) (void)hipFree(g_seed_scan_candles);
		if (g_seed_scan_features != nullptr) (void)hipFree(g_seed_scan_features);
		g_seed_scan_candles = nullptr;
		g_seed_scan_features = nullptr;
		g_seed_scan_candle_capacity = 0u;
		g_seed_scan_host_candles = nullptr;
		g_seed_scan_host_features = nullptr;
		g_seed_scan_host_candle_count = 0u;
		if (hipMalloc((void **)&g_seed_scan_candles, (size_t)candle_count * sizeof(Candle)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_features, (size_t)candle_count * sizeof(CandleFeat)) != hipSuccess) {
			goto cleanup_seed;
		}
		g_seed_scan_candle_capacity = candle_count;
	}
	if (g_seed_scan_range_capacity < range_len) {
		if (g_seed_scan_best_end != nullptr) (void)hipFree(g_seed_scan_best_end);
		if (g_seed_scan_quality_raw != nullptr) (void)hipFree(g_seed_scan_quality_raw);
		if (g_seed_scan_net_move_raw != nullptr) (void)hipFree(g_seed_scan_net_move_raw);
		if (g_seed_scan_price_amplitude_raw != nullptr) (void)hipFree(g_seed_scan_price_amplitude_raw);
		if (g_seed_scan_retracement_ratio != nullptr) (void)hipFree(g_seed_scan_retracement_ratio);
		if (g_seed_scan_overlap_ratio != nullptr) (void)hipFree(g_seed_scan_overlap_ratio);
		if (g_seed_scan_wick_to_body_ratio != nullptr) (void)hipFree(g_seed_scan_wick_to_body_ratio);
		if (g_seed_scan_strongest_leg_raw != nullptr) (void)hipFree(g_seed_scan_strongest_leg_raw);
		if (g_seed_scan_duration != nullptr) (void)hipFree(g_seed_scan_duration);
		g_seed_scan_best_end = nullptr;
		g_seed_scan_quality_raw = nullptr;
		g_seed_scan_net_move_raw = nullptr;
		g_seed_scan_price_amplitude_raw = nullptr;
		g_seed_scan_retracement_ratio = nullptr;
		g_seed_scan_overlap_ratio = nullptr;
		g_seed_scan_wick_to_body_ratio = nullptr;
		g_seed_scan_strongest_leg_raw = nullptr;
		g_seed_scan_duration = nullptr;
		g_seed_scan_range_capacity = 0u;
		if (hipMalloc((void **)&g_seed_scan_best_end, (size_t)range_len * sizeof(uint32_t)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_quality_raw, (size_t)range_len * sizeof(int32_t)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_net_move_raw, (size_t)range_len * sizeof(int32_t)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_price_amplitude_raw, (size_t)range_len * sizeof(int32_t)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_retracement_ratio, (size_t)range_len * sizeof(int32_t)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_overlap_ratio, (size_t)range_len * sizeof(int32_t)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_wick_to_body_ratio, (size_t)range_len * sizeof(int32_t)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_strongest_leg_raw, (size_t)range_len * sizeof(int32_t)) != hipSuccess ||
			hipMalloc((void **)&g_seed_scan_duration, (size_t)range_len * sizeof(uint32_t)) != hipSuccess) {
			goto cleanup_seed;
		}
		g_seed_scan_range_capacity = range_len;
	}
	if (g_seed_scan_host_candles != candles || g_seed_scan_host_features != features || g_seed_scan_host_candle_count != candle_count) {
		if (hipMemcpy(g_seed_scan_candles, candles, (size_t)candle_count * sizeof(Candle), hipMemcpyHostToDevice) != hipSuccess ||
			hipMemcpy(g_seed_scan_features, features, (size_t)candle_count * sizeof(CandleFeat), hipMemcpyHostToDevice) != hipSuccess) {
			goto cleanup_seed;
		}
		g_seed_scan_host_candles = candles;
		g_seed_scan_host_features = features;
		g_seed_scan_host_candle_count = candle_count;
	}
	hipLaunchKernelGGL(wave_seed_scan_kernel,
		dim3(range_len),
		dim3(WAVE_DP_THREADS),
		0,
		0,
		g_seed_scan_candles,
		g_seed_scan_features,
		candle_count,
		*params,
		range_start,
		range_end,
		seed_pass,
		g_seed_scan_best_end,
		g_seed_scan_quality_raw,
		g_seed_scan_net_move_raw,
		g_seed_scan_price_amplitude_raw,
		g_seed_scan_retracement_ratio,
		g_seed_scan_overlap_ratio,
		g_seed_scan_wick_to_body_ratio,
		g_seed_scan_strongest_leg_raw,
		g_seed_scan_duration);
	if (hipGetLastError() != hipSuccess || hipDeviceSynchronize() != hipSuccess) {
		goto cleanup_seed;
	}
	if (hipMemcpy(out_best_end, g_seed_scan_best_end, (size_t)range_len * sizeof(uint32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_quality_raw, g_seed_scan_quality_raw, (size_t)range_len * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_net_move_raw, g_seed_scan_net_move_raw, (size_t)range_len * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_price_amplitude_raw, g_seed_scan_price_amplitude_raw, (size_t)range_len * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_retracement_ratio, g_seed_scan_retracement_ratio, (size_t)range_len * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_overlap_ratio, g_seed_scan_overlap_ratio, (size_t)range_len * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_wick_to_body_ratio, g_seed_scan_wick_to_body_ratio, (size_t)range_len * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_strongest_leg_raw, g_seed_scan_strongest_leg_raw, (size_t)range_len * sizeof(int32_t), hipMemcpyDeviceToHost) != hipSuccess ||
		hipMemcpy(out_duration, g_seed_scan_duration, (size_t)range_len * sizeof(uint32_t), hipMemcpyDeviceToHost) != hipSuccess) {
		goto cleanup_seed;
	}
	rc = 0;

cleanup_seed:
	return rc;
}
