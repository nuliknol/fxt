#include "wave_dp.h"
#include "core_api.h"
#include "gpu_dispatch.h"
#include "internal_store.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define WAVE_DEBUG_SEEDED_PASS1_ONLY 0
#define WAVE_DEBUG_SEEDED_PASS1_GROWN_ONLY 0
#define WAVE_DEBUG_SEEDED_DISABLE_PASS2 1

typedef struct {
	uint32_t start;
	uint32_t end;
	uint32_t duration;
	int32_t class_error[WAVE_CLASS_COUNT];
	uint32_t best_class;
	uint32_t second_best_class;
	int32_t best_fit;
	int32_t second_best_error;
	int32_t ambiguity_margin;
	int32_t net_move_raw;
	int32_t net_move_ratio;
	int32_t retracement_ratio;
	int32_t sign_change_density;
	int32_t active_body_ratio;
	int32_t directional_purity_body;
	int32_t advance_per_candle_norm;
	int32_t range_growth_ratio;
	int32_t context_range;
	int32_t primitive_consecutive_ratio;
	int32_t primitive_alternating_ratio;
	int32_t primitive_max_consecutive_run;
	int32_t primitive_max_alternating_run;
	int32_t primitive_alternating_pair_count;
	int32_t primitive_counter_body_ratio;
	int32_t primitive_minority_body_ratio;
	int32_t relative_net_move_raw;
	int32_t relative_abs_body_sum_raw;
	int32_t relative_abs_step_sum_raw;
	int32_t relative_directional_efficiency_body;
	int32_t relative_directional_efficiency_step;
	int32_t relative_avg_body_efficiency;
	int32_t relative_body_direction_share;
	int32_t relative_upper_wick_sum_raw;
	int32_t relative_lower_wick_sum_raw;
	int32_t relative_total_wick_sum_raw;
	int32_t relative_wick_to_body_ratio;
	int32_t relative_wick_bias_up;
	int32_t relative_wick_bias_down;
	int32_t relative_wick_bias_aligned;
	int32_t relative_max_pullback_raw;
	int32_t relative_impulse_leg_raw;
	int32_t relative_retracement_ratio;
	int32_t relative_body_overlap_sum_raw;
	int32_t relative_overlap_ratio;
	int32_t relative_range_growth_ratio;
	int32_t relative_body_growth_ratio;
	int32_t relative_trend_quality_raw;
	int32_t relative_trend_quality_bucket;
	int32_t dominant_direction;
	int32_t v_min_side_strength;
	int32_t v_bridge_chop;
} WaveSegmentEval;

typedef struct {
	uint32_t count;
	uint32_t sequential_count;
	uint32_t alternating_count;
	uint32_t separator_count;
	uint32_t sequential_candles;
	uint32_t alternating_candles;
	uint32_t separator_candles;
	uint32_t sequential_bull_candles;
	uint32_t sequential_bear_candles;
	uint32_t alternating_bull_candles;
	uint32_t alternating_bear_candles;
	int32_t sequential_net_move_sum_raw;
	int32_t alternating_net_move_sum_raw;
	int32_t separator_net_move_sum_raw;
	int32_t max_sequential_net_move_raw;
	int32_t max_alternating_net_move_raw;
	int32_t max_child_net_move_raw;
	int32_t sequential_bull_net_move_sum_raw;
	int32_t sequential_bear_net_move_sum_raw;
	int32_t alternating_bull_net_move_sum_raw;
	int32_t alternating_bear_net_move_sum_raw;
	int32_t directional_agreement;
	int32_t sequential_ratio;
	int32_t alternating_ratio;
} LayeredChildSummary;

typedef struct {
	uint32_t start;
	uint32_t end;
	int sign;
} LayeredSequentialCandidate;

typedef struct {
	uint32_t start;
	uint32_t end;
	uint32_t signed_count;
	int sign;
} LayeredPrimitiveSpan;

static uint64_t monotonic_ms(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0) {
		return 0u;
	}
	return (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
}

static int candle_body_sign_raw(const Candle *candles, uint32_t index)
{
	if (candles[index].close_pips > candles[index].open_pips) {
		return 1;
	}
	if (candles[index].close_pips < candles[index].open_pips) {
		return -1;
	}
	return 0;
}

static void log_wave_stage_iter(uint32_t dataset_id,
	const char *stage,
	uint32_t iter,
	uint32_t token_count,
	uint64_t stage_start_ms)
{
	trainer_log_info("wave inference progress dataset_id=%u stage=%s iter=%u tokens=%u elapsed_ms=%llu",
		dataset_id,
		stage != NULL ? stage : "unknown",
		iter,
		token_count,
		(unsigned long long)(monotonic_ms() - stage_start_ms));
}

static void log_wave_stage_done(uint32_t dataset_id,
	const char *stage,
	uint32_t iter,
	uint32_t token_count,
	uint64_t stage_start_ms)
{
	trainer_log_info("wave inference stage done dataset_id=%u stage=%s iter=%u tokens=%u elapsed_ms=%llu",
		dataset_id,
		stage != NULL ? stage : "unknown",
		iter,
		token_count,
		(unsigned long long)(monotonic_ms() - stage_start_ms));
}

typedef struct {
	uint32_t start;
	uint32_t end;
	uint32_t seed_start;
	uint32_t seed_end;
	uint32_t seed_pass;
	uint32_t class_id;
	int32_t quality_raw;
	int32_t net_move_raw;
	int32_t price_amplitude_raw;
	int32_t retracement_ratio;
	int32_t overlap_ratio;
	int32_t wick_to_body_ratio;
	int32_t seed_leg_raw;
	int32_t strongest_leg_raw;
	uint32_t duration;
} TrendLockSpan;

static int effective_body_sign(const Candle *candles, const CandleFeat *features, uint32_t index);
static uint32_t classify_split_span(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t start,
	uint32_t end);
static int primitive_span_is_strict_consecutive(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t start,
	uint32_t end,
	int *out_sign);
static int layered_span_is_pure_sequential(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int *out_sign);
static int layered_span_is_strict_alternating(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int *out_first_sign,
	uint32_t *out_pair_count,
	uint32_t *out_signed_count);
static int layered_span_can_be_sequential_string(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int *out_sign);
static int layered_span_can_be_alternating_string(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	uint32_t *out_pair_count);
static int collapse_layered_layer1_singletons(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int build_primitive_rule_first_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count);
static int build_layered_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count,
	WaveToken **out_layer1_tokens,
	uint32_t *out_layer1_token_count);
static int optimize_layered_token_boundaries(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t token_count);
static void reclassify_layered_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t token_count);
static int split_layered_edge_outliers(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int split_layered_internal_outliers(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int split_layered_internal_range_islands(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int split_layered_cross_token_range_islands(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static void remove_token_at(WaveToken *tokens, uint32_t *count, uint32_t index);
static int merge_same_direction_layered_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int merge_adjacent_layered_ranges(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int token_is_layered_separator_only(const WaveToken *tok);
static int extract_leading_layered_trend_into_following_range(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int extract_layered_dominant_trend_blocks(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int trim_layered_unbridged_alternating_edges_from_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int extract_layered_suffix_trend_from_mixed_trend(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count);
static int32_t compute_relative_trend_price_amplitude(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction,
	uint32_t *out_start,
	uint32_t *out_end);
static int is_seed_relative_trend_for_pass(const WaveSegmentEval *eval, const WaveParams *params, uint32_t seed_pass);
static int detect_seed_priority_spans(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t range_start,
	uint32_t range_end,
	uint32_t seed_pass,
	TrendLockSpan *locks,
	uint32_t *out_lock_count);
static int grow_seeded_trend_locks(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t allowed_start,
	uint32_t allowed_end,
	TrendLockSpan *locks,
	uint32_t lock_count);
static void insert_token_at(WaveToken *tokens, uint32_t *count, uint32_t index, const WaveToken *token);
static void force_token_class_span(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *token,
	uint32_t class_id);
static int split_relative_edge_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int split_primitive_mixed_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int split_primitive_internal_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int split_primitive_edge_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int merge_separator_into_alternating(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int merge_separator_into_consecutive(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int collapse_separator_runs(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static void coalesce_maximal_primitive_spans(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int bridge_neutral_primitive_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int extend_consecutive_with_adjacent_edges(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int resolve_relative_same_direction_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	WaveKPI *kpi);
static int absorb_adjacent_one_candle_same_direction_ranges(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static void normalize_relative_trend_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int split_relative_ranges_by_edge_trim(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static void enforce_relative_two_stage_trend_edges(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t count);
static int merge_relative_trend_across_small_noise(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int extend_relative_trend_boundaries(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t count);
static int demote_relative_micro_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int reduce_relative_choppy_runs(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count);
static int relative_class_id_is_valid(uint32_t class_id);
static int is_relative_wave_model(uint32_t wave_model);
static int32_t abs_i32(int32_t v);
static int32_t max_i32(int32_t a, int32_t b);
static uint32_t max_u32(uint32_t a, uint32_t b);
static void compute_segment_eval(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t start,
	uint32_t end,
	WaveSegmentEval *out);
static void mark_token_reduction(WaveToken *token, uint32_t reduction_flag);
static void merge_token_range(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	uint32_t first,
	uint32_t last,
	uint32_t new_class_id);

static int compare_trend_lock_span(const void *lhs, const void *rhs)
{
	const TrendLockSpan *a = (const TrendLockSpan *)lhs;
	const TrendLockSpan *b = (const TrendLockSpan *)rhs;

	if (a->start < b->start) {
		return -1;
	}
	if (a->start > b->start) {
		return 1;
	}
	if (a->end < b->end) {
		return -1;
	}
	if (a->end > b->end) {
		return 1;
	}
	return 0;
}

static int token_is_shallow_chop_candidate(const WaveToken *tok, const WaveParams *params)
{
	int32_t shallow_move_limit;
	int32_t context_scaled_limit;
	int32_t shallow_impulse_limit;

	if (tok == NULL || params == NULL) {
		return 0;
	}
	if (!relative_class_id_is_valid(tok->class_id)) {
		return 0;
	}
	context_scaled_limit = (tok->context_range > 0) ? ((tok->context_range * 180) / params->norm_scale) : 0;
	shallow_move_limit = max_i32(max_i32(params->minimum_pips_for_trend * 8, 40), context_scaled_limit);
	if (abs_i32(tok->relative_net_move_raw) > shallow_move_limit) {
		return 0;
	}
	if (tok->duration_candles > 4u) {
		return 0;
	}
	if (tok->class_id == RELATIVE_CLASS_TREND) {
		if (tok->seed_pass == 1u) {
			return 0;
		}
		shallow_impulse_limit = max_i32(max_i32(params->minimum_pips_for_trend * 20,
			(context_scaled_limit * 3) / 2), 160);
		if (tok->relative_trend_quality_raw <= 320 &&
			tok->relative_impulse_leg_raw > shallow_impulse_limit) {
			return 0;
		}
		if (tok->relative_impulse_leg_raw > shallow_impulse_limit) {
			return 0;
		}
	}
	return 1;
}

static int token_is_micro_trend_candidate(const WaveToken *tok, const WaveParams *params)
{
	int32_t move_limit;
	int32_t quality_limit;

	if (tok == NULL || params == NULL || tok->class_id != RELATIVE_CLASS_TREND) {
		return 0;
	}
	if (tok->seed_pass == 1u) {
		return 0;
	}
	if (tok->duration_candles > 2u) {
		return 0;
	}
	move_limit = max_i32((tok->context_range * 220) / params->norm_scale,
		max_i32(params->minimum_pips_for_trend * 12, 90));
	quality_limit = 520;
	if (abs_i32(tok->relative_net_move_raw) > move_limit) {
		return 0;
	}
	if (tok->relative_trend_quality_raw < quality_limit && tok->relative_impulse_leg_raw > max_i32(params->minimum_pips_for_trend * 6, 40)) {
		return 0;
	}
	return 1;
}

static int relative_eval_should_stay_trend_despite_chop(const WaveSegmentEval *eval, const WaveParams *params)
{
	int32_t directional_move_floor;
	int32_t efficiency_step_floor;
	int32_t efficiency_body_floor;

	if (eval == NULL || params == NULL) {
		return 0;
	}
	directional_move_floor = max_i32((eval->context_range * 110) / params->norm_scale,
		max_i32(params->minimum_pips_for_trend * 10, 120));
	efficiency_step_floor = max_i32(params->relative_trend_der_step_min - 120,
		params->norm_scale / 3);
	efficiency_body_floor = max_i32(params->relative_trend_der_body_min - 120,
		params->norm_scale / 3);
	if (eval->dominant_direction == 0) {
		return 0;
	}
	if (abs_i32(eval->relative_net_move_raw) < directional_move_floor) {
		return 0;
	}
	if (eval->relative_directional_efficiency_step < efficiency_step_floor) {
		return 0;
	}
	if (eval->relative_directional_efficiency_body < efficiency_body_floor) {
		return 0;
	}
	if (eval->relative_retracement_ratio >
		(((params->relative_trend_retracement_max + 50) < 280) ?
			(params->relative_trend_retracement_max + 50) : 280)) {
		return 0;
	}
	if (eval->relative_overlap_ratio > params->relative_trend_overlap_max + 40) {
		return 0;
	}
	return 1;
}

static int demote_relative_micro_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	if (!is_relative_wave_model((uint32_t)params->wave_model)) {
		return 0;
	}
	for (i = 0u; i < *count; ++i) {
		uint32_t window_start;
		uint32_t window_end;
		uint32_t left_index;
		uint32_t right_index;
		WaveSegmentEval eval;
		int has_left;
		int has_right;
		int32_t drift_limit;
		int force_chop_demote = 0;

		if (!token_is_micro_trend_candidate(&tokens[i], params)) {
			continue;
		}
		has_left = (i > 0u &&
			tokens[i - 1u].end_candle + 1u == tokens[i].start_candle &&
			token_is_shallow_chop_candidate(&tokens[i - 1u], params));
		has_right = (i + 1u < *count &&
			tokens[i].end_candle + 1u == tokens[i + 1u].start_candle &&
			token_is_shallow_chop_candidate(&tokens[i + 1u], params));
		if (!has_left && !has_right) {
			continue;
		}
		left_index = has_left ? (i - 1u) : i;
		right_index = has_right ? (i + 1u) : i;
		window_start = tokens[left_index].start_candle;
		window_end = tokens[right_index].end_candle;
		compute_segment_eval(candles, features, candle_count, params, window_start, window_end, &eval);
		drift_limit = max_i32((eval.context_range * 200) / params->norm_scale,
			max_i32(params->minimum_pips_for_trend * 10, 70));
		if (has_left && has_right) {
			const WaveToken *left = &tokens[i - 1u];
			const WaveToken *mid = &tokens[i];
			const WaveToken *right = &tokens[i + 1u];
			int neighbors_shallow;
			int opposite_flanks;
			int32_t local_abs_sum;

			neighbors_shallow =
				token_is_shallow_chop_candidate(left, params) &&
				token_is_shallow_chop_candidate(right, params);
			opposite_flanks =
				left->dominant_direction != 0 &&
				right->dominant_direction != 0 &&
				left->dominant_direction == right->dominant_direction &&
				mid->dominant_direction != 0 &&
				mid->dominant_direction != left->dominant_direction;
			local_abs_sum = abs_i32(left->relative_net_move_raw) +
				abs_i32(mid->relative_net_move_raw) +
				abs_i32(right->relative_net_move_raw);
			if (neighbors_shallow &&
				opposite_flanks &&
				mid->duration_candles <= 2u &&
				local_abs_sum <= max_i32((eval.context_range * 450) / params->norm_scale,
					max_i32(params->minimum_pips_for_trend * 24, 180)) &&
				abs_i32(eval.relative_net_move_raw) <= drift_limit) {
				force_chop_demote = 1;
			}
		}
		if (force_chop_demote) {
			force_token_class_span(candles, features, candle_count, params, &tokens[i], RELATIVE_CLASS_RANGE);
			mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL);
			return 1;
		}
		if (abs_i32(eval.relative_net_move_raw) > drift_limit &&
			eval.class_error[RELATIVE_CLASS_RANGE] > eval.class_error[RELATIVE_CLASS_TREND] + params->segment_penalty_lambda) {
			continue;
		}
		force_token_class_span(candles, features, candle_count, params, &tokens[i], RELATIVE_CLASS_RANGE);
		mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL);
		return 1;
	}
	return 0;
}

#if !WAVE_DEBUG_SEEDED_DISABLE_PASS2
static int recover_directional_relative_ranges_as_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	if (!is_relative_wave_model((uint32_t)params->wave_model)) {
		return 0;
	}
	for (i = 0u; i < *count; ++i) {
		WaveSegmentEval eval;
		int32_t directional_move_floor;
		int32_t efficiency_step_floor;
		int32_t efficiency_body_floor;

		if (tokens[i].class_id != RELATIVE_CLASS_RANGE) {
			continue;
		}
		compute_segment_eval(candles,
			features,
			candle_count,
			params,
			tokens[i].start_candle,
			tokens[i].end_candle,
			&eval);
		if (!relative_eval_should_stay_trend_despite_chop(&eval, params)) {
			continue;
		}
		directional_move_floor = max_i32((eval.context_range * 110) / params->norm_scale,
			max_i32(params->minimum_pips_for_trend * 10, 120));
		efficiency_step_floor = max_i32(params->relative_trend_der_step_min - 120,
			params->norm_scale / 3);
		efficiency_body_floor = max_i32(params->relative_trend_der_body_min - 120,
			params->norm_scale / 3);
		if (abs_i32(eval.relative_net_move_raw) < directional_move_floor ||
			eval.relative_directional_efficiency_step < efficiency_step_floor ||
			eval.relative_directional_efficiency_body < efficiency_body_floor) {
			continue;
		}
		force_token_class_span(candles, features, candle_count, params, &tokens[i], RELATIVE_CLASS_TREND);
		mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_RULE_FIRST);
		return 1;
	}
	return 0;
}
#endif

static int reduce_relative_choppy_runs(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	if (!is_relative_wave_model((uint32_t)params->wave_model)) {
		return 0;
	}
	for (i = 0u; i < *count; ++i) {
		uint32_t best_end = i;
		int found_best = 0;
		int32_t best_score = 0x7fffffff;

		if (!token_is_shallow_chop_candidate(&tokens[i], params)) {
			continue;
		}
		if (i + 1u < *count &&
			tokens[i].end_candle + 1u == tokens[i + 1u].start_candle &&
			token_is_shallow_chop_candidate(&tokens[i + 1u], params) &&
			(tokens[i].class_id == RELATIVE_CLASS_RANGE || tokens[i + 1u].class_id == RELATIVE_CLASS_RANGE) &&
			tokens[i].duration_candles + tokens[i + 1u].duration_candles <= 8u) {
			WaveSegmentEval pair_eval;
			int32_t pair_drift_limit;

			compute_segment_eval(candles,
				features,
				candle_count,
				params,
				tokens[i].start_candle,
				tokens[i + 1u].end_candle,
				&pair_eval);
			if (relative_eval_should_stay_trend_despite_chop(&pair_eval, params)) {
				continue;
			}
			pair_drift_limit = max_i32((pair_eval.context_range * 260) / params->norm_scale,
				max_i32(params->minimum_pips_for_trend * 16, 140));
			if (pair_eval.best_class == RELATIVE_CLASS_RANGE ||
				pair_eval.class_error[RELATIVE_CLASS_RANGE] <= pair_eval.class_error[RELATIVE_CLASS_TREND] + params->segment_penalty_lambda ||
				abs_i32(pair_eval.relative_net_move_raw) <= pair_drift_limit) {
				merge_token_range(candles,
					features,
					candle_count,
					params,
					tokens,
					count,
					i,
					i + 1u,
					RELATIVE_CLASS_RANGE);
				mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_COALESCE);
				return 1;
			}
		}
		if (i + 1u < *count &&
			tokens[i].end_candle + 1u == tokens[i + 1u].start_candle &&
			token_is_shallow_chop_candidate(&tokens[i + 1u], params) &&
			tokens[i].class_id == RELATIVE_CLASS_TREND &&
			tokens[i + 1u].class_id == RELATIVE_CLASS_TREND &&
			tokens[i].dominant_direction != 0 &&
			tokens[i + 1u].dominant_direction != 0 &&
			tokens[i].dominant_direction != tokens[i + 1u].dominant_direction) {
			if (tokens[i].duration_candles + tokens[i + 1u].duration_candles <= 6u) {
				merge_token_range(candles,
					features,
					candle_count,
					params,
					tokens,
					count,
					i,
					i + 1u,
					RELATIVE_CLASS_RANGE);
				mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_COALESCE);
				return 1;
			}
		}
		{
			uint32_t run_end;

			for (run_end = i + 1u; run_end < *count && run_end <= i + 3u; ++run_end) {
				uint32_t j;
				uint32_t trend_count = 0u;
				WaveSegmentEval merged_eval;
				int32_t score;

				if (tokens[run_end - 1u].end_candle + 1u != tokens[run_end].start_candle ||
					!token_is_shallow_chop_candidate(&tokens[run_end], params)) {
					break;
				}
				for (j = i; j <= run_end; ++j) {
					if (tokens[j].class_id == RELATIVE_CLASS_TREND) {
						trend_count += 1u;
					}
				}
				if (trend_count == 0u) {
					continue;
				}
				compute_segment_eval(candles,
					features,
					candle_count,
					params,
					tokens[i].start_candle,
					tokens[run_end].end_candle,
					&merged_eval);
				if (relative_eval_should_stay_trend_despite_chop(&merged_eval, params)) {
					continue;
				}
				if (merged_eval.best_class != RELATIVE_CLASS_RANGE) {
					int32_t drift_limit = max_i32((merged_eval.context_range * 180) / params->norm_scale,
						max_i32(params->minimum_pips_for_trend * 10, 60));
					if (merged_eval.class_error[RELATIVE_CLASS_RANGE] > merged_eval.class_error[RELATIVE_CLASS_TREND] + params->segment_penalty_lambda &&
						abs_i32(merged_eval.relative_net_move_raw) > drift_limit) {
						continue;
					}
				}
				score = merged_eval.class_error[RELATIVE_CLASS_RANGE];
				if (!found_best || score < best_score) {
					best_score = score;
					best_end = run_end;
					found_best = 1;
				}
			}
		}
		if (!found_best) {
			continue;
		}
		merge_token_range(candles,
			features,
			candle_count,
			params,
			tokens,
			count,
			i,
			best_end,
			RELATIVE_CLASS_RANGE);
		mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_COALESCE);
		return 1;
	}
	return 0;
}

static int compare_trend_lock_priority(const void *lhs, const void *rhs)
{
	const TrendLockSpan *a = (const TrendLockSpan *)lhs;
	const TrendLockSpan *b = (const TrendLockSpan *)rhs;
	int32_t a_move = (a->price_amplitude_raw > 0) ? a->price_amplitude_raw :
		((a->net_move_raw >= 0) ? a->net_move_raw : -a->net_move_raw);
	int32_t b_move = (b->price_amplitude_raw > 0) ? b->price_amplitude_raw :
		((b->net_move_raw >= 0) ? b->net_move_raw : -b->net_move_raw);

	if (a->quality_raw != b->quality_raw) {
		return (a->quality_raw < b->quality_raw) ? -1 : 1;
	}
	if (a_move != b_move) {
		return (a_move > b_move) ? -1 : 1;
	}
	if (a->retracement_ratio != b->retracement_ratio) {
		return (a->retracement_ratio < b->retracement_ratio) ? -1 : 1;
	}
	if (a->overlap_ratio != b->overlap_ratio) {
		return (a->overlap_ratio < b->overlap_ratio) ? -1 : 1;
	}
	if (a->wick_to_body_ratio != b->wick_to_body_ratio) {
		return (a->wick_to_body_ratio < b->wick_to_body_ratio) ? -1 : 1;
	}
	if (a->duration != b->duration) {
		return (a->duration < b->duration) ? -1 : 1;
	}
	return compare_trend_lock_span(lhs, rhs);
}

static int compare_seeded_growth_candidate(const TrendLockSpan *a, const TrendLockSpan *b)
{
	int32_t a_move;
	int32_t b_move;

	if (a == NULL || b == NULL) {
		return 0;
	}
	a_move = (a->price_amplitude_raw > 0) ? a->price_amplitude_raw :
		((a->net_move_raw >= 0) ? a->net_move_raw : -a->net_move_raw);
	b_move = (b->price_amplitude_raw > 0) ? b->price_amplitude_raw :
		((b->net_move_raw >= 0) ? b->net_move_raw : -b->net_move_raw);
	if (a_move != b_move) {
		return (a_move > b_move) ? -1 : 1;
	}
	if (a->quality_raw != b->quality_raw) {
		return (a->quality_raw < b->quality_raw) ? -1 : 1;
	}
	if (a->retracement_ratio != b->retracement_ratio) {
		return (a->retracement_ratio < b->retracement_ratio) ? -1 : 1;
	}
	if (a->overlap_ratio != b->overlap_ratio) {
		return (a->overlap_ratio < b->overlap_ratio) ? -1 : 1;
	}
	if (a->wick_to_body_ratio != b->wick_to_body_ratio) {
		return (a->wick_to_body_ratio < b->wick_to_body_ratio) ? -1 : 1;
	}
	if (a->duration != b->duration) {
		return (a->duration > b->duration) ? -1 : 1;
	}
	return compare_trend_lock_span(a, b);
}

static int relative_block_has_short_counter_run_then_resume(const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction,
	int counter_run_on_left)
{
	uint32_t counter_count = 0u;
	uint32_t i;

	if (features == NULL || start > end || dominant_direction == 0) {
		return 0;
	}
	if (counter_run_on_left) {
		for (i = start; i <= end; ++i) {
			if (features[i].b * dominant_direction < 0) {
				counter_count += 1u;
				if (counter_count > 2u) {
					return 0;
				}
				continue;
			}
			break;
		}
		if (counter_count == 0u || i > end) {
			return 0;
		}
		for (; i <= end; ++i) {
			if (features[i].b * dominant_direction <= 0) {
				return 0;
			}
		}
		return 1;
	}
	for (i = end + 1u; i > start; ) {
		uint32_t idx = i - 1u;

		if (features[idx].b * dominant_direction < 0) {
			counter_count += 1u;
			if (counter_count > 2u) {
				return 0;
			}
			i -= 1u;
			continue;
		}
		break;
	}
	if (counter_count == 0u || i <= start) {
		return 0;
	}
	for (i = start; i < (end + 1u - counter_count); ++i) {
		if (features[i].b * dominant_direction <= 0) {
			return 0;
		}
	}
	return 1;
}

static int compare_wave_token_start(const void *lhs, const void *rhs)
{
	const WaveToken *a = (const WaveToken *)lhs;
	const WaveToken *b = (const WaveToken *)rhs;

	if (a->start_candle < b->start_candle) {
		return -1;
	}
	if (a->start_candle > b->start_candle) {
		return 1;
	}
	if (a->end_candle < b->end_candle) {
		return -1;
	}
	if (a->end_candle > b->end_candle) {
		return 1;
	}
	return 0;
}

static int validate_token_sequence(const WaveToken *tokens, uint32_t token_count, uint32_t candle_count)
{
	uint32_t i;

	if (tokens == NULL) {
		return -1;
	}
	for (i = 0; i < token_count; ++i) {
		if (tokens[i].start_candle > tokens[i].end_candle || tokens[i].end_candle >= candle_count) {
			return -1;
		}
		if (i > 0u && tokens[i - 1u].end_candle >= tokens[i].start_candle) {
			return -1;
		}
	}
	return 0;
}

static void log_invalid_token_sequence(const char *tag, const WaveToken *tokens, uint32_t token_count, uint32_t candle_count)
{
	uint32_t i;

	if (tokens == NULL) {
		trainer_log_error("%s invalid token sequence: tokens=NULL", tag != NULL ? tag : "wave");
		return;
	}
	for (i = 0; i < token_count; ++i) {
		if (tokens[i].start_candle > tokens[i].end_candle || tokens[i].end_candle >= candle_count) {
			trainer_log_error("%s invalid token[%u] start=%u end=%u candle_count=%u class=%u seed_pass=%u",
				tag != NULL ? tag : "wave",
				i,
				tokens[i].start_candle,
				tokens[i].end_candle,
				candle_count,
				tokens[i].class_id,
				tokens[i].seed_pass);
			return;
		}
		if (i > 0u && tokens[i - 1u].end_candle >= tokens[i].start_candle) {
			trainer_log_error("%s overlap token[%u] prev=[%u,%u] curr=[%u,%u] prev_class=%u curr_class=%u prev_seed_pass=%u curr_seed_pass=%u",
				tag != NULL ? tag : "wave",
				i,
				tokens[i - 1u].start_candle,
				tokens[i - 1u].end_candle,
				tokens[i].start_candle,
				tokens[i].end_candle,
				tokens[i - 1u].class_id,
				tokens[i].class_id,
				tokens[i - 1u].seed_pass,
				tokens[i].seed_pass);
			return;
		}
	}
	trainer_log_error("%s invalid token sequence: unknown failure token_count=%u candle_count=%u",
		tag != NULL ? tag : "wave",
		token_count,
		candle_count);
}

static uint32_t wave_output_class_count(const WaveParams *params)
{
	if (params != NULL &&
		(params->wave_model == WAVE_MODEL_RELATIVE ||
		 params->wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
		 params->wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		 params->wave_model == WAVE_MODEL_RELATIVE_ITERATIVE ||
		 params->wave_model == WAVE_MODEL_LAYERED)) {
		return RELATIVE_CLASS_COUNT;
	}
	if (params != NULL && params->wave_model == WAVE_MODEL_PRIMITIVE) {
		return PRIMITIVE_CLASS_COUNT;
	}
	return WAVE_CLASS_COUNT;
}

static int primitive_class_id_is_valid(uint32_t class_id)
{
	return class_id < PRIMITIVE_CLASS_COUNT;
}

static int relative_class_id_is_valid(uint32_t class_id)
{
	return class_id < RELATIVE_CLASS_COUNT;
}

static int is_relative_wave_model(uint32_t wave_model)
{
	return wave_model == WAVE_MODEL_RELATIVE ||
		wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
		wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		wave_model == WAVE_MODEL_RELATIVE_ITERATIVE;
}

static int is_relative_class_wave_model(uint32_t wave_model)
{
	return is_relative_wave_model(wave_model) || wave_model == WAVE_MODEL_LAYERED;
}

static int is_relative_seeded_family(uint32_t wave_model)
{
	return wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		wave_model == WAVE_MODEL_RELATIVE_ITERATIVE;
}

static int32_t relative_trend_quality_bucket_from_raw(int32_t raw)
{
	if (raw <= 320) {
		return 0;
	}
	if (raw <= 416) {
		return 1;
	}
	if (raw <= 512) {
		return 2;
	}
	if (raw <= 608) {
		return 3;
	}
	if (raw <= 704) {
		return 4;
	}
	if (raw <= 800) {
		return 5;
	}
	if (raw <= 896) {
		return 6;
	}
	return 7;
}

static void set_err(char *err, size_t err_sz, const char *msg)
{
	if (err != NULL && err_sz > 0) {
		snprintf(err, err_sz, "%s", msg);
	}
	if (msg != NULL && msg[0] != '\0') {
		trainer_log_error("wave inference error: %s", msg);
	}
}

static int32_t abs_i32(int32_t v)
{
	return (v >= 0) ? v : -v;
}

static int sign_i32(int32_t v)
{
	if (v > 0) {
		return 1;
	}
	if (v < 0) {
		return -1;
	}
	return 0;
}

static int32_t max_i32(int32_t a, int32_t b)
{
	return (a > b) ? a : b;
}

static uint32_t max_u32(uint32_t a, uint32_t b)
{
	return (a > b) ? a : b;
}

static int32_t min_i32(int32_t a, int32_t b)
{
	return (a < b) ? a : b;
}

static int32_t penalty_low(int32_t x, int32_t a)
{
	return (x < a) ? (a - x) : 0;
}

static int32_t penalty_high(int32_t x, int32_t b)
{
	return (x > b) ? (x - b) : 0;
}

static int32_t penalty_band(int32_t x, int32_t a, int32_t b)
{
	if (x < a) {
		return a - x;
	}
	if (x > b) {
		return x - b;
	}
	return 0;
}

static int close_step_direction(const Candle *candles, uint32_t prev, uint32_t next)
{
	int32_t delta;

	if (candles == NULL || next <= prev) {
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

static int is_inside_candle(const Candle *candles, uint32_t index)
{
	if (candles == NULL || index == 0u) {
		return 0;
	}
	return candles[index].high_pips <= candles[index - 1u].high_pips &&
		candles[index].low_pips >= candles[index - 1u].low_pips;
}

static int is_tolerated_counter_trend_candle(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	uint32_t index,
	int32_t canonical_sign)
{
	uint32_t next_index;

	if (candles == NULL || features == NULL || index <= start || index >= end) {
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


static WaveParams default_wave_params(void)
{
	WaveParams p;

	memset(&p, 0, sizeof(p));
	p.wave_model = WAVE_MODEL_LEGACY;
	p.lmin_candles = 1;
	p.lmax_candles = 60;
	p.regime_context_window = 512;
	p.regime_context_mode = WAVE_CONTEXT_TRAILING;
	p.min_wave_move_pips = 0;
	p.minimum_pips_for_trend = 7;
	p.norm_scale = 1000;
	p.context_range_floor = 1;
	p.epsilon = 1;
	p.segment_penalty_lambda = 220;
	p.trend_priority_mode = 1;
	p.retrace_break_threshold = 200;
	p.trend_split_gain_min = 80;
	p.compression_min_duration = 4;
	p.expansion_min_duration = 4;
	p.v_reversal_min_trend_strength = 420;
	p.v_reversal_max_bridge_chop = 520;
	p.v_reversal_max_duration = 12;
	p.weak_fit_threshold = 140;
	p.ambiguity_low_margin_threshold = 40;
	p.boundary_low_conf_threshold = 30;

	p.trend_net_min = 520;
	p.trend_purity_min = 720;
	p.trend_retrace_max = 380;
	p.trend_sign_max = 300;
	p.trend_adv_min = 18;
	p.trend_w_net = 4;
	p.trend_w_purity = 4;
	p.trend_w_retrace = 3;
	p.trend_w_sign = 3;
	p.trend_w_adv = 2;

	p.retrace_retrace_min = 380;
	p.retrace_net_lo = 120;
	p.retrace_net_hi = 720;
	p.retrace_sign_lo = 150;
	p.retrace_sign_hi = 720;
	p.retrace_duration_max = 16;
	p.retrace_purity_lo = 250;
	p.retrace_purity_hi = 900;
	p.retrace_w_retrace = 4;
	p.retrace_w_net = 2;
	p.retrace_w_sign = 2;
	p.retrace_w_duration = 1;
	p.retrace_w_purity = 2;

	p.chop_sign_min = 650;
	p.chop_net_max = 280;
	p.chop_purity_max = 520;
	p.chop_adv_max = 16;
	p.chop_w_sign = 5;
	p.chop_w_net = 3;
	p.chop_w_purity = 3;
	p.chop_w_adv = 2;

	p.compress_growth_max = 900;
	p.compress_net_lo = 120;
	p.compress_net_hi = 620;
	p.compress_sign_lo = 250;
	p.compress_sign_hi = 760;
	p.compress_w_growth = 4;
	p.compress_w_net = 2;
	p.compress_w_sign = 2;

	p.expand_growth_min = 1100;
	p.expand_gross_min = 950;
	p.expand_w_growth = 4;
	p.expand_w_gross = 3;

	p.v_reversal_w_strength = 4;
	p.v_reversal_w_bridge = 3;
	p.v_reversal_w_duration = 2;

	p.transition_penalty_retracement_edge = 120;
	p.transition_penalty_retracement_context = 120;
	p.transition_penalty_retracement_direction = 100;
	p.transition_penalty_same_dir_trend_split = 80;
	p.transition_penalty_opp_dir_trend_no_v = 60;
	p.transition_penalty_retracement_chain = 120;
	p.relative_trend_retracement_max = 300;
	p.relative_strong_trend_retracement_max = 180;
	p.relative_trend_der_step_min = 550;
	p.relative_trend_der_body_min = 500;
	p.relative_trend_body_eff_min = 450;
	p.relative_trend_overlap_max = 350;
	p.relative_trend_sign_flip_max = 450;
	p.relative_trend_wick_bias_min = 1050;
	p.relative_range_der_step_max = 500;
	p.relative_range_body_eff_max = 500;
	p.relative_range_overlap_min = 350;
	p.relative_range_sign_flip_min = 400;
	return p;
}

static void merge_wave_params(WaveParams *dst, const WaveParams *src)
{
	if (dst == NULL || src == NULL) {
		return;
	}
#define MERGE_POSITIVE(field) if (src->field > 0) { dst->field = src->field; }
	dst->wave_model = src->wave_model;
	MERGE_POSITIVE(lmin_candles);
	MERGE_POSITIVE(lmax_candles);
	MERGE_POSITIVE(regime_context_window);
	dst->regime_context_mode = src->regime_context_mode;
	dst->min_wave_move_pips = src->min_wave_move_pips;
	MERGE_POSITIVE(minimum_pips_for_trend);
	MERGE_POSITIVE(norm_scale);
	MERGE_POSITIVE(context_range_floor);
	MERGE_POSITIVE(epsilon);
	MERGE_POSITIVE(segment_penalty_lambda);
	dst->trend_priority_mode = src->trend_priority_mode;
	MERGE_POSITIVE(retrace_break_threshold);
	MERGE_POSITIVE(trend_split_gain_min);
	MERGE_POSITIVE(compression_min_duration);
	MERGE_POSITIVE(expansion_min_duration);
	MERGE_POSITIVE(v_reversal_min_trend_strength);
	MERGE_POSITIVE(v_reversal_max_bridge_chop);
	MERGE_POSITIVE(v_reversal_max_duration);
	MERGE_POSITIVE(weak_fit_threshold);
	MERGE_POSITIVE(ambiguity_low_margin_threshold);
	MERGE_POSITIVE(boundary_low_conf_threshold);
	MERGE_POSITIVE(trend_net_min);
	MERGE_POSITIVE(trend_purity_min);
	MERGE_POSITIVE(trend_retrace_max);
	MERGE_POSITIVE(trend_sign_max);
	MERGE_POSITIVE(trend_adv_min);
	MERGE_POSITIVE(trend_w_net);
	MERGE_POSITIVE(trend_w_purity);
	MERGE_POSITIVE(trend_w_retrace);
	MERGE_POSITIVE(trend_w_sign);
	MERGE_POSITIVE(trend_w_adv);
	MERGE_POSITIVE(retrace_retrace_min);
	MERGE_POSITIVE(retrace_net_lo);
	MERGE_POSITIVE(retrace_net_hi);
	MERGE_POSITIVE(retrace_sign_lo);
	MERGE_POSITIVE(retrace_sign_hi);
	MERGE_POSITIVE(retrace_duration_max);
	MERGE_POSITIVE(retrace_purity_lo);
	MERGE_POSITIVE(retrace_purity_hi);
	MERGE_POSITIVE(retrace_w_retrace);
	MERGE_POSITIVE(retrace_w_net);
	MERGE_POSITIVE(retrace_w_sign);
	MERGE_POSITIVE(retrace_w_duration);
	MERGE_POSITIVE(retrace_w_purity);
	MERGE_POSITIVE(chop_sign_min);
	MERGE_POSITIVE(chop_net_max);
	MERGE_POSITIVE(chop_purity_max);
	MERGE_POSITIVE(chop_adv_max);
	MERGE_POSITIVE(chop_w_sign);
	MERGE_POSITIVE(chop_w_net);
	MERGE_POSITIVE(chop_w_purity);
	MERGE_POSITIVE(chop_w_adv);
	MERGE_POSITIVE(compress_growth_max);
	MERGE_POSITIVE(compress_net_lo);
	MERGE_POSITIVE(compress_net_hi);
	MERGE_POSITIVE(compress_sign_lo);
	MERGE_POSITIVE(compress_sign_hi);
	MERGE_POSITIVE(compress_w_growth);
	MERGE_POSITIVE(compress_w_net);
	MERGE_POSITIVE(compress_w_sign);
	MERGE_POSITIVE(expand_growth_min);
	MERGE_POSITIVE(expand_gross_min);
	MERGE_POSITIVE(expand_w_growth);
	MERGE_POSITIVE(expand_w_gross);
	MERGE_POSITIVE(v_reversal_w_strength);
	MERGE_POSITIVE(v_reversal_w_bridge);
	MERGE_POSITIVE(v_reversal_w_duration);
	MERGE_POSITIVE(transition_penalty_retracement_edge);
	MERGE_POSITIVE(transition_penalty_retracement_context);
	MERGE_POSITIVE(transition_penalty_retracement_direction);
	MERGE_POSITIVE(transition_penalty_same_dir_trend_split);
	MERGE_POSITIVE(transition_penalty_opp_dir_trend_no_v);
	MERGE_POSITIVE(transition_penalty_retracement_chain);
	MERGE_POSITIVE(relative_trend_retracement_max);
	MERGE_POSITIVE(relative_strong_trend_retracement_max);
	MERGE_POSITIVE(relative_trend_der_step_min);
	MERGE_POSITIVE(relative_trend_der_body_min);
	MERGE_POSITIVE(relative_trend_body_eff_min);
	MERGE_POSITIVE(relative_trend_overlap_max);
	MERGE_POSITIVE(relative_trend_sign_flip_max);
	MERGE_POSITIVE(relative_trend_wick_bias_min);
	MERGE_POSITIVE(relative_range_der_step_max);
	MERGE_POSITIVE(relative_range_body_eff_max);
	MERGE_POSITIVE(relative_range_overlap_min);
	MERGE_POSITIVE(relative_range_sign_flip_min);
#undef MERGE_POSITIVE
}

static int validate_params(WaveParams *params, uint32_t candle_count)
{
	if (params->lmin_candles == 0) {
		params->lmin_candles = 1;
	}
	if (params->lmax_candles < params->lmin_candles) {
		params->lmax_candles = params->lmin_candles;
	}
	if (params->lmax_candles > candle_count) {
		params->lmax_candles = candle_count;
	}
	if (params->regime_context_window < params->lmax_candles) {
		params->regime_context_window = params->lmax_candles;
	}
	params->regime_context_mode = WAVE_CONTEXT_TRAILING;
	if (params->wave_model != WAVE_MODEL_LEGACY &&
		params->wave_model != WAVE_MODEL_PRIMITIVE &&
		params->wave_model != WAVE_MODEL_RELATIVE &&
		params->wave_model != WAVE_MODEL_RELATIVE_2_STAGE &&
		params->wave_model != WAVE_MODEL_RELATIVE_SEEDED &&
		params->wave_model != WAVE_MODEL_RELATIVE_ITERATIVE &&
		params->wave_model != WAVE_MODEL_LAYERED) {
		params->wave_model = WAVE_MODEL_LEGACY;
	}
	if (params->norm_scale <= 0) {
		params->norm_scale = 1000;
	}
	if (params->minimum_pips_for_trend <= 0) {
		params->minimum_pips_for_trend = 7;
	}
	if (params->context_range_floor <= 0) {
		params->context_range_floor = 1;
	}
	if (params->epsilon <= 0) {
		params->epsilon = 1;
	}
	if (params->compression_min_duration <= 0) {
		params->compression_min_duration = 4;
	}
	if (params->expansion_min_duration <= 0) {
		params->expansion_min_duration = 4;
	}
	if (params->trend_priority_mode < 0 || params->trend_priority_mode > 2) {
		params->trend_priority_mode = 1;
	}
	if (params->relative_strong_trend_retracement_max <= 0) {
		params->relative_strong_trend_retracement_max = 180;
	}
	if (params->relative_trend_retracement_max <= 0) {
		params->relative_trend_retracement_max = 300;
	}
	if (params->relative_strong_trend_retracement_max > params->relative_trend_retracement_max) {
		params->relative_strong_trend_retracement_max = params->relative_trend_retracement_max;
	}
	if (params->relative_trend_der_step_min <= 0) {
		params->relative_trend_der_step_min = 550;
	}
	if (params->relative_trend_der_body_min <= 0) {
		params->relative_trend_der_body_min = 500;
	}
	if (params->relative_trend_body_eff_min <= 0) {
		params->relative_trend_body_eff_min = 450;
	}
	if (params->relative_trend_overlap_max <= 0) {
		params->relative_trend_overlap_max = 350;
	}
	if (params->relative_trend_sign_flip_max <= 0) {
		params->relative_trend_sign_flip_max = 450;
	}
	if (params->relative_trend_wick_bias_min <= 0) {
		params->relative_trend_wick_bias_min = 1050;
	}
	if (params->relative_range_der_step_max <= 0) {
		params->relative_range_der_step_max = 500;
	}
	if (params->relative_range_body_eff_max <= 0) {
		params->relative_range_body_eff_max = 500;
	}
	if (params->relative_range_overlap_min <= 0) {
		params->relative_range_overlap_min = 350;
	}
	if (params->relative_range_sign_flip_min <= 0) {
		params->relative_range_sign_flip_min = 400;
	}
	return (params->lmin_candles <= params->lmax_candles && params->lmax_candles > 0) ? 0 : -1;
}

static int ensure_features(uint32_t dataset_id, DatasetRecord **out_rec)
{
	DatasetRecord *rec = store_find_dataset(dataset_id);

	if (rec == NULL) {
		return -1;
	}
	if (rec->features == NULL && core_build_candle_features(dataset_id) != 0) {
		return -2;
	}
	if (out_rec != NULL) {
		*out_rec = rec;
	}
	return 0;
}

static void compute_context_bounds(uint32_t candle_count,
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
	{
		uint32_t ctx_end = end;
		uint32_t ctx_start = (ctx_len > end + 1u) ? 0u : (end + 1u - ctx_len);

		(void)start;
		(void)candle_count;
		*out_ctx_start = ctx_start;
		*out_ctx_end = ctx_end;
	}
}

static int32_t fallback_class_for_eval(const WaveSegmentEval *eval, int disallow_retracement)
{
	uint32_t c;
	int32_t best_class = WAVE_CLASS_STRONG_TREND;
	int32_t best_err = 0x7fffffff;

	if (eval == NULL) {
		return WAVE_CLASS_STRONG_TREND;
	}
	for (c = 0; c < WAVE_CLASS_COUNT; ++c) {
		if (disallow_retracement && c == WAVE_CLASS_RETRACEMENT) {
			continue;
		}
		if (eval->class_error[c] < best_err) {
			best_err = eval->class_error[c];
			best_class = (int32_t)c;
		}
	}
	return best_class;
}

static uint32_t fallback_non_priority_class_for_eval(const WaveSegmentEval *eval)
{
	uint32_t c;
	uint32_t best_class = WAVE_CLASS_VOLATILE_EXPANSION;
	int32_t best_err = 0x7fffffff;

	if (eval == NULL) {
		return WAVE_CLASS_VOLATILE_EXPANSION;
	}
	for (c = 0; c < WAVE_CLASS_COUNT; ++c) {
		if (c == WAVE_CLASS_STRONG_TREND || c == WAVE_CLASS_V_SHAPED_REVERSAL) {
			continue;
		}
		if (eval->class_error[c] < best_err) {
			best_err = eval->class_error[c];
			best_class = c;
		}
	}
	return best_class;
}

static uint32_t primitive_class_from_eval(const WaveSegmentEval *eval)
{
	if (eval == NULL || !primitive_class_id_is_valid(eval->best_class)) {
		return PRIMITIVE_CLASS_ALTERNATING;
	}
	return eval->best_class;
}

static void compute_segment_eval(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t start,
	uint32_t end,
	WaveSegmentEval *out)
{
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
	int32_t prev_body_sign = 0;
	int32_t first_half_sum = 0;
	int32_t second_half_sum = 0;
	uint32_t first_half_len = duration / 2u;
	uint32_t second_half_len;
	int32_t first_half_body_sum = 0;
	int32_t second_half_body_sum = 0;
	int32_t range_growth_ratio;
	int32_t relative_body_growth_ratio;
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
	int32_t primitive_alternating_pair_count = 0;
	int32_t primitive_counter_body_ratio;
	int32_t primitive_minority_body_ratio;
	int32_t relative_directional_efficiency_body;
	int32_t relative_directional_efficiency_step;
	int32_t relative_avg_body_efficiency;
	int32_t relative_body_direction_share;
	int32_t relative_wick_bias_up;
	int32_t relative_wick_bias_down;
	int32_t relative_wick_bias_aligned;
	int32_t relative_total_wick_sum_raw;
	int32_t relative_wick_to_body_ratio;
	int32_t relative_overlap_ratio;
	int32_t relative_retracement_ratio;
	int32_t relative_trend_quality_raw;
	int32_t relative_trend_quality_bucket;
	int32_t trend_quality_penalty;
	int32_t range_quality_penalty;
	int32_t move_penalty;
	int32_t v_strength = 0;
	int32_t relative_strong_trend_error;
	int32_t relative_weak_trend_error;
	int32_t relative_trend_error;
	int32_t relative_range_error;
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
	uint32_t first_body_index = candle_count;
	uint32_t last_body_index = candle_count;
	int32_t relative_running_extreme = candles[start].close_pips;
	uint32_t c;

	memset(out, 0, sizeof(*out));
	out->start = start;
	out->end = end;
	out->duration = duration;

	compute_context_bounds(candle_count, params, start, end, &ctx_start, &ctx_end);
	ctx_high = candles[ctx_start].high_pips;
	ctx_low = candles[ctx_start].low_pips;
	for (c = ctx_start + 1u; c <= ctx_end; ++c) {
		if (candles[c].high_pips > ctx_high) {
			ctx_high = candles[c].high_pips;
		}
		if (candles[c].low_pips < ctx_low) {
			ctx_low = candles[c].low_pips;
		}
	}
	context_range = ctx_high - ctx_low;
	context_denom = max_i32(context_range, params->context_range_floor);
	net_move_raw = abs_i32(raw_net);
	if (first_half_len == 0u) {
		first_half_len = 1u;
	}
	second_half_len = duration - first_half_len;
	if (second_half_len == 0u) {
		second_half_len = 1u;
	}
	for (c = start; c <= end; ++c) {
		int32_t canonical_body = features[c].b * canonical_sign;
		int32_t body_abs = abs_i32(features[c].b);
		int32_t range_abs = max_i32(features[c].r, 0);
		int32_t upper_wick = candles[c].high_pips - max_i32(candles[c].open_pips, candles[c].close_pips);
		int32_t lower_wick = min_i32(candles[c].open_pips, candles[c].close_pips) - candles[c].low_pips;
		int tolerated_counter = 0;
		int32_t sign = 0;
		int ignore_inside = is_inside_candle(candles, c);
		int ignore_doji = (features[c].b == 0);
		uint32_t offset = c - start;

		if (tolerated_counter_index == candle_count &&
			is_tolerated_counter_trend_candle(candles, features, start, end, c, canonical_sign)) {
			tolerated_counter = 1;
			tolerated_counter_index = c;
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
		if (canonical_body != 0 || tolerated_counter) {
			active_body_count += 1;
		}
		if (features[c].b != 0) {
			if (first_body_index == candle_count) {
				first_body_index = c;
			}
			last_body_index = c;
		}
		if (!ignore_inside && !ignore_doji && canonical_body != 0) {
			if (first_effective_index == candle_count) {
				first_effective_index = c;
			}
		}
		if (!ignore_inside && !ignore_doji && sign != 0 && last_sign != 0 && sign != last_sign) {
			sign_changes += 1;
		}
		if (!ignore_inside && !ignore_doji && sign != 0) {
			last_sign = sign;
		}
		if (features[c].b != 0) {
			int32_t body_sign = (features[c].b > 0) ? 1 : -1;

			if (prev_body_sign != 0 && body_sign != prev_body_sign) {
				primitive_alternating_pair_count += 1;
			}
			prev_body_sign = body_sign;
		}
		gross_move_raw += abs_i32(canonical_body);
		gross_range_raw += features[c].r;
		relative_abs_body_sum_raw += body_abs;
		relative_body_efficiency_sum += (int32_t)(((int64_t)body_abs * params->norm_scale) /
			(int64_t)max_i32(range_abs, params->epsilon));
		relative_upper_wick_sum_raw += upper_wick;
		relative_lower_wick_sum_raw += lower_wick;
		if (offset < first_half_len) {
			first_half_sum += features[c].r;
			first_half_body_sum += body_abs;
		} else {
			second_half_sum += features[c].r;
			second_half_body_sum += body_abs;
		}
		if (c > start) {
			int32_t step_abs = abs_i32(candles[c].close_pips - candles[c - 1u].close_pips);
			int32_t prev_body_lo = min_i32(candles[c - 1u].open_pips, candles[c - 1u].close_pips);
			int32_t prev_body_hi = max_i32(candles[c - 1u].open_pips, candles[c - 1u].close_pips);
			int32_t body_lo = min_i32(candles[c].open_pips, candles[c].close_pips);
			int32_t body_hi = max_i32(candles[c].open_pips, candles[c].close_pips);
			int32_t overlap = min_i32(body_hi, prev_body_hi) - max_i32(body_lo, prev_body_lo);

			relative_abs_step_sum_raw += step_abs;
			if (overlap > 0) {
				relative_body_overlap_sum_raw += overlap;
			}
		}
		if (raw_net > 0) {
			if (candles[c].close_pips > relative_running_extreme) {
				relative_running_extreme = candles[c].close_pips;
			}
			if (relative_running_extreme - candles[c].close_pips > relative_max_pullback_raw) {
				relative_max_pullback_raw = relative_running_extreme - candles[c].close_pips;
			}
			if (relative_running_extreme - candles[start].close_pips > relative_impulse_leg_raw) {
				relative_impulse_leg_raw = relative_running_extreme - candles[start].close_pips;
			}
		} else if (raw_net < 0) {
			if (candles[c].close_pips < relative_running_extreme) {
				relative_running_extreme = candles[c].close_pips;
			}
			if (candles[c].close_pips - relative_running_extreme > relative_max_pullback_raw) {
				relative_max_pullback_raw = candles[c].close_pips - relative_running_extreme;
			}
			if (candles[start].close_pips - relative_running_extreme > relative_impulse_leg_raw) {
				relative_impulse_leg_raw = candles[start].close_pips - relative_running_extreme;
			}
		}
	}
	for (c = start; c < end; ++c) {
		int32_t left_raw = candles[c].close_pips - candles[start].open_pips;
		int32_t right_raw = candles[end].close_pips - candles[c + 1u].open_pips;

		if ((left_raw > 0 && right_raw < 0) || (left_raw < 0 && right_raw > 0)) {
			int32_t left_strength = (int32_t)(((int64_t)abs_i32(left_raw) * params->norm_scale) / (int64_t)context_denom);
			int32_t right_strength = (int32_t)(((int64_t)abs_i32(right_raw) * params->norm_scale) / (int64_t)context_denom);
			int32_t min_strength = min_i32(left_strength, right_strength);

			if (min_strength > v_strength) {
				v_strength = min_strength;
			}
		}
	}
	for (c = start + 1u; c <= end; ++c) {
		int32_t step_dir = close_step_direction(candles, c - 1u, c);

		if (c == tolerated_counter_index) {
			continue;
		}
		/* Doji candles stay available for higher-level range/volatility scoring, but they are neutral for primitive sequential runs. */
		if (features[c].b == 0) {
			continue;
		}
		if (is_inside_candle(candles, c)) {
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
			(int64_t)max_i32(first_half_sum / (int32_t)first_half_len, params->epsilon));
	relative_body_growth_ratio = (duration < 2u)
		? params->norm_scale
		: (int32_t)(((int64_t)(second_half_body_sum / (int32_t)second_half_len) * params->norm_scale) /
			(int64_t)max_i32(first_half_body_sum / (int32_t)first_half_len, params->epsilon));
	net_move_norm = (int32_t)(((int64_t)net_move_raw * params->norm_scale) / (int64_t)context_denom);
	gross_range_norm = (int32_t)(((int64_t)gross_range_raw * params->norm_scale) / (int64_t)context_denom);
	net_move_ratio = (int32_t)(((int64_t)net_move_raw * params->norm_scale) /
		(int64_t)max_i32(gross_range_raw, params->epsilon));
	retracement_ratio = (int32_t)(((int64_t)retrace_body * params->norm_scale) /
		(int64_t)max_i32(trend_body, params->epsilon));
	sign_change_density = (int32_t)(((int64_t)sign_changes * params->norm_scale) /
		(int64_t)max_i32((int32_t)duration - 1, 1));
	active_body_ratio = (int32_t)(((int64_t)active_body_count * params->norm_scale) /
		(int64_t)max_i32((int32_t)duration, 1));
	primitive_consecutive_ratio = (int32_t)(((int64_t)step_consecutive_count * params->norm_scale) /
		(int64_t)max_i32(step_nonzero_count, 1));
	primitive_alternating_ratio = (int32_t)(((int64_t)step_alternating_count * params->norm_scale) /
		(int64_t)max_i32(step_nonzero_count, 1));
	primitive_max_consecutive_run = step_max_same_run + ((step_max_same_run > 0) ? 1 : 0);
	primitive_max_alternating_run = step_max_alt_run + ((step_max_alt_run > 0) ? 1 : 0);
	primitive_counter_body_ratio = (int32_t)(((int64_t)counter_body_count * params->norm_scale) /
		(int64_t)max_i32(active_body_count, 1));
	primitive_minority_body_ratio = (int32_t)(((int64_t)min_i32(trend_body_count, counter_body_count) * params->norm_scale) /
		(int64_t)max_i32(active_body_count, 1));
	relative_directional_efficiency_body = (int32_t)(((int64_t)net_move_raw * params->norm_scale) /
		(int64_t)max_i32(relative_abs_body_sum_raw, params->epsilon));
	relative_directional_efficiency_step = (int32_t)(((int64_t)net_move_raw * params->norm_scale) /
		(int64_t)max_i32(relative_abs_step_sum_raw, params->epsilon));
	relative_avg_body_efficiency = relative_body_efficiency_sum / max_i32((int32_t)duration, 1);
	relative_body_direction_share = (int32_t)(((int64_t)relative_aligned_body_sum * params->norm_scale) /
		(int64_t)max_i32(relative_aligned_body_sum + relative_counter_body_sum, params->epsilon));
	relative_wick_bias_up = (int32_t)(((int64_t)(relative_lower_wick_sum_raw + params->epsilon) * params->norm_scale) /
		(int64_t)max_i32(relative_upper_wick_sum_raw + params->epsilon, 1));
	relative_wick_bias_down = (int32_t)(((int64_t)(relative_upper_wick_sum_raw + params->epsilon) * params->norm_scale) /
		(int64_t)max_i32(relative_lower_wick_sum_raw + params->epsilon, 1));
	relative_total_wick_sum_raw = relative_upper_wick_sum_raw + relative_lower_wick_sum_raw;
	relative_wick_to_body_ratio = (int32_t)(((int64_t)relative_total_wick_sum_raw * params->norm_scale) /
		(int64_t)max_i32(relative_abs_body_sum_raw, params->epsilon));
	relative_wick_bias_aligned = (raw_net > 0) ? relative_wick_bias_up : ((raw_net < 0) ? relative_wick_bias_down : params->norm_scale);
	relative_overlap_ratio = (int32_t)(((int64_t)relative_body_overlap_sum_raw * params->norm_scale) /
		(int64_t)max_i32(relative_abs_body_sum_raw, params->epsilon));
	relative_retracement_ratio = (raw_net == 0 || relative_impulse_leg_raw <= 0)
		? params->norm_scale
		: (int32_t)(((int64_t)relative_max_pullback_raw * params->norm_scale) /
			(int64_t)max_i32(relative_impulse_leg_raw, params->epsilon));
	move_penalty = penalty_low(net_move_raw, params->min_wave_move_pips);
	const int trend_move_too_small = (net_move_raw < params->minimum_pips_for_trend);
	trend_quality_penalty =
		((params->norm_scale - min_i32(relative_directional_efficiency_step, params->norm_scale)) >> 2) +
		((params->norm_scale - min_i32(relative_directional_efficiency_body, params->norm_scale)) >> 3) +
		(relative_retracement_ratio >> 3) +
		(relative_overlap_ratio >> 3) +
		(relative_wick_to_body_ratio >> 3) +
		((params->norm_scale - min_i32(relative_avg_body_efficiency, params->norm_scale)) >> 3) +
		(sign_change_density >> 4) +
		(penalty_low(relative_wick_bias_aligned, params->norm_scale) >> 3) +
		(penalty_low(net_move_norm, max_i32(params->norm_scale / 4, 1)) >> 1);
	relative_trend_quality_raw = trend_quality_penalty + move_penalty;
	if (raw_net == 0) {
		relative_trend_quality_raw = 0x3ff;
	}
	relative_trend_quality_bucket = relative_trend_quality_bucket_from_raw(relative_trend_quality_raw);
	range_quality_penalty =
		(relative_directional_efficiency_step >> 3) +
		(relative_directional_efficiency_body >> 4) +
		(relative_avg_body_efficiency >> 4) +
		(penalty_low(relative_overlap_ratio, params->relative_range_overlap_min) >> 1) +
		(penalty_low(sign_change_density, params->relative_range_sign_flip_min) >> 2) +
		(penalty_low(relative_retracement_ratio, params->relative_trend_retracement_max) >> 2) +
		(penalty_low(relative_wick_to_body_ratio, params->norm_scale / 4) >> 2) +
		(penalty_high(net_move_norm, max_i32(params->norm_scale / 5, 1)) >> 1);
	directional_purity_body = (int32_t)(((int64_t)trend_body * params->norm_scale) /
		(int64_t)max_i32(gross_move_raw, params->epsilon));
	advance_per_candle_norm = net_move_norm / max_i32((int32_t)duration, 1);
	if (first_body_index != candle_count &&
		features[first_body_index].b * canonical_sign < 0) {
		edge_direction_mismatch = 1;
	}
	if (last_body_index != candle_count &&
		features[last_body_index].b * canonical_sign < 0) {
		edge_direction_mismatch = 1;
	}

	strong_trend_error =
		5 * penalty_low(primitive_consecutive_ratio, 840) +
		5 * penalty_high(primitive_alternating_ratio, 180) +
		4 * penalty_low(primitive_max_consecutive_run, 5) +
		8 * penalty_high(primitive_counter_body_ratio, 0) +
		3 * penalty_low(net_move_ratio, max_i32(params->trend_net_min - 40, 360)) +
		3 * penalty_low(directional_purity_body, max_i32(params->trend_purity_min - 40, 680)) +
		2 * penalty_high(sign_change_density, 220);
	weak_trend_error =
		5 * penalty_low(primitive_consecutive_ratio, 680) +
		4 * penalty_high(primitive_alternating_ratio, 320) +
		3 * penalty_low(primitive_max_consecutive_run, 4) +
		6 * penalty_high(primitive_counter_body_ratio, 0) +
		3 * penalty_low(net_move_ratio, max_i32(params->trend_net_min - 120, 280)) +
		3 * penalty_low(directional_purity_body, max_i32(params->trend_purity_min - 140, 560)) +
		2 * penalty_high(sign_change_density, 380);
	retracement_error =
		3 * penalty_low(retracement_ratio, max_i32(params->retrace_retrace_min, params->retrace_break_threshold)) +
		3 * penalty_high(primitive_consecutive_ratio, 460) +
		2 * penalty_high(primitive_max_consecutive_run, 3) +
		2 * penalty_band(net_move_ratio, params->retrace_net_lo, params->retrace_net_hi);
	alternating_chop_error =
		4 * penalty_low(primitive_alternating_ratio, 680) +
		4 * penalty_band(primitive_minority_body_ratio, 300, 500) +
		3 * penalty_high(net_move_ratio, params->chop_net_max) +
		2 * penalty_high(primitive_max_consecutive_run, 2);
	mixed_compression_error =
		3 * penalty_low(primitive_alternating_ratio, 420) +
		2 * penalty_band(primitive_minority_body_ratio, 300, 500) +
		2 * penalty_high(range_growth_ratio, params->compress_growth_max) +
		2 * penalty_band(net_move_ratio, params->compress_net_lo, params->compress_net_hi) +
		penalty_low((int32_t)duration, params->compression_min_duration);
	volatile_expansion_error =
		3 * penalty_low(primitive_alternating_ratio, 420) +
		2 * penalty_band(primitive_minority_body_ratio, 300, 500) +
		params->expand_w_growth * penalty_low(range_growth_ratio, params->expand_growth_min) +
		params->expand_w_gross * penalty_low(gross_range_norm, params->expand_gross_min) +
		penalty_low((int32_t)duration, params->expansion_min_duration);
	v_shaped_reversal_error =
		params->v_reversal_w_strength * penalty_low(v_strength, params->v_reversal_min_trend_strength) +
		params->v_reversal_w_bridge * penalty_high(sign_change_density, params->v_reversal_max_bridge_chop) +
		params->v_reversal_w_duration * penalty_high((int32_t)duration, params->v_reversal_max_duration);
	separator_error = 0x3fffffff;
	if (duration == 1u) {
		separator_error =
			120 +
			penalty_low(active_body_ratio, 300) +
			penalty_low(gross_range_norm, max_i32(params->expand_gross_min / 4, 60));
	}

	if (params->wave_model == WAVE_MODEL_PRIMITIVE) {
		int32_t consecutive_error;
		int32_t alternating_error;

		consecutive_error = min_i32(strong_trend_error, weak_trend_error) + move_penalty;
		if (edge_direction_mismatch || trend_move_too_small) {
			consecutive_error = 0x3fffffff;
		}
		alternating_error = min_i32(retracement_error,
			min_i32(alternating_chop_error,
			min_i32(mixed_compression_error,
			min_i32(volatile_expansion_error, v_shaped_reversal_error)))) + move_penalty;
		for (c = 0; c < WAVE_CLASS_COUNT; ++c) {
			out->class_error[c] = 0x3fffffff;
		}
		out->class_error[PRIMITIVE_CLASS_CONSECUTIVE] = consecutive_error;
		out->class_error[PRIMITIVE_CLASS_ALTERNATING] = alternating_error;
		out->class_error[PRIMITIVE_CLASS_SEPARATOR] = separator_error;
	} else if (is_relative_wave_model(params->wave_model)) {
		for (c = 0; c < WAVE_CLASS_COUNT; ++c) {
			out->class_error[c] = 0x3fffffff;
		}
		relative_strong_trend_error =
			4 * penalty_low(relative_directional_efficiency_step, params->relative_trend_der_step_min + 120) +
			3 * penalty_low(relative_directional_efficiency_body, params->relative_trend_der_body_min + 80) +
			4 * penalty_high(relative_retracement_ratio, params->relative_strong_trend_retracement_max) +
			3 * penalty_low(relative_avg_body_efficiency, params->relative_trend_body_eff_min + 60) +
			3 * penalty_high(relative_overlap_ratio, max_i32(params->relative_trend_overlap_max - 100, 0)) +
			2 * penalty_high(sign_change_density, max_i32(params->relative_trend_sign_flip_max - 120, 0)) +
			penalty_low(relative_wick_bias_aligned, params->relative_trend_wick_bias_min) +
			trend_quality_penalty;
		relative_weak_trend_error =
			4 * penalty_low(relative_directional_efficiency_step, params->relative_trend_der_step_min) +
			3 * penalty_low(relative_directional_efficiency_body, params->relative_trend_der_body_min) +
			4 * penalty_high(relative_retracement_ratio, params->relative_trend_retracement_max) +
			3 * penalty_low(relative_avg_body_efficiency, params->relative_trend_body_eff_min) +
			3 * penalty_high(relative_overlap_ratio, params->relative_trend_overlap_max) +
			2 * penalty_high(sign_change_density, params->relative_trend_sign_flip_max) +
			penalty_low(relative_wick_bias_aligned, params->relative_trend_wick_bias_min) +
			trend_quality_penalty;
		relative_trend_error = min_i32(relative_strong_trend_error, relative_weak_trend_error) + move_penalty;
		if (raw_net == 0 || trend_move_too_small) {
			relative_trend_error = 0x3fffffff;
		}
		relative_range_error =
			4 * penalty_high(relative_directional_efficiency_step, params->relative_range_der_step_max) +
			3 * penalty_high(relative_avg_body_efficiency, params->relative_range_body_eff_max) +
			3 * penalty_low(relative_overlap_ratio, params->relative_range_overlap_min) +
			2 * penalty_low(sign_change_density, params->relative_range_sign_flip_min) +
			2 * penalty_low(relative_retracement_ratio, params->relative_trend_retracement_max) +
			penalty_low(params->norm_scale - relative_wick_bias_aligned, 0) +
			range_quality_penalty +
			move_penalty;
		out->class_error[RELATIVE_CLASS_TREND] = relative_trend_error;
		out->class_error[RELATIVE_CLASS_RANGE] = relative_range_error;
	} else {
		out->class_error[WAVE_CLASS_STRONG_TREND] =
			params->trend_w_net * penalty_low(net_move_ratio, params->trend_net_min) +
			params->trend_w_purity * penalty_low(directional_purity_body, params->trend_purity_min) +
			params->trend_w_retrace * penalty_high(retracement_ratio, params->trend_retrace_max) +
			params->trend_w_sign * penalty_high(sign_change_density, params->trend_sign_max) +
			params->trend_w_adv * penalty_low(advance_per_candle_norm, params->trend_adv_min) +
			4 * penalty_low(active_body_ratio, 350);
		out->class_error[WAVE_CLASS_WEAK_TREND] =
			params->trend_w_net * penalty_low(net_move_ratio, max_i32(params->trend_net_min - 140, 220)) +
			params->trend_w_purity * penalty_low(directional_purity_body, max_i32(params->trend_purity_min - 180, 420)) +
			params->trend_w_retrace * penalty_band(retracement_ratio, 120, params->trend_retrace_max + 220) +
			params->trend_w_sign * penalty_band(sign_change_density, 120, params->trend_sign_max + 260) +
			params->trend_w_adv * penalty_low(advance_per_candle_norm, max_i32(params->trend_adv_min - 8, 4)) +
			2 * penalty_low(active_body_ratio, 220);
		if (trend_move_too_small) {
			out->class_error[WAVE_CLASS_STRONG_TREND] = 0x3fffffff;
			out->class_error[WAVE_CLASS_WEAK_TREND] = 0x3fffffff;
		}
		out->class_error[WAVE_CLASS_RETRACEMENT] =
			params->retrace_w_retrace * penalty_low(retracement_ratio, max_i32(params->retrace_retrace_min, params->retrace_break_threshold)) +
			params->retrace_w_net * penalty_band(net_move_ratio, params->retrace_net_lo, params->retrace_net_hi) +
			params->retrace_w_sign * penalty_band(sign_change_density, params->retrace_sign_lo, params->retrace_sign_hi) +
			params->retrace_w_duration * penalty_high((int32_t)duration, params->retrace_duration_max) +
			params->retrace_w_purity * penalty_band(directional_purity_body, params->retrace_purity_lo, params->retrace_purity_hi);
		out->class_error[WAVE_CLASS_ALTERNATING_CHOP] =
			params->chop_w_sign * penalty_low(sign_change_density, params->chop_sign_min) +
			params->chop_w_net * penalty_high(net_move_ratio, params->chop_net_max) +
			params->chop_w_purity * penalty_high(directional_purity_body, params->chop_purity_max) +
			params->chop_w_adv * penalty_high(advance_per_candle_norm, params->chop_adv_max);
		out->class_error[WAVE_CLASS_MIXED_COMPRESSION] =
			params->compress_w_growth * penalty_high(range_growth_ratio, params->compress_growth_max) +
			params->compress_w_net * penalty_band(net_move_ratio, params->compress_net_lo, params->compress_net_hi) +
			params->compress_w_sign * penalty_band(sign_change_density, params->compress_sign_lo, params->compress_sign_hi) +
			penalty_low((int32_t)duration, params->compression_min_duration);
		out->class_error[WAVE_CLASS_VOLATILE_EXPANSION] =
			params->expand_w_growth * penalty_low(range_growth_ratio, params->expand_growth_min) +
			params->expand_w_gross * penalty_low(gross_range_norm, params->expand_gross_min) +
			penalty_low((int32_t)duration, params->expansion_min_duration);
		out->class_error[WAVE_CLASS_V_SHAPED_REVERSAL] = v_shaped_reversal_error;
		for (c = 0; c < WAVE_CLASS_COUNT; ++c) {
			out->class_error[c] += move_penalty;
		}
	}
	out->best_class = 0u;
	out->second_best_class = 0u;
	out->best_fit = out->class_error[0];
	out->second_best_error = 0x7fffffff;
	for (c = 1; c < wave_output_class_count(params); ++c) {
		if (out->class_error[c] < out->best_fit) {
			out->second_best_error = out->best_fit;
			out->second_best_class = out->best_class;
			out->best_fit = out->class_error[c];
			out->best_class = c;
		} else if (out->class_error[c] < out->second_best_error) {
			out->second_best_error = out->class_error[c];
			out->second_best_class = c;
		}
	}
	if (out->second_best_error == 0x7fffffff) {
		out->second_best_error = out->best_fit;
		out->second_best_class = out->best_class;
	}
	out->ambiguity_margin = out->second_best_error - out->best_fit;
	out->net_move_raw = net_move_raw;
	out->net_move_ratio = net_move_ratio;
	out->retracement_ratio = retracement_ratio;
	out->sign_change_density = sign_change_density;
	out->active_body_ratio = active_body_ratio;
	out->primitive_consecutive_ratio = primitive_consecutive_ratio;
	out->primitive_alternating_ratio = primitive_alternating_ratio;
	out->primitive_max_consecutive_run = primitive_max_consecutive_run;
	out->primitive_max_alternating_run = primitive_max_alternating_run;
	out->primitive_alternating_pair_count = primitive_alternating_pair_count;
	out->primitive_counter_body_ratio = primitive_counter_body_ratio;
	out->primitive_minority_body_ratio = primitive_minority_body_ratio;
	out->relative_net_move_raw = raw_net;
	out->relative_abs_body_sum_raw = relative_abs_body_sum_raw;
	out->relative_abs_step_sum_raw = relative_abs_step_sum_raw;
	out->relative_directional_efficiency_body = relative_directional_efficiency_body;
	out->relative_directional_efficiency_step = relative_directional_efficiency_step;
	out->relative_avg_body_efficiency = relative_avg_body_efficiency;
	out->relative_body_direction_share = relative_body_direction_share;
	out->relative_upper_wick_sum_raw = relative_upper_wick_sum_raw;
	out->relative_lower_wick_sum_raw = relative_lower_wick_sum_raw;
	out->relative_total_wick_sum_raw = relative_total_wick_sum_raw;
	out->relative_wick_to_body_ratio = relative_wick_to_body_ratio;
	out->relative_wick_bias_up = relative_wick_bias_up;
	out->relative_wick_bias_down = relative_wick_bias_down;
	out->relative_wick_bias_aligned = relative_wick_bias_aligned;
	out->relative_max_pullback_raw = relative_max_pullback_raw;
	out->relative_impulse_leg_raw = relative_impulse_leg_raw;
	out->relative_retracement_ratio = relative_retracement_ratio;
	out->relative_body_overlap_sum_raw = relative_body_overlap_sum_raw;
	out->relative_overlap_ratio = relative_overlap_ratio;
	out->relative_range_growth_ratio = range_growth_ratio;
	out->relative_body_growth_ratio = relative_body_growth_ratio;
	out->relative_trend_quality_raw = relative_trend_quality_raw;
	out->relative_trend_quality_bucket = relative_trend_quality_bucket;
	out->directional_purity_body = directional_purity_body;
	out->advance_per_candle_norm = advance_per_candle_norm;
	out->range_growth_ratio = range_growth_ratio;
	out->context_range = context_range;
	out->dominant_direction = (raw_net > 0) ? 1 : ((raw_net < 0) ? -1 : 0);
	out->v_min_side_strength = v_strength;
	out->v_bridge_chop = sign_change_density;
}

static int is_trend_priority_enabled(const WaveParams *params)
{
	return (params != NULL && params->trend_priority_mode > 0);
}

static int is_trend_class(uint32_t class_id)
{
	return (class_id == WAVE_CLASS_STRONG_TREND || class_id == WAVE_CLASS_WEAK_TREND);
}

static int is_relative_trend_class(uint32_t class_id, const WaveParams *params)
{
	return params != NULL &&
		is_relative_wave_model((uint32_t)params->wave_model) &&
		class_id == RELATIVE_CLASS_TREND;
}

static uint32_t trend_lock_min_len(const WaveParams *params);
static int relative_span_is_trend_valid(const WaveSegmentEval *eval, const WaveParams *params);

static int relative_span_has_consistent_edges(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction)
{
	int first_sign = 0;
	int last_sign = 0;

	if (candles == NULL || features == NULL || end < start || dominant_direction == 0) {
		return 0;
	}
	if (features[start].b > 0) {
		first_sign = 1;
	} else if (features[start].b < 0) {
		first_sign = -1;
	}
	if (features[end].b > 0) {
		last_sign = 1;
	} else if (features[end].b < 0) {
		last_sign = -1;
	}
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

static int relative_span_preserves_seed_leg(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	uint32_t seed_start,
	uint32_t seed_end,
	int dominant_direction,
	int32_t allowed_pullback)
{
	uint32_t anchor_start = seed_start;
	uint32_t anchor_end = seed_end;
	int32_t seed_origin;
	int32_t seed_terminal;
	uint32_t i;

	if (candles == NULL || features == NULL || end < start || seed_end < seed_start || dominant_direction == 0) {
		return 0;
	}
	(void)compute_relative_trend_price_amplitude(candles,
		features,
		seed_start,
		seed_end,
		dominant_direction,
		&anchor_start,
		&anchor_end);
	if (dominant_direction > 0) {
		int32_t allowed_low;

		seed_origin = candles[anchor_start].low_pips;
		seed_terminal = candles[anchor_end].high_pips;
		allowed_low = seed_terminal - allowed_pullback;
		for (i = anchor_end + 1u; i <= end; ++i) {
			int32_t price_low = candles[i].low_pips;

			if (price_low < allowed_low) {
				return 0;
			}
		}
	} else {
		int32_t allowed_high;

		seed_origin = candles[anchor_start].high_pips;
		seed_terminal = candles[anchor_end].low_pips;
		allowed_high = seed_terminal + allowed_pullback;
		for (i = anchor_end + 1u; i <= end; ++i) {
			int32_t price_high = candles[i].high_pips;

			if (price_high > allowed_high) {
				return 0;
			}
		}
	}
	(void)seed_origin;
	return 1;
}

static int relative_span_preserves_seed_leg_body(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	uint32_t seed_start,
	uint32_t seed_end,
	int dominant_direction,
	int32_t allowed_pullback)
{
	uint32_t anchor_start = seed_start;
	uint32_t anchor_end = seed_end;
	uint32_t i;

	if (candles == NULL || features == NULL || end < start || seed_end < seed_start || dominant_direction == 0) {
		return 0;
	}
	(void)compute_relative_trend_price_amplitude(candles,
		features,
		seed_start,
		seed_end,
		dominant_direction,
		&anchor_start,
		&anchor_end);
	if (dominant_direction > 0) {
		int32_t seed_terminal = max_i32(candles[anchor_end].open_pips, candles[anchor_end].close_pips);
		int32_t allowed_low = seed_terminal - allowed_pullback;

		for (i = anchor_end + 1u; i <= end; ++i) {
			int32_t body_low = min_i32(candles[i].open_pips, candles[i].close_pips);

			if (body_low < allowed_low) {
				return 0;
			}
		}
	} else {
		int32_t seed_terminal = min_i32(candles[anchor_end].open_pips, candles[anchor_end].close_pips);
		int32_t allowed_high = seed_terminal + allowed_pullback;

		for (i = anchor_end + 1u; i <= end; ++i) {
			int32_t body_high = max_i32(candles[i].open_pips, candles[i].close_pips);

			if (body_high > allowed_high) {
				return 0;
			}
		}
	}
	(void)start;
	return 1;
}

static int is_lockable_relative_trend(const WaveSegmentEval *eval, const WaveParams *params)
{
	int32_t min_duration;
	int32_t min_ambiguity_margin;
	int32_t max_quality_raw;
	int32_t min_der_step;
	int32_t min_der_body;
	int32_t max_retrace;
	int32_t max_overlap;
	int32_t max_sign_density;
	int32_t max_wick_to_body;
	int32_t min_body_share;

	if (eval == NULL || params == NULL || params->wave_model != WAVE_MODEL_RELATIVE_2_STAGE) {
		return 0;
	}
	min_duration = max_i32((int32_t)(params->lmax_candles / 4u), 6);
	min_ambiguity_margin = (params->trend_priority_mode >= 2) ? 300 : 180;
	max_quality_raw = (params->trend_priority_mode >= 2) ? 400 : 480;
	min_der_step = max_i32(params->relative_trend_der_step_min - 70, params->norm_scale / 3);
	min_der_body = max_i32(params->relative_trend_der_body_min - 70, params->norm_scale / 3);
	max_retrace = min_i32(params->relative_trend_retracement_max, 280);
	max_overlap = min_i32(params->relative_trend_overlap_max, 280);
	max_sign_density = min_i32(params->relative_trend_sign_flip_max, 260);
	max_wick_to_body = (params->trend_priority_mode >= 2) ? params->norm_scale : (params->norm_scale + (params->norm_scale / 8));
	min_body_share = (params->trend_priority_mode >= 2) ? 720 : 680;
	if (max_retrace < 0) {
		max_retrace = 0;
	}
	if (max_overlap < 0) {
		max_overlap = 0;
	}
	if (max_sign_density < 0) {
		max_sign_density = 0;
	}
	if (eval->best_class != RELATIVE_CLASS_TREND) {
		return 0;
	}
	return eval->duration >= (uint32_t)max_i32((int32_t)trend_lock_min_len(params), min_duration) &&
		eval->net_move_raw >= params->minimum_pips_for_trend &&
		eval->ambiguity_margin >= min_ambiguity_margin &&
		eval->relative_trend_quality_raw <= max_quality_raw &&
		eval->relative_directional_efficiency_step >= min_der_step &&
		eval->relative_directional_efficiency_body >= min_der_body &&
		eval->relative_retracement_ratio <= max_retrace &&
		eval->relative_overlap_ratio <= max_overlap &&
		eval->relative_wick_to_body_ratio <= max_wick_to_body &&
		eval->sign_change_density <= max_sign_density &&
		eval->relative_body_direction_share >= min_body_share &&
		eval->dominant_direction != 0;
}

static int is_seed_relative_trend(const WaveSegmentEval *eval, const WaveParams *params)
{
	return is_seed_relative_trend_for_pass(eval, params, 1u);
}

static int is_seed_relative_trend_for_pass(const WaveSegmentEval *eval, const WaveParams *params, uint32_t seed_pass)
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
	int32_t min_impulse_leg;
	int32_t min_price_amplitude;

	if (eval == NULL || params == NULL || !is_relative_seeded_family((uint32_t)params->wave_model)) {
		return 0;
	}
	if (seed_pass <= 1u) {
		min_der_step = min_i32(params->norm_scale, params->relative_trend_der_step_min + 140);
		min_der_body = min_i32(params->norm_scale, params->relative_trend_der_body_min + 120);
		max_retrace = min_i32(params->relative_strong_trend_retracement_max, 180);
		max_overlap = min_i32(params->relative_trend_overlap_max, 180);
		max_wick_to_body = (params->norm_scale * 3) / 4;
		max_sign_density = 160;
		min_body_share = 820;
		min_ambiguity = 240;
		max_quality = 260;
		min_impulse_leg = max_i32(params->minimum_pips_for_trend * 2, 14);
		min_price_amplitude = max_i32(params->minimum_pips_for_trend * 3, 20);
		if (params->trend_priority_mode >= 2) {
			min_der_step = min_i32(params->norm_scale, min_der_step + 40);
			min_der_body = min_i32(params->norm_scale, min_der_body + 40);
			max_retrace = min_i32(max_retrace, 140);
			max_overlap = min_i32(max_overlap, 140);
			max_wick_to_body = min_i32(max_wick_to_body, (params->norm_scale * 2) / 3);
			max_sign_density = 120;
			min_body_share = 860;
			min_ambiguity = 320;
			max_quality = 220;
			min_impulse_leg = max_i32(min_impulse_leg, 18);
			min_price_amplitude = max_i32(min_price_amplitude, 24);
		}
	} else {
		min_der_step = min_i32(params->norm_scale, params->relative_trend_der_step_min + 60);
		min_der_body = min_i32(params->norm_scale, params->relative_trend_der_body_min + 50);
		max_retrace = min_i32(params->relative_trend_retracement_max, 240);
		max_overlap = min_i32(params->relative_trend_overlap_max, 240);
		max_wick_to_body = (params->norm_scale * 9) / 10;
		max_sign_density = 240;
		min_body_share = 740;
		min_ambiguity = 160;
		max_quality = 420;
		min_impulse_leg = params->minimum_pips_for_trend;
		min_price_amplitude = max_i32(params->minimum_pips_for_trend + 3, 10);
		if (params->trend_priority_mode >= 2) {
			min_der_step = min_i32(params->norm_scale, min_der_step + 30);
			min_der_body = min_i32(params->norm_scale, min_der_body + 20);
			max_retrace = min_i32(max_retrace, 210);
			max_overlap = min_i32(max_overlap, 210);
			max_wick_to_body = min_i32(max_wick_to_body, (params->norm_scale * 17) / 20);
			max_sign_density = 200;
			min_body_share = 780;
			min_ambiguity = 220;
			max_quality = 360;
			min_impulse_leg = max_i32(min_impulse_leg, 10);
			min_price_amplitude = max_i32(min_price_amplitude, 12);
		}
	}
	return eval->best_class == RELATIVE_CLASS_TREND &&
		eval->duration >= (uint32_t)max_i32((int32_t)params->lmin_candles, 3) &&
		eval->net_move_raw >= params->minimum_pips_for_trend &&
		eval->relative_impulse_leg_raw >= min_impulse_leg &&
		abs_i32(eval->relative_net_move_raw) >= min_price_amplitude &&
		eval->dominant_direction != 0 &&
		eval->ambiguity_margin >= min_ambiguity &&
		eval->relative_trend_quality_raw <= max_quality &&
		eval->relative_directional_efficiency_step >= min_der_step &&
		eval->relative_directional_efficiency_body >= min_der_body &&
		eval->relative_retracement_ratio <= max_retrace &&
		eval->relative_overlap_ratio <= max_overlap &&
		eval->relative_wick_to_body_ratio <= max_wick_to_body &&
		eval->sign_change_density <= max_sign_density &&
		eval->relative_body_direction_share >= min_body_share;
}

static int relative_span_is_trend_valid(const WaveSegmentEval *eval, const WaveParams *params)
{
	if (eval == NULL || params == NULL) {
		return 0;
	}
	if (eval->dominant_direction == 0 ||
		eval->net_move_raw < params->minimum_pips_for_trend ||
		eval->relative_impulse_leg_raw < params->min_wave_move_pips) {
		return 0;
	}
	return eval->relative_retracement_ratio <= params->relative_trend_retracement_max &&
		eval->relative_directional_efficiency_step >= params->relative_trend_der_step_min &&
		eval->relative_avg_body_efficiency >= params->relative_trend_body_eff_min &&
		eval->relative_overlap_ratio <= params->relative_trend_overlap_max;
}

static int is_lockable_strong_trend(const WaveSegmentEval *eval, const WaveParams *params)
{
	int32_t net_min;
	int32_t purity_min;
	int32_t sign_max;
	int32_t fit_max;
	int32_t active_min;

	if (eval == NULL || params == NULL || params->trend_priority_mode <= 0) {
		return 0;
	}
	if (params->wave_model == WAVE_MODEL_PRIMITIVE) {
		return eval->class_error[WAVE_CLASS_STRONG_TREND] <= params->weak_fit_threshold + 40 &&
			eval->net_move_raw >= params->minimum_pips_for_trend &&
			eval->primitive_consecutive_ratio >= 840 &&
			eval->primitive_alternating_ratio <= 180 &&
			eval->primitive_max_consecutive_run >= 5 &&
			eval->primitive_counter_body_ratio == 0 &&
			eval->net_move_ratio >= max_i32(params->trend_net_min - 40, 360) &&
			eval->directional_purity_body >= max_i32(params->trend_purity_min - 40, 680) &&
			eval->sign_change_density <= 220 &&
			eval->dominant_direction != 0;
	}
	net_min = params->trend_net_min + ((params->trend_priority_mode >= 2) ? 80 : 0);
	purity_min = params->trend_purity_min + ((params->trend_priority_mode >= 2) ? 40 : 0);
	sign_max = params->trend_sign_max - ((params->trend_priority_mode >= 2) ? 60 : 0);
	fit_max = params->weak_fit_threshold + ((params->trend_priority_mode >= 2) ? 0 : 40);
	active_min = 350;
	if (sign_max < 0) {
		sign_max = 0;
	}
	return eval->class_error[WAVE_CLASS_STRONG_TREND] <= fit_max &&
		eval->net_move_raw >= params->minimum_pips_for_trend &&
		eval->net_move_ratio >= net_min &&
		eval->directional_purity_body >= purity_min &&
		eval->retracement_ratio <= params->trend_retrace_max &&
		eval->sign_change_density <= sign_max &&
		eval->active_body_ratio >= active_min &&
		eval->dominant_direction != 0;
}

static int is_lockable_weak_trend(const WaveSegmentEval *eval, const WaveParams *params)
{
	int32_t net_min;
	int32_t purity_min;
	int32_t retrace_max;
	int32_t sign_max;
	int32_t fit_max;
	int32_t active_min;

	if (eval == NULL || params == NULL || params->trend_priority_mode <= 0) {
		return 0;
	}
	if (params->wave_model == WAVE_MODEL_PRIMITIVE) {
		return eval->class_error[WAVE_CLASS_WEAK_TREND] <= params->weak_fit_threshold + 80 &&
			eval->net_move_raw >= params->minimum_pips_for_trend &&
			eval->primitive_consecutive_ratio >= 680 &&
			eval->primitive_alternating_ratio <= 320 &&
			eval->primitive_max_consecutive_run >= 4 &&
			eval->primitive_counter_body_ratio == 0 &&
			eval->net_move_ratio >= max_i32(params->trend_net_min - 120, 280) &&
			eval->directional_purity_body >= max_i32(params->trend_purity_min - 140, 560) &&
			eval->sign_change_density <= 380 &&
			eval->dominant_direction != 0;
	}
	net_min = max_i32(params->trend_net_min - 140, 220);
	purity_min = max_i32(params->trend_purity_min - 180, 420);
	retrace_max = params->trend_retrace_max + 220;
	sign_max = params->trend_sign_max + 260;
	fit_max = params->weak_fit_threshold + ((params->trend_priority_mode >= 2) ? 40 : 100);
	active_min = 220;
	return eval->class_error[WAVE_CLASS_WEAK_TREND] <= fit_max &&
		eval->net_move_raw >= params->minimum_pips_for_trend &&
		eval->net_move_ratio >= net_min &&
		eval->directional_purity_body >= purity_min &&
		eval->retracement_ratio <= retrace_max &&
		eval->sign_change_density <= sign_max &&
		eval->active_body_ratio >= active_min &&
		eval->dominant_direction != 0;
}

static uint32_t select_priority_class(const WaveSegmentEval *eval, const WaveParams *params)
{
	if (eval == NULL || params == NULL) {
		return WAVE_CLASS_COUNT;
	}
	if (params->wave_model == WAVE_MODEL_RELATIVE_2_STAGE) {
		return is_lockable_relative_trend(eval, params) ? RELATIVE_CLASS_TREND : RELATIVE_CLASS_COUNT;
	}
	if (is_relative_seeded_family((uint32_t)params->wave_model)) {
		return is_seed_relative_trend(eval, params) ? RELATIVE_CLASS_TREND : RELATIVE_CLASS_COUNT;
	}
	if (is_lockable_strong_trend(eval, params)) {
		return WAVE_CLASS_STRONG_TREND;
	}
	if (is_lockable_weak_trend(eval, params)) {
		return WAVE_CLASS_WEAK_TREND;
	}
	return WAVE_CLASS_COUNT;
}

static uint32_t select_merged_trend_class(const WaveSegmentEval *eval, const WaveParams *params)
{
	if (eval == NULL || params == NULL) {
		return WAVE_CLASS_COUNT;
	}
	if (params->wave_model == WAVE_MODEL_RELATIVE_2_STAGE) {
		return is_lockable_relative_trend(eval, params) ? RELATIVE_CLASS_TREND : RELATIVE_CLASS_COUNT;
	}
	if (is_relative_seeded_family((uint32_t)params->wave_model)) {
		return is_seed_relative_trend(eval, params) ? RELATIVE_CLASS_TREND : RELATIVE_CLASS_COUNT;
	}
	if (is_lockable_strong_trend(eval, params)) {
		return WAVE_CLASS_STRONG_TREND;
	}
	if (is_lockable_weak_trend(eval, params)) {
		return WAVE_CLASS_WEAK_TREND;
	}
	return WAVE_CLASS_COUNT;
}

static uint32_t trend_lock_min_len(const WaveParams *params)
{
	uint32_t min_len;

	if (params == NULL) {
		return 1u;
	}
	if (params->trend_priority_mode >= 2) {
		min_len = max_i32((int32_t)(params->lmax_candles / 3u), 5);
	} else {
		min_len = max_i32((int32_t)(params->lmax_candles / 4u), 3);
	}
	if (min_len > params->lmax_candles) {
		min_len = params->lmax_candles;
	}
	if (min_len < params->lmin_candles) {
		min_len = params->lmin_candles;
	}
	return min_len;
}

static int compute_token_weak_fit(const WaveToken *token, const WaveParams *params)
{
	if (token == NULL || params == NULL) {
		return 0;
	}
	if (is_relative_class_wave_model((uint32_t)params->wave_model)) {
		if (token->class_id == RELATIVE_CLASS_TREND) {
			return (token->relative_trend_quality_bucket >= 4) ? 1 : 0;
		}
		if (token->class_id == RELATIVE_CLASS_RANGE) {
			return (token->fit_error > (params->weak_fit_threshold + 400)) ? 1 : 0;
		}
	}
	return (token->fit_error > params->weak_fit_threshold) ? 1 : 0;
}

static void fill_token_from_eval(WaveToken *token, const WaveSegmentEval *eval, uint32_t class_id, const WaveParams *params)
{
	if (token == NULL || eval == NULL || params == NULL) {
		return;
	}
	token->start_candle = eval->start;
	token->end_candle = eval->end;
	token->seed_start_candle = eval->start;
	token->seed_end_candle = eval->end;
	token->seed_pass = 0u;
	token->layered_child_start_index = 0u;
	token->layered_child_end_index = 0u;
	token->layered_sequential_count = 0u;
	token->layered_alternating_count = 0u;
	token->layered_sequential_candles = 0u;
	token->layered_alternating_candles = 0u;
	token->layered_sequential_net_move_sum_raw = 0;
	token->layered_alternating_net_move_sum_raw = 0;
	token->layered_directional_agreement = 0;
	token->class_id = class_id;
	token->fit_error = eval->class_error[class_id];
	token->second_best_error = eval->second_best_error;
	token->ambiguity_margin = eval->ambiguity_margin;
	token->duration_candles = eval->duration;
	token->net_move_raw = eval->net_move_raw;
	token->retracement_ratio = eval->retracement_ratio;
	token->sign_change_density = eval->sign_change_density;
	token->directional_purity_body = eval->directional_purity_body;
	token->range_growth_ratio = eval->range_growth_ratio;
	token->context_range = eval->context_range;
	token->primitive_consecutive_ratio = eval->primitive_consecutive_ratio;
	token->primitive_alternating_ratio = eval->primitive_alternating_ratio;
	token->primitive_max_consecutive_run = eval->primitive_max_consecutive_run;
	token->primitive_max_alternating_run = eval->primitive_max_alternating_run;
	token->primitive_alternating_pair_count = eval->primitive_alternating_pair_count;
	token->primitive_counter_body_ratio = eval->primitive_counter_body_ratio;
	token->primitive_minority_body_ratio = eval->primitive_minority_body_ratio;
	token->relative_net_move_raw = eval->relative_net_move_raw;
	token->relative_abs_body_sum_raw = eval->relative_abs_body_sum_raw;
	token->relative_abs_step_sum_raw = eval->relative_abs_step_sum_raw;
	token->relative_directional_efficiency_body = eval->relative_directional_efficiency_body;
	token->relative_directional_efficiency_step = eval->relative_directional_efficiency_step;
	token->relative_avg_body_efficiency = eval->relative_avg_body_efficiency;
	token->relative_body_direction_share = eval->relative_body_direction_share;
	token->relative_upper_wick_sum_raw = eval->relative_upper_wick_sum_raw;
	token->relative_lower_wick_sum_raw = eval->relative_lower_wick_sum_raw;
	token->relative_total_wick_sum_raw = eval->relative_total_wick_sum_raw;
	token->relative_wick_to_body_ratio = eval->relative_wick_to_body_ratio;
	token->relative_wick_bias_up = eval->relative_wick_bias_up;
	token->relative_wick_bias_down = eval->relative_wick_bias_down;
	token->relative_wick_bias_aligned = eval->relative_wick_bias_aligned;
	token->relative_max_pullback_raw = eval->relative_max_pullback_raw;
	token->relative_impulse_leg_raw = eval->relative_impulse_leg_raw;
	token->relative_retracement_ratio = eval->relative_retracement_ratio;
	token->relative_body_overlap_sum_raw = eval->relative_body_overlap_sum_raw;
	token->relative_overlap_ratio = eval->relative_overlap_ratio;
	token->relative_range_growth_ratio = eval->relative_range_growth_ratio;
	token->relative_body_growth_ratio = eval->relative_body_growth_ratio;
	token->relative_trend_quality_raw = eval->relative_trend_quality_raw;
	token->relative_trend_quality_bucket = eval->relative_trend_quality_bucket;
	token->knn_class_agreement = 0;
	token->knn_neighbor_purity = 0;
	token->knn_distance_continuation = -1;
	token->knn_distance_exhaustion = -1;
	token->knn_majority_label = -1;
	token->knn_margin = 0;
	token->dominant_direction = eval->dominant_direction;
	token->boundary_confidence = 0;
	token->weak_fit = compute_token_weak_fit(token, params);
	token->debug_flags = 0u;
	token->debug_reduction_flags = 0u;
	token->debug_reduction_count = 0u;
}

static int32_t wave_token_knn_distance(const WaveParams *params, const WaveToken *a, const WaveToken *b)
{
	int32_t norm_scale;
	int32_t distance = 0;

	if (params == NULL || a == NULL || b == NULL) {
		return 0x3fffffff;
	}
	norm_scale = max_i32(params->norm_scale, 1);
	distance += abs_i32(a->relative_directional_efficiency_step - b->relative_directional_efficiency_step);
	distance += abs_i32(a->relative_directional_efficiency_body - b->relative_directional_efficiency_body);
	distance += abs_i32(a->relative_retracement_ratio - b->relative_retracement_ratio);
	distance += abs_i32(a->relative_overlap_ratio - b->relative_overlap_ratio);
	distance += abs_i32(a->relative_avg_body_efficiency - b->relative_avg_body_efficiency);
	distance += abs_i32(a->relative_body_direction_share - b->relative_body_direction_share);
	distance += abs_i32(a->relative_wick_to_body_ratio - b->relative_wick_to_body_ratio);
	distance += abs_i32(a->sign_change_density - b->sign_change_density);
	distance += abs_i32(a->relative_trend_quality_raw - b->relative_trend_quality_raw);
	distance += abs_i32(a->duration_candles - b->duration_candles) * (norm_scale / 8);
	distance += abs_i32(abs_i32(a->relative_net_move_raw) - abs_i32(b->relative_net_move_raw));
	if (a->dominant_direction != b->dominant_direction) {
		distance += norm_scale / 2;
	}
	return distance;
}

static int token_is_continuation_case(const WaveParams *params, const WaveToken *tok)
{
	if (params == NULL || tok == NULL) {
		return 0;
	}
	if (!is_relative_wave_model((uint32_t)params->wave_model) || tok->class_id != RELATIVE_CLASS_TREND) {
		return 0;
	}
	return tok->relative_trend_quality_bucket <= 1 &&
		tok->relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max, 180) &&
		tok->relative_overlap_ratio <= min_i32(params->relative_trend_overlap_max, 220) &&
		tok->relative_directional_efficiency_step >= params->relative_trend_der_step_min &&
		tok->relative_directional_efficiency_body >= params->relative_trend_der_body_min;
}

static int token_is_exhaustion_case(const WaveParams *params, const WaveToken *tok)
{
	int32_t retrace_cut;
	int32_t overlap_cut;

	if (params == NULL || tok == NULL) {
		return 0;
	}
	if (!is_relative_wave_model((uint32_t)params->wave_model) || tok->class_id != RELATIVE_CLASS_TREND) {
		return 0;
	}
	retrace_cut = (params->relative_trend_retracement_max * 3) / 4;
	overlap_cut = (params->relative_trend_overlap_max * 4) / 5;
	return tok->relative_trend_quality_bucket >= 5 ||
		tok->relative_retracement_ratio >= retrace_cut ||
		tok->relative_overlap_ratio >= overlap_cut ||
		tok->sign_change_density >= (params->relative_trend_sign_flip_max * 4) / 5;
}

static void compute_knn_wave_metrics(const WaveParams *params, WaveToken *tokens, uint32_t token_count)
{
	enum { KNN_K = 8 };
	uint32_t i;

	if (params == NULL || tokens == NULL || token_count == 0u) {
		return;
	}
	for (i = 0u; i < token_count; ++i) {
		int32_t best_dist[KNN_K];
		uint32_t best_index[KNN_K];
		uint32_t neighbor_count = 0u;
		uint32_t label_counts[WAVE_CLASS_COUNT];
		uint32_t same_label_count = 0u;
		uint32_t majority_label = 0u;
		uint32_t majority_count = 0u;
		uint32_t second_count = 0u;
		int32_t cont_best[KNN_K];
		int32_t ex_best[KNN_K];
		uint32_t cont_count = 0u;
		uint32_t ex_count = 0u;
		uint32_t j;
		uint32_t c;
		int32_t cont_sum = 0;
		int32_t ex_sum = 0;

		memset(label_counts, 0, sizeof(label_counts));
		for (j = 0u; j < KNN_K; ++j) {
			best_dist[j] = 0x3fffffff;
			best_index[j] = 0xffffffffu;
			cont_best[j] = 0x3fffffff;
			ex_best[j] = 0x3fffffff;
		}
		for (j = 0u; j < token_count; ++j) {
			int32_t dist;
			uint32_t pos;

			if (j == i) {
				continue;
			}
			dist = wave_token_knn_distance(params, &tokens[i], &tokens[j]);
			for (pos = 0u; pos < KNN_K; ++pos) {
				if (dist < best_dist[pos]) {
					uint32_t shift;

					for (shift = KNN_K - 1u; shift > pos; --shift) {
						best_dist[shift] = best_dist[shift - 1u];
						best_index[shift] = best_index[shift - 1u];
					}
					best_dist[pos] = dist;
					best_index[pos] = j;
					break;
				}
			}
			if (token_is_continuation_case(params, &tokens[j])) {
				for (pos = 0u; pos < KNN_K; ++pos) {
					if (dist < cont_best[pos]) {
						uint32_t shift;

						for (shift = KNN_K - 1u; shift > pos; --shift) {
							cont_best[shift] = cont_best[shift - 1u];
						}
						cont_best[pos] = dist;
						break;
					}
				}
			}
			if (token_is_exhaustion_case(params, &tokens[j])) {
				for (pos = 0u; pos < KNN_K; ++pos) {
					if (dist < ex_best[pos]) {
						uint32_t shift;

						for (shift = KNN_K - 1u; shift > pos; --shift) {
							ex_best[shift] = ex_best[shift - 1u];
						}
						ex_best[pos] = dist;
						break;
					}
				}
			}
		}
		for (j = 0u; j < KNN_K; ++j) {
			if (best_index[j] == 0xffffffffu) {
				continue;
			}
			neighbor_count += 1u;
			if (tokens[best_index[j]].class_id < WAVE_CLASS_COUNT) {
				label_counts[tokens[best_index[j]].class_id] += 1u;
				if (tokens[best_index[j]].class_id == tokens[i].class_id) {
					same_label_count += 1u;
				}
			}
		}
		for (c = 0u; c < WAVE_CLASS_COUNT; ++c) {
			if (label_counts[c] > majority_count) {
				second_count = majority_count;
				majority_count = label_counts[c];
				majority_label = c;
			} else if (label_counts[c] > second_count) {
				second_count = label_counts[c];
			}
		}
		for (j = 0u; j < KNN_K; ++j) {
			if (cont_best[j] == 0x3fffffff) {
				break;
			}
			cont_sum += cont_best[j];
			cont_count += 1u;
		}
		for (j = 0u; j < KNN_K; ++j) {
			if (ex_best[j] == 0x3fffffff) {
				break;
			}
			ex_sum += ex_best[j];
			ex_count += 1u;
		}
		tokens[i].knn_class_agreement = (neighbor_count > 0u) ?
			(int32_t)(((int64_t)same_label_count * params->norm_scale) / (int64_t)neighbor_count) : 0;
		tokens[i].knn_neighbor_purity = (neighbor_count > 0u) ?
			(int32_t)(((int64_t)majority_count * params->norm_scale) / (int64_t)neighbor_count) : 0;
		tokens[i].knn_majority_label = (neighbor_count > 0u) ? (int32_t)majority_label : -1;
		tokens[i].knn_distance_continuation = (cont_count > 0u) ? (cont_sum / (int32_t)cont_count) : -1;
		tokens[i].knn_distance_exhaustion = (ex_count > 0u) ? (ex_sum / (int32_t)ex_count) : -1;
		if (tokens[i].knn_distance_continuation >= 0 && tokens[i].knn_distance_exhaustion >= 0) {
			tokens[i].knn_margin = tokens[i].knn_distance_exhaustion - tokens[i].knn_distance_continuation;
		} else {
			tokens[i].knn_margin = 0;
		}
		(void)second_count;
	}
}

static void mark_token_reduction(WaveToken *token, uint32_t reduction_flag)
{
	if (token == NULL || reduction_flag == 0u) {
		return;
	}
	token->debug_reduction_flags |= reduction_flag;
	token->debug_reduction_count += 1u;
}

static void recompute_token(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *token)
{
	WaveSegmentEval eval;
	uint32_t debug_flags;
	uint32_t debug_reduction_flags;
	uint32_t debug_reduction_count;
	uint32_t seed_start_candle;
	uint32_t seed_end_candle;
	uint32_t seed_pass;
	uint32_t layered_child_start_index;
	uint32_t layered_child_end_index;
	uint32_t layered_sequential_count;
	uint32_t layered_alternating_count;
	uint32_t layered_sequential_candles;
	uint32_t layered_alternating_candles;
	int32_t layered_sequential_net_move_sum_raw;
	int32_t layered_alternating_net_move_sum_raw;
	int32_t layered_directional_agreement;
	int32_t fit_error;
	int32_t second_best_error;

	debug_flags = token->debug_flags;
	debug_reduction_flags = token->debug_reduction_flags;
	debug_reduction_count = token->debug_reduction_count;
	seed_start_candle = token->seed_start_candle;
	seed_end_candle = token->seed_end_candle;
	seed_pass = token->seed_pass;
	layered_child_start_index = token->layered_child_start_index;
	layered_child_end_index = token->layered_child_end_index;
	layered_sequential_count = token->layered_sequential_count;
	layered_alternating_count = token->layered_alternating_count;
	layered_sequential_candles = token->layered_sequential_candles;
	layered_alternating_candles = token->layered_alternating_candles;
	layered_sequential_net_move_sum_raw = token->layered_sequential_net_move_sum_raw;
	layered_alternating_net_move_sum_raw = token->layered_alternating_net_move_sum_raw;
	layered_directional_agreement = token->layered_directional_agreement;
	fit_error = token->fit_error;
	second_best_error = token->second_best_error;
	compute_segment_eval(candles, features, candle_count, params, token->start_candle, token->end_candle, &eval);
	fill_token_from_eval(token, &eval, token->class_id, params);
	token->debug_flags = debug_flags;
	token->debug_reduction_flags = debug_reduction_flags;
	token->debug_reduction_count = debug_reduction_count;
	token->seed_start_candle = seed_start_candle;
	token->seed_end_candle = seed_end_candle;
	token->seed_pass = seed_pass;
	token->layered_child_start_index = layered_child_start_index;
	token->layered_child_end_index = layered_child_end_index;
	token->layered_sequential_count = layered_sequential_count;
	token->layered_alternating_count = layered_alternating_count;
	token->layered_sequential_candles = layered_sequential_candles;
	token->layered_alternating_candles = layered_alternating_candles;
	token->layered_sequential_net_move_sum_raw = layered_sequential_net_move_sum_raw;
	token->layered_alternating_net_move_sum_raw = layered_alternating_net_move_sum_raw;
	token->layered_directional_agreement = layered_directional_agreement;
	if (params->wave_model == WAVE_MODEL_LAYERED) {
		token->fit_error = fit_error;
		token->second_best_error = second_best_error;
	}
}

static void merge_token_range(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	uint32_t first,
	uint32_t last,
	uint32_t new_class_id)
{
	WaveSegmentEval eval;
	uint32_t remove_count;
	uint32_t i;
	uint32_t debug_flags = 0u;
	uint32_t debug_reduction_flags = 0u;
	uint32_t debug_reduction_count = 0u;

	for (i = first; i <= last; ++i) {
		debug_flags |= tokens[i].debug_flags;
		debug_reduction_flags |= tokens[i].debug_reduction_flags;
		debug_reduction_count += tokens[i].debug_reduction_count;
	}

	compute_segment_eval(candles, features, candle_count, params, tokens[first].start_candle, tokens[last].end_candle, &eval);
	fill_token_from_eval(&tokens[first], &eval, new_class_id, params);
	tokens[first].debug_flags = debug_flags;
	tokens[first].debug_reduction_flags = debug_reduction_flags;
	tokens[first].debug_reduction_count = debug_reduction_count;
	remove_count = last - first;
	for (i = first + 1u; i + remove_count < *count; ++i) {
		tokens[i] = tokens[i + remove_count];
	}
	*count -= remove_count;
}

static int detect_priority_spans(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	TrendLockSpan *locks,
	uint32_t *out_lock_count)
{
	uint8_t *occupied = NULL;
	uint32_t class_count_limit;
	uint32_t lock_count = 0;

	if (out_lock_count == NULL) {
		return -1;
	}
	*out_lock_count = 0;
	if (!is_trend_priority_enabled(params) || locks == NULL || candle_count == 0u) {
		return 0;
	}
	class_count_limit = wave_output_class_count(params);

	occupied = (uint8_t *)calloc((size_t)candle_count, sizeof(*occupied));
	if (occupied == NULL) {
		return -1;
	}

	if (params->wave_model == WAVE_MODEL_RELATIVE_2_STAGE || is_relative_seeded_family((uint32_t)params->wave_model)) {
		uint32_t candidate_count = 0u;
		uint32_t min_len = trend_lock_min_len(params);
		uint32_t start;

		for (start = 0u; start < candle_count; ++start) {
			TrendLockSpan best_lock;
			int has_best = 0;
			uint32_t len;

			memset(&best_lock, 0, sizeof(best_lock));
			for (len = min_len; len <= params->lmax_candles && start + len <= candle_count; ++len) {
				WaveSegmentEval eval;
				TrendLockSpan cand_lock;
				uint32_t end = start + len - 1u;

				compute_segment_eval(candles, features, candle_count, params, start, end, &eval);
				cand_lock.class_id = select_priority_class(&eval, params);
				if (cand_lock.class_id >= class_count_limit) {
					continue;
				}
				if (cand_lock.class_id == RELATIVE_CLASS_TREND &&
					!relative_span_has_consistent_edges(candles, features, start, end, eval.dominant_direction)) {
					continue;
				}
				cand_lock.start = start;
				cand_lock.end = end;
				cand_lock.seed_start = start;
				cand_lock.seed_end = end;
				cand_lock.seed_pass = 1u;
				cand_lock.quality_raw = eval.relative_trend_quality_raw;
				cand_lock.net_move_raw = eval.relative_net_move_raw;
				cand_lock.price_amplitude_raw = compute_relative_trend_price_amplitude(candles,
					features,
					start,
					end,
					eval.dominant_direction,
					NULL,
					NULL);
				cand_lock.retracement_ratio = eval.relative_retracement_ratio;
				cand_lock.overlap_ratio = eval.relative_overlap_ratio;
				cand_lock.wick_to_body_ratio = eval.relative_wick_to_body_ratio;
				cand_lock.seed_leg_raw = max_i32(cand_lock.price_amplitude_raw, abs_i32(eval.relative_net_move_raw));
				cand_lock.strongest_leg_raw = abs_i32(eval.relative_net_move_raw);
				cand_lock.duration = eval.duration;
				if (!has_best || compare_trend_lock_priority(&cand_lock, &best_lock) < 0) {
					best_lock = cand_lock;
					has_best = 1;
				}
			}
			if (has_best) {
				locks[candidate_count] = best_lock;
				candidate_count += 1u;
			}
		}
		if (candidate_count > 1u) {
			qsort(locks, candidate_count, sizeof(*locks), compare_trend_lock_priority);
		}
		for (uint32_t i = 0u; i < candidate_count; ++i) {
			uint32_t c;
			int overlaps = 0;

			for (c = locks[i].start; c <= locks[i].end; ++c) {
				if (occupied[c]) {
					overlaps = 1;
					break;
				}
			}
			if (overlaps) {
				continue;
			}
			for (c = locks[i].start; c <= locks[i].end; ++c) {
				occupied[c] = 1u;
			}
			locks[lock_count] = locks[i];
			lock_count += 1u;
		}
	} else {
		uint32_t len;

		for (len = params->lmax_candles; len >= trend_lock_min_len(params); --len) {
			uint32_t start;

			if (len > candle_count) {
				if (len == 0u) {
					break;
				}
				continue;
			}
			for (start = 0u; start + len <= candle_count; ++start) {
				WaveSegmentEval eval;
				uint32_t end = start + len - 1u;
				uint32_t c;
				int overlaps = 0;

				for (c = start; c <= end; ++c) {
					if (occupied[c]) {
						overlaps = 1;
						break;
					}
				}
				if (overlaps) {
					continue;
				}
				compute_segment_eval(candles, features, candle_count, params, start, end, &eval);
				locks[lock_count].class_id = select_priority_class(&eval, params);
				if (locks[lock_count].class_id >= class_count_limit) {
					continue;
				}
				locks[lock_count].start = start;
				locks[lock_count].end = end;
				locks[lock_count].seed_leg_raw = abs_i32(eval.net_move_raw);
				locks[lock_count].strongest_leg_raw = abs_i32(eval.net_move_raw);
				lock_count += 1u;
				for (c = start; c <= end; ++c) {
					occupied[c] = 1u;
				}
				start = end;
			}
			if (len == 0u) {
				break;
			}
		}
	}
	*out_lock_count = lock_count;
	if (lock_count > 1u) {
		qsort(locks, lock_count, sizeof(*locks), compare_trend_lock_span);
	}
	free(occupied);
	return 0;
}

static int detect_seed_priority_spans(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t range_start,
	uint32_t range_end,
	uint32_t seed_pass,
	TrendLockSpan *locks,
	uint32_t *out_lock_count)
{
	uint8_t *occupied = NULL;
	uint32_t *best_end = NULL;
	int32_t *quality_raw = NULL;
	int32_t *net_move_raw = NULL;
	int32_t *price_amplitude_raw = NULL;
	int32_t *retracement_ratio = NULL;
	int32_t *overlap_ratio = NULL;
	int32_t *wick_to_body_ratio = NULL;
	int32_t *seed_leg_raw = NULL;
	int32_t *strongest_leg_raw = NULL;
	uint32_t *duration = NULL;
	uint32_t candidate_count = 0u;
	uint32_t lock_count = 0u;
	uint32_t range_len;
	uint32_t i;

	if (out_lock_count == NULL) {
		return -1;
	}
	*out_lock_count = 0u;
	if (candles == NULL || features == NULL || params == NULL || locks == NULL ||
		!is_relative_seeded_family((uint32_t)params->wave_model) ||
		range_end < range_start || range_end >= candle_count) {
		return 0;
	}
	range_len = range_end - range_start + 1u;
	occupied = (uint8_t *)calloc((size_t)range_len, sizeof(*occupied));
	best_end = (uint32_t *)malloc((size_t)range_len * sizeof(*best_end));
	quality_raw = (int32_t *)malloc((size_t)range_len * sizeof(*quality_raw));
	net_move_raw = (int32_t *)malloc((size_t)range_len * sizeof(*net_move_raw));
	price_amplitude_raw = (int32_t *)malloc((size_t)range_len * sizeof(*price_amplitude_raw));
	retracement_ratio = (int32_t *)malloc((size_t)range_len * sizeof(*retracement_ratio));
	overlap_ratio = (int32_t *)malloc((size_t)range_len * sizeof(*overlap_ratio));
	wick_to_body_ratio = (int32_t *)malloc((size_t)range_len * sizeof(*wick_to_body_ratio));
	seed_leg_raw = (int32_t *)malloc((size_t)range_len * sizeof(*seed_leg_raw));
	strongest_leg_raw = (int32_t *)malloc((size_t)range_len * sizeof(*strongest_leg_raw));
	duration = (uint32_t *)malloc((size_t)range_len * sizeof(*duration));
	if (occupied == NULL) {
		free(duration);
		free(strongest_leg_raw);
		free(wick_to_body_ratio);
		free(overlap_ratio);
		free(retracement_ratio);
		free(price_amplitude_raw);
		free(net_move_raw);
		free(quality_raw);
		free(best_end);
		return -1;
	}
	if (best_end == NULL || quality_raw == NULL || net_move_raw == NULL || price_amplitude_raw == NULL ||
		retracement_ratio == NULL || overlap_ratio == NULL || wick_to_body_ratio == NULL ||
		seed_leg_raw == NULL || strongest_leg_raw == NULL || duration == NULL) {
		free(duration);
		free(strongest_leg_raw);
		free(seed_leg_raw);
		free(wick_to_body_ratio);
		free(overlap_ratio);
		free(retracement_ratio);
		free(price_amplitude_raw);
		free(net_move_raw);
		free(quality_raw);
		free(best_end);
		free(occupied);
		return -1;
	}
	if (gpu_wave_seed_scan(candles + range_start,
		features + range_start,
		range_len,
		params,
		0u,
		range_len - 1u,
		seed_pass,
		best_end,
		quality_raw,
		net_move_raw,
		price_amplitude_raw,
		retracement_ratio,
		overlap_ratio,
		wick_to_body_ratio,
		strongest_leg_raw,
		duration) != 0) {
		free(duration);
		free(strongest_leg_raw);
		free(seed_leg_raw);
		free(wick_to_body_ratio);
		free(overlap_ratio);
		free(retracement_ratio);
		free(price_amplitude_raw);
		free(net_move_raw);
		free(quality_raw);
		free(best_end);
		free(occupied);
		return -2;
	}
	for (i = 0u; i < range_len; ++i) {
		uint32_t start = range_start + i;
		uint32_t end;

		if (best_end[i] == 0xffffffffu || duration[i] == 0u) {
			continue;
		}
		end = range_start + best_end[i];
		if (end < start || end > range_end) {
			continue;
		}
		memset(&locks[candidate_count], 0, sizeof(*locks));
		locks[candidate_count].start = start;
		locks[candidate_count].end = end;
		locks[candidate_count].seed_start = start;
		locks[candidate_count].seed_end = end;
		locks[candidate_count].seed_pass = seed_pass;
		locks[candidate_count].class_id = RELATIVE_CLASS_TREND;
		locks[candidate_count].quality_raw = quality_raw[i];
		locks[candidate_count].net_move_raw = net_move_raw[i];
		locks[candidate_count].price_amplitude_raw = price_amplitude_raw[i];
		locks[candidate_count].retracement_ratio = retracement_ratio[i];
		locks[candidate_count].overlap_ratio = overlap_ratio[i];
		locks[candidate_count].wick_to_body_ratio = wick_to_body_ratio[i];
		locks[candidate_count].seed_leg_raw = max_i32(price_amplitude_raw[i], abs_i32(net_move_raw[i]));
		locks[candidate_count].strongest_leg_raw = strongest_leg_raw[i];
		locks[candidate_count].duration = duration[i];
		candidate_count += 1u;
	}
	if (candidate_count > 1u) {
		qsort(locks, candidate_count, sizeof(*locks), compare_trend_lock_priority);
	}
	for (i = 0u; i < candidate_count; ++i) {
		uint32_t c;
		int overlaps = 0;

		for (c = locks[i].start; c <= locks[i].end; ++c) {
			if (occupied[c - range_start]) {
				overlaps = 1;
				break;
			}
		}
		if (overlaps) {
			continue;
		}
		for (c = locks[i].start; c <= locks[i].end; ++c) {
			occupied[c - range_start] = 1u;
		}
		locks[lock_count] = locks[i];
		lock_count += 1u;
	}
	free(duration);
	free(strongest_leg_raw);
	free(seed_leg_raw);
	free(wick_to_body_ratio);
	free(overlap_ratio);
	free(retracement_ratio);
	free(price_amplitude_raw);
	free(net_move_raw);
	free(quality_raw);
	free(best_end);
	free(occupied);
	if (lock_count > 1u) {
		qsort(locks, lock_count, sizeof(*locks), compare_trend_lock_span);
	}
	*out_lock_count = lock_count;
	return 0;
}

static int append_gpu_gap_tokens(const Candle *candles,
	const CandleFeat *features,
	const WaveParams *params,
	uint32_t gap_start,
	uint32_t gap_end,
	WaveToken *tokens,
	uint32_t *io_token_count,
	int demote_priority_classes)
{
	int32_t *prev_start = NULL;
	uint32_t *class_id = NULL;
	int32_t *fit_error = NULL;
	int32_t *net_move_raw = NULL;
	int32_t *retracement_ratio = NULL;
	int32_t *sign_change_density = NULL;
	int32_t *directional_purity_body = NULL;
	int32_t *range_growth_ratio = NULL;
	int32_t *context_range = NULL;
	uint32_t range_len;
	uint32_t local_count = 0;
	uint32_t write_base;
	uint32_t cursor;
	int64_t total_fit_error = 0;
	int rc;
	uint32_t class_count_limit;

	if (gap_end < gap_start) {
		return 0;
	}
	class_count_limit = wave_output_class_count(params);
	range_len = gap_end - gap_start + 1u;
	prev_start = (int32_t *)malloc((size_t)range_len * sizeof(*prev_start));
	class_id = (uint32_t *)malloc((size_t)range_len * sizeof(*class_id));
	fit_error = (int32_t *)malloc((size_t)range_len * sizeof(*fit_error));
	net_move_raw = (int32_t *)malloc((size_t)range_len * sizeof(*net_move_raw));
	retracement_ratio = (int32_t *)malloc((size_t)range_len * sizeof(*retracement_ratio));
	sign_change_density = (int32_t *)malloc((size_t)range_len * sizeof(*sign_change_density));
	directional_purity_body = (int32_t *)malloc((size_t)range_len * sizeof(*directional_purity_body));
	range_growth_ratio = (int32_t *)malloc((size_t)range_len * sizeof(*range_growth_ratio));
	context_range = (int32_t *)malloc((size_t)range_len * sizeof(*context_range));
	if (prev_start == NULL || class_id == NULL || fit_error == NULL || net_move_raw == NULL ||
		retracement_ratio == NULL || sign_change_density == NULL || directional_purity_body == NULL ||
		range_growth_ratio == NULL || context_range == NULL) {
		free(prev_start);
		free(class_id);
		free(fit_error);
		free(net_move_raw);
		free(retracement_ratio);
		free(sign_change_density);
		free(directional_purity_body);
		free(range_growth_ratio);
		free(context_range);
		return -1;
	}

	rc = gpu_wave_dp(candles + gap_start,
		features + gap_start,
		range_len,
		params,
		prev_start,
		class_id,
		fit_error,
		net_move_raw,
		retracement_ratio,
		sign_change_density,
		directional_purity_body,
		range_growth_ratio,
		context_range,
		&total_fit_error);
	(void)fit_error;
	(void)net_move_raw;
	(void)retracement_ratio;
	(void)sign_change_density;
	(void)directional_purity_body;
	(void)range_growth_ratio;
	(void)context_range;
	(void)total_fit_error;
	if (rc != 0) {
		free(prev_start);
		free(class_id);
		free(fit_error);
		free(net_move_raw);
		free(retracement_ratio);
		free(sign_change_density);
		free(directional_purity_body);
		free(range_growth_ratio);
		free(context_range);
		return -2;
	}

	write_base = *io_token_count;
	cursor = range_len - 1u;
	for (;;) {
		uint32_t start = (uint32_t)prev_start[cursor];
		uint32_t token_class = class_id[cursor];

		if (start > cursor || token_class >= class_count_limit || local_count >= range_len) {
			free(prev_start);
			free(class_id);
			free(fit_error);
			free(net_move_raw);
			free(retracement_ratio);
			free(sign_change_density);
			free(directional_purity_body);
			free(range_growth_ratio);
			free(context_range);
			return -3;
		}
		if (demote_priority_classes &&
			params->wave_model != WAVE_MODEL_PRIMITIVE) {
			if ((params->wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
				 is_relative_seeded_family((uint32_t)params->wave_model)) &&
				token_class == RELATIVE_CLASS_TREND) {
				token_class = RELATIVE_CLASS_RANGE;
			} else if (is_trend_class(token_class) || token_class == WAVE_CLASS_V_SHAPED_REVERSAL) {
				WaveSegmentEval eval;

				compute_segment_eval(candles,
					features,
					gap_end - gap_start + 1u,
					params,
					start,
					cursor,
					&eval);
				token_class = fallback_non_priority_class_for_eval(&eval);
			}
		}
		tokens[write_base + local_count].start_candle = gap_start + start;
		tokens[write_base + local_count].end_candle = gap_start + cursor;
		tokens[write_base + local_count].class_id = token_class;
		tokens[write_base + local_count].debug_flags = 0u;
		tokens[write_base + local_count].debug_reduction_flags = WAVE_TOKEN_REDUCTION_GPU_DP;
		tokens[write_base + local_count].debug_reduction_count = 1u;
		local_count += 1u;
		if (start == 0u) {
			break;
		}
		cursor = start - 1u;
	}
	for (cursor = 0u; cursor < local_count / 2u; ++cursor) {
		WaveToken tmp = tokens[write_base + cursor];

		tokens[write_base + cursor] = tokens[write_base + local_count - 1u - cursor];
		tokens[write_base + local_count - 1u - cursor] = tmp;
	}
	*io_token_count += local_count;

	free(prev_start);
	free(class_id);
	free(fit_error);
	free(net_move_raw);
	free(retracement_ratio);
	free(sign_change_density);
	free(directional_purity_body);
	free(range_growth_ratio);
	free(context_range);
	return 0;
}

static int primitive_counter_candle_validated(const Candle *candles,
	const CandleFeat *features,
	uint32_t prev_effective_index,
	uint32_t counter_index,
	uint32_t confirm_index,
	int sign)
{
	if (candles == NULL || features == NULL) {
		return 0;
	}
	if (prev_effective_index >= counter_index || counter_index >= confirm_index) {
		return 0;
	}
	if (sign > 0) {
		return features[counter_index].b < 0 &&
			features[confirm_index].b > 0 &&
			candles[counter_index].close_pips >= candles[prev_effective_index].low_pips &&
			candles[confirm_index].close_pips > candles[prev_effective_index].close_pips;
	}
	if (sign < 0) {
		return features[counter_index].b > 0 &&
			features[confirm_index].b < 0 &&
			candles[counter_index].close_pips <= candles[prev_effective_index].high_pips &&
			candles[confirm_index].close_pips < candles[prev_effective_index].close_pips;
	}
	return 0;
}

static int primitive_find_rule_run(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t scan_start,
	uint32_t *out_start,
	uint32_t *out_end,
	int *out_sign,
	uint32_t *out_effective_count)
{
	uint32_t start;
	uint32_t max_end;
	uint32_t c;
	uint32_t last_effective_index;
	uint32_t pending_counter = candle_count;
	uint32_t effective_count;
	int sign;

	if (candles == NULL || features == NULL || params == NULL ||
		out_start == NULL || out_end == NULL || out_sign == NULL || out_effective_count == NULL) {
		return 0;
	}
	start = scan_start;
	while (start < candle_count && effective_body_sign(candles, features, start) == 0) {
		start += 1u;
	}
	if (start >= candle_count) {
		return 0;
	}
	sign = effective_body_sign(candles, features, start);
	max_end = candle_count - 1u;
	if (params->lmax_candles > 0u && start + params->lmax_candles - 1u < max_end) {
		max_end = start + params->lmax_candles - 1u;
	}
	last_effective_index = start;
	effective_count = 1u;
	for (c = start + 1u; c <= max_end; ++c) {
		int body_sign = effective_body_sign(candles, features, c);

		if (body_sign == 0) {
			continue;
		}
		if (pending_counter != candle_count) {
			if (body_sign == sign &&
				primitive_counter_candle_validated(candles, features, last_effective_index, pending_counter, c, sign)) {
				last_effective_index = c;
				effective_count += 1u;
				pending_counter = candle_count;
				continue;
			}
			*out_start = start;
			*out_end = pending_counter - 1u;
			*out_sign = sign;
			*out_effective_count = effective_count;
			return 1;
		}
		if (body_sign == sign) {
			last_effective_index = c;
			effective_count += 1u;
			continue;
		}
		pending_counter = c;
	}
	*out_start = start;
	if (pending_counter != candle_count) {
		*out_end = pending_counter - 1u;
	} else {
		*out_end = max_end;
	}
	*out_sign = sign;
	*out_effective_count = effective_count;
	return 1;
}

static int primitive_find_next_hard_consecutive_span(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t scan_start,
	uint32_t *out_start,
	uint32_t *out_end)
{
	uint32_t candidate;

	if (candles == NULL || features == NULL || params == NULL || out_start == NULL || out_end == NULL) {
		return 0;
	}
	for (candidate = scan_start; candidate < candle_count; ++candidate) {
		uint32_t run_start;
		uint32_t run_end;
		uint32_t effective_count;
		int sign;

		if (effective_body_sign(candles, features, candidate) == 0) {
			continue;
		}
		if (!primitive_find_rule_run(candles,
			features,
			candle_count,
			params,
			candidate,
			&run_start,
			&run_end,
			&sign,
			&effective_count)) {
			break;
		}
		(void)sign;
		if (run_start != candidate || run_end < run_start || effective_count < 3u) {
			continue;
		}
		if (!primitive_span_is_strict_consecutive(candles, features, candle_count, params, run_start, run_end, NULL)) {
			continue;
		}
		*out_start = run_start;
		*out_end = run_end;
		return 1;
	}
	return 0;
}

static int primitive_span_is_strict_consecutive(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t start,
	uint32_t end,
	int *out_sign)
{
	uint32_t effective_start = start;
	uint32_t effective_end = end;
	uint32_t run_start;
	uint32_t run_end;
	uint32_t effective_count;
	uint32_t same_prefix_count = 0u;
	uint32_t c;
	int sign;

	if (candles == NULL || features == NULL || params == NULL || end < start) {
		return 0;
	}
	while (effective_start <= end && effective_body_sign(candles, features, effective_start) == 0) {
		effective_start += 1u;
	}
	if (effective_start > end) {
		return 0;
	}
	while (effective_end > effective_start && effective_body_sign(candles, features, effective_end) == 0) {
		effective_end -= 1u;
	}
	if (!primitive_find_rule_run(candles,
		features,
		candle_count,
		params,
		effective_start,
		&run_start,
		&run_end,
		&sign,
		&effective_count)) {
		return 0;
	}
	if (run_start != effective_start || run_end != effective_end || effective_count < 3u) {
		return 0;
	}
	for (c = start; c < effective_start; ++c) {
		if (features[c].b != 0 && features[c].b * sign < 0) {
			return 0;
		}
	}
	for (c = effective_end + 1u; c <= end; ++c) {
		if (features[c].b != 0 && features[c].b * sign < 0) {
			return 0;
		}
	}
	for (c = effective_start; c <= effective_end; ++c) {
		int body_sign = effective_body_sign(candles, features, c);

		if (body_sign == 0) {
			continue;
		}
		if (body_sign == sign) {
			same_prefix_count += 1u;
			continue;
		}
		if (same_prefix_count < 3u) {
			return 0;
		}
		break;
	}
	if (out_sign != NULL) {
		*out_sign = sign;
	}
	return 1;
}

static int build_primitive_rule_first_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count)
{
	uint32_t token_count = 0;
	uint32_t cursor = 0u;
	int rc;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || out_token_count == NULL) {
		return -1;
	}
	*out_token_count = 0u;
	while (cursor < candle_count) {
		uint32_t run_start;
		uint32_t run_end;

		if (!primitive_find_next_hard_consecutive_span(candles,
			features,
			candle_count,
			params,
			cursor,
			&run_start,
			&run_end)) {
			rc = append_gpu_gap_tokens(candles, features, params, cursor, candle_count - 1u, tokens, &token_count, 0);
			if (rc != 0) {
				return rc;
			}
			break;
		}
		if (run_start > cursor) {
			rc = append_gpu_gap_tokens(candles, features, params, cursor, run_start - 1u, tokens, &token_count, 0);
			if (rc != 0) {
				return rc;
			}
		}
		{
			WaveSegmentEval eval;

			compute_segment_eval(candles, features, candle_count, params, run_start, run_end, &eval);
			fill_token_from_eval(&tokens[token_count], &eval, PRIMITIVE_CLASS_CONSECUTIVE, params);
			mark_token_reduction(&tokens[token_count], WAVE_TOKEN_REDUCTION_RULE_FIRST);
			token_count += 1u;
		}
		cursor = run_end + 1u;
	}
	if (token_count > 1u) {
		qsort(tokens, token_count, sizeof(*tokens), compare_wave_token_start);
	}
	if (validate_token_sequence(tokens, token_count, candle_count) != 0) {
		log_invalid_token_sequence("build_layered_layer1_tokens", tokens, token_count, candle_count);
		return -2;
	}
	*out_token_count = token_count;
	return 0;
}

static int build_relative_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count)
{
	return append_gpu_gap_tokens(candles, features, params, 0u, candle_count - 1u, tokens, out_token_count, 0);
}

static int build_relative_two_stage_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count)
{
	TrendLockSpan *locks = NULL;
	uint32_t lock_count = 0u;
	uint32_t lock_index;
	uint32_t cursor = 0u;
	uint32_t token_count = 0u;
	int rc;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || out_token_count == NULL) {
		return -1;
	}
	*out_token_count = 0u;
	if (candle_count == 0u) {
		return 0;
	}

	locks = (TrendLockSpan *)malloc((size_t)candle_count * sizeof(*locks));
	if (locks == NULL) {
		return -1;
	}
	rc = detect_priority_spans(candles, features, candle_count, params, locks, &lock_count);
	if (rc != 0) {
		free(locks);
		return rc;
	}
	if (lock_count == 0u) {
		free(locks);
		rc = append_gpu_gap_tokens(candles, features, params, 0u, candle_count - 1u, tokens, out_token_count, 1);
		if (rc != 0) {
			trainer_log_error("build_relative_seeded_tokens full gap fill failed rc=%d", rc);
		}
		return rc;
	}

	for (lock_index = 0u; lock_index < lock_count; ++lock_index) {
		if (cursor < locks[lock_index].start) {
			rc = append_gpu_gap_tokens(candles,
				features,
				params,
				cursor,
				locks[lock_index].start - 1u,
				tokens,
				&token_count,
				1);
			if (rc != 0) {
				free(locks);
				return rc;
			}
		}
		tokens[token_count].start_candle = locks[lock_index].start;
		tokens[token_count].end_candle = locks[lock_index].end;
		tokens[token_count].seed_start_candle = locks[lock_index].seed_start;
		tokens[token_count].seed_end_candle = locks[lock_index].seed_end;
		tokens[token_count].class_id = RELATIVE_CLASS_TREND;
		tokens[token_count].debug_flags = 0u;
		tokens[token_count].debug_reduction_flags = WAVE_TOKEN_REDUCTION_RULE_FIRST;
		tokens[token_count].debug_reduction_count = 1u;
		token_count += 1u;
		cursor = locks[lock_index].end + 1u;
	}
	if (cursor < candle_count) {
		rc = append_gpu_gap_tokens(candles, features, params, cursor, candle_count - 1u, tokens, &token_count, 1);
		if (rc != 0) {
			free(locks);
			return rc;
		}
	}

	free(locks);
	if (token_count > 1u) {
		qsort(tokens, token_count, sizeof(*tokens), compare_wave_token_start);
	}
	if (validate_token_sequence(tokens, token_count, candle_count) != 0) {
		log_invalid_token_sequence("build_relative_seeded_tokens", tokens, token_count, candle_count);
		return -2;
	}
	*out_token_count = token_count;
	return 0;
}

static int merge_seeded_trend_locks(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	TrendLockSpan *locks,
	uint32_t *io_lock_count)
{
	uint32_t lock_count;
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || locks == NULL || io_lock_count == NULL) {
		return -1;
	}
	lock_count = *io_lock_count;
	if (lock_count < 2u) {
		return 0;
	}
	for (i = 0u; i + 1u < lock_count; ++i) {
		WaveSegmentEval left_eval;
		WaveSegmentEval right_eval;
		WaveSegmentEval merged_eval;
		WaveSegmentEval gap_eval;
		uint32_t gap_start = locks[i].end + 1u;
		uint32_t gap_end = locks[i + 1u].start - 1u;
		int32_t strongest_leg_raw;
		int32_t allowed_gap_move;
		int32_t allowed_gap_body;
		int32_t allowed_quality;
		int32_t left_allowed_pullback;
		int32_t right_allowed_pullback;
		int left_seed_preserved;
		int right_seed_preserved;
		int merge_progress_ok;
		int recovery_gap_bridge;

		compute_segment_eval(candles, features, candle_count, params, locks[i].start, locks[i].end, &left_eval);
		compute_segment_eval(candles, features, candle_count, params, locks[i + 1u].start, locks[i + 1u].end, &right_eval);
		if (left_eval.dominant_direction == 0 ||
			left_eval.dominant_direction != right_eval.dominant_direction) {
			continue;
		}
		strongest_leg_raw = max_i32(abs_i32(left_eval.relative_net_move_raw), abs_i32(right_eval.relative_net_move_raw));
		if (strongest_leg_raw <= 0) {
			continue;
		}
		left_allowed_pullback = max_i32((max_i32(locks[i].seed_leg_raw, 1) * params->relative_trend_retracement_max) / params->norm_scale, 1);
		right_allowed_pullback = max_i32((max_i32(locks[i + 1u].seed_leg_raw, 1) * params->relative_trend_retracement_max) / params->norm_scale, 1);
		allowed_gap_move = max_i32((strongest_leg_raw * params->relative_trend_retracement_max) / params->norm_scale, 1);
		allowed_gap_body = max_i32((strongest_leg_raw * 3) / 2, 2);
		allowed_quality = max_i32(left_eval.relative_trend_quality_raw, right_eval.relative_trend_quality_raw) + 320;
		if (gap_start <= gap_end) {
			compute_segment_eval(candles, features, candle_count, params, gap_start, gap_end, &gap_eval);
			recovery_gap_bridge =
				(gap_eval.duration == 2u &&
				features[gap_start].b * left_eval.dominant_direction < 0 &&
				features[gap_end].b * left_eval.dominant_direction < 0);
			if (abs_i32(gap_eval.relative_net_move_raw) > allowed_gap_move ||
				gap_eval.relative_abs_body_sum_raw > max_i32(allowed_gap_body * 2, 2) ||
				gap_eval.relative_overlap_ratio > params->relative_trend_overlap_max + 260 ||
				gap_eval.relative_body_direction_share < 350 ||
				(!recovery_gap_bridge &&
				 gap_eval.relative_directional_efficiency_step > params->relative_trend_der_step_min + 120) ||
				gap_eval.relative_avg_body_efficiency > params->relative_trend_body_eff_min + 160) {
				continue;
			}
		} else {
			recovery_gap_bridge = 0;
		}
		compute_segment_eval(candles, features, candle_count, params, locks[i].start, locks[i + 1u].end, &merged_eval);
		left_seed_preserved =
			relative_span_preserves_seed_leg(candles, features, locks[i].start, locks[i + 1u].end,
				locks[i].seed_start, locks[i].seed_end, left_eval.dominant_direction, left_allowed_pullback) ||
			relative_span_preserves_seed_leg_body(candles, features, locks[i].start, locks[i + 1u].end,
				locks[i].seed_start, locks[i].seed_end, left_eval.dominant_direction, left_allowed_pullback);
		right_seed_preserved =
			relative_span_preserves_seed_leg(candles, features, locks[i].start, locks[i + 1u].end,
				locks[i + 1u].seed_start, locks[i + 1u].seed_end, right_eval.dominant_direction, right_allowed_pullback) ||
			relative_span_preserves_seed_leg_body(candles, features, locks[i].start, locks[i + 1u].end,
				locks[i + 1u].seed_start, locks[i + 1u].seed_end, right_eval.dominant_direction, right_allowed_pullback);
		merge_progress_ok =
			(merged_eval.best_class == RELATIVE_CLASS_TREND ||
				(abs_i32(merged_eval.relative_net_move_raw) >= max_i32(params->minimum_pips_for_trend * 6, 50) &&
				merged_eval.relative_directional_efficiency_step >= max_i32(params->relative_trend_der_step_min - 260, params->norm_scale / 6) &&
				merged_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 260, params->norm_scale / 6) &&
				merged_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 220, params->norm_scale / 6) &&
				merged_eval.relative_body_direction_share >= 520));
		if (!merge_progress_ok ||
			merged_eval.dominant_direction != left_eval.dominant_direction ||
			!left_seed_preserved ||
			!right_seed_preserved ||
			!relative_span_has_consistent_edges(candles, features, locks[i].start, locks[i + 1u].end, merged_eval.dominant_direction) ||
			merged_eval.relative_trend_quality_raw > allowed_quality ||
			merged_eval.relative_retracement_ratio > min_i32(params->relative_trend_retracement_max + (recovery_gap_bridge ? 260 : 220), params->norm_scale) ||
			merged_eval.relative_overlap_ratio > params->relative_trend_overlap_max + 220 ||
			merged_eval.relative_directional_efficiency_step < max_i32(params->relative_trend_der_step_min - 260, params->norm_scale / 6)) {
			continue;
		}
		locks[i].start = locks[i].start;
		locks[i].end = locks[i + 1u].end;
		locks[i].seed_start = locks[i].seed_start;
		locks[i].seed_end = locks[i].seed_end;
		locks[i].seed_pass = min_i32((int32_t)locks[i].seed_pass, (int32_t)locks[i + 1u].seed_pass);
		locks[i].class_id = RELATIVE_CLASS_TREND;
		locks[i].quality_raw = merged_eval.relative_trend_quality_raw;
		locks[i].net_move_raw = merged_eval.relative_net_move_raw;
		locks[i].price_amplitude_raw = compute_relative_trend_price_amplitude(candles,
			features,
			locks[i].start,
			locks[i + 1u].end,
			merged_eval.dominant_direction,
			NULL,
			NULL);
		locks[i].retracement_ratio = merged_eval.relative_retracement_ratio;
		locks[i].overlap_ratio = merged_eval.relative_overlap_ratio;
		locks[i].wick_to_body_ratio = merged_eval.relative_wick_to_body_ratio;
		locks[i].seed_leg_raw = max_i32(locks[i].seed_leg_raw, locks[i + 1u].seed_leg_raw);
		locks[i].strongest_leg_raw = max_i32(locks[i].strongest_leg_raw, locks[i + 1u].strongest_leg_raw);
		locks[i].duration = merged_eval.duration;
		for (uint32_t j = i + 1u; j + 1u < lock_count; ++j) {
			locks[j] = locks[j + 1u];
		}
		lock_count -= 1u;
		changed = 1;
		i = 0u;
		if (lock_count < 2u) {
			break;
		}
	}
	*io_lock_count = lock_count;
	return changed;
}

static int absorb_adjacent_one_candle_same_direction_ranges(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return 0;
	}
	for (i = 0u; i + 1u < *count; ++i) {
		WaveSegmentEval range_eval;
		WaveSegmentEval trend_eval;
		int32_t allowed_pullback;

		if (tokens[i].class_id != RELATIVE_CLASS_RANGE ||
			tokens[i + 1u].class_id != RELATIVE_CLASS_TREND ||
			(tokens[i].end_candle - tokens[i].start_candle + 1u) > 2u ||
			tokens[i].end_candle + 1u != tokens[i + 1u].start_candle) {
			continue;
		}
		compute_segment_eval(candles, features, candle_count, params,
			tokens[i].start_candle, tokens[i].end_candle, &range_eval);
		compute_segment_eval(candles, features, candle_count, params,
			tokens[i + 1u].start_candle, tokens[i + 1u].end_candle, &trend_eval);
		if (trend_eval.dominant_direction == 0 ||
			range_eval.dominant_direction != trend_eval.dominant_direction ||
			features[tokens[i].start_candle].b * trend_eval.dominant_direction <= 0) {
			continue;
		}
		allowed_pullback = max_i32((max_i32(compute_relative_trend_price_amplitude(candles,
			features,
			tokens[i + 1u].seed_start_candle,
			tokens[i + 1u].seed_end_candle,
			trend_eval.dominant_direction,
			NULL,
			NULL), 1) * params->relative_trend_retracement_max) / params->norm_scale, 1);
		if (!relative_span_preserves_seed_leg_body(candles,
			features,
			tokens[i].start_candle,
			tokens[i + 1u].end_candle,
			tokens[i + 1u].seed_start_candle,
			tokens[i + 1u].seed_end_candle,
			trend_eval.dominant_direction,
			allowed_pullback)) {
			continue;
		}
		tokens[i + 1u].start_candle = tokens[i].start_candle;
		for (uint32_t j = i; j + 1u < *count; ++j) {
			tokens[j] = tokens[j + 1u];
		}
		*count -= 1u;
		changed = 1;
		if (*count < 2u) {
			break;
		}
		i = 0u;
	}
	return changed;
}

static int grow_seeded_trend_locks(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t allowed_start,
	uint32_t allowed_end,
	TrendLockSpan *locks,
	uint32_t lock_count)
{
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || locks == NULL) {
		return -1;
	}
	for (i = 0u; i < lock_count; ++i) {
		uint32_t left_limit = (i > 0u) ? (locks[i - 1u].end + 1u) : allowed_start;
		uint32_t right_limit = (i + 1u < lock_count) ? (locks[i + 1u].start - 1u) : allowed_end;
		uint32_t growth_window = max_i32((int32_t)params->lmax_candles, 24);
			int local_changed;
			int dominant_direction = sign_i32(locks[i].net_move_raw);
			int32_t seed_leg_raw = max_i32(locks[i].seed_leg_raw, 1);

		do {
			TrendLockSpan best_lock = locks[i];
			int has_better = 0;
			uint32_t candidate_start;
			uint32_t candidate_end;
			int32_t allowed_pullback;
			int32_t allowed_quality;
			int32_t allowed_overlap;
			int32_t allowed_wick;
			int32_t min_step_eff;
			int32_t min_body_eff;
			int32_t allowed_bridge_move;

			local_changed = 0;
			allowed_pullback = max_i32((seed_leg_raw * params->relative_trend_retracement_max) / params->norm_scale, 1);
			allowed_quality = locks[i].quality_raw + 320;
			allowed_overlap = params->relative_trend_overlap_max + 180;
			allowed_wick = params->norm_scale + (params->norm_scale / 2);
			min_step_eff = max_i32(params->relative_trend_der_step_min - 260, params->norm_scale / 5);
			min_body_eff = max_i32(params->relative_trend_body_eff_min - 180, params->norm_scale / 4);
			allowed_bridge_move = max_i32(allowed_pullback + (seed_leg_raw / 3), 2);

			for (candidate_start = locks[i].start;
				candidate_start > left_limit &&
				locks[i].start - candidate_start < growth_window;
				--candidate_start) {
				WaveSegmentEval merged_eval;
				WaveSegmentEval add_eval;
				TrendLockSpan cand_lock;
				uint32_t new_start = candidate_start - 1u;
				int same_direction_single_candle = 0;
				int aligned_direction_block = 0;
				int recovery_bridge = 0;
				int delayed_recovery_bridge = 0;
				int continuation_block = 0;
				int seed_continuation_block = 0;
				int seed_progress_block = 0;
				int continuation_extension_ok = 0;
				int seed_preserved = 0;
				int seed_preserved_body = 0;

				compute_segment_eval(candles, features, candle_count, params, new_start, locks[i].end, &merged_eval);
				compute_segment_eval(candles, features, candle_count, params, new_start, locks[i].start - 1u, &add_eval);
				same_direction_single_candle =
					(add_eval.duration == 1u &&
					add_eval.dominant_direction == dominant_direction &&
					features[new_start].b * dominant_direction > 0);
				aligned_direction_block =
					(add_eval.dominant_direction == dominant_direction &&
					add_eval.relative_body_direction_share >= 900 &&
					add_eval.sign_change_density <= 120 &&
					add_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 80 &&
					add_eval.relative_directional_efficiency_step >= params->relative_range_der_step_max);
				recovery_bridge =
					(add_eval.duration == 2u &&
					features[new_start].b * dominant_direction > 0 &&
					features[locks[i].start - 1u].b * dominant_direction < 0);
				delayed_recovery_bridge =
					(add_eval.duration >= 3u &&
					add_eval.dominant_direction == dominant_direction &&
					relative_block_has_short_counter_run_then_resume(features,
						new_start,
						locks[i].start - 1u,
						dominant_direction,
						0));
				seed_preserved = relative_span_preserves_seed_leg(candles, features, new_start, locks[i].end,
					locks[i].seed_start, locks[i].seed_end, dominant_direction, allowed_pullback);
				seed_preserved_body = relative_span_preserves_seed_leg_body(candles, features, new_start, locks[i].end,
					locks[i].seed_start, locks[i].seed_end, dominant_direction, allowed_pullback);
				if (!seed_preserved && (delayed_recovery_bridge || recovery_bridge)) {
					seed_preserved = seed_preserved_body;
				}
				continuation_block =
					(add_eval.best_class == RELATIVE_CLASS_TREND &&
					add_eval.dominant_direction == dominant_direction &&
					abs_i32(add_eval.relative_net_move_raw) >= max_i32(params->minimum_pips_for_trend * 3, 20) &&
					add_eval.relative_body_direction_share >= 700 &&
					add_eval.sign_change_density <= params->relative_trend_sign_flip_max &&
					add_eval.relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max + 120, params->norm_scale) &&
					add_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 120);
				seed_continuation_block =
					(add_eval.dominant_direction == dominant_direction &&
					abs_i32(add_eval.relative_net_move_raw) >= max_i32(params->minimum_pips_for_trend * 3, 20) &&
					seed_preserved_body &&
					add_eval.relative_directional_efficiency_step >= max_i32(params->relative_range_der_step_max + 120, params->relative_trend_der_step_min - 120) &&
					add_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 140, params->norm_scale / 3) &&
					add_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 80, params->relative_range_body_eff_max) &&
					add_eval.relative_body_direction_share >= 700 &&
					add_eval.sign_change_density <= max_i32(params->relative_trend_sign_flip_max - 120, 0) &&
					add_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 80 &&
					merged_eval.best_class == RELATIVE_CLASS_TREND &&
					merged_eval.dominant_direction == dominant_direction &&
					relative_span_has_consistent_edges(candles, features, new_start, locks[i].end, merged_eval.dominant_direction) &&
					merged_eval.relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max + 160, params->norm_scale) &&
					merged_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 220 &&
					merged_eval.relative_wick_to_body_ratio <= allowed_wick + params->norm_scale &&
					merged_eval.relative_directional_efficiency_step >= max_i32(params->relative_trend_der_step_min - 220, params->norm_scale / 5) &&
					merged_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 220, params->norm_scale / 5) &&
					merged_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 180, params->norm_scale / 5) &&
					merged_eval.relative_body_direction_share >= 560);
				seed_progress_block =
					(add_eval.dominant_direction == dominant_direction &&
					abs_i32(add_eval.relative_net_move_raw) >= max_i32(params->minimum_pips_for_trend * 5, 40) &&
					seed_preserved_body &&
					add_eval.relative_body_direction_share >= 560 &&
					add_eval.sign_change_density <= params->relative_trend_sign_flip_max + 180 &&
					add_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 240 &&
					merged_eval.best_class == RELATIVE_CLASS_TREND &&
					merged_eval.dominant_direction == dominant_direction &&
					relative_span_has_consistent_edges(candles, features, new_start, locks[i].end, merged_eval.dominant_direction) &&
					merged_eval.relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max + 220, params->norm_scale) &&
					merged_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 260 &&
					merged_eval.relative_wick_to_body_ratio <= allowed_wick + params->norm_scale &&
					merged_eval.relative_directional_efficiency_step >= max_i32(params->relative_trend_der_step_min - 260, params->norm_scale / 6) &&
					merged_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 260, params->norm_scale / 6) &&
					merged_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 220, params->norm_scale / 6) &&
					merged_eval.relative_body_direction_share >= 520);
				continuation_extension_ok =
					(continuation_block || seed_continuation_block || seed_progress_block || aligned_direction_block) &&
					merged_eval.best_class == RELATIVE_CLASS_TREND &&
					merged_eval.dominant_direction == dominant_direction &&
					relative_span_has_consistent_edges(candles, features, new_start, locks[i].end, merged_eval.dominant_direction) &&
					(seed_preserved || ((same_direction_single_candle || aligned_direction_block || recovery_bridge || seed_continuation_block || seed_progress_block) && seed_preserved_body)) &&
					merged_eval.relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max + 120, params->norm_scale) &&
					merged_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 160 &&
					merged_eval.relative_wick_to_body_ratio <= allowed_wick + (params->norm_scale / 2) &&
					merged_eval.relative_directional_efficiency_step >= max_i32(params->relative_trend_der_step_min - 160, params->norm_scale / 4) &&
					merged_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 160, params->norm_scale / 4) &&
					merged_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 120, params->norm_scale / 4) &&
					merged_eval.relative_body_direction_share >= 650 &&
					merged_eval.relative_max_pullback_raw <= allowed_pullback + max_i32(locks[i].strongest_leg_raw / 2, 1);
				if (!continuation_extension_ok &&
					(merged_eval.best_class != RELATIVE_CLASS_TREND ||
					merged_eval.dominant_direction != dominant_direction ||
					!relative_span_has_consistent_edges(candles, features, new_start, locks[i].end, merged_eval.dominant_direction) ||
					(!(seed_preserved || ((same_direction_single_candle || aligned_direction_block || recovery_bridge || seed_continuation_block || seed_progress_block) && seed_preserved_body))) ||
					(!delayed_recovery_bridge &&
					 merged_eval.relative_max_pullback_raw > allowed_pullback) ||
					merged_eval.relative_trend_quality_raw > allowed_quality ||
					merged_eval.relative_overlap_ratio > allowed_overlap ||
					merged_eval.relative_wick_to_body_ratio > allowed_wick ||
					merged_eval.relative_directional_efficiency_step < min_step_eff ||
					merged_eval.relative_avg_body_efficiency < min_body_eff ||
					(!(same_direction_single_candle || aligned_direction_block || recovery_bridge || delayed_recovery_bridge || continuation_block || seed_continuation_block || seed_progress_block) &&
					 abs_i32(add_eval.relative_net_move_raw) > allowed_bridge_move) ||
					(!delayed_recovery_bridge &&
					 add_eval.relative_max_pullback_raw > allowed_pullback) ||
					(!(continuation_block || seed_continuation_block || seed_progress_block || aligned_direction_block || delayed_recovery_bridge) &&
					 add_eval.relative_overlap_ratio > params->relative_range_overlap_min + 220))) {
					continue;
				}
				cand_lock.start = new_start;
				cand_lock.end = locks[i].end;
				cand_lock.seed_start = locks[i].seed_start;
				cand_lock.seed_end = locks[i].seed_end;
				cand_lock.seed_pass = locks[i].seed_pass;
				cand_lock.class_id = RELATIVE_CLASS_TREND;
				cand_lock.quality_raw = merged_eval.relative_trend_quality_raw;
				cand_lock.net_move_raw = merged_eval.relative_net_move_raw;
				cand_lock.price_amplitude_raw = compute_relative_trend_price_amplitude(candles,
					features,
					new_start,
					locks[i].end,
					merged_eval.dominant_direction,
					NULL,
					NULL);
				if (cand_lock.price_amplitude_raw <= locks[i].price_amplitude_raw) {
					continue;
				}
				cand_lock.retracement_ratio = merged_eval.relative_retracement_ratio;
				cand_lock.overlap_ratio = merged_eval.relative_overlap_ratio;
				cand_lock.wick_to_body_ratio = merged_eval.relative_wick_to_body_ratio;
				cand_lock.seed_leg_raw = locks[i].seed_leg_raw;
				cand_lock.strongest_leg_raw = max_i32(locks[i].strongest_leg_raw,
					(add_eval.best_class == RELATIVE_CLASS_TREND &&
					 add_eval.dominant_direction == merged_eval.dominant_direction)
						? abs_i32(add_eval.relative_net_move_raw)
						: 0);
				cand_lock.duration = merged_eval.duration;
				if (!has_better || compare_seeded_growth_candidate(&cand_lock, &best_lock) < 0) {
					best_lock = cand_lock;
					has_better = 1;
				}
			}

			for (candidate_end = locks[i].end;
				candidate_end < right_limit &&
				candidate_end - locks[i].end < growth_window;
				++candidate_end) {
				WaveSegmentEval merged_eval;
				WaveSegmentEval add_eval;
				TrendLockSpan cand_lock;
				uint32_t new_end = candidate_end + 1u;
				int same_direction_single_candle = 0;
				int aligned_direction_block = 0;
				int recovery_bridge = 0;
				int delayed_recovery_bridge = 0;
				int continuation_block = 0;
				int seed_continuation_block = 0;
				int seed_progress_block = 0;
				int continuation_extension_ok = 0;
				int seed_preserved = 0;
				int seed_preserved_body = 0;

				compute_segment_eval(candles, features, candle_count, params, locks[i].start, new_end, &merged_eval);
				compute_segment_eval(candles, features, candle_count, params, locks[i].end + 1u, new_end, &add_eval);
				same_direction_single_candle =
					(add_eval.duration == 1u &&
					add_eval.dominant_direction == dominant_direction &&
					features[new_end].b * dominant_direction > 0);
				aligned_direction_block =
					(add_eval.dominant_direction == dominant_direction &&
					add_eval.relative_body_direction_share >= 900 &&
					add_eval.sign_change_density <= 120 &&
					add_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 80 &&
					add_eval.relative_directional_efficiency_step >= params->relative_range_der_step_max);
				recovery_bridge =
					(add_eval.duration == 2u &&
					features[locks[i].end + 1u].b * dominant_direction < 0 &&
					features[new_end].b * dominant_direction > 0);
				delayed_recovery_bridge =
					(add_eval.duration >= 3u &&
					add_eval.dominant_direction == dominant_direction &&
					relative_block_has_short_counter_run_then_resume(features,
						locks[i].end + 1u,
						new_end,
						dominant_direction,
						1));
				seed_preserved = relative_span_preserves_seed_leg(candles, features, locks[i].start, new_end,
					locks[i].seed_start, locks[i].seed_end, dominant_direction, allowed_pullback);
				seed_preserved_body = relative_span_preserves_seed_leg_body(candles, features, locks[i].start, new_end,
					locks[i].seed_start, locks[i].seed_end, dominant_direction, allowed_pullback);
				if (!seed_preserved && (delayed_recovery_bridge || recovery_bridge)) {
					seed_preserved = seed_preserved_body;
				}
				continuation_block =
					(add_eval.best_class == RELATIVE_CLASS_TREND &&
					add_eval.dominant_direction == dominant_direction &&
					abs_i32(add_eval.relative_net_move_raw) >= max_i32(params->minimum_pips_for_trend * 3, 20) &&
					add_eval.relative_body_direction_share >= 700 &&
					add_eval.sign_change_density <= params->relative_trend_sign_flip_max &&
					add_eval.relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max + 120, params->norm_scale) &&
					add_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 120);
				seed_continuation_block =
					(add_eval.dominant_direction == dominant_direction &&
					abs_i32(add_eval.relative_net_move_raw) >= max_i32(params->minimum_pips_for_trend * 3, 20) &&
					seed_preserved_body &&
					add_eval.relative_directional_efficiency_step >= max_i32(params->relative_range_der_step_max + 120, params->relative_trend_der_step_min - 120) &&
					add_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 140, params->norm_scale / 3) &&
					add_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 80, params->relative_range_body_eff_max) &&
					add_eval.relative_body_direction_share >= 700 &&
					add_eval.sign_change_density <= max_i32(params->relative_trend_sign_flip_max - 120, 0) &&
					add_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 80 &&
					merged_eval.best_class == RELATIVE_CLASS_TREND &&
					merged_eval.dominant_direction == dominant_direction &&
					relative_span_has_consistent_edges(candles, features, locks[i].start, new_end, merged_eval.dominant_direction) &&
					merged_eval.relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max + 160, params->norm_scale) &&
					merged_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 220 &&
					merged_eval.relative_wick_to_body_ratio <= allowed_wick + params->norm_scale &&
					merged_eval.relative_directional_efficiency_step >= max_i32(params->relative_trend_der_step_min - 220, params->norm_scale / 5) &&
					merged_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 220, params->norm_scale / 5) &&
					merged_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 180, params->norm_scale / 5) &&
					merged_eval.relative_body_direction_share >= 560);
				seed_progress_block =
					(add_eval.dominant_direction == dominant_direction &&
					abs_i32(add_eval.relative_net_move_raw) >= max_i32(params->minimum_pips_for_trend * 5, 40) &&
					seed_preserved_body &&
					add_eval.relative_body_direction_share >= 560 &&
					add_eval.sign_change_density <= params->relative_trend_sign_flip_max + 180 &&
					add_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 240 &&
					merged_eval.best_class == RELATIVE_CLASS_TREND &&
					merged_eval.dominant_direction == dominant_direction &&
					relative_span_has_consistent_edges(candles, features, locks[i].start, new_end, merged_eval.dominant_direction) &&
					merged_eval.relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max + 220, params->norm_scale) &&
					merged_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 260 &&
					merged_eval.relative_wick_to_body_ratio <= allowed_wick + params->norm_scale &&
					merged_eval.relative_directional_efficiency_step >= max_i32(params->relative_trend_der_step_min - 260, params->norm_scale / 6) &&
					merged_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 260, params->norm_scale / 6) &&
					merged_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 220, params->norm_scale / 6) &&
					merged_eval.relative_body_direction_share >= 520);
				continuation_extension_ok =
					(continuation_block || seed_continuation_block || seed_progress_block || aligned_direction_block) &&
					merged_eval.best_class == RELATIVE_CLASS_TREND &&
					merged_eval.dominant_direction == dominant_direction &&
					relative_span_has_consistent_edges(candles, features, locks[i].start, new_end, merged_eval.dominant_direction) &&
					(seed_preserved || ((same_direction_single_candle || aligned_direction_block || recovery_bridge || seed_continuation_block || seed_progress_block) && seed_preserved_body)) &&
					merged_eval.relative_retracement_ratio <= min_i32(params->relative_trend_retracement_max + 120, params->norm_scale) &&
					merged_eval.relative_overlap_ratio <= params->relative_trend_overlap_max + 160 &&
					merged_eval.relative_wick_to_body_ratio <= allowed_wick + (params->norm_scale / 2) &&
					merged_eval.relative_directional_efficiency_step >= max_i32(params->relative_trend_der_step_min - 160, params->norm_scale / 4) &&
					merged_eval.relative_directional_efficiency_body >= max_i32(params->relative_trend_der_body_min - 160, params->norm_scale / 4) &&
					merged_eval.relative_avg_body_efficiency >= max_i32(params->relative_trend_body_eff_min - 120, params->norm_scale / 4) &&
					merged_eval.relative_body_direction_share >= 650 &&
					merged_eval.relative_max_pullback_raw <= allowed_pullback + max_i32(locks[i].strongest_leg_raw / 2, 1);
				if (!continuation_extension_ok &&
					(merged_eval.best_class != RELATIVE_CLASS_TREND ||
					merged_eval.dominant_direction != dominant_direction ||
					!relative_span_has_consistent_edges(candles, features, locks[i].start, new_end, merged_eval.dominant_direction) ||
					(!(seed_preserved || ((same_direction_single_candle || aligned_direction_block || recovery_bridge || seed_continuation_block || seed_progress_block) && seed_preserved_body))) ||
					(!delayed_recovery_bridge &&
					 merged_eval.relative_max_pullback_raw > allowed_pullback) ||
					merged_eval.relative_trend_quality_raw > allowed_quality ||
					merged_eval.relative_overlap_ratio > allowed_overlap ||
					merged_eval.relative_wick_to_body_ratio > allowed_wick ||
					merged_eval.relative_directional_efficiency_step < min_step_eff ||
					merged_eval.relative_avg_body_efficiency < min_body_eff ||
					(!(same_direction_single_candle || aligned_direction_block || recovery_bridge || delayed_recovery_bridge || continuation_block || seed_continuation_block || seed_progress_block) &&
					 abs_i32(add_eval.relative_net_move_raw) > allowed_bridge_move) ||
					(!delayed_recovery_bridge &&
					 add_eval.relative_max_pullback_raw > allowed_pullback) ||
					(!(continuation_block || seed_continuation_block || seed_progress_block || aligned_direction_block || delayed_recovery_bridge) &&
					 add_eval.relative_overlap_ratio > params->relative_range_overlap_min + 220))) {
					continue;
				}
				cand_lock.start = locks[i].start;
				cand_lock.end = new_end;
				cand_lock.seed_start = locks[i].seed_start;
				cand_lock.seed_end = locks[i].seed_end;
				cand_lock.seed_pass = locks[i].seed_pass;
				cand_lock.class_id = RELATIVE_CLASS_TREND;
				cand_lock.quality_raw = merged_eval.relative_trend_quality_raw;
				cand_lock.net_move_raw = merged_eval.relative_net_move_raw;
				cand_lock.price_amplitude_raw = compute_relative_trend_price_amplitude(candles,
					features,
					locks[i].start,
					new_end,
					merged_eval.dominant_direction,
					NULL,
					NULL);
				if (cand_lock.price_amplitude_raw <= locks[i].price_amplitude_raw) {
					continue;
				}
				cand_lock.retracement_ratio = merged_eval.relative_retracement_ratio;
				cand_lock.overlap_ratio = merged_eval.relative_overlap_ratio;
				cand_lock.wick_to_body_ratio = merged_eval.relative_wick_to_body_ratio;
				cand_lock.seed_leg_raw = locks[i].seed_leg_raw;
				cand_lock.strongest_leg_raw = max_i32(locks[i].strongest_leg_raw,
					(add_eval.best_class == RELATIVE_CLASS_TREND &&
					 add_eval.dominant_direction == merged_eval.dominant_direction)
						? abs_i32(add_eval.relative_net_move_raw)
						: 0);
				cand_lock.duration = merged_eval.duration;
				if (!has_better || compare_seeded_growth_candidate(&cand_lock, &best_lock) < 0) {
					best_lock = cand_lock;
					has_better = 1;
				}
			}

			if (has_better &&
				(best_lock.start != locks[i].start || best_lock.end != locks[i].end)) {
				locks[i] = best_lock;
				local_changed = 1;
				changed = 1;
			}
		} while (local_changed);
	}
	return changed;
}

static int build_relative_seeded_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count)
{
	TrendLockSpan *locks = NULL;
	TrendLockSpan *pass_locks = NULL;
	uint32_t lock_count = 0u;
	uint32_t pass_lock_count = 0u;
	uint32_t lock_index;
	uint32_t cursor = 0u;
	uint32_t token_count = 0u;
	int rc;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || out_token_count == NULL) {
		return -1;
	}
	*out_token_count = 0u;
	if (candle_count == 0u) {
		return 0;
	}
	locks = (TrendLockSpan *)malloc((size_t)candle_count * sizeof(*locks));
	pass_locks = (TrendLockSpan *)malloc((size_t)candle_count * sizeof(*pass_locks));
	if (locks == NULL || pass_locks == NULL) {
		free(locks);
		free(pass_locks);
		return -1;
	}
	rc = detect_seed_priority_spans(candles, features, candle_count, params, 0u, candle_count - 1u, 1u, pass_locks, &pass_lock_count);
	if (rc != 0) {
		trainer_log_error("build_relative_seeded_tokens pass1 detect failed rc=%d", rc);
		free(locks);
		free(pass_locks);
		return rc;
	}
#if !WAVE_DEBUG_SEEDED_PASS1_ONLY
	for (uint32_t iter = 0u; iter < 32u; ++iter) {
		int merged = 0;
		int grown = 0;

		while (merge_seeded_trend_locks(candles, features, candle_count, params, pass_locks, &pass_lock_count) > 0) {
			merged = 1;
		}
		if (pass_lock_count > 0u) {
			grown = grow_seeded_trend_locks(candles, features, candle_count, params, 0u, candle_count - 1u, pass_locks, pass_lock_count);
			if (grown < 0) {
				trainer_log_error("build_relative_seeded_tokens pass1 grow failed rc=%d", grown);
				free(locks);
				free(pass_locks);
				return grown;
			}
		}
		if (!merged && !grown) {
			break;
		}
	}
#endif
	for (lock_index = 0u; lock_index < pass_lock_count; ++lock_index) {
		locks[lock_count++] = pass_locks[lock_index];
	}
	if (lock_count > 1u) {
		qsort(locks, lock_count, sizeof(*locks), compare_trend_lock_span);
	}
#if WAVE_DEBUG_SEEDED_PASS1_ONLY
	token_count = 0u;
	for (lock_index = 0u; lock_index < lock_count; ++lock_index) {
		tokens[token_count].start_candle = locks[lock_index].start;
		tokens[token_count].end_candle = locks[lock_index].end;
		tokens[token_count].seed_start_candle = locks[lock_index].seed_start;
		tokens[token_count].seed_end_candle = locks[lock_index].seed_end;
		tokens[token_count].seed_pass = 1u;
		tokens[token_count].class_id = RELATIVE_CLASS_TREND;
		tokens[token_count].debug_flags = 0u;
		tokens[token_count].debug_reduction_flags = WAVE_TOKEN_REDUCTION_RULE_FIRST;
		tokens[token_count].debug_reduction_count = 1u;
		token_count += 1u;
	}
	free(locks);
	if (token_count > 1u) {
		qsort(tokens, token_count, sizeof(*tokens), compare_wave_token_start);
	}
	*out_token_count = token_count;
	return 0;
#endif
#if WAVE_DEBUG_SEEDED_PASS1_GROWN_ONLY
	{
		uint32_t pass1_lock_count = lock_count;

	for (uint32_t iter = 0u; iter < 32u && pass1_lock_count > 1u; ++iter) {
		int merged = merge_seeded_trend_locks(candles, features, candle_count, params, locks, &pass1_lock_count);

		if (merged <= 0) {
			break;
		}
	}
	token_count = 0u;
	for (lock_index = 0u; lock_index < pass1_lock_count; ++lock_index) {
		tokens[token_count].start_candle = locks[lock_index].start;
		tokens[token_count].end_candle = locks[lock_index].end;
		tokens[token_count].seed_start_candle = locks[lock_index].seed_start;
		tokens[token_count].seed_end_candle = locks[lock_index].seed_end;
		tokens[token_count].seed_pass = 1u;
		tokens[token_count].class_id = RELATIVE_CLASS_TREND;
		tokens[token_count].debug_flags = 0u;
		tokens[token_count].debug_reduction_flags = WAVE_TOKEN_REDUCTION_RULE_FIRST;
		tokens[token_count].debug_reduction_count = 1u;
		token_count += 1u;
	}
	for (uint32_t iter = 0u; iter < 32u && token_count > 1u; ++iter) {
		int merged_any = 0;

		for (uint32_t j = 0u; j + 1u < token_count; ++j) {
			WaveSegmentEval left_eval;
			WaveSegmentEval right_eval;
			WaveSegmentEval merged_eval;
			uint32_t gap_start;
			uint32_t gap_end;
			int32_t left_allowed_pullback;
			int32_t right_allowed_pullback;
			int left_seed_ok;
			int right_seed_ok;

			if (tokens[j].class_id != RELATIVE_CLASS_TREND ||
				tokens[j + 1u].class_id != RELATIVE_CLASS_TREND ||
				tokens[j + 1u].start_candle <= tokens[j].end_candle) {
				continue;
			}
			compute_segment_eval(candles, features, candle_count, params,
				tokens[j].start_candle, tokens[j].end_candle, &left_eval);
			compute_segment_eval(candles, features, candle_count, params,
				tokens[j + 1u].start_candle, tokens[j + 1u].end_candle, &right_eval);
			if (left_eval.dominant_direction == 0 ||
				left_eval.dominant_direction != right_eval.dominant_direction) {
				continue;
			}
			gap_start = tokens[j].end_candle + 1u;
			gap_end = tokens[j + 1u].start_candle - 1u;
			if (gap_start <= gap_end && (gap_end - gap_start + 1u) > 2u) {
				continue;
			}
			compute_segment_eval(candles, features, candle_count, params,
				tokens[j].start_candle, tokens[j + 1u].end_candle, &merged_eval);
			if (merged_eval.best_class != RELATIVE_CLASS_TREND ||
				merged_eval.dominant_direction != left_eval.dominant_direction ||
				!relative_span_has_consistent_edges(candles, features,
					tokens[j].start_candle, tokens[j + 1u].end_candle, merged_eval.dominant_direction)) {
				continue;
			}
			left_allowed_pullback = max_i32((max_i32(compute_relative_trend_price_amplitude(candles, features,
				tokens[j].seed_start_candle, tokens[j].seed_end_candle, left_eval.dominant_direction, NULL, NULL), 1) *
				params->relative_trend_retracement_max) / params->norm_scale, 1);
			right_allowed_pullback = max_i32((max_i32(compute_relative_trend_price_amplitude(candles, features,
				tokens[j + 1u].seed_start_candle, tokens[j + 1u].seed_end_candle, right_eval.dominant_direction, NULL, NULL), 1) *
				params->relative_trend_retracement_max) / params->norm_scale, 1);
			left_seed_ok =
				relative_span_preserves_seed_leg(candles, features, tokens[j].start_candle, tokens[j + 1u].end_candle,
					tokens[j].seed_start_candle, tokens[j].seed_end_candle, left_eval.dominant_direction, left_allowed_pullback) ||
				relative_span_preserves_seed_leg_body(candles, features, tokens[j].start_candle, tokens[j + 1u].end_candle,
					tokens[j].seed_start_candle, tokens[j].seed_end_candle, left_eval.dominant_direction, left_allowed_pullback);
			right_seed_ok =
				relative_span_preserves_seed_leg(candles, features, tokens[j].start_candle, tokens[j + 1u].end_candle,
					tokens[j + 1u].seed_start_candle, tokens[j + 1u].seed_end_candle, right_eval.dominant_direction, right_allowed_pullback) ||
				relative_span_preserves_seed_leg_body(candles, features, tokens[j].start_candle, tokens[j + 1u].end_candle,
					tokens[j + 1u].seed_start_candle, tokens[j + 1u].seed_end_candle, right_eval.dominant_direction, right_allowed_pullback);
			if (!left_seed_ok || !right_seed_ok) {
				continue;
			}
			tokens[j].end_candle = tokens[j + 1u].end_candle;
			if (tokens[j + 1u].seed_start_candle < tokens[j].seed_start_candle) {
				tokens[j].seed_start_candle = tokens[j + 1u].seed_start_candle;
				tokens[j].seed_end_candle = tokens[j + 1u].seed_end_candle;
			}
			for (uint32_t k = j + 1u; k + 1u < token_count; ++k) {
				tokens[k] = tokens[k + 1u];
			}
			token_count -= 1u;
			merged_any = 1;
			break;
		}
		if (!merged_any) {
			break;
		}
	}
		for (uint32_t iter = 0u; iter < 32u && token_count > 1u; ++iter) {
			int absorbed_any = 0;

			for (uint32_t j = 0u; j + 1u < token_count; ++j) {
				WaveSegmentEval range_eval;
				WaveSegmentEval trend_eval;
				int32_t allowed_pullback;
				int seed_ok;

			if (tokens[j].class_id != RELATIVE_CLASS_RANGE ||
				tokens[j + 1u].class_id != RELATIVE_CLASS_TREND ||
				tokens[j].end_candle + 1u != tokens[j + 1u].start_candle) {
				continue;
			}
			compute_segment_eval(candles, features, candle_count, params,
				tokens[j].start_candle, tokens[j].end_candle, &range_eval);
			compute_segment_eval(candles, features, candle_count, params,
				tokens[j + 1u].start_candle, tokens[j + 1u].end_candle, &trend_eval);
				if (range_eval.duration != 1u ||
					range_eval.dominant_direction != trend_eval.dominant_direction ||
					range_eval.dominant_direction == 0) {
					continue;
				}
				allowed_pullback = max_i32((max_i32(compute_relative_trend_price_amplitude(candles, features,
					tokens[j + 1u].seed_start_candle, tokens[j + 1u].seed_end_candle,
					trend_eval.dominant_direction, NULL, NULL), 1) *
					params->relative_trend_retracement_max) / params->norm_scale, 1);
				seed_ok =
					relative_span_preserves_seed_leg_body(candles, features, tokens[j].start_candle, tokens[j + 1u].end_candle,
						tokens[j + 1u].seed_start_candle, tokens[j + 1u].seed_end_candle, trend_eval.dominant_direction, allowed_pullback);
				if (tokens[j].start_candle == 346u &&
					tokens[j].end_candle == 346u &&
					tokens[j + 1u].start_candle == 347u &&
					tokens[j + 1u].end_candle == 354u) {
					trainer_log_info("absorb346 range_dir=%d trend_dir=%d seed_ok=%d first_body=%d allowed_pullback=%d seed=[%u,%u]",
						range_eval.dominant_direction,
						trend_eval.dominant_direction,
						seed_ok,
						features[tokens[j].start_candle].b,
						allowed_pullback,
						tokens[j + 1u].seed_start_candle,
						tokens[j + 1u].seed_end_candle);
				}
				if (!seed_ok) {
					continue;
				}
				if (features[tokens[j].start_candle].b * trend_eval.dominant_direction <= 0) {
					continue;
				}
				tokens[j + 1u].start_candle = tokens[j].start_candle;
				for (uint32_t k = j; k + 1u < token_count; ++k) {
					tokens[k] = tokens[k + 1u];
				}
			token_count -= 1u;
			absorbed_any = 1;
			break;
		}
		if (!absorbed_any) {
			break;
		}
	}
	free(locks);
	if (token_count > 1u) {
		qsort(tokens, token_count, sizeof(*tokens), compare_wave_token_start);
	}
	*out_token_count = token_count;
	return 0;
	}
#endif
	cursor = 0u;
#if !WAVE_DEBUG_SEEDED_DISABLE_PASS2
	{
	uint32_t pass1_lock_count = lock_count;

	for (lock_index = 0u; lock_index <= pass1_lock_count; ++lock_index) {
		uint32_t gap_start = cursor;
		uint32_t gap_end = 0u;
		int has_gap = 0;

		if (lock_index < pass1_lock_count) {
			if (locks[lock_index].start > gap_start) {
				gap_end = locks[lock_index].start - 1u;
				has_gap = 1;
			}
		} else {
			gap_end = candle_count - 1u;
			has_gap = (gap_start < candle_count);
		}
		if (has_gap && gap_start <= gap_end) {
			uint32_t local_count = 0u;

			rc = detect_seed_priority_spans(candles, features, candle_count, params, gap_start, gap_end, 2u, pass_locks, &local_count);
			if (rc != 0) {
				trainer_log_error("build_relative_seeded_tokens pass2 detect failed gap=[%u,%u] rc=%d", gap_start, gap_end, rc);
				free(locks);
				free(pass_locks);
				return rc;
			}
			for (uint32_t iter = 0u; iter < 32u; ++iter) {
				int merged = 0;
				int grown = 0;

				while (merge_seeded_trend_locks(candles, features, candle_count, params, pass_locks, &local_count) > 0) {
					merged = 1;
				}
				if (local_count > 0u) {
					grown = grow_seeded_trend_locks(candles, features, candle_count, params, gap_start, gap_end, pass_locks, local_count);
					if (grown < 0) {
						trainer_log_error("build_relative_seeded_tokens pass2 grow failed gap=[%u,%u] rc=%d", gap_start, gap_end, grown);
						free(locks);
						free(pass_locks);
						return grown;
					}
				}
				if (!merged && !grown) {
					break;
				}
			}
			for (uint32_t j = 0u; j < local_count; ++j) {
				locks[lock_count++] = pass_locks[j];
			}
		}
		if (lock_index < pass1_lock_count) {
			cursor = locks[lock_index].end + 1u;
		}
	}
	}
#endif
	free(pass_locks);
	if (lock_count == 0u) {
		free(locks);
		return append_gpu_gap_tokens(candles, features, params, 0u, candle_count - 1u, tokens, out_token_count, 1);
	}
	if (lock_count > 1u) {
		qsort(locks, lock_count, sizeof(*locks), compare_trend_lock_span);
	}
	cursor = 0u;
	for (lock_index = 0u; lock_index < lock_count; ++lock_index) {
		if (cursor < locks[lock_index].start) {
			rc = append_gpu_gap_tokens(candles,
				features,
				params,
				cursor,
				locks[lock_index].start - 1u,
				tokens,
				&token_count,
				1);
			if (rc != 0) {
				trainer_log_error("build_relative_seeded_tokens gap fill failed gap=[%u,%u] rc=%d", cursor, locks[lock_index].start - 1u, rc);
				free(locks);
				return rc;
			}
		}
		tokens[token_count].start_candle = locks[lock_index].start;
		tokens[token_count].end_candle = locks[lock_index].end;
		tokens[token_count].seed_start_candle = locks[lock_index].seed_start;
		tokens[token_count].seed_end_candle = locks[lock_index].seed_end;
		tokens[token_count].seed_pass = locks[lock_index].seed_pass;
		tokens[token_count].class_id = RELATIVE_CLASS_TREND;
		tokens[token_count].debug_flags = 0u;
		tokens[token_count].debug_reduction_flags = WAVE_TOKEN_REDUCTION_RULE_FIRST;
		tokens[token_count].debug_reduction_count = 1u;
		token_count += 1u;
		cursor = locks[lock_index].end + 1u;
	}
	if (cursor < candle_count) {
		rc = append_gpu_gap_tokens(candles, features, params, cursor, candle_count - 1u, tokens, &token_count, 1);
		if (rc != 0) {
			trainer_log_error("build_relative_seeded_tokens tail gap fill failed gap=[%u,%u] rc=%d", cursor, candle_count - 1u, rc);
			free(locks);
			return rc;
		}
	}
	free(locks);
	if (token_count > 1u) {
		qsort(tokens, token_count, sizeof(*tokens), compare_wave_token_start);
	}
	if (validate_token_sequence(tokens, token_count, candle_count) != 0) {
		return -2;
	}
	*out_token_count = token_count;
	return 0;
}

static int layered_span_is_pure_sequential(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int *out_sign)
{
	uint32_t c;
	uint32_t signed_count = 0u;
	int sign = 0;

	(void)features;
	if (candles == NULL || end < start) {
		return 0;
	}
	for (c = start; c <= end; ++c) {
		int body_sign = candle_body_sign_raw(candles, c);

		if (body_sign == 0) {
			continue;
		}
		if (sign == 0) {
			sign = body_sign;
		} else if (body_sign != sign) {
			return 0;
		}
		signed_count += 1u;
	}
	if (sign == 0 || signed_count < 2u) {
		return 0;
	}
	if (out_sign != NULL) {
		*out_sign = sign;
	}
	return 1;
}

static int layered_span_is_strict_alternating(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int *out_first_sign,
	uint32_t *out_pair_count,
	uint32_t *out_signed_count)
{
	uint32_t c;
	uint32_t pair_count = 0u;
	uint32_t signed_count = 0u;
	int prev_sign = 0;
	int first_sign = 0;

	(void)features;
	if (candles == NULL || end < start) {
		return 0;
	}
	for (c = start; c <= end; ++c) {
		int body_sign = candle_body_sign_raw(candles, c);

		if (body_sign == 0) {
			continue;
		}
		if (first_sign == 0) {
			first_sign = body_sign;
		}
		if (prev_sign != 0) {
			if (body_sign == prev_sign) {
				return 0;
			}
			pair_count += 1u;
		}
		prev_sign = body_sign;
		signed_count += 1u;
	}
	if (signed_count < 2u || pair_count == 0u) {
		return 0;
	}
	if (out_first_sign != NULL) {
		*out_first_sign = first_sign;
	}
	if (out_pair_count != NULL) {
		*out_pair_count = pair_count;
	}
	if (out_signed_count != NULL) {
		*out_signed_count = signed_count;
	}
	return 1;
}

static int layered_span_can_be_sequential_string(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int *out_sign)
{
	return layered_span_is_pure_sequential(candles, features, start, end, out_sign);
}

static int layered_span_can_be_alternating_string(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	uint32_t *out_pair_count)
{
	return layered_span_is_strict_alternating(candles,
		features,
		start,
		end,
		NULL,
		out_pair_count,
		NULL);
}

static int layered_find_next_signed_index(const Candle *candles, uint32_t candle_count, uint32_t start, uint32_t *out_index)
{
	uint32_t c;

	if (candles == NULL || out_index == NULL) {
		return 0;
	}
	for (c = start; c < candle_count; ++c) {
		if (candle_body_sign_raw(candles, c) != 0) {
			*out_index = c;
			return 1;
		}
	}
	return 0;
}

static int layered_find_max_sequential_span(const Candle *candles,
	uint32_t candle_count,
	uint32_t start,
	LayeredPrimitiveSpan *out)
{
	uint32_t first_signed;
	uint32_t c;
	uint32_t signed_count = 0u;
	int sign;

	if (candles == NULL || out == NULL) {
		return 0;
	}
	if (!layered_find_next_signed_index(candles, candle_count, start, &first_signed)) {
		return 0;
	}
	sign = candle_body_sign_raw(candles, first_signed);
	out->start = start;
	out->end = first_signed;
	out->signed_count = 0u;
	out->sign = sign;
	for (c = first_signed; c < candle_count; ++c) {
		int body_sign = candle_body_sign_raw(candles, c);

		if (body_sign == 0) {
			out->end = c;
			continue;
		}
		if (body_sign != sign) {
			break;
		}
		out->end = c;
		signed_count += 1u;
	}
	if (signed_count < 2u) {
		return 0;
	}
	out->signed_count = signed_count;
	return 1;
}

static int layered_find_max_alternating_span(const Candle *candles,
	uint32_t candle_count,
	uint32_t start,
	LayeredPrimitiveSpan *out)
{
	uint32_t first_signed;
	uint32_t c;
	uint32_t signed_count = 0u;
	int prev_sign = 0;
	int first_sign = 0;

	if (candles == NULL || out == NULL) {
		return 0;
	}
	if (!layered_find_next_signed_index(candles, candle_count, start, &first_signed)) {
		return 0;
	}
	out->start = start;
	out->end = first_signed;
	out->signed_count = 0u;
	out->sign = 0;
	for (c = first_signed; c < candle_count; ++c) {
		int body_sign = candle_body_sign_raw(candles, c);

		if (body_sign == 0) {
			out->end = c;
			continue;
		}
		if (prev_sign != 0 && body_sign == prev_sign) {
			break;
		}
		if (first_sign == 0) {
			first_sign = body_sign;
		}
		prev_sign = body_sign;
		out->end = c;
		signed_count += 1u;
	}
	if (signed_count < 2u) {
		return 0;
	}
	out->signed_count = signed_count;
	out->sign = first_sign;
	return 1;
}

static int append_layered_layer1_gap_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t start,
	uint32_t end,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t cursor = start;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || token_count == NULL || end < start) {
		return -1;
	}
	while (cursor <= end) {
		LayeredPrimitiveSpan alternating_span;
		WaveSegmentEval eval;
		uint32_t token_end = cursor;
		uint32_t class_id = PRIMITIVE_CLASS_SEPARATOR;

		if (layered_find_max_alternating_span(candles, end + 1u, cursor, &alternating_span)) {
			token_end = alternating_span.end;
			class_id = PRIMITIVE_CLASS_ALTERNATING;
		} else {
			uint32_t first_signed;

			if (layered_find_next_signed_index(candles, end + 1u, cursor, &first_signed)) {
				token_end = first_signed;
			} else {
				token_end = end;
			}
		}
		compute_segment_eval(candles,
			features,
			candle_count,
			params,
			cursor,
			token_end,
			&eval);
		fill_token_from_eval(&tokens[*token_count], &eval, class_id, params);
		*token_count += 1u;
		if (token_end >= end) {
			break;
		}
		cursor = token_end + 1u;
	}
	return 0;
}

static int build_layered_layer1_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count)
{
	WaveParams primitive_params;
	LayeredSequentialCandidate *candidates = NULL;
	uint32_t candidate_count = 0u;
	uint32_t token_count = 0u;
	uint32_t cursor = 0u;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || out_token_count == NULL) {
		return -1;
	}
	primitive_params = *params;
	primitive_params.wave_model = WAVE_MODEL_PRIMITIVE;
	candidates = (LayeredSequentialCandidate *)malloc((size_t)candle_count * sizeof(*candidates));
	if (candidates == NULL) {
		return -1;
	}
	while (cursor < candle_count) {
		LayeredPrimitiveSpan sequential_span;
		uint32_t first_signed;

		if (!layered_find_next_signed_index(candles, candle_count, cursor, &first_signed)) {
			break;
		}
		if (layered_find_max_sequential_span(candles, candle_count, first_signed, &sequential_span)) {
			candidates[candidate_count].start = sequential_span.start;
			candidates[candidate_count].end = sequential_span.end;
			candidates[candidate_count].sign = sequential_span.sign;
			candidate_count += 1u;
			cursor = sequential_span.end + 1u;
			continue;
		}
		cursor = first_signed + 1u;
	}
	cursor = 0u;
	for (uint32_t i = 0u; i < candidate_count; ++i) {
		WaveSegmentEval eval;

		if (cursor < candidates[i].start) {
			if (append_layered_layer1_gap_tokens(candles,
				features,
				candle_count,
				&primitive_params,
				cursor,
				candidates[i].start - 1u,
				tokens,
				&token_count) != 0) {
				free(candidates);
				return -1;
			}
		}
		compute_segment_eval(candles,
			features,
			candle_count,
			&primitive_params,
			candidates[i].start,
			candidates[i].end,
			&eval);
		fill_token_from_eval(&tokens[token_count], &eval, PRIMITIVE_CLASS_CONSECUTIVE, &primitive_params);
		tokens[token_count].dominant_direction = candidates[i].sign;
		token_count += 1u;
		cursor = candidates[i].end + 1u;
	}
	if (cursor < candle_count) {
		if (append_layered_layer1_gap_tokens(candles,
			features,
			candle_count,
			&primitive_params,
			cursor,
			candle_count - 1u,
			tokens,
			&token_count) != 0) {
			free(candidates);
			return -1;
		}
	}
	while (collapse_layered_layer1_singletons(candles,
		features,
		candle_count,
		&primitive_params,
		tokens,
		&token_count) > 0) {
	}
	free(candidates);
	if (validate_token_sequence(tokens, token_count, candle_count) != 0) {
		log_invalid_token_sequence("build_layered_layer1_tokens", tokens, token_count, candle_count);
		return -2;
	}
	*out_token_count = token_count;
	return 0;
}

static int collapse_layered_layer1_singletons(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	if (*count <= 1u) {
		return 0;
	}
	for (i = 0u; i < *count; ++i) {
		uint32_t left_class = 0u;
		uint32_t right_class = 0u;
		int left_sign = 0;
		int right_sign = 0;
		int left_ok = 0;
		int right_ok = 0;
		int32_t left_move = 0x7fffffff;
		int32_t right_move = 0x7fffffff;

		if (tokens[i].duration_candles != 1u || tokens[i].class_id == PRIMITIVE_CLASS_SEPARATOR) {
			continue;
		}
		if (i > 0u) {
			uint32_t start = tokens[i - 1u].start_candle;
			uint32_t end = tokens[i].end_candle;
			WaveSegmentEval eval;

			if (layered_span_can_be_sequential_string(candles, features, start, end, &left_sign)) {
				left_class = PRIMITIVE_CLASS_CONSECUTIVE;
				left_ok = 1;
			} else if (layered_span_can_be_alternating_string(candles, features, start, end, NULL)) {
				left_class = PRIMITIVE_CLASS_ALTERNATING;
				left_ok = 1;
			}
			if (left_ok) {
				compute_segment_eval(candles, features, candle_count, params, start, end, &eval);
				left_move = abs_i32(eval.net_move_raw);
			}
		}
		if (i + 1u < *count) {
			uint32_t start = tokens[i].start_candle;
			uint32_t end = tokens[i + 1u].end_candle;
			WaveSegmentEval eval;

			if (layered_span_can_be_sequential_string(candles, features, start, end, &right_sign)) {
				right_class = PRIMITIVE_CLASS_CONSECUTIVE;
				right_ok = 1;
			} else if (layered_span_can_be_alternating_string(candles, features, start, end, NULL)) {
				right_class = PRIMITIVE_CLASS_ALTERNATING;
				right_ok = 1;
			}
			if (right_ok) {
				compute_segment_eval(candles, features, candle_count, params, start, end, &eval);
				right_move = abs_i32(eval.net_move_raw);
			}
		}
		if (!left_ok && !right_ok) {
			continue;
		}
		if (left_ok &&
			(!right_ok ||
			 (left_class == PRIMITIVE_CLASS_CONSECUTIVE && right_class != PRIMITIVE_CLASS_CONSECUTIVE) ||
			 (left_class == right_class && left_move <= right_move))) {
			merge_token_range(candles, features, candle_count, params, tokens, count, i - 1u, i, left_class);
			if (left_class == PRIMITIVE_CLASS_CONSECUTIVE) {
				tokens[i - 1u].dominant_direction = left_sign;
			}
			return 1;
		}
		merge_token_range(candles, features, candle_count, params, tokens, count, i, i + 1u, right_class);
		if (right_class == PRIMITIVE_CLASS_CONSECUTIVE) {
			tokens[i].dominant_direction = right_sign;
		}
		return 1;
	}
	return 0;
}

static void compute_layered_child_summary(const WaveToken *children,
	uint32_t child_start,
	uint32_t child_end,
	const WaveParams *params,
	LayeredChildSummary *out)
{
	uint32_t i;
	uint32_t total_candles = 0u;
	uint32_t seq_dir_candles;

	memset(out, 0, sizeof(*out));
	for (i = child_start; i <= child_end; ++i) {
		uint32_t duration = children[i].duration_candles;
		int32_t move = abs_i32(children[i].net_move_raw);

		if (children[i].class_id == PRIMITIVE_CLASS_CONSECUTIVE) {
			out->count += 1u;
			total_candles += duration;
			out->sequential_count += 1u;
			out->sequential_candles += duration;
			out->sequential_net_move_sum_raw += move;
			out->max_sequential_net_move_raw = max_i32(out->max_sequential_net_move_raw,
				move);
			out->max_child_net_move_raw = max_i32(out->max_child_net_move_raw,
				move);
			if (children[i].dominant_direction > 0) {
				out->sequential_bull_candles += duration;
				out->sequential_bull_net_move_sum_raw += move;
			} else if (children[i].dominant_direction < 0) {
				out->sequential_bear_candles += duration;
				out->sequential_bear_net_move_sum_raw += move;
			}
		} else if (children[i].class_id == PRIMITIVE_CLASS_ALTERNATING) {
			out->count += 1u;
			total_candles += duration;
			out->alternating_count += 1u;
			out->alternating_candles += duration;
			out->alternating_net_move_sum_raw += move;
			out->max_alternating_net_move_raw = max_i32(out->max_alternating_net_move_raw,
				move);
			out->max_child_net_move_raw = max_i32(out->max_child_net_move_raw,
				move);
			if (children[i].dominant_direction > 0) {
				out->alternating_bull_candles += duration;
				out->alternating_bull_net_move_sum_raw += move;
			} else if (children[i].dominant_direction < 0) {
				out->alternating_bear_candles += duration;
				out->alternating_bear_net_move_sum_raw += move;
			}
		} else {
			out->separator_count += 1u;
			out->separator_candles += duration;
			out->separator_net_move_sum_raw += move;
		}
	}
	seq_dir_candles = max_u32(out->sequential_bull_candles, out->sequential_bear_candles);
	out->directional_agreement = (int32_t)(((int64_t)seq_dir_candles * params->norm_scale) /
		max_i32((int32_t)out->sequential_candles, 1));
	out->sequential_ratio = (int32_t)(((int64_t)out->sequential_candles * params->norm_scale) /
		max_i32((int32_t)total_candles, 1));
	out->alternating_ratio = (int32_t)(((int64_t)out->alternating_candles * params->norm_scale) /
		max_i32((int32_t)total_candles, 1));
}

static void compute_layered_span_errors(const WaveSegmentEval *eval,
	const LayeredChildSummary *child,
	const WaveParams *params,
	int32_t *out_trend_error,
	int32_t *out_range_error)
{
	int32_t trend_error;
	int32_t range_error;
	int32_t drift_floor;
	int32_t trend_sign_ceiling;
	int32_t trend_overlap_ceiling;
	int32_t trend_retrace_ceiling;
	int32_t range_sign_floor;
	int32_t range_overlap_floor;
	int32_t short_wave_penalty;
	int32_t range_variance_ratio;
	int32_t avg_child_move;
	int32_t total_child_move_sum;
	int32_t total_seq_move_sum;
	int32_t aligned_seq_move_sum;
	int32_t counter_seq_move_sum;
	int32_t aligned_alt_move_sum;
	int32_t counter_alt_move_sum;
	int32_t sequential_balance_ratio;
	int32_t sequential_drift_ratio;
	int32_t range_structure_ratio;
	int32_t missing_range_structure_penalty;
	int32_t parent_drift_vs_child_peak_ratio;
	int two_sided_child_flow;
	int32_t aligned_seq_share;
	int32_t counter_seq_share;
	int32_t aligned_alt_share;
	int32_t counter_alt_share;
	int32_t alternating_share;
	int32_t seq_count_ratio;
	int32_t alt_count_ratio;

	drift_floor = max_i32(params->minimum_pips_for_trend * 5, 28);
	trend_sign_ceiling = max_i32(params->relative_trend_sign_flip_max - 40, 220);
	trend_overlap_ceiling = max_i32(params->relative_trend_overlap_max + 80, 180);
	trend_retrace_ceiling = max_i32(params->relative_trend_retracement_max + 120, 220);
	range_sign_floor = max_i32(params->relative_range_sign_flip_min - 120, 180);
	range_overlap_floor = max_i32(params->relative_range_overlap_min - 140, 120);
	short_wave_penalty = params->segment_penalty_lambda * 4;
	seq_count_ratio = (int32_t)(((int64_t)child->sequential_count * params->norm_scale) /
		max_i32((int32_t)child->count, 1));
	alt_count_ratio = (int32_t)(((int64_t)child->alternating_count * params->norm_scale) /
		max_i32((int32_t)child->count, 1));
	total_child_move_sum = child->sequential_net_move_sum_raw + child->alternating_net_move_sum_raw;
	if (eval->dominant_direction >= 0) {
		aligned_seq_move_sum = child->sequential_bull_net_move_sum_raw;
		counter_seq_move_sum = child->sequential_bear_net_move_sum_raw;
		aligned_alt_move_sum = child->alternating_bull_net_move_sum_raw;
		counter_alt_move_sum = child->alternating_bear_net_move_sum_raw;
	} else {
		aligned_seq_move_sum = child->sequential_bear_net_move_sum_raw;
		counter_seq_move_sum = child->sequential_bull_net_move_sum_raw;
		aligned_alt_move_sum = child->alternating_bear_net_move_sum_raw;
		counter_alt_move_sum = child->alternating_bull_net_move_sum_raw;
	}
	aligned_seq_share = (int32_t)(((int64_t)aligned_seq_move_sum * params->norm_scale) /
		max_i32(total_child_move_sum, 1));
	counter_seq_share = (int32_t)(((int64_t)counter_seq_move_sum * params->norm_scale) /
		max_i32(total_child_move_sum, 1));
	aligned_alt_share = (int32_t)(((int64_t)aligned_alt_move_sum * params->norm_scale) /
		max_i32(total_child_move_sum, 1));
	counter_alt_share = (int32_t)(((int64_t)counter_alt_move_sum * params->norm_scale) /
		max_i32(total_child_move_sum, 1));
	alternating_share = (int32_t)(((int64_t)child->alternating_net_move_sum_raw * params->norm_scale) /
		max_i32(total_child_move_sum, 1));
	total_seq_move_sum = child->sequential_bull_net_move_sum_raw +
		child->sequential_bear_net_move_sum_raw;
	sequential_balance_ratio = (int32_t)(((int64_t)min_i32(child->sequential_bull_net_move_sum_raw,
			child->sequential_bear_net_move_sum_raw) * 2 * params->norm_scale) /
		max_i32(total_seq_move_sum, 1));
	sequential_drift_ratio = (int32_t)(((int64_t)abs_i32(child->sequential_bull_net_move_sum_raw -
			child->sequential_bear_net_move_sum_raw) * params->norm_scale) /
		max_i32(total_seq_move_sum, 1));
	range_structure_ratio = max_i32(alternating_share, sequential_balance_ratio);
	missing_range_structure_penalty = params->segment_penalty_lambda * 6;
	parent_drift_vs_child_peak_ratio = (int32_t)(((int64_t)abs_i32(eval->net_move_raw) *
			params->norm_scale) /
		max_i32(child->max_child_net_move_raw, 1));
	two_sided_child_flow =
		((child->sequential_bull_net_move_sum_raw > 0 || child->alternating_bull_net_move_sum_raw > 0) &&
		 (child->sequential_bear_net_move_sum_raw > 0 || child->alternating_bear_net_move_sum_raw > 0));
	avg_child_move = (child->sequential_net_move_sum_raw + child->alternating_net_move_sum_raw) /
		max_i32((int32_t)child->count, 1);
	range_variance_ratio = (int32_t)(((int64_t)child->max_child_net_move_raw * params->norm_scale) /
		max_i32(avg_child_move, 1));
	trend_error =
		10 * penalty_low(aligned_seq_share, 520) +
		5 * penalty_low(aligned_alt_share, 90) +
		6 * penalty_high(alternating_share, 520) +
		10 * penalty_high(counter_seq_share, 220) +
		8 * penalty_high(counter_alt_share, 260) +
		7 * penalty_low(seq_count_ratio, 560) +
		7 * penalty_low(child->sequential_ratio, 580) +
		6 * penalty_high(alt_count_ratio, 440) +
		6 * penalty_high(child->alternating_ratio, 420) +
		6 * penalty_low(child->directional_agreement, 760) +
		8 * penalty_high(sequential_balance_ratio, 420) +
		6 * penalty_low(sequential_drift_ratio, 260) +
		5 * penalty_low(eval->relative_directional_efficiency_step, params->relative_trend_der_step_min - 60) +
		5 * penalty_low(eval->relative_directional_efficiency_body, params->relative_trend_der_body_min - 60) +
		3 * penalty_low(eval->relative_avg_body_efficiency, params->relative_trend_body_eff_min - 40) +
		4 * penalty_high(eval->relative_overlap_ratio, trend_overlap_ceiling) +
		4 * penalty_high(eval->sign_change_density, trend_sign_ceiling) +
		4 * penalty_high(eval->relative_retracement_ratio, trend_retrace_ceiling) +
		3 * penalty_low(abs_i32(eval->net_move_raw), drift_floor);
	if (two_sided_child_flow) {
		trend_error += 10 * penalty_low(parent_drift_vs_child_peak_ratio, 420);
	}
	range_error =
		10 * penalty_low(range_structure_ratio, 420) +
		6 * penalty_high(aligned_seq_share, 760) +
		6 * penalty_high(aligned_alt_share, 420) +
		5 * penalty_low(counter_seq_share, 180) +
		4 * penalty_high(counter_alt_share, 420) +
		3 * penalty_low(alt_count_ratio, 180) +
		3 * penalty_low(child->alternating_ratio, 180) +
		2 * penalty_high(seq_count_ratio, 920) +
		2 * penalty_high(child->sequential_ratio, 920) +
		8 * penalty_high(sequential_drift_ratio, 520) +
		10 * penalty_high(range_variance_ratio, 1800) +
		6 * penalty_high(child->directional_agreement, 760) +
		4 * penalty_high(eval->relative_directional_efficiency_step, params->relative_range_der_step_max + 80) +
		4 * penalty_high(eval->relative_directional_efficiency_body, params->relative_range_der_step_max + 60) +
		4 * penalty_low(eval->sign_change_density, range_sign_floor) +
		4 * penalty_low(eval->relative_overlap_ratio, range_overlap_floor) +
		4 * penalty_high(abs_i32(eval->net_move_raw), drift_floor * 3);
	if (two_sided_child_flow) {
		range_error += 4 * penalty_high(parent_drift_vs_child_peak_ratio, 760);
	}
	if (child->count == 1u) {
		trend_error += short_wave_penalty;
		range_error += short_wave_penalty;
	} else if (child->count == 2u) {
		trend_error += short_wave_penalty / 2;
		range_error += short_wave_penalty / 2;
	}
	if (child->alternating_count == 0u && sequential_balance_ratio < 240) {
		range_error += missing_range_structure_penalty;
	}
	*out_trend_error = trend_error;
	*out_range_error = range_error;
}

static int32_t layered_dp_class_bias(const WaveSegmentEval *eval,
	const WaveParams *params,
	uint32_t start_candle,
	uint32_t end_candle,
	uint32_t class_id)
{
	int32_t move_raw;
	int32_t candle_span;
	int32_t unit;

	if (eval == NULL || params == NULL || end_candle < start_candle) {
		return 0;
	}
	move_raw = abs_i32(eval->net_move_raw);
	candle_span = (int32_t)(end_candle - start_candle + 1u);
	unit = max_i32(params->minimum_pips_for_trend, 1);
	if (class_id == RELATIVE_CLASS_TREND) {
		return -(move_raw * 4) + candle_span * unit * 3 - params->segment_penalty_lambda;
	}
	if (class_id == RELATIVE_CLASS_RANGE) {
		return move_raw * 3 - candle_span * unit * 10;
	}
	return 0;
}

static int64_t layered_trend_rank(const WaveToken *tok, const WaveParams *params)
{
	int32_t move_raw;
	int32_t candle_span;
	int32_t unit;

	if (tok == NULL || params == NULL) {
		return 0x3fffffffffffffffLL;
	}
	move_raw = abs_i32(tok->net_move_raw);
	candle_span = (int32_t)tok->duration_candles;
	unit = max_i32(params->minimum_pips_for_trend, 1);
	return (int64_t)tok->fit_error -
		(int64_t)move_raw * 4 +
		(int64_t)candle_span * unit * 3;
}

static int64_t layered_range_rank(const WaveToken *tok, const WaveParams *params)
{
	int32_t move_raw;
	int32_t candle_span;
	int32_t unit;

	if (tok == NULL || params == NULL) {
		return 0x3fffffffffffffffLL;
	}
	move_raw = abs_i32(tok->net_move_raw);
	candle_span = (int32_t)tok->duration_candles;
	unit = max_i32(params->minimum_pips_for_trend, 1);
	return (int64_t)tok->fit_error +
		(int64_t)move_raw * 3 -
		(int64_t)candle_span * unit * 10;
}

static int64_t layered_token_rank(const WaveToken *tok, const WaveParams *params)
{
	if (tok == NULL || params == NULL) {
		return 0x3fffffffffffffffLL;
	}
	if (tok->class_id == RELATIVE_CLASS_TREND) {
		return layered_trend_rank(tok, params);
	}
	if (tok->class_id == RELATIVE_CLASS_RANGE) {
		return layered_range_rank(tok, params);
	}
	return 0x3fffffffffffffffLL;
}

static void fill_layered_token(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	uint32_t child_start,
	uint32_t child_end,
	uint32_t class_id,
	int32_t fit_error,
	int32_t second_best_error,
	WaveToken *out)
{
	WaveSegmentEval eval;
	LayeredChildSummary child;
	uint32_t start_candle = layer1_tokens[child_start].start_candle;
	uint32_t end_candle = layer1_tokens[child_end].end_candle;

	compute_segment_eval(candles, features, candle_count, params, start_candle, end_candle, &eval);
	fill_token_from_eval(out, &eval, class_id, params);
	out->fit_error = fit_error;
	out->second_best_error = second_best_error;
	out->seed_start_candle = start_candle;
	out->seed_end_candle = end_candle;
	out->layered_child_start_index = child_start;
	out->layered_child_end_index = child_end;
	compute_layered_child_summary(layer1_tokens, child_start, child_end, params, &child);
	out->layered_sequential_count = child.sequential_count;
	out->layered_alternating_count = child.alternating_count;
	out->layered_sequential_candles = child.sequential_candles;
	out->layered_alternating_candles = child.alternating_candles;
	out->layered_sequential_net_move_sum_raw = child.sequential_net_move_sum_raw;
	out->layered_alternating_net_move_sum_raw = child.alternating_net_move_sum_raw;
	out->layered_directional_agreement = child.directional_agreement;
}

static void build_layered_token_for_fixed_class(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	uint32_t child_start,
	uint32_t child_end,
	uint32_t class_id,
	WaveToken *out,
	int32_t *out_price_amplitude)
{
	WaveSegmentEval eval;
	LayeredChildSummary child;
	int32_t trend_error;
	int32_t range_error;
	uint32_t start_candle = layer1_tokens[child_start].start_candle;
	uint32_t end_candle = layer1_tokens[child_end].end_candle;

	compute_segment_eval(candles, features, candle_count, params, start_candle, end_candle, &eval);
	compute_layered_child_summary(layer1_tokens, child_start, child_end, params, &child);
	compute_layered_span_errors(&eval, &child, params, &trend_error, &range_error);
	fill_token_from_eval(out, &eval, class_id, params);
	out->fit_error = (class_id == RELATIVE_CLASS_TREND) ? trend_error : range_error;
	out->second_best_error = (class_id == RELATIVE_CLASS_TREND) ? range_error : trend_error;
	out->seed_start_candle = start_candle;
	out->seed_end_candle = end_candle;
	out->layered_child_start_index = child_start;
	out->layered_child_end_index = child_end;
	out->layered_sequential_count = child.sequential_count;
	out->layered_alternating_count = child.alternating_count;
	out->layered_sequential_candles = child.sequential_candles;
	out->layered_alternating_candles = child.alternating_candles;
	out->layered_sequential_net_move_sum_raw = child.sequential_net_move_sum_raw;
	out->layered_alternating_net_move_sum_raw = child.alternating_net_move_sum_raw;
	out->layered_directional_agreement = child.directional_agreement;
	if (out_price_amplitude != NULL) {
		if (class_id == RELATIVE_CLASS_TREND && out->dominant_direction != 0) {
			*out_price_amplitude = compute_relative_trend_price_amplitude(candles,
				features,
				start_candle,
				end_candle,
				out->dominant_direction,
				NULL,
				NULL);
		} else {
			*out_price_amplitude = abs_i32(out->relative_net_move_raw);
		}
	}
}

static int optimize_layered_token_boundaries(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t token_count)
{
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL) {
		return -1;
	}
	for (i = 0u; i + 1u < token_count; ++i) {
		uint32_t left_start = tokens[i].layered_child_start_index;
		uint32_t right_end = tokens[i + 1u].layered_child_end_index;
		uint32_t split;
		uint32_t best_split;
		int64_t best_score = 0x3fffffffffffffffLL;
		int32_t best_amplitude = -1;
		uint32_t best_trend_candles = 0xffffffffu;
		WaveToken best_left;
		WaveToken best_right;
		int found = 0;

		if (tokens[i].layered_child_end_index + 1u != tokens[i + 1u].layered_child_start_index) {
			continue;
		}
		if (left_start >= right_end) {
			continue;
		}
		best_split = tokens[i + 1u].layered_child_start_index;
		for (split = left_start + 1u; split <= right_end; ++split) {
			WaveToken cand_left;
			WaveToken cand_right;
			int32_t left_amplitude = 0;
			int32_t right_amplitude = 0;
			int32_t trend_amplitude = 0;
			uint32_t trend_candles = 0u;
			int64_t score;

			build_layered_token_for_fixed_class(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				left_start,
				split - 1u,
				tokens[i].class_id,
				&cand_left,
				&left_amplitude);
			build_layered_token_for_fixed_class(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				split,
				right_end,
				tokens[i + 1u].class_id,
				&cand_right,
				&right_amplitude);
			if (cand_left.start_candle > cand_left.end_candle ||
				cand_right.start_candle > cand_right.end_candle) {
				continue;
			}
			score = (int64_t)cand_left.fit_error + (int64_t)cand_right.fit_error;
			if (tokens[i].class_id == RELATIVE_CLASS_TREND) {
				trend_amplitude += left_amplitude;
				trend_candles += cand_left.duration_candles;
			}
			if (tokens[i + 1u].class_id == RELATIVE_CLASS_TREND) {
				trend_amplitude += right_amplitude;
				trend_candles += cand_right.duration_candles;
			}
			if (!found ||
				trend_amplitude > best_amplitude ||
				(trend_amplitude == best_amplitude && trend_candles < best_trend_candles) ||
				(trend_amplitude == best_amplitude && trend_candles == best_trend_candles && score < best_score) ||
				(trend_amplitude == best_amplitude && trend_candles == best_trend_candles && score == best_score &&
				 split == tokens[i + 1u].layered_child_start_index)) {
				best_score = score;
				best_amplitude = trend_amplitude;
				best_trend_candles = trend_candles;
				best_split = split;
				best_left = cand_left;
				best_right = cand_right;
				found = 1;
			}
		}
		if (!found) {
			continue;
		}
		if (best_split != tokens[i + 1u].layered_child_start_index) {
			tokens[i] = best_left;
			tokens[i + 1u] = best_right;
			changed = 1;
		}
	}
	return changed;
}

static void reclassify_layered_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL) {
		return;
	}
	for (i = 0u; i < token_count; ++i) {
		WaveToken rebuilt;
		WaveSegmentEval eval;
		LayeredChildSummary child;
		int32_t trend_error;
		int32_t range_error;
		uint32_t class_id;

		compute_segment_eval(candles,
			features,
			candle_count,
			params,
			tokens[i].start_candle,
			tokens[i].end_candle,
			&eval);
		compute_layered_child_summary(layer1_tokens,
			tokens[i].layered_child_start_index,
			tokens[i].layered_child_end_index,
			params,
			&child);
		compute_layered_span_errors(&eval, &child, params, &trend_error, &range_error);
		class_id = (trend_error <= range_error) ? RELATIVE_CLASS_TREND : RELATIVE_CLASS_RANGE;
		fill_layered_token(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens[i].layered_child_start_index,
			tokens[i].layered_child_end_index,
			class_id,
			(class_id == RELATIVE_CLASS_TREND) ? trend_error : range_error,
			(class_id == RELATIVE_CLASS_TREND) ? range_error : trend_error,
			&rebuilt);
		tokens[i] = rebuilt;
	}
}

static void build_best_layered_token(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	uint32_t child_start,
	uint32_t child_end,
	WaveToken *out)
{
	WaveSegmentEval eval;
	LayeredChildSummary child;
	int32_t trend_error;
	int32_t range_error;
	uint32_t class_id;

	compute_segment_eval(candles,
		features,
		candle_count,
		params,
		layer1_tokens[child_start].start_candle,
		layer1_tokens[child_end].end_candle,
		&eval);
	compute_layered_child_summary(layer1_tokens, child_start, child_end, params, &child);
	compute_layered_span_errors(&eval, &child, params, &trend_error, &range_error);
	class_id = (trend_error <= range_error) ? RELATIVE_CLASS_TREND : RELATIVE_CLASS_RANGE;
	fill_layered_token(candles,
		features,
		candle_count,
		params,
		layer1_tokens,
		child_start,
		child_end,
		class_id,
		(class_id == RELATIVE_CLASS_TREND) ? trend_error : range_error,
		(class_id == RELATIVE_CLASS_TREND) ? range_error : trend_error,
		out);
}

static int split_layered_edge_outliers(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	for (i = 0u; i < *token_count; ++i) {
		uint32_t child_start = tokens[i].layered_child_start_index;
		uint32_t child_end = tokens[i].layered_child_end_index;
		uint32_t child_count;
		const WaveToken *first_child;
		const WaveToken *last_child;
		int32_t first_move;
		int32_t last_move;
		int32_t remainder_sum = 0;
		int32_t remainder_avg;
		uint32_t c;

		if (child_end < child_start) {
			continue;
		}
		child_count = child_end - child_start + 1u;
		if (child_count < 3u) {
			continue;
		}
		first_child = &layer1_tokens[child_start];
		last_child = &layer1_tokens[child_end];
		first_move = abs_i32(first_child->net_move_raw);
		last_move = abs_i32(last_child->net_move_raw);
		for (c = child_start; c <= child_end; ++c) {
			remainder_sum += abs_i32(layer1_tokens[c].net_move_raw);
		}
		if (remainder_sum <= 0) {
			continue;
		}
		if (first_move >= last_move) {
			remainder_avg = (remainder_sum - first_move) / max_i32((int32_t)child_count - 1, 1);
			if (remainder_avg > 0 &&
				first_move >= remainder_avg * 3) {
				WaveToken edge_token;
				WaveToken rest_token;

				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, child_start, child_start, &edge_token);
				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, child_start + 1u, child_end, &rest_token);
				tokens[i] = edge_token;
				insert_token_at(tokens, token_count, i + 1u, &rest_token);
				return 1;
			}
		}
		remainder_avg = (remainder_sum - last_move) / max_i32((int32_t)child_count - 1, 1);
		if (remainder_avg > 0 &&
			last_move >= remainder_avg * 3) {
			WaveToken rest_token;
			WaveToken edge_token;

			build_best_layered_token(candles, features, candle_count, params, layer1_tokens, child_start, child_end - 1u, &rest_token);
			build_best_layered_token(candles, features, candle_count, params, layer1_tokens, child_end, child_end, &edge_token);
			tokens[i] = rest_token;
			insert_token_at(tokens, token_count, i + 1u, &edge_token);
			return 1;
		}
	}
	return 0;
}

static int split_layered_internal_outliers(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	for (i = 0u; i < *token_count; ++i) {
		uint32_t child_start = tokens[i].layered_child_start_index;
		uint32_t child_end = tokens[i].layered_child_end_index;
		uint32_t child_count;
		uint32_t c;
		int32_t total_move = 0;
		int32_t avg_move;

		if (child_end < child_start) {
			continue;
		}
		child_count = child_end - child_start + 1u;
		if (child_count < 5u) {
			continue;
		}
		for (c = child_start; c <= child_end; ++c) {
			total_move += abs_i32(layer1_tokens[c].net_move_raw);
		}
		avg_move = total_move / max_i32((int32_t)child_count, 1);
		if (avg_move <= 0) {
			continue;
		}
		for (c = child_start + 1u; c + 1u <= child_end; ++c) {
			const WaveToken *curr = &layer1_tokens[c];
			const WaveToken *next = &layer1_tokens[c + 1u];
			int32_t curr_move;
			int32_t next_move;

			if (curr->class_id != PRIMITIVE_CLASS_CONSECUTIVE ||
				next->class_id != PRIMITIVE_CLASS_CONSECUTIVE ||
				curr->dominant_direction == 0 ||
				next->dominant_direction == 0 ||
				curr->dominant_direction == next->dominant_direction) {
				continue;
			}
			curr_move = abs_i32(curr->net_move_raw);
			next_move = abs_i32(next->net_move_raw);
			if (curr_move < avg_move * 2 || next_move < avg_move * 2) {
				continue;
			}
			{
				uint32_t first_start = c;
				uint32_t first_end = c;
				uint32_t second_start = c + 1u;
				uint32_t second_end = c + 1u;
				WaveToken left_token;
				WaveToken first_token;
				WaveToken second_token;
				WaveToken right_token;
				int have_left;
				int have_right;
				uint32_t insert_at = i;

				if (c > child_start) {
					const WaveToken *prev = &layer1_tokens[c - 1u];

					if (prev->class_id == PRIMITIVE_CLASS_ALTERNATING &&
						prev->dominant_direction == curr->dominant_direction &&
						abs_i32(prev->net_move_raw) * 2 <= curr_move) {
						first_start = c - 1u;
					}
				}
				if (c + 2u <= child_end) {
					const WaveToken *after = &layer1_tokens[c + 2u];

					if (after->class_id == PRIMITIVE_CLASS_ALTERNATING &&
						after->dominant_direction == next->dominant_direction &&
						abs_i32(after->net_move_raw) * 2 <= next_move) {
						second_end = c + 2u;
					}
				}
				have_left = (first_start > child_start);
				have_right = (second_end < child_end);
				if (have_left) {
					build_best_layered_token(candles, features, candle_count, params, layer1_tokens, child_start, first_start - 1u, &left_token);
					tokens[insert_at] = left_token;
					insert_at += 1u;
				}
				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, first_start, first_end, &first_token);
				if (!have_left) {
					tokens[i] = first_token;
					insert_at = i + 1u;
				} else {
					insert_token_at(tokens, token_count, insert_at, &first_token);
					insert_at += 1u;
				}
				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, second_start, second_end, &second_token);
				insert_token_at(tokens, token_count, insert_at, &second_token);
				insert_at += 1u;
				if (have_right) {
					build_best_layered_token(candles, features, candle_count, params, layer1_tokens, second_end + 1u, child_end, &right_token);
					insert_token_at(tokens, token_count, insert_at, &right_token);
				}
				return 1;
			}
		}
	}
	return 0;
}

static int split_layered_internal_range_islands(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	for (i = 0u; i < *token_count; ++i) {
		uint32_t child_start = tokens[i].layered_child_start_index;
		uint32_t child_end = tokens[i].layered_child_end_index;
		uint32_t mid_start;
		int64_t best_rank = 0x3fffffffffffffffLL;
		WaveToken best_left;
		WaveToken best_mid;
		WaveToken best_right;
		int found = 0;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND || child_end <= child_start + 3u) {
			continue;
		}
		for (mid_start = child_start + 1u; mid_start + 1u < child_end; ++mid_start) {
			uint32_t mid_end;

			for (mid_end = mid_start; mid_end + 1u <= child_end; ++mid_end) {
				WaveToken left_token;
				WaveToken mid_token;
				WaveToken right_token;
				int32_t left_move;
				int32_t mid_move;
				int32_t right_move;
				int32_t surrounding_move;
				int64_t split_score;
				int64_t rank;

				if (mid_start <= child_start || mid_end >= child_end) {
					continue;
				}
				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, child_start, mid_start - 1u, &left_token);
				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, mid_start, mid_end, &mid_token);
				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, mid_end + 1u, child_end, &right_token);
				if (left_token.class_id != RELATIVE_CLASS_TREND ||
					mid_token.class_id != RELATIVE_CLASS_RANGE ||
					right_token.class_id != RELATIVE_CLASS_TREND) {
					continue;
				}
				if (left_token.dominant_direction == 0 ||
					left_token.dominant_direction != right_token.dominant_direction) {
					continue;
				}
				left_move = abs_i32(left_token.net_move_raw);
				mid_move = abs_i32(mid_token.net_move_raw);
				right_move = abs_i32(right_token.net_move_raw);
				surrounding_move = min_i32(left_move, right_move);
				if (mid_move > max_i32(surrounding_move / 2, params->minimum_pips_for_trend * 4)) {
					continue;
				}
				if (mid_token.layered_alternating_count == 0u ||
					mid_token.layered_alternating_count < mid_token.layered_sequential_count) {
					continue;
				}
				if (mid_token.sign_change_density < max_i32(params->relative_range_sign_flip_min - 120, 180)) {
					continue;
				}
				split_score = (int64_t)left_token.fit_error +
					(int64_t)mid_token.fit_error +
					(int64_t)right_token.fit_error +
					(int64_t)params->segment_penalty_lambda * 2;
				rank = (int64_t)mid_move * 1000 +
					(int64_t)split_score -
					(int64_t)(left_move + right_move);
				if (!found || rank < best_rank) {
					best_rank = rank;
					best_left = left_token;
					best_mid = mid_token;
					best_right = right_token;
					found = 1;
				}
			}
		}
		if (found) {
			tokens[i] = best_left;
			insert_token_at(tokens, token_count, i + 1u, &best_mid);
			insert_token_at(tokens, token_count, i + 2u, &best_right);
			return 1;
		}
	}
	return 0;
}

static int split_layered_cross_token_range_islands(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	if (*token_count < 2u) {
		return 0;
	}
	for (i = 0u; i + 1u < *token_count; ++i) {
		uint32_t child_start;
		uint32_t child_end;
		uint32_t mid_start;
		int64_t best_rank = 0x3fffffffffffffffLL;
		WaveToken best_left;
		WaveToken best_mid;
		WaveToken best_right;
		int found = 0;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND ||
			tokens[i + 1u].class_id != RELATIVE_CLASS_TREND ||
			tokens[i].dominant_direction == 0 ||
			tokens[i].dominant_direction != tokens[i + 1u].dominant_direction) {
			continue;
		}
		if (tokens[i].layered_child_end_index + 1u != tokens[i + 1u].layered_child_start_index) {
			continue;
		}
		child_start = tokens[i].layered_child_start_index;
		child_end = tokens[i + 1u].layered_child_end_index;
		if (child_end <= child_start + 3u) {
			continue;
		}
		for (mid_start = child_start + 1u; mid_start + 1u < child_end; ++mid_start) {
			uint32_t mid_end;

			for (mid_end = mid_start; mid_end + 1u <= child_end; ++mid_end) {
				WaveToken left_token;
				WaveToken mid_token;
				WaveToken right_token;
				int32_t left_move;
				int32_t mid_move;
				int32_t right_move;
				int32_t surrounding_move;
				int64_t split_score;
				int64_t rank;

				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, child_start, mid_start - 1u, &left_token);
				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, mid_start, mid_end, &mid_token);
				build_best_layered_token(candles, features, candle_count, params, layer1_tokens, mid_end + 1u, child_end, &right_token);
				if (left_token.class_id != RELATIVE_CLASS_TREND ||
					mid_token.class_id != RELATIVE_CLASS_RANGE ||
					right_token.class_id != RELATIVE_CLASS_TREND) {
					continue;
				}
				if (left_token.dominant_direction == 0 ||
					left_token.dominant_direction != right_token.dominant_direction) {
					continue;
				}
				left_move = abs_i32(left_token.net_move_raw);
				mid_move = abs_i32(mid_token.net_move_raw);
				right_move = abs_i32(right_token.net_move_raw);
				surrounding_move = min_i32(left_move, right_move);
				if (mid_move > max_i32(surrounding_move / 2, params->minimum_pips_for_trend * 4)) {
					continue;
				}
				if (mid_token.layered_alternating_count == 0u ||
					mid_token.layered_alternating_count < mid_token.layered_sequential_count) {
					continue;
				}
				if (mid_token.sign_change_density < max_i32(params->relative_range_sign_flip_min - 120, 180)) {
					continue;
				}
				split_score = (int64_t)left_token.fit_error +
					(int64_t)mid_token.fit_error +
					(int64_t)right_token.fit_error +
					(int64_t)params->segment_penalty_lambda;
				rank = (int64_t)mid_move * 1000 +
					(int64_t)split_score -
					(int64_t)(left_move + right_move);
				if (!found || rank < best_rank) {
					best_rank = rank;
					best_left = left_token;
					best_mid = mid_token;
					best_right = right_token;
					found = 1;
				}
			}
		}
		if (found) {
			tokens[i] = best_left;
			tokens[i + 1u] = best_mid;
			insert_token_at(tokens, token_count, i + 2u, &best_right);
			return 1;
		}
	}
	return 0;
}

static int merge_same_direction_layered_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	if (*token_count < 2u) {
		return 0;
	}
	for (i = 0u; i + 1u < *token_count; ++i) {
		WaveToken merged_token;
		const WaveToken *boundary_child;
		int32_t boundary_move;
		uint32_t child_start = tokens[i].layered_child_start_index;
		uint32_t child_end = tokens[i + 1u].layered_child_end_index;
		int64_t merged_rank;
		int64_t split_rank;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND ||
			tokens[i + 1u].class_id != RELATIVE_CLASS_TREND ||
			tokens[i].dominant_direction == 0 ||
			tokens[i].dominant_direction != tokens[i + 1u].dominant_direction) {
			continue;
		}
		if (tokens[i].layered_child_end_index + 1u != tokens[i + 1u].layered_child_start_index) {
			continue;
		}
		boundary_child = &layer1_tokens[tokens[i + 1u].layered_child_start_index];
		if (boundary_child->class_id != PRIMITIVE_CLASS_CONSECUTIVE ||
			boundary_child->dominant_direction == 0 ||
			boundary_child->dominant_direction == tokens[i].dominant_direction) {
			continue;
		}
		boundary_move = abs_i32(boundary_child->net_move_raw);
		if (boundary_move > min_i32(abs_i32(tokens[i].net_move_raw), abs_i32(tokens[i + 1u].net_move_raw)) / 3) {
			continue;
		}
		build_best_layered_token(candles, features, candle_count, params, layer1_tokens, child_start, child_end, &merged_token);
		if (merged_token.class_id != RELATIVE_CLASS_TREND ||
			merged_token.dominant_direction != tokens[i].dominant_direction) {
			continue;
		}
		merged_rank = layered_trend_rank(&merged_token, params);
		split_rank = layered_trend_rank(&tokens[i], params) +
			layered_trend_rank(&tokens[i + 1u], params) +
			(int64_t)params->segment_penalty_lambda;
		if (merged_rank > split_rank) {
			continue;
		}
		tokens[i] = merged_token;
		remove_token_at(tokens, token_count, i + 1u);
		return 1;
	}
	return 0;
}

static int merge_adjacent_layered_ranges(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	if (*token_count < 2u) {
		return 0;
	}
	for (i = 0u; i + 1u < *token_count; ++i) {
		WaveToken merged_token;
		int64_t merged_rank;
		int64_t split_rank;
		LayeredChildSummary merged_child;
		int32_t merged_seq_cancel_ratio = 0;

		if (tokens[i].class_id != RELATIVE_CLASS_RANGE ||
			tokens[i + 1u].class_id != RELATIVE_CLASS_RANGE) {
			continue;
		}
		if (tokens[i].layered_child_end_index + 1u != tokens[i + 1u].layered_child_start_index) {
			continue;
		}
		build_best_layered_token(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens[i].layered_child_start_index,
			tokens[i + 1u].layered_child_end_index,
			&merged_token);
		compute_layered_child_summary(layer1_tokens,
			tokens[i].layered_child_start_index,
			tokens[i + 1u].layered_child_end_index,
			params,
			&merged_child);
		if (merged_child.sequential_net_move_sum_raw > 0) {
			int32_t merged_seq_min = min_i32(merged_child.sequential_bull_net_move_sum_raw,
				merged_child.sequential_bear_net_move_sum_raw);

			merged_seq_cancel_ratio = (int32_t)(((int64_t)merged_seq_min * params->norm_scale) /
				max_i32(merged_child.sequential_net_move_sum_raw, 1));
		}
		if (merged_token.class_id != RELATIVE_CLASS_RANGE) {
			if (merged_child.sequential_bull_net_move_sum_raw <= 0 ||
				merged_child.sequential_bear_net_move_sum_raw <= 0 ||
				merged_seq_cancel_ratio < 300 ||
				abs_i32(merged_token.net_move_raw) > params->minimum_pips_for_trend * 12) {
				continue;
			}
		}
		merged_rank = layered_range_rank(&merged_token, params);
		if (merged_child.sequential_bull_net_move_sum_raw > 0 &&
			merged_child.sequential_bear_net_move_sum_raw > 0) {
			merged_rank -= (int64_t)merged_seq_cancel_ratio * 3;
		}
		split_rank = layered_range_rank(&tokens[i], params) +
			layered_range_rank(&tokens[i + 1u], params) +
			(int64_t)params->segment_penalty_lambda;
		if (merged_rank > split_rank) {
			continue;
		}
		tokens[i] = merged_token;
		remove_token_at(tokens, token_count, i + 1u);
		return 1;
	}
	return 0;
}

static int merge_adjacent_layered_tokens_into_range(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	if (*token_count < 2u) {
		return 0;
	}
	for (i = 0u; i + 1u < *token_count; ++i) {
		WaveToken merged_token;
		LayeredChildSummary merged_child;
		int64_t merged_rank;
		int64_t split_rank;
		int32_t seq_balance_ratio = 0;

		if (token_is_layered_separator_only(&tokens[i]) ||
			token_is_layered_separator_only(&tokens[i + 1u])) {
			continue;
		}
		if (tokens[i].layered_child_end_index + 1u != tokens[i + 1u].layered_child_start_index) {
			continue;
		}
		build_best_layered_token(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens[i].layered_child_start_index,
			tokens[i + 1u].layered_child_end_index,
			&merged_token);
		if (merged_token.class_id != RELATIVE_CLASS_RANGE) {
			continue;
		}
		compute_layered_child_summary(layer1_tokens,
			tokens[i].layered_child_start_index,
			tokens[i + 1u].layered_child_end_index,
			params,
			&merged_child);
		if (merged_child.sequential_net_move_sum_raw > 0) {
			seq_balance_ratio = (int32_t)(((int64_t)min_i32(merged_child.sequential_bull_net_move_sum_raw,
					merged_child.sequential_bear_net_move_sum_raw) * 2 * params->norm_scale) /
				max_i32(merged_child.sequential_net_move_sum_raw, 1));
		}
		merged_rank = layered_range_rank(&merged_token, params);
		if (seq_balance_ratio > 0) {
			merged_rank -= (int64_t)seq_balance_ratio * 2;
		}
		split_rank = layered_token_rank(&tokens[i], params) +
			layered_token_rank(&tokens[i + 1u], params) +
			(int64_t)params->segment_penalty_lambda;
		if (merged_rank > split_rank) {
			continue;
		}
		tokens[i] = merged_token;
		remove_token_at(tokens, token_count, i + 1u);
		return 1;
	}
	return 0;
}

static int token_is_layered_separator_only(const WaveToken *tok)
{
	if (tok == NULL) {
		return 0;
	}
	return tok->layered_sequential_count == 0u &&
		tok->layered_alternating_count == 0u;
}

static int merge_same_direction_layered_trends_across_separator(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	if (*token_count < 3u) {
		return 0;
	}
	for (i = 0u; i + 2u < *token_count; ++i) {
		WaveToken merged_token;
		uint32_t child_start;
		uint32_t child_end;
		int32_t separator_move;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND ||
			tokens[i + 2u].class_id != RELATIVE_CLASS_TREND ||
			tokens[i].dominant_direction == 0 ||
			tokens[i].dominant_direction != tokens[i + 2u].dominant_direction) {
			continue;
		}
		if (!token_is_layered_separator_only(&tokens[i + 1u])) {
			continue;
		}
		separator_move = abs_i32(tokens[i + 1u].net_move_raw);
		if (separator_move > min_i32(abs_i32(tokens[i].net_move_raw), abs_i32(tokens[i + 2u].net_move_raw)) / 4) {
			continue;
		}
		if (tokens[i].layered_child_end_index + 1u != tokens[i + 1u].layered_child_start_index ||
			tokens[i + 1u].layered_child_end_index + 1u != tokens[i + 2u].layered_child_start_index) {
			continue;
		}
		child_start = tokens[i].layered_child_start_index;
		child_end = tokens[i + 2u].layered_child_end_index;
		build_layered_token_for_fixed_class(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			child_start,
			child_end,
			RELATIVE_CLASS_TREND,
			&merged_token,
			NULL);
		if (merged_token.dominant_direction != tokens[i].dominant_direction) {
			continue;
		}
		tokens[i] = merged_token;
		remove_token_at(tokens, token_count, i + 1u);
		remove_token_at(tokens, token_count, i + 1u);
		return 1;
	}
	return 0;
}

static int absorb_layered_separator_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	if (*token_count == 0u) {
		return 0;
	}
	for (i = 0u; i < *token_count; ++i) {
		WaveToken merged_token;
		uint32_t child_start;
		uint32_t child_end;

		if (!token_is_layered_separator_only(&tokens[i])) {
			continue;
		}
		if (i > 0u &&
			i + 1u < *token_count &&
			tokens[i - 1u].class_id == RELATIVE_CLASS_TREND &&
			tokens[i + 1u].class_id == RELATIVE_CLASS_TREND &&
			tokens[i - 1u].dominant_direction != 0 &&
			tokens[i - 1u].dominant_direction == tokens[i + 1u].dominant_direction &&
			tokens[i - 1u].layered_child_end_index + 1u == tokens[i].layered_child_start_index &&
			tokens[i].layered_child_end_index + 1u == tokens[i + 1u].layered_child_start_index) {
			child_start = tokens[i - 1u].layered_child_start_index;
			child_end = tokens[i + 1u].layered_child_end_index;
			build_layered_token_for_fixed_class(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				child_start,
				child_end,
				RELATIVE_CLASS_TREND,
				&merged_token,
				NULL);
			merged_token.dominant_direction = tokens[i - 1u].dominant_direction;
			tokens[i - 1u] = merged_token;
			remove_token_at(tokens, token_count, i);
			remove_token_at(tokens, token_count, i);
			return 1;
		}
		if (i > 0u) {
			child_start = tokens[i - 1u].layered_child_start_index;
			child_end = tokens[i].layered_child_end_index;
			build_layered_token_for_fixed_class(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				child_start,
				child_end,
				tokens[i - 1u].class_id,
				&merged_token,
				NULL);
			tokens[i - 1u] = merged_token;
			remove_token_at(tokens, token_count, i);
			return 1;
		}
		if (i + 1u < *token_count) {
			child_start = tokens[i].layered_child_start_index;
			child_end = tokens[i + 1u].layered_child_end_index;
			build_layered_token_for_fixed_class(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				child_start,
				child_end,
				tokens[i + 1u].class_id,
				&merged_token,
				NULL);
			tokens[i] = merged_token;
			remove_token_at(tokens, token_count, i + 1u);
			return 1;
		}
	}
	return 0;
}

static int extract_leading_layered_trend_into_following_range(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	if (*token_count < 2u) {
		return 0;
	}
	for (i = 0u; i + 1u < *token_count; ++i) {
		uint32_t child_start;
		uint32_t child_end;
		const WaveToken *lead_child;
		WaveToken lead_token;
		WaveToken merged_range;
		int32_t lead_move;
		int32_t remainder_sum = 0;
		int32_t remainder_avg;
		int32_t tail_max_move = 0;
		int32_t tail_net_move = 0;
		int seen_bull = 0;
		int seen_bear = 0;
		uint32_t c;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND ||
			tokens[i + 1u].class_id != RELATIVE_CLASS_RANGE) {
			continue;
		}
		child_start = tokens[i].layered_child_start_index;
		child_end = tokens[i].layered_child_end_index;
		if (child_end < child_start + 2u) {
			continue;
		}
		lead_child = &layer1_tokens[child_start];
		lead_move = abs_i32(lead_child->net_move_raw);
		for (c = child_start + 1u; c <= child_end; ++c) {
			int32_t move = abs_i32(layer1_tokens[c].net_move_raw);

			remainder_sum += move;
			tail_max_move = max_i32(tail_max_move, move);
		}
		for (c = child_start + 1u; c <= tokens[i + 1u].layered_child_end_index; ++c) {
			tail_net_move += layer1_tokens[c].net_move_raw;
			if (layer1_tokens[c].dominant_direction > 0) {
				seen_bull = 1;
			} else if (layer1_tokens[c].dominant_direction < 0) {
				seen_bear = 1;
			}
		}
		remainder_avg = remainder_sum / max_i32((int32_t)(child_end - child_start), 1);
		if (remainder_avg <= 0 || lead_move < remainder_avg * 3) {
			continue;
		}
		build_best_layered_token(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			child_start,
			child_start,
			&lead_token);
		build_best_layered_token(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			child_start + 1u,
			tokens[i + 1u].layered_child_end_index,
			&merged_range);
		if (lead_token.class_id != RELATIVE_CLASS_TREND) {
			continue;
		}
		if (merged_range.class_id != RELATIVE_CLASS_RANGE) {
			if (!(seen_bull && seen_bear) ||
				tail_max_move > max_i32(lead_move / 2, params->minimum_pips_for_trend * 10) ||
				abs_i32(tail_net_move) > max_i32(lead_move / 2, params->minimum_pips_for_trend * 8)) {
				continue;
			}
			build_layered_token_for_fixed_class(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				child_start + 1u,
				tokens[i + 1u].layered_child_end_index,
				RELATIVE_CLASS_RANGE,
				&merged_range,
				NULL);
		}
		tokens[i] = lead_token;
		tokens[i + 1u] = merged_range;
		return 1;
	}
	return 0;
}

static int extract_layered_dominant_trend_blocks(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	for (i = 0u; i < *token_count; ++i) {
		uint32_t child_start = tokens[i].layered_child_start_index;
		uint32_t child_end = tokens[i].layered_child_end_index;
		uint32_t child_count;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND ||
			tokens[i].dominant_direction == 0 ||
			child_end < child_start) {
			continue;
		}
		child_count = child_end - child_start + 1u;
		if (child_count < 5u) {
			continue;
		}

		for (int side = 0; side < 2; ++side) {
			uint32_t block_start;
			uint32_t block_end;
			const WaveToken *edge_a;
			const WaveToken *edge_b;
			const WaveToken *connector;
			int32_t edge_a_move;
			int32_t edge_b_move;
			int32_t connector_move;
			int32_t outside_sum = 0;
			int32_t outside_avg;
			uint32_t c;
			WaveToken main_token;
			WaveToken block_token;

			if (side == 0) {
				block_start = child_start;
				block_end = child_start + 2u;
			} else {
				block_start = child_end - 2u;
				block_end = child_end;
			}
			edge_a = &layer1_tokens[block_start];
			connector = &layer1_tokens[block_start + 1u];
			edge_b = &layer1_tokens[block_end];
			if (edge_a->class_id != PRIMITIVE_CLASS_CONSECUTIVE ||
				edge_b->class_id != PRIMITIVE_CLASS_CONSECUTIVE ||
				edge_a->dominant_direction != tokens[i].dominant_direction ||
				edge_b->dominant_direction != tokens[i].dominant_direction) {
				continue;
			}
			if (connector->dominant_direction == tokens[i].dominant_direction) {
				continue;
			}
			if (connector->class_id != PRIMITIVE_CLASS_ALTERNATING &&
				connector->class_id != PRIMITIVE_CLASS_CONSECUTIVE) {
				continue;
			}
			edge_a_move = abs_i32(edge_a->net_move_raw);
			edge_b_move = abs_i32(edge_b->net_move_raw);
			connector_move = abs_i32(connector->net_move_raw);
			for (c = child_start; c <= child_end; ++c) {
				if (c >= block_start && c <= block_end) {
					continue;
				}
				outside_sum += abs_i32(layer1_tokens[c].net_move_raw);
			}
			outside_avg = outside_sum / max_i32((int32_t)child_count - 3, 1);
			if (outside_avg <= 0) {
				continue;
			}
			if (edge_a_move < outside_avg * 2 ||
				edge_b_move < outside_avg * 2) {
				continue;
			}
			if (connector_move > max_i32(min_i32(edge_a_move, edge_b_move) * 2 / 3,
				outside_avg * 2)) {
				continue;
			}
			build_best_layered_token(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				block_start,
				block_end,
				&block_token);
			if (block_token.class_id != RELATIVE_CLASS_TREND ||
				block_token.dominant_direction != tokens[i].dominant_direction) {
				continue;
			}
			if (side == 0) {
				build_best_layered_token(candles,
					features,
					candle_count,
					params,
					layer1_tokens,
					block_end + 1u,
					child_end,
					&main_token);
				tokens[i] = block_token;
				insert_token_at(tokens, token_count, i + 1u, &main_token);
			} else {
				build_best_layered_token(candles,
					features,
					candle_count,
					params,
					layer1_tokens,
					child_start,
					block_start - 1u,
					&main_token);
				tokens[i] = main_token;
				insert_token_at(tokens, token_count, i + 1u, &block_token);
			}
			return 1;
		}
	}
	return 0;
}

static int trim_layered_unbridged_alternating_edges_from_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	for (i = 0u; i < *token_count; ++i) {
		uint32_t child_start = tokens[i].layered_child_start_index;
		uint32_t child_end = tokens[i].layered_child_end_index;
		uint32_t child_count;
		WaveToken remainder_token;
		WaveToken edge_token;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND ||
			tokens[i].dominant_direction == 0 ||
			child_end < child_start) {
			continue;
		}
		child_count = child_end - child_start + 1u;
		if (child_count < 2u) {
			continue;
		}
		if (layer1_tokens[child_start].class_id == PRIMITIVE_CLASS_ALTERNATING) {
			build_best_layered_token(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				child_start + 1u,
				child_end,
				&remainder_token);
			if (remainder_token.class_id == RELATIVE_CLASS_TREND &&
				remainder_token.dominant_direction == tokens[i].dominant_direction) {
				build_best_layered_token(candles,
					features,
					candle_count,
					params,
					layer1_tokens,
					child_start,
					child_start,
					&edge_token);
				tokens[i] = edge_token;
				insert_token_at(tokens, token_count, i + 1u, &remainder_token);
				return 1;
			}
		}
		if (layer1_tokens[child_end].class_id == PRIMITIVE_CLASS_ALTERNATING) {
			build_best_layered_token(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				child_start,
				child_end - 1u,
				&remainder_token);
			if (remainder_token.class_id == RELATIVE_CLASS_TREND &&
				remainder_token.dominant_direction == tokens[i].dominant_direction) {
				build_best_layered_token(candles,
					features,
					candle_count,
					params,
					layer1_tokens,
					child_end,
					child_end,
					&edge_token);
				tokens[i] = remainder_token;
				insert_token_at(tokens, token_count, i + 1u, &edge_token);
				return 1;
			}
		}
	}
	return 0;
}

static int extract_layered_suffix_trend_from_mixed_trend(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *layer1_tokens,
	WaveToken *tokens,
	uint32_t *token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || layer1_tokens == NULL || tokens == NULL || token_count == NULL) {
		return -1;
	}
	for (i = 0u; i < *token_count; ++i) {
		uint32_t child_start = tokens[i].layered_child_start_index;
		uint32_t child_end = tokens[i].layered_child_end_index;
		uint32_t child_count;
		const WaveToken *suffix_child;
		int32_t suffix_move;
		int32_t prefix_sum = 0;
		int32_t prefix_avg;
		int32_t prefix_aligned_seq = 0;
		int32_t prefix_counter_seq = 0;
		int32_t prefix_aligned_alt = 0;
		int32_t prefix_counter_alt = 0;
		uint32_t c;
		WaveToken prefix_token;
		WaveToken suffix_token;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND ||
			tokens[i].dominant_direction == 0 ||
			child_end <= child_start) {
			continue;
		}
		child_count = child_end - child_start + 1u;
		if (child_count < 2u) {
			continue;
		}
		suffix_child = &layer1_tokens[child_end];
		if (suffix_child->class_id != PRIMITIVE_CLASS_CONSECUTIVE ||
			suffix_child->dominant_direction != tokens[i].dominant_direction) {
			continue;
		}
		suffix_move = abs_i32(suffix_child->net_move_raw);
		for (c = child_start; c < child_end; ++c) {
			int32_t move = abs_i32(layer1_tokens[c].net_move_raw);

			prefix_sum += move;
			if (layer1_tokens[c].class_id == PRIMITIVE_CLASS_CONSECUTIVE) {
				if (layer1_tokens[c].dominant_direction == tokens[i].dominant_direction) {
					prefix_aligned_seq += move;
				} else if (layer1_tokens[c].dominant_direction != 0) {
					prefix_counter_seq += move;
				}
			} else {
				if (layer1_tokens[c].dominant_direction == tokens[i].dominant_direction) {
					prefix_aligned_alt += move;
				} else if (layer1_tokens[c].dominant_direction != 0) {
					prefix_counter_alt += move;
				}
			}
		}
		prefix_avg = prefix_sum / max_i32((int32_t)child_count - 1, 1);
		if (prefix_avg <= 0) {
			continue;
		}
		if (suffix_move < prefix_avg * 2) {
			continue;
		}
		if (prefix_aligned_seq > prefix_counter_seq) {
			continue;
		}
		if (prefix_aligned_alt > prefix_counter_alt) {
			continue;
		}
		build_best_layered_token(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			child_start,
			child_end - 1u,
			&prefix_token);
		build_best_layered_token(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			child_end,
			child_end,
			&suffix_token);
		if (suffix_token.class_id != RELATIVE_CLASS_TREND ||
			suffix_token.dominant_direction != tokens[i].dominant_direction) {
			continue;
		}
		if (prefix_token.class_id != RELATIVE_CLASS_RANGE) {
			build_layered_token_for_fixed_class(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				child_start,
				child_end - 1u,
				RELATIVE_CLASS_RANGE,
				&prefix_token,
				NULL);
		}
		tokens[i] = prefix_token;
		insert_token_at(tokens, token_count, i + 1u, &suffix_token);
		return 1;
	}
	return 0;
}

static int build_layered_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count,
	WaveToken **out_layer1_tokens,
	uint32_t *out_layer1_token_count)
{
	WaveToken *layer1_tokens;
	uint32_t layer1_count = 0u;
	int64_t *dp_cost;
	int32_t *parent;
	uint32_t *parent_class;
	int32_t *best_fit;
	int32_t *best_second;
	uint32_t end_index;
	uint32_t token_count = 0u;
	uint32_t child_limit;
	uint32_t boundary_opt_limit;
	uint32_t rewrite_limit;
	uint32_t absorb_limit;
	int rc = 0;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL ||
		out_token_count == NULL || out_layer1_tokens == NULL || out_layer1_token_count == NULL) {
		return -1;
	}
	*out_token_count = 0u;
	*out_layer1_tokens = NULL;
	*out_layer1_token_count = 0u;
	layer1_tokens = (WaveToken *)malloc((size_t)candle_count * sizeof(*layer1_tokens));
	if (layer1_tokens == NULL) {
		return -1;
	}
	rc = build_layered_layer1_tokens(candles, features, candle_count, params, layer1_tokens, &layer1_count);
	if (rc != 0) {
		free(layer1_tokens);
		return rc;
	}
	dp_cost = (int64_t *)malloc((size_t)(layer1_count + 1u) * sizeof(*dp_cost));
	parent = (int32_t *)malloc((size_t)(layer1_count + 1u) * sizeof(*parent));
	parent_class = (uint32_t *)malloc((size_t)(layer1_count + 1u) * sizeof(*parent_class));
	best_fit = (int32_t *)malloc((size_t)(layer1_count + 1u) * sizeof(*best_fit));
	best_second = (int32_t *)malloc((size_t)(layer1_count + 1u) * sizeof(*best_second));
	if (dp_cost == NULL || parent == NULL || parent_class == NULL || best_fit == NULL || best_second == NULL) {
		free(layer1_tokens);
		free(dp_cost);
		free(parent);
		free(parent_class);
		free(best_fit);
		free(best_second);
		return -1;
	}
	for (end_index = 0u; end_index <= layer1_count; ++end_index) {
		dp_cost[end_index] = 0x3fffffffffffffffLL;
		parent[end_index] = -1;
		parent_class[end_index] = RELATIVE_CLASS_RANGE;
		best_fit[end_index] = 0x3fffffff;
		best_second[end_index] = 0x3fffffff;
	}
	dp_cost[0] = 0;
	child_limit = max_u32(48u, params->lmax_candles / 2u);
	for (end_index = 1u; end_index <= layer1_count; ++end_index) {
		uint32_t start_index = (end_index > child_limit) ? (end_index - child_limit) : 0u;
		uint32_t cand_start;

		for (cand_start = end_index; cand_start > start_index; --cand_start) {
			uint32_t child_start = cand_start - 1u;
			uint32_t child_end = end_index - 1u;
			WaveSegmentEval eval;
			LayeredChildSummary child;
			int32_t trend_error;
			int32_t range_error;
			int32_t chosen_error;
			int32_t second_error;
			uint32_t class_id;
			int64_t total_cost;

			compute_segment_eval(candles,
				features,
				candle_count,
				params,
				layer1_tokens[child_start].start_candle,
				layer1_tokens[child_end].end_candle,
				&eval);
			compute_layered_child_summary(layer1_tokens, child_start, child_end, params, &child);
			compute_layered_span_errors(&eval, &child, params, &trend_error, &range_error);
			if (trend_error <= range_error) {
				class_id = RELATIVE_CLASS_TREND;
				chosen_error = trend_error;
				second_error = range_error;
			} else {
				class_id = RELATIVE_CLASS_RANGE;
				chosen_error = range_error;
				second_error = trend_error;
			}
			total_cost = dp_cost[child_start] +
				(int64_t)chosen_error +
				(int64_t)params->segment_penalty_lambda +
				(int64_t)layered_dp_class_bias(&eval,
					params,
					layer1_tokens[child_start].start_candle,
					layer1_tokens[child_end].end_candle,
					class_id);
			if (total_cost >= dp_cost[end_index]) {
				continue;
			}
			dp_cost[end_index] = total_cost;
			parent[end_index] = (int32_t)child_start;
			parent_class[end_index] = class_id;
			best_fit[end_index] = chosen_error;
			best_second[end_index] = second_error;
		}
	}
	if (parent[layer1_count] < 0) {
		free(layer1_tokens);
		free(dp_cost);
		free(parent);
		free(parent_class);
		free(best_fit);
		free(best_second);
		return -2;
	}
	end_index = layer1_count;
	while (end_index > 0u) {
		uint32_t child_start = (uint32_t)parent[end_index];
		fill_layered_token(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			child_start,
			end_index - 1u,
			parent_class[end_index],
			best_fit[end_index],
			best_second[end_index],
			&tokens[token_count]);
		token_count += 1u;
		end_index = child_start;
	}
	if (token_count > 1u) {
		qsort(tokens, token_count, sizeof(*tokens), compare_wave_token_start);
	}
	boundary_opt_limit = max_u32(8u, token_count * 2u);
	rewrite_limit = max_u32(64u, token_count * 4u);
	absorb_limit = max_u32(128u, token_count * 4u);
	if (token_count > 1u) {
		for (uint32_t iter = 0u; iter < boundary_opt_limit; ++iter) {
			int opt_rc = optimize_layered_token_boundaries(candles,
				features,
				candle_count,
				params,
				layer1_tokens,
				tokens,
				token_count);
			if (opt_rc <= 0) {
				break;
			}
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = split_layered_edge_outliers(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = split_layered_internal_outliers(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = split_layered_internal_range_islands(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = split_layered_cross_token_range_islands(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int merge_rc = merge_same_direction_layered_trends(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (merge_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int merge_rc = merge_same_direction_layered_trends_across_separator(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (merge_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int merge_rc = merge_adjacent_layered_ranges(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (merge_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int merge_rc = merge_adjacent_layered_tokens_into_range(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (merge_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int rewrite_rc = extract_leading_layered_trend_into_following_range(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (rewrite_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = extract_layered_dominant_trend_blocks(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = trim_layered_unbridged_alternating_edges_from_trends(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = extract_layered_suffix_trend_from_mixed_trend(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	reclassify_layered_tokens(candles,
		features,
		candle_count,
		params,
		layer1_tokens,
		tokens,
		token_count);
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int merge_rc = merge_adjacent_layered_ranges(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (merge_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int merge_rc = merge_adjacent_layered_tokens_into_range(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (merge_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < absorb_limit; ++iter) {
		int absorb_rc = absorb_layered_separator_tokens(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (absorb_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = trim_layered_unbridged_alternating_edges_from_trends(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	for (uint32_t iter = 0u; iter < rewrite_limit; ++iter) {
		int split_rc = extract_layered_suffix_trend_from_mixed_trend(candles,
			features,
			candle_count,
			params,
			layer1_tokens,
			tokens,
			&token_count);
		if (split_rc <= 0) {
			break;
		}
	}
	reclassify_layered_tokens(candles,
		features,
		candle_count,
		params,
		layer1_tokens,
		tokens,
		token_count);
	if (validate_token_sequence(tokens, token_count, candle_count) != 0) {
		log_invalid_token_sequence("build_layered_tokens", tokens, token_count, candle_count);
		free(layer1_tokens);
		free(dp_cost);
		free(parent);
		free(parent_class);
		free(best_fit);
		free(best_second);
		return -2;
	}
	free(dp_cost);
	free(parent);
	free(parent_class);
	free(best_fit);
	free(best_second);
	*out_token_count = token_count;
	*out_layer1_tokens = layer1_tokens;
	*out_layer1_token_count = layer1_count;
	return 0;
}

static int build_trend_priority_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *out_token_count,
	WaveToken **out_layer1_tokens,
	uint32_t *out_layer1_token_count)
{
	TrendLockSpan *locks = NULL;
	uint32_t lock_count = 0;
	uint32_t lock_index;
	uint32_t cursor = 0;
	uint32_t token_count = 0;
	int rc;

	if (out_token_count == NULL || out_layer1_tokens == NULL || out_layer1_token_count == NULL) {
		return -1;
	}
	*out_token_count = 0;
	*out_layer1_tokens = NULL;
	*out_layer1_token_count = 0u;
	if (params->wave_model == WAVE_MODEL_RELATIVE) {
		return build_relative_tokens(candles, features, candle_count, params, tokens, out_token_count);
	}
	if (params->wave_model == WAVE_MODEL_RELATIVE_2_STAGE) {
		return build_relative_two_stage_tokens(candles, features, candle_count, params, tokens, out_token_count);
	}
	if (is_relative_seeded_family((uint32_t)params->wave_model)) {
		return build_relative_seeded_tokens(candles, features, candle_count, params, tokens, out_token_count);
	}
	if (params->wave_model == WAVE_MODEL_PRIMITIVE) {
		return build_primitive_rule_first_tokens(candles, features, candle_count, params, tokens, out_token_count);
	}
	if (params->wave_model == WAVE_MODEL_LAYERED) {
		return build_layered_tokens(candles,
			features,
			candle_count,
			params,
			tokens,
			out_token_count,
			out_layer1_tokens,
			out_layer1_token_count);
	}
	if (!is_trend_priority_enabled(params)) {
		return append_gpu_gap_tokens(candles, features, params, 0u, candle_count - 1u, tokens, out_token_count, 0);
	}

	locks = (TrendLockSpan *)malloc((size_t)candle_count * sizeof(*locks));
	if (locks == NULL) {
		return -1;
	}
	rc = detect_priority_spans(candles, features, candle_count, params, locks, &lock_count);
	if (rc != 0) {
		free(locks);
		return rc;
	}
	if (lock_count == 0u) {
		free(locks);
		return append_gpu_gap_tokens(candles, features, params, 0u, candle_count - 1u, tokens, out_token_count, 1);
	}

	for (lock_index = 0u; lock_index < lock_count; ++lock_index) {
		if (cursor < locks[lock_index].start) {
			rc = append_gpu_gap_tokens(candles,
				features,
				params,
				cursor,
				locks[lock_index].start - 1u,
				tokens,
				&token_count,
				1);
			if (rc != 0) {
				free(locks);
				return rc;
			}
		}
		tokens[token_count].start_candle = locks[lock_index].start;
		tokens[token_count].end_candle = locks[lock_index].end;
		tokens[token_count].class_id = locks[lock_index].class_id;
		tokens[token_count].debug_flags = 0u;
		tokens[token_count].debug_reduction_flags = 0u;
		tokens[token_count].debug_reduction_count = 0u;
		token_count += 1u;
		cursor = locks[lock_index].end + 1u;
	}
	if (cursor < candle_count) {
		rc = append_gpu_gap_tokens(candles, features, params, cursor, candle_count - 1u, tokens, &token_count, 1);
		if (rc != 0) {
			free(locks);
			return rc;
		}
	}

	free(locks);
	if (token_count > 1u) {
		qsort(tokens, token_count, sizeof(*tokens), compare_wave_token_start);
	}
	if (validate_token_sequence(tokens, token_count, candle_count) != 0) {
		return -2;
	}
	*out_token_count = token_count;
	return 0;
}

static void merge_same_direction_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	WaveKPI *kpi)
{
	uint32_t i = 0;

	while (i + 1u < *count) {
		if (is_trend_class(tokens[i].class_id) &&
			is_trend_class(tokens[i + 1u].class_id) &&
			tokens[i].dominant_direction != 0 &&
			tokens[i].dominant_direction == tokens[i + 1u].dominant_direction) {
			WaveSegmentEval eval;
			int32_t split_gain;
			uint32_t merged_class_id;

			compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, tokens[i + 1u].end_candle, &eval);
			split_gain = eval.class_error[WAVE_CLASS_WEAK_TREND] - (tokens[i].fit_error + tokens[i + 1u].fit_error);
			merged_class_id = select_merged_trend_class(&eval, params);
			if (merged_class_id < WAVE_CLASS_COUNT && split_gain < params->trend_split_gain_min) {
				merge_token_range(candles, features, candle_count, params, tokens, count, i, i + 1u, merged_class_id);
				kpi->same_direction_trend_merged_count += 1u;
				continue;
			}
			kpi->same_direction_trend_kept_count += 1u;
		}
		i += 1u;
	}
}

static void merge_v_reversals(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	WaveKPI *kpi)
{
	uint32_t i = 0;

	while (i + 1u < *count) {
		if (is_trend_class(tokens[i].class_id) &&
			is_trend_class(tokens[i + 1u].class_id) &&
			tokens[i].dominant_direction != 0 &&
			tokens[i + 1u].dominant_direction != 0 &&
			tokens[i].dominant_direction != tokens[i + 1u].dominant_direction) {
			WaveSegmentEval eval;
			int32_t split_err = tokens[i].fit_error + tokens[i + 1u].fit_error + params->transition_penalty_opp_dir_trend_no_v;

			compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, tokens[i + 1u].end_candle, &eval);
			if (eval.class_error[WAVE_CLASS_V_SHAPED_REVERSAL] < split_err) {
				merge_token_range(candles, features, candle_count, params, tokens, count, i, i + 1u, WAVE_CLASS_V_SHAPED_REVERSAL);
				kpi->opposite_direction_v_reversal_merged_count += 1u;
				continue;
			}
			kpi->opposite_direction_v_reversal_rejected_count += 1u;
		}
		i += 1u;
	}
}

static uint32_t classify_split_run(const WaveSegmentEval *eval, const WaveParams *params)
{
	if (eval == NULL || params == NULL) {
		return PRIMITIVE_CLASS_ALTERNATING;
	}
	if (params->wave_model == WAVE_MODEL_PRIMITIVE) {
		return primitive_class_from_eval(eval);
	}
	if (is_relative_wave_model((uint32_t)params->wave_model)) {
		return relative_class_id_is_valid(eval->best_class) ? eval->best_class : RELATIVE_CLASS_RANGE;
	}
	if (is_lockable_strong_trend(eval, params)) {
		return WAVE_CLASS_STRONG_TREND;
	}
	if (is_lockable_weak_trend(eval, params)) {
		return WAVE_CLASS_WEAK_TREND;
	}
	return fallback_non_priority_class_for_eval(eval);
}

enum {
	PRIMITIVE_KIND_INVALID = 0,
	PRIMITIVE_KIND_CONSECUTIVE = 1,
	PRIMITIVE_KIND_ALTERNATING = 2
};

static int primitive_kind_from_eval(const WaveSegmentEval *eval)
{
	if (eval == NULL) {
		return PRIMITIVE_KIND_INVALID;
	}
	if (primitive_class_from_eval(eval) == PRIMITIVE_CLASS_CONSECUTIVE) {
		return PRIMITIVE_KIND_CONSECUTIVE;
	}
	return PRIMITIVE_KIND_ALTERNATING;
}

static int should_split_primitive_token(const WaveToken *tok)
{
	if (tok == NULL) {
		return 0;
	}
	if (tok->duration_candles <= 1u ||
		tok->class_id == PRIMITIVE_CLASS_SEPARATOR ||
		tok->class_id != PRIMITIVE_CLASS_ALTERNATING) {
		return 0;
	}
	if (tok->primitive_counter_body_ratio == 0) {
		return 0;
	}
	if (tok->weak_fit && tok->primitive_max_consecutive_run >= 6) {
		return 1;
	}
	if (tok->primitive_minority_body_ratio < 300) {
		return 1;
	}
	if (tok->primitive_max_consecutive_run >= 6 &&
		tok->primitive_consecutive_ratio <= 520 &&
		tok->primitive_alternating_ratio >= 470 &&
		tok->primitive_counter_body_ratio >= 550) {
		return 1;
	}
	if (tok->primitive_minority_body_ratio >= 300 &&
		tok->primitive_minority_body_ratio <= 700 &&
		tok->primitive_max_consecutive_run >= 6 &&
		tok->primitive_alternating_ratio >= tok->primitive_consecutive_ratio &&
		(tok->primitive_alternating_ratio >= 550 ||
		tok->primitive_max_alternating_run >= 8)) {
		return 1;
	}
	if (tok->primitive_max_consecutive_run >= 5 &&
		tok->primitive_consecutive_ratio >= 540) {
		return 1;
	}
	return 0;
}

static int split_primitive_mixed_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	WaveToken *scratch;
	uint32_t write_count = 0;
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	scratch = (WaveToken *)malloc((size_t)candle_count * sizeof(*scratch));
	if (scratch == NULL) {
		return -1;
	}
	for (i = 0; i < *count; ++i) {
		const WaveToken *src = &tokens[i];

		if (!should_split_primitive_token(src)) {
			scratch[write_count] = *src;
			write_count += 1u;
			continue;
		}
		{
			uint32_t run_start = src->start_candle;
			int current_sign = 0;
			uint32_t c;

			for (c = src->start_candle; c <= src->end_candle; ++c) {
				int sign = effective_body_sign(candles, features, c);

				if (current_sign == 0) {
					if (sign != 0) {
						current_sign = sign;
					}
					continue;
				}
				if (sign != 0 && sign != current_sign) {
					WaveSegmentEval eval;
					uint32_t class_id;

					compute_segment_eval(candles, features, candle_count, params, run_start, c - 1u, &eval);
					class_id = classify_split_run(&eval, params);
					fill_token_from_eval(&scratch[write_count], &eval, class_id, params);
					scratch[write_count].debug_flags = src->debug_flags;
					scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
					scratch[write_count].debug_reduction_count = src->debug_reduction_count;
					mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_MIXED);
					write_count += 1u;
					run_start = c;
					current_sign = sign;
				}
			}
			if (run_start <= src->end_candle) {
				WaveSegmentEval eval;
				uint32_t class_id;

				compute_segment_eval(candles, features, candle_count, params, run_start, src->end_candle, &eval);
				class_id = classify_split_run(&eval, params);
				fill_token_from_eval(&scratch[write_count], &eval, class_id, params);
				scratch[write_count].debug_flags = src->debug_flags;
				scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
				scratch[write_count].debug_reduction_count = src->debug_reduction_count;
				mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_MIXED);
				write_count += 1u;
			}
		}
	}
	memcpy(tokens, scratch, (size_t)write_count * sizeof(*tokens));
	*count = write_count;
	free(scratch);
	return 0;
}

static int split_primitive_internal_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	WaveToken *scratch;
	uint32_t write_count = 0;
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	scratch = (WaveToken *)malloc((size_t)candle_count * sizeof(*scratch));
	if (scratch == NULL) {
		return -1;
	}
	for (i = 0; i < *count; ++i) {
		const WaveToken *src = &tokens[i];
		uint32_t best_run_start = 0;
		uint32_t best_run_end = 0;
		uint32_t best_effective_count = 0;
		int32_t best_run_error = 0x7fffffff;
		int have_best = 0;

		if (src->class_id == PRIMITIVE_CLASS_ALTERNATING && src->duration_candles >= 5u) {
			uint32_t current_start = 0;
			uint32_t current_last = 0;
			uint32_t current_count = 0;
			int current_sign = 0;
			uint32_t c;

			for (c = src->start_candle; c <= src->end_candle + 1u; ++c) {
				int sign = (c <= src->end_candle)
					? effective_body_sign(candles, features, c)
					: 2;

				if (current_sign == 0) {
					if (c <= src->end_candle && sign != 0) {
						current_start = c;
						current_last = c;
						current_count = 1u;
						current_sign = sign;
					}
					continue;
				}
				if (c <= src->end_candle && (sign == 0 || sign == current_sign)) {
					current_last = c;
					if (sign == current_sign) {
						current_count += 1u;
					}
					continue;
				}
				if (current_count >= 3u &&
					current_start > src->start_candle &&
					current_last < src->end_candle) {
					WaveSegmentEval run_eval;

					compute_segment_eval(candles, features, candle_count, params, current_start, current_last, &run_eval);
					if (current_count > best_effective_count ||
						(current_count == best_effective_count &&
						run_eval.class_error[PRIMITIVE_CLASS_CONSECUTIVE] < best_run_error)) {
						best_effective_count = current_count;
						best_run_error = run_eval.class_error[PRIMITIVE_CLASS_CONSECUTIVE];
						best_run_start = current_start;
						best_run_end = current_last;
						have_best = 1;
					}
				}
				if (c <= src->end_candle && sign != 0) {
					current_start = c;
					current_last = c;
					current_count = 1u;
					current_sign = sign;
				} else {
					current_sign = 0;
				}
			}
		}

		if (!have_best) {
			scratch[write_count] = *src;
			write_count += 1u;
			continue;
		}
		if (best_run_start > src->start_candle) {
			WaveSegmentEval left_eval;
			uint32_t left_class_id;

			compute_segment_eval(candles, features, candle_count, params, src->start_candle, best_run_start - 1u, &left_eval);
			left_class_id = classify_split_span(candles, features, candle_count, params, src->start_candle, best_run_start - 1u);
			fill_token_from_eval(&scratch[write_count], &left_eval, left_class_id, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL);
			write_count += 1u;
		}
		{
			WaveSegmentEval run_eval;

			compute_segment_eval(candles, features, candle_count, params, best_run_start, best_run_end, &run_eval);
			fill_token_from_eval(&scratch[write_count], &run_eval, PRIMITIVE_CLASS_CONSECUTIVE, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL);
			write_count += 1u;
		}
		if (best_run_end < src->end_candle) {
			WaveSegmentEval right_eval;
			uint32_t right_class_id;

			compute_segment_eval(candles, features, candle_count, params, best_run_end + 1u, src->end_candle, &right_eval);
			right_class_id = classify_split_span(candles, features, candle_count, params, best_run_end + 1u, src->end_candle);
			fill_token_from_eval(&scratch[write_count], &right_eval, right_class_id, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL);
			write_count += 1u;
		}
		changed = 1;
	}
	memcpy(tokens, scratch, (size_t)write_count * sizeof(*tokens));
	*count = write_count;
	free(scratch);
	return changed;
}

static int effective_body_sign(const Candle *candles, const CandleFeat *features, uint32_t index)
{
	if (candles == NULL || features == NULL) {
		return 0;
	}
	if (features[index].b == 0 || is_inside_candle(candles, index)) {
		return 0;
	}
	if (features[index].b > 0) {
		return 1;
	}
	if (features[index].b < 0) {
		return -1;
	}
	return 0;
}

static uint32_t classify_split_span(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	uint32_t start,
	uint32_t end)
{
	WaveSegmentEval eval;

	compute_segment_eval(candles, features, candle_count, params, start, end, &eval);
	return classify_split_run(&eval, params);
}

static int find_primitive_edge_run(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *src,
	int trailing,
	uint32_t *out_run_start,
	uint32_t *out_run_end)
{
	uint32_t run_start;
	uint32_t run_end;
	int sign;
	WaveSegmentEval run_eval;

	if (candles == NULL || features == NULL || params == NULL || src == NULL ||
		out_run_start == NULL || out_run_end == NULL) {
		return 0;
	}
	if (src->class_id != PRIMITIVE_CLASS_ALTERNATING || src->duration_candles < 4u) {
		return 0;
	}

	if (trailing) {
		run_end = src->end_candle;
		while (run_end > src->start_candle &&
			effective_body_sign(candles, features, run_end) == 0) {
			run_end -= 1u;
		}
		sign = effective_body_sign(candles, features, run_end);
		if (sign == 0) {
			return 0;
		}
		run_start = run_end;
		while (run_start > src->start_candle) {
			int prev_sign = effective_body_sign(candles, features, run_start - 1u);

			if (prev_sign == 0 || prev_sign == sign) {
				run_start -= 1u;
				continue;
			}
			break;
		}
	} else {
		run_start = src->start_candle;
		while (run_start < src->end_candle &&
			effective_body_sign(candles, features, run_start) == 0) {
			run_start += 1u;
		}
		sign = effective_body_sign(candles, features, run_start);
		if (sign == 0) {
			return 0;
		}
		run_end = run_start;
		while (run_end < src->end_candle) {
			int next_sign = effective_body_sign(candles, features, run_end + 1u);

			if (next_sign == 0 || next_sign == sign) {
				run_end += 1u;
				continue;
			}
			break;
		}
	}

	if (run_end < run_start) {
		return 0;
	}
	{
		uint32_t c;
		uint32_t effective_count = 0;

		for (c = run_start; c <= run_end; ++c) {
			if (effective_body_sign(candles, features, c) == sign) {
				effective_count += 1u;
			}
		}
		if (effective_count < 3u) {
			return 0;
		}
	}
	if (run_end - run_start + 1u < 3u) {
		return 0;
	}
	if (run_start == src->start_candle && run_end == src->end_candle) {
		return 0;
	}

	compute_segment_eval(candles, features, candle_count, params, run_start, run_end, &run_eval);
	if (primitive_class_from_eval(&run_eval) != PRIMITIVE_CLASS_CONSECUTIVE) {
		return 0;
	}
	if (run_eval.best_fit > params->weak_fit_threshold) {
		return 0;
	}

	*out_run_start = run_start;
	*out_run_end = run_end;
	return 1;
}

static int relative_edge_run_is_trend_candidate(const WaveSegmentEval *eval, const WaveParams *params)
{
	int32_t min_der_step;
	int32_t min_der_body;
	int32_t max_retrace;
	int32_t max_overlap;
	int32_t min_body_share;
	int32_t max_sign_density;
	int32_t min_price_amplitude;

	if (eval == NULL || params == NULL) {
		return 0;
	}
	min_der_step = max_i32(params->relative_trend_der_step_min - 80, params->norm_scale / 3);
	min_der_body = max_i32(params->relative_trend_der_body_min - 80, params->norm_scale / 3);
	max_retrace = min_i32(params->relative_trend_retracement_max + 120, params->norm_scale);
	max_overlap = params->relative_trend_overlap_max + 120;
	min_body_share = 700;
	max_sign_density = params->relative_trend_sign_flip_max;
	min_price_amplitude = max_i32(params->minimum_pips_for_trend * 2, 14);
	return eval->best_class == RELATIVE_CLASS_TREND &&
		eval->dominant_direction != 0 &&
		eval->net_move_raw >= params->minimum_pips_for_trend &&
		abs_i32(eval->relative_net_move_raw) >= min_price_amplitude &&
		eval->relative_directional_efficiency_step >= min_der_step &&
		eval->relative_directional_efficiency_body >= min_der_body &&
		eval->relative_retracement_ratio <= max_retrace &&
		eval->relative_overlap_ratio <= max_overlap &&
		eval->sign_change_density <= max_sign_density &&
		eval->relative_body_direction_share >= min_body_share;
}

static int find_relative_edge_run(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	const WaveToken *src,
	int trailing,
	uint32_t *out_run_start,
	uint32_t *out_run_end)
{
	uint32_t run_start;
	uint32_t run_end;
	int sign;
	WaveSegmentEval run_eval;

	if (candles == NULL || features == NULL || params == NULL || src == NULL ||
		out_run_start == NULL || out_run_end == NULL) {
		return 0;
	}
	if (!is_relative_wave_model(params->wave_model) ||
		src->class_id != RELATIVE_CLASS_RANGE ||
		src->duration_candles < 4u) {
		return 0;
	}

	if (trailing) {
		run_end = src->end_candle;
		while (run_end > src->start_candle &&
			effective_body_sign(candles, features, run_end) == 0) {
			run_end -= 1u;
		}
		sign = effective_body_sign(candles, features, run_end);
		if (sign == 0) {
			return 0;
		}
		run_start = run_end;
		while (run_start > src->start_candle) {
			int prev_sign = effective_body_sign(candles, features, run_start - 1u);

			if (prev_sign == 0 || prev_sign == sign) {
				run_start -= 1u;
				continue;
			}
			break;
		}
	} else {
		run_start = src->start_candle;
		while (run_start < src->end_candle &&
			effective_body_sign(candles, features, run_start) == 0) {
			run_start += 1u;
		}
		sign = effective_body_sign(candles, features, run_start);
		if (sign == 0) {
			return 0;
		}
		run_end = run_start;
		while (run_end < src->end_candle) {
			int next_sign = effective_body_sign(candles, features, run_end + 1u);

			if (next_sign == 0 || next_sign == sign) {
				run_end += 1u;
				continue;
			}
			break;
		}
	}

	if (run_end < run_start) {
		return 0;
	}
	{
		uint32_t c;
		uint32_t effective_count = 0u;

		for (c = run_start; c <= run_end; ++c) {
			if (effective_body_sign(candles, features, c) == sign) {
				effective_count += 1u;
			}
		}
		if (effective_count < 2u) {
			return 0;
		}
	}
	if (run_end - run_start + 1u < 2u) {
		return 0;
	}
	if (run_start == src->start_candle && run_end == src->end_candle) {
		return 0;
	}

	compute_segment_eval(candles, features, candle_count, params, run_start, run_end, &run_eval);
	if (!relative_edge_run_is_trend_candidate(&run_eval, params) ||
		!relative_span_has_consistent_edges(candles, features, run_start, run_end, run_eval.dominant_direction)) {
		return 0;
	}

	*out_run_start = run_start;
	*out_run_end = run_end;
	return 1;
}

static int split_relative_edge_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	WaveToken *scratch;
	uint32_t write_count = 0u;
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	if (!is_relative_wave_model(params->wave_model)) {
		return 0;
	}
	scratch = (WaveToken *)malloc((size_t)candle_count * sizeof(*scratch));
	if (scratch == NULL) {
		return -1;
	}
	for (i = 0u; i < *count; ++i) {
		const WaveToken *src = &tokens[i];
		uint32_t lead_start = 0u;
		uint32_t lead_end = 0u;
		uint32_t trail_start = 0u;
		uint32_t trail_end = 0u;
		int has_leading;
		int has_trailing;
		int use_trailing = 0;

		if (src->class_id != RELATIVE_CLASS_RANGE || src->duration_candles < 4u) {
			scratch[write_count] = *src;
			write_count += 1u;
			continue;
		}

		has_leading = find_relative_edge_run(candles, features, candle_count, params, src, 0, &lead_start, &lead_end);
		has_trailing = find_relative_edge_run(candles, features, candle_count, params, src, 1, &trail_start, &trail_end);
		if (!has_leading && !has_trailing) {
			scratch[write_count] = *src;
			write_count += 1u;
			continue;
		}
		if (has_trailing && has_leading) {
			uint32_t trailing_len = trail_end - trail_start + 1u;
			uint32_t leading_len = lead_end - lead_start + 1u;

			if (trailing_len > leading_len) {
				use_trailing = 1;
			} else if (trailing_len == leading_len) {
				WaveSegmentEval trailing_eval;
				WaveSegmentEval leading_eval;

				compute_segment_eval(candles, features, candle_count, params, trail_start, trail_end, &trailing_eval);
				compute_segment_eval(candles, features, candle_count, params, lead_start, lead_end, &leading_eval);
				if (abs_i32(trailing_eval.relative_net_move_raw) >= abs_i32(leading_eval.relative_net_move_raw)) {
					use_trailing = 1;
				}
			}
		} else if (has_trailing) {
			use_trailing = 1;
		}

		if (use_trailing) {
			WaveSegmentEval left_eval;
			WaveSegmentEval right_eval;
			uint32_t left_class_id;
			uint32_t left_duration;
			uint32_t trailing_duration;
			uint32_t right_remainder_duration;

			trailing_duration = trail_end - trail_start + 1u;
			right_remainder_duration = (trail_start > src->start_candle) ? (trail_start - src->start_candle) : 0u;
			if ((trailing_duration <= 4u && right_remainder_duration <= 1u) ||
				(trailing_duration <= 4u && right_remainder_duration <= 4u &&
				 src->duration_candles <= 8u)) {
				scratch[write_count] = *src;
				write_count += 1u;
				continue;
			}

			compute_segment_eval(candles, features, candle_count, params, src->start_candle, trail_start - 1u, &left_eval);
			compute_segment_eval(candles, features, candle_count, params, trail_start, trail_end, &right_eval);
			left_duration = (trail_start > src->start_candle) ? (trail_start - src->start_candle) : 0u;
			if (left_duration <= 2u) {
				left_class_id = RELATIVE_CLASS_RANGE;
			} else {
				left_class_id = classify_split_span(candles, features, candle_count, params, src->start_candle, trail_start - 1u);
			}
			fill_token_from_eval(&scratch[write_count], &left_eval, left_class_id, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			write_count += 1u;
			fill_token_from_eval(&scratch[write_count], &right_eval, RELATIVE_CLASS_TREND, params);
			scratch[write_count].seed_start_candle = trail_start;
			scratch[write_count].seed_end_candle = trail_end;
			scratch[write_count].seed_pass = is_relative_seeded_family((uint32_t)params->wave_model) ? 2u : 0u;
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			write_count += 1u;
			changed = 1;
			continue;
		}
		{
			WaveSegmentEval left_eval;
			WaveSegmentEval right_eval;
			uint32_t right_class_id;
			uint32_t right_duration;
			uint32_t lead_duration;
			uint32_t left_remainder_duration;

			lead_duration = lead_end - lead_start + 1u;
			left_remainder_duration = (src->end_candle >= lead_end + 1u) ? (src->end_candle - lead_end) : 0u;
			if ((lead_duration <= 4u && left_remainder_duration <= 1u) ||
				(lead_duration <= 4u && left_remainder_duration <= 4u &&
				 src->duration_candles <= 8u)) {
				scratch[write_count] = *src;
				write_count += 1u;
				continue;
			}

			compute_segment_eval(candles, features, candle_count, params, lead_start, lead_end, &left_eval);
			compute_segment_eval(candles, features, candle_count, params, lead_end + 1u, src->end_candle, &right_eval);
			right_duration = (src->end_candle >= lead_end + 1u) ? (src->end_candle - lead_end) : 0u;
			if (right_duration <= 2u) {
				right_class_id = RELATIVE_CLASS_RANGE;
			} else {
				right_class_id = classify_split_span(candles, features, candle_count, params, lead_end + 1u, src->end_candle);
			}
			fill_token_from_eval(&scratch[write_count], &left_eval, RELATIVE_CLASS_TREND, params);
			scratch[write_count].seed_start_candle = lead_start;
			scratch[write_count].seed_end_candle = lead_end;
			scratch[write_count].seed_pass = is_relative_seeded_family((uint32_t)params->wave_model) ? 2u : 0u;
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			write_count += 1u;
			fill_token_from_eval(&scratch[write_count], &right_eval, right_class_id, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			write_count += 1u;
			changed = 1;
		}
	}
	memcpy(tokens, scratch, (size_t)write_count * sizeof(*tokens));
	*count = write_count;
	free(scratch);
	return changed;
}

static int resolve_relative_same_direction_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	WaveKPI *kpi)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL || kpi == NULL) {
		return -1;
	}
	if (!is_relative_wave_model(params->wave_model)) {
		return 0;
	}
	for (i = 0u; i + 1u < *count; ++i) {
		uint32_t combined_start;
		uint32_t combined_end;
		WaveSegmentEval merged_eval;

		if (!is_relative_trend_class(tokens[i].class_id, params) ||
			!is_relative_trend_class(tokens[i + 1u].class_id, params) ||
			tokens[i].dominant_direction == 0 ||
			tokens[i].dominant_direction != tokens[i + 1u].dominant_direction ||
			tokens[i].end_candle + 1u != tokens[i + 1u].start_candle) {
			continue;
		}

		combined_start = tokens[i].start_candle;
		combined_end = tokens[i + 1u].end_candle;
		compute_segment_eval(candles, features, candle_count, params, combined_start, combined_end, &merged_eval);
		if (merged_eval.best_class == RELATIVE_CLASS_TREND &&
			relative_span_is_trend_valid(&merged_eval, params) &&
			relative_span_has_consistent_edges(candles, features, combined_start, combined_end, merged_eval.dominant_direction) &&
			merged_eval.dominant_direction == tokens[i].dominant_direction) {
			merge_token_range(candles, features, candle_count, params, tokens, count, i, i + 1u, RELATIVE_CLASS_TREND);
			kpi->same_direction_trend_merged_count += 1u;
			return 1;
		}
		{
			uint32_t mid_start;
			int found_best = 0;
			uint32_t best_mid_start = 0u;
			uint32_t best_mid_end = 0u;
			WaveSegmentEval best_left_eval;
			WaveSegmentEval best_mid_eval;
			WaveSegmentEval best_right_eval;
			int32_t best_total = 0x7fffffff;

			memset(&best_left_eval, 0, sizeof(best_left_eval));
			memset(&best_mid_eval, 0, sizeof(best_mid_eval));
			memset(&best_right_eval, 0, sizeof(best_right_eval));

			for (mid_start = tokens[i].start_candle + 1u; mid_start + 1u <= tokens[i + 1u].end_candle; ++mid_start) {
				uint32_t max_mid_end = (uint32_t)min_i32((int32_t)(mid_start + 3u), (int32_t)(tokens[i + 1u].end_candle - 1u));
				uint32_t mid_end;

				for (mid_end = mid_start; mid_end <= max_mid_end; ++mid_end) {
					WaveSegmentEval left_eval;
					WaveSegmentEval mid_eval;
					WaveSegmentEval right_eval;
					int32_t total_fit;

					compute_segment_eval(candles, features, candle_count, params, combined_start, mid_start - 1u, &left_eval);
					compute_segment_eval(candles, features, candle_count, params, mid_start, mid_end, &mid_eval);
					compute_segment_eval(candles, features, candle_count, params, mid_end + 1u, combined_end, &right_eval);
					if (left_eval.best_class != RELATIVE_CLASS_TREND ||
						right_eval.best_class != RELATIVE_CLASS_TREND ||
						mid_eval.best_class != RELATIVE_CLASS_RANGE ||
						left_eval.dominant_direction != tokens[i].dominant_direction ||
						right_eval.dominant_direction != tokens[i].dominant_direction ||
						!relative_span_is_trend_valid(&left_eval, params) ||
						!relative_span_is_trend_valid(&right_eval, params) ||
						!relative_span_has_consistent_edges(candles, features, combined_start, mid_start - 1u, left_eval.dominant_direction) ||
						!relative_span_has_consistent_edges(candles, features, mid_end + 1u, combined_end, right_eval.dominant_direction)) {
						continue;
					}
					total_fit = left_eval.class_error[RELATIVE_CLASS_TREND] +
						mid_eval.class_error[RELATIVE_CLASS_RANGE] +
						right_eval.class_error[RELATIVE_CLASS_TREND] +
						(2 * params->segment_penalty_lambda);
					if (!found_best || total_fit < best_total) {
						best_total = total_fit;
						best_mid_start = mid_start;
						best_mid_end = mid_end;
						best_left_eval = left_eval;
						best_mid_eval = mid_eval;
						best_right_eval = right_eval;
						found_best = 1;
					}
				}
			}
			if (found_best) {
				int32_t current_total = tokens[i].fit_error + tokens[i + 1u].fit_error + params->segment_penalty_lambda;
				WaveToken middle_token;

				if (best_total <= current_total + params->trend_split_gain_min) {
					tokens[i].start_candle = combined_start;
					tokens[i].end_candle = best_mid_start - 1u;
					fill_token_from_eval(&tokens[i], &best_left_eval, RELATIVE_CLASS_TREND, params);

					memset(&middle_token, 0, sizeof(middle_token));
					middle_token.start_candle = best_mid_start;
					middle_token.end_candle = best_mid_end;
					fill_token_from_eval(&middle_token, &best_mid_eval, RELATIVE_CLASS_RANGE, params);
					middle_token.debug_reduction_flags = WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL;
					middle_token.debug_reduction_count = 1u;
					insert_token_at(tokens, count, i + 1u, &middle_token);

					tokens[i + 2u].start_candle = best_mid_end + 1u;
					tokens[i + 2u].end_candle = combined_end;
					fill_token_from_eval(&tokens[i + 2u], &best_right_eval, RELATIVE_CLASS_TREND, params);
					kpi->same_direction_trend_kept_count += 1u;
					return 1;
				}
			}
		}
		{
			uint32_t demote_index = i;

			if (tokens[i + 1u].seed_pass == 0u && tokens[i].seed_pass != 0u) {
				demote_index = i + 1u;
			} else if (tokens[i].seed_pass == 0u && tokens[i + 1u].seed_pass != 0u) {
				demote_index = i;
			} else if (tokens[i + 1u].fit_error > tokens[i].fit_error) {
				demote_index = i + 1u;
			} else if (tokens[i + 1u].fit_error == tokens[i].fit_error &&
				tokens[i + 1u].relative_trend_quality_raw > tokens[i].relative_trend_quality_raw) {
				demote_index = i + 1u;
			}
			force_token_class_span(candles, features, candle_count, params, &tokens[demote_index], RELATIVE_CLASS_RANGE);
			mark_token_reduction(&tokens[demote_index], WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL);
			kpi->same_direction_trend_kept_count += 1u;
			return 1;
		}
		kpi->same_direction_trend_kept_count += 1u;
	}
	return 0;
}

static int split_primitive_edge_trends(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	WaveToken *scratch;
	uint32_t write_count = 0;
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	scratch = (WaveToken *)malloc((size_t)candle_count * sizeof(*scratch));
	if (scratch == NULL) {
		return -1;
	}
	for (i = 0; i < *count; ++i) {
		const WaveToken *src = &tokens[i];
		uint32_t lead_start = 0;
		uint32_t lead_end = 0;
		uint32_t trail_start = 0;
		uint32_t trail_end = 0;
		int has_leading;
		int has_trailing;
		int use_trailing = 0;

		if (src->class_id != PRIMITIVE_CLASS_ALTERNATING || src->duration_candles < 4u) {
			scratch[write_count] = *src;
			write_count += 1u;
			continue;
		}

		has_leading = find_primitive_edge_run(candles, features, candle_count, params, src, 0, &lead_start, &lead_end);
		has_trailing = find_primitive_edge_run(candles, features, candle_count, params, src, 1, &trail_start, &trail_end);
		if (!has_leading && !has_trailing) {
			scratch[write_count] = *src;
			write_count += 1u;
			continue;
		}
		if (has_trailing && has_leading) {
			uint32_t trailing_len = trail_end - trail_start + 1u;
			uint32_t leading_len = lead_end - lead_start + 1u;

			if (trailing_len > leading_len) {
				use_trailing = 1;
			} else if (trailing_len == leading_len) {
				use_trailing = 1;
			}
		} else if (has_trailing) {
			use_trailing = 1;
		}

		if (use_trailing) {
			WaveSegmentEval left_eval;
			WaveSegmentEval right_eval;
			uint32_t left_class_id;

			compute_segment_eval(candles, features, candle_count, params, src->start_candle, trail_start - 1u, &left_eval);
			compute_segment_eval(candles, features, candle_count, params, trail_start, trail_end, &right_eval);
			left_class_id = classify_split_run(&left_eval, params);
			fill_token_from_eval(&scratch[write_count], &left_eval, left_class_id, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			write_count += 1u;
			fill_token_from_eval(&scratch[write_count], &right_eval, PRIMITIVE_CLASS_CONSECUTIVE, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			write_count += 1u;
			changed = 1;
			continue;
		}
		{
			WaveSegmentEval left_eval;
			WaveSegmentEval right_eval;
			uint32_t right_class_id;

			compute_segment_eval(candles, features, candle_count, params, lead_start, lead_end, &left_eval);
			compute_segment_eval(candles, features, candle_count, params, lead_end + 1u, src->end_candle, &right_eval);
			right_class_id = classify_split_run(&right_eval, params);
			fill_token_from_eval(&scratch[write_count], &left_eval, PRIMITIVE_CLASS_CONSECUTIVE, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			write_count += 1u;
			fill_token_from_eval(&scratch[write_count], &right_eval, right_class_id, params);
			scratch[write_count].debug_flags = src->debug_flags;
			scratch[write_count].debug_reduction_flags = src->debug_reduction_flags;
			scratch[write_count].debug_reduction_count = src->debug_reduction_count;
			mark_token_reduction(&scratch[write_count], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			write_count += 1u;
			changed = 1;
		}
	}
	memcpy(tokens, scratch, (size_t)write_count * sizeof(*tokens));
	*count = write_count;
	free(scratch);
	return changed;
}

static int expand_consecutive_boundaries(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	for (i = 0; i + 1u < *count; ++i) {
		if (tokens[i].class_id == PRIMITIVE_CLASS_ALTERNATING &&
			tokens[i + 1u].class_id == PRIMITIVE_CLASS_CONSECUTIVE &&
			tokens[i].duration_candles > 1u) {
			uint32_t pivot = tokens[i].end_candle;
			int dir = effective_body_sign(candles, features, pivot);

			if (dir != 0 && dir == tokens[i + 1u].dominant_direction) {
				WaveSegmentEval left_eval;
				WaveSegmentEval right_eval;

				compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, pivot - 1u, &left_eval);
				compute_segment_eval(candles, features, candle_count, params, pivot, tokens[i + 1u].end_candle, &right_eval);
				if (primitive_class_from_eval(&right_eval) == PRIMITIVE_CLASS_CONSECUTIVE &&
					right_eval.best_fit <= params->weak_fit_threshold) {
					uint32_t left_flags = tokens[i].debug_flags;
					uint32_t right_flags = tokens[i + 1u].debug_flags;
					uint32_t left_class_id = classify_split_run(&left_eval, params);

					fill_token_from_eval(&tokens[i], &left_eval, left_class_id, params);
					tokens[i].debug_flags = left_flags;
					fill_token_from_eval(&tokens[i + 1u], &right_eval, PRIMITIVE_CLASS_CONSECUTIVE, params);
					tokens[i + 1u].debug_flags = right_flags;
					return 1;
				}
			}
		}
		if (tokens[i].class_id == PRIMITIVE_CLASS_CONSECUTIVE &&
			tokens[i + 1u].class_id == PRIMITIVE_CLASS_ALTERNATING &&
			tokens[i + 1u].duration_candles > 1u) {
			uint32_t pivot = tokens[i + 1u].start_candle;
			int dir = effective_body_sign(candles, features, pivot);

			if (dir != 0 && dir == tokens[i].dominant_direction) {
				WaveSegmentEval left_eval;
				WaveSegmentEval right_eval;

				compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, pivot, &left_eval);
				compute_segment_eval(candles, features, candle_count, params, pivot + 1u, tokens[i + 1u].end_candle, &right_eval);
				if (primitive_class_from_eval(&left_eval) == PRIMITIVE_CLASS_CONSECUTIVE &&
					left_eval.best_fit <= params->weak_fit_threshold) {
					uint32_t left_flags = tokens[i].debug_flags;
					uint32_t right_flags = tokens[i + 1u].debug_flags;
					uint32_t right_class_id = classify_split_run(&right_eval, params);

					fill_token_from_eval(&tokens[i], &left_eval, PRIMITIVE_CLASS_CONSECUTIVE, params);
					tokens[i].debug_flags = left_flags;
					fill_token_from_eval(&tokens[i + 1u], &right_eval, right_class_id, params);
					tokens[i + 1u].debug_flags = right_flags;
					return 1;
				}
			}
		}
	}
	return 0;
}

static int merge_separator_into_alternating(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;
	const int32_t merge_abs_tol = 180;
	const int32_t merge_rel_num = 5;
	const int32_t merge_rel_den = 4;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	for (i = 0; i < *count; ++i) {
		WaveSegmentEval left_eval;
		WaveSegmentEval right_eval;
		int left_ok = 0;
		int right_ok = 0;
		int32_t left_split_fit = 0;
		int32_t right_split_fit = 0;
		int32_t left_degrade = 0x7fffffff;
		int32_t right_degrade = 0x7fffffff;
		int prefer_right = 0;

		if (tokens[i].class_id != PRIMITIVE_CLASS_SEPARATOR) {
			continue;
		}
		if (i > 0u && tokens[i - 1u].class_id == PRIMITIVE_CLASS_ALTERNATING) {
			compute_segment_eval(candles,
				features,
				candle_count,
				params,
				tokens[i - 1u].start_candle,
				tokens[i].end_candle,
				&left_eval);
			if (primitive_class_from_eval(&left_eval) == PRIMITIVE_CLASS_ALTERNATING) {
				left_split_fit = tokens[i - 1u].fit_error + tokens[i].fit_error;
				left_degrade = left_eval.class_error[PRIMITIVE_CLASS_ALTERNATING] - left_split_fit;
				if (left_degrade <= merge_abs_tol ||
					left_eval.class_error[PRIMITIVE_CLASS_ALTERNATING] * merge_rel_den <= max_i32(left_split_fit, 1) * merge_rel_num) {
					left_ok = 1;
				}
			}
		}
		if (i + 1u < *count && tokens[i + 1u].class_id == PRIMITIVE_CLASS_ALTERNATING) {
			compute_segment_eval(candles,
				features,
				candle_count,
				params,
				tokens[i].start_candle,
				tokens[i + 1u].end_candle,
				&right_eval);
			if (primitive_class_from_eval(&right_eval) == PRIMITIVE_CLASS_ALTERNATING) {
				right_split_fit = tokens[i].fit_error + tokens[i + 1u].fit_error;
				right_degrade = right_eval.class_error[PRIMITIVE_CLASS_ALTERNATING] - right_split_fit;
				if (right_degrade <= merge_abs_tol ||
					right_eval.class_error[PRIMITIVE_CLASS_ALTERNATING] * merge_rel_den <= max_i32(right_split_fit, 1) * merge_rel_num) {
					right_ok = 1;
				}
			}
		}
		if (!left_ok && !right_ok) {
			continue;
		}
		if (right_ok && (!left_ok ||
			right_eval.class_error[PRIMITIVE_CLASS_ALTERNATING] < left_eval.class_error[PRIMITIVE_CLASS_ALTERNATING] ||
			(right_eval.class_error[PRIMITIVE_CLASS_ALTERNATING] == left_eval.class_error[PRIMITIVE_CLASS_ALTERNATING] && right_degrade <= left_degrade))) {
			prefer_right = 1;
		}
		if (prefer_right) {
			uint32_t debug_flags = tokens[i + 1u].debug_flags | tokens[i].debug_flags | WAVE_TOKEN_FLAG_SEPARATOR_LEFT_ADDED;

			merge_token_range(candles, features, candle_count, params, tokens, count, i, i + 1u, PRIMITIVE_CLASS_ALTERNATING);
			tokens[i].debug_flags = debug_flags;
			mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_ALT);
			return 1;
		}
		{
			uint32_t debug_flags = tokens[i - 1u].debug_flags | tokens[i].debug_flags | WAVE_TOKEN_FLAG_SEPARATOR_RIGHT_ADDED;

			merge_token_range(candles, features, candle_count, params, tokens, count, i - 1u, i, PRIMITIVE_CLASS_ALTERNATING);
			tokens[i - 1u].debug_flags = debug_flags;
			mark_token_reduction(&tokens[i - 1u], WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_ALT);
			return 1;
		}
	}
	return 0;
}

static int merge_separator_into_consecutive(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	for (i = 0; i < *count; ++i) {
		int sep_sign;

		if (tokens[i].class_id != PRIMITIVE_CLASS_SEPARATOR) {
			continue;
		}
		sep_sign = effective_body_sign(candles, features, tokens[i].start_candle);
		if (sep_sign == 0) {
			if (features[tokens[i].start_candle].b > 0) {
				sep_sign = 1;
			} else if (features[tokens[i].start_candle].b < 0) {
				sep_sign = -1;
			}
		}
		if (i > 0u &&
			tokens[i - 1u].class_id == PRIMITIVE_CLASS_CONSECUTIVE &&
			tokens[i - 1u].dominant_direction == sep_sign &&
			primitive_span_is_strict_consecutive(candles,
				features,
				candle_count,
				params,
				tokens[i - 1u].start_candle,
				tokens[i].end_candle,
				NULL)) {
			merge_token_range(candles, features, candle_count, params, tokens, count, i - 1u, i, PRIMITIVE_CLASS_CONSECUTIVE);
			mark_token_reduction(&tokens[i - 1u], WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_CONSEC);
			return 1;
		}
		if (i + 1u < *count &&
			tokens[i + 1u].class_id == PRIMITIVE_CLASS_CONSECUTIVE &&
			tokens[i + 1u].dominant_direction == sep_sign &&
			primitive_span_is_strict_consecutive(candles,
				features,
				candle_count,
				params,
				tokens[i].start_candle,
				tokens[i + 1u].end_candle,
				NULL)) {
			merge_token_range(candles, features, candle_count, params, tokens, count, i, i + 1u, PRIMITIVE_CLASS_CONSECUTIVE);
			mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_CONSEC);
			return 1;
		}
	}
	return 0;
}

static int collapse_separator_runs(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	i = 0u;
	while (i < *count) {
		uint32_t run_end = i;

		if (tokens[i].class_id != PRIMITIVE_CLASS_SEPARATOR) {
			i += 1u;
			continue;
		}
		while (run_end + 1u < *count && tokens[run_end + 1u].class_id == PRIMITIVE_CLASS_SEPARATOR) {
			run_end += 1u;
		}
		if (run_end > i) {
			WaveSegmentEval eval;
			uint32_t class_id = PRIMITIVE_CLASS_ALTERNATING;

			compute_segment_eval(candles,
				features,
				candle_count,
				params,
				tokens[i].start_candle,
				tokens[run_end].end_candle,
				&eval);
			if (primitive_class_from_eval(&eval) == PRIMITIVE_CLASS_CONSECUTIVE) {
				if (primitive_span_is_strict_consecutive(candles,
					features,
					candle_count,
					params,
					tokens[i].start_candle,
					tokens[run_end].end_candle,
					NULL)) {
					class_id = PRIMITIVE_CLASS_CONSECUTIVE;
				}
			}
			merge_token_range(candles, features, candle_count, params, tokens, count, i, run_end, class_id);
			mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_COLLAPSE_SEPARATOR);
			return 1;
		}
		i = run_end + 1u;
	}
	return 0;
}

static void coalesce_maximal_primitive_spans(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i = 0;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return;
	}
	while (i + 1u < *count) {
		WaveSegmentEval merged_eval;
		int merged_kind;
		uint32_t merged_duration = tokens[i + 1u].end_candle - tokens[i].start_candle + 1u;
		int32_t split_fit;

		if (merged_duration > params->lmax_candles) {
			i += 1u;
			continue;
		}

		compute_segment_eval(candles,
			features,
			candle_count,
			params,
			tokens[i].start_candle,
			tokens[i + 1u].end_candle,
			&merged_eval);
		merged_kind = primitive_kind_from_eval(&merged_eval);
		if (merged_kind == PRIMITIVE_KIND_INVALID) {
			i += 1u;
			continue;
		}
		if (merged_kind == PRIMITIVE_KIND_CONSECUTIVE &&
			!primitive_span_is_strict_consecutive(candles,
				features,
				candle_count,
				params,
				tokens[i].start_candle,
				tokens[i + 1u].end_candle,
				NULL)) {
			i += 1u;
			continue;
		}
		if (merged_kind == PRIMITIVE_KIND_ALTERNATING &&
			merged_eval.primitive_alternating_ratio < 420) {
			i += 1u;
			continue;
		}
		if (tokens[i].class_id != primitive_class_from_eval(&merged_eval) ||
			tokens[i + 1u].class_id != primitive_class_from_eval(&merged_eval)) {
			i += 1u;
			continue;
		}
		split_fit = tokens[i].fit_error + tokens[i + 1u].fit_error;
		if (merged_eval.best_fit > split_fit) {
			i += 1u;
			continue;
		}
		merge_token_range(candles,
			features,
			candle_count,
			params,
			tokens,
			count,
			i,
			i + 1u,
			primitive_class_from_eval(&merged_eval));
		mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_COALESCE);
	}
}

static int primitive_token_is_neutral_bridge(const Candle *candles,
	const CandleFeat *features,
	const WaveToken *token)
{
	uint32_t c;

	if (candles == NULL || features == NULL || token == NULL) {
		return 0;
	}
	if (token->dominant_direction != 0) {
		return 0;
	}
	for (c = token->start_candle; c <= token->end_candle; ++c) {
		if (features[c].b != 0 && !is_inside_candle(candles, c)) {
			return 0;
		}
	}
	return 1;
}

static int bridge_neutral_primitive_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	for (i = 0u; i + 2u < *count; ++i) {
		uint32_t neutral_end = i + 1u;
		uint32_t right_index;
		WaveSegmentEval merged_eval;
		int32_t split_total = 0;
		int32_t merged_total;
		uint32_t k;

		if (tokens[i].class_id != PRIMITIVE_CLASS_CONSECUTIVE ||
			tokens[i].dominant_direction == 0) {
			continue;
		}
		if (!primitive_token_is_neutral_bridge(candles, features, &tokens[neutral_end])) {
			continue;
		}
		while (neutral_end + 1u < *count &&
			primitive_token_is_neutral_bridge(candles, features, &tokens[neutral_end + 1u])) {
			neutral_end += 1u;
		}
		right_index = neutral_end + 1u;
		if (right_index >= *count) {
			continue;
		}
		if (tokens[right_index].class_id != PRIMITIVE_CLASS_CONSECUTIVE ||
			tokens[right_index].dominant_direction != tokens[i].dominant_direction) {
			continue;
		}

		compute_segment_eval(candles,
			features,
			candle_count,
			params,
			tokens[i].start_candle,
			tokens[right_index].end_candle,
			&merged_eval);
		if (primitive_class_from_eval(&merged_eval) != PRIMITIVE_CLASS_CONSECUTIVE ||
			merged_eval.dominant_direction != tokens[i].dominant_direction ||
			!primitive_span_is_strict_consecutive(candles,
				features,
				candle_count,
				params,
				tokens[i].start_candle,
				tokens[right_index].end_candle,
				NULL)) {
			continue;
		}

		for (k = i; k <= right_index; ++k) {
			split_total += tokens[k].fit_error + params->segment_penalty_lambda;
		}
		merged_total = merged_eval.class_error[PRIMITIVE_CLASS_CONSECUTIVE] + params->segment_penalty_lambda;
		if (merged_total > split_total) {
			continue;
		}

		merge_token_range(candles,
			features,
			candle_count,
			params,
			tokens,
			count,
			i,
			right_index,
			PRIMITIVE_CLASS_CONSECUTIVE);
		mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_BRIDGE_NEUTRAL);
		return 1;
	}
	return 0;
}

static void remove_token_at(WaveToken *tokens, uint32_t *count, uint32_t index)
{
	uint32_t i;

	if (tokens == NULL || count == NULL || index >= *count) {
		return;
	}
	for (i = index + 1u; i < *count; ++i) {
		tokens[i - 1u] = tokens[i];
	}
	*count -= 1u;
}

static void insert_token_at(WaveToken *tokens, uint32_t *count, uint32_t index, const WaveToken *token)
{
	uint32_t i;

	if (tokens == NULL || count == NULL || token == NULL || index > *count) {
		return;
	}
	for (i = *count; i > index; --i) {
		tokens[i] = tokens[i - 1u];
	}
	tokens[index] = *token;
	*count += 1u;
}

static void reclassify_token_span(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *token)
{
	WaveSegmentEval eval;
	uint32_t class_id;
	uint32_t debug_flags;
	uint32_t debug_reduction_flags;
	uint32_t debug_reduction_count;
	uint32_t seed_start_candle;
	uint32_t seed_end_candle;
	uint32_t seed_pass;

	if (candles == NULL || features == NULL || params == NULL || token == NULL) {
		return;
	}
	debug_flags = token->debug_flags;
	debug_reduction_flags = token->debug_reduction_flags;
	debug_reduction_count = token->debug_reduction_count;
	seed_start_candle = token->seed_start_candle;
	seed_end_candle = token->seed_end_candle;
	seed_pass = token->seed_pass;
	compute_segment_eval(candles, features, candle_count, params, token->start_candle, token->end_candle, &eval);
	class_id = classify_split_run(&eval, params);
	fill_token_from_eval(token, &eval, class_id, params);
	token->debug_flags = debug_flags;
	token->debug_reduction_flags = debug_reduction_flags;
	token->debug_reduction_count = debug_reduction_count;
	token->seed_start_candle = seed_start_candle;
	token->seed_end_candle = seed_end_candle;
	token->seed_pass = (class_id == RELATIVE_CLASS_TREND) ? seed_pass : 0u;
}

static void force_token_class_span(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *token,
	uint32_t class_id)
{
	WaveSegmentEval eval;
	uint32_t debug_flags;
	uint32_t debug_reduction_flags;
	uint32_t debug_reduction_count;
	uint32_t seed_start_candle;
	uint32_t seed_end_candle;
	uint32_t seed_pass;

	if (candles == NULL || features == NULL || params == NULL || token == NULL) {
		return;
	}
	debug_flags = token->debug_flags;
	debug_reduction_flags = token->debug_reduction_flags;
	debug_reduction_count = token->debug_reduction_count;
	seed_start_candle = token->seed_start_candle;
	seed_end_candle = token->seed_end_candle;
	seed_pass = token->seed_pass;
	compute_segment_eval(candles, features, candle_count, params, token->start_candle, token->end_candle, &eval);
	fill_token_from_eval(token, &eval, class_id, params);
	token->debug_flags = debug_flags;
	token->debug_reduction_flags = debug_reduction_flags;
	token->debug_reduction_count = debug_reduction_count;
	token->seed_start_candle = seed_start_candle;
	token->seed_end_candle = seed_end_candle;
	token->seed_pass = (class_id == RELATIVE_CLASS_TREND) ? seed_pass : 0u;
}

static uint32_t find_relative_trend_terminal_index(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction)
{
	uint32_t best_index = start;
	int32_t best_price;
	uint32_t c;

	if (candles == NULL || features == NULL || end < start || dominant_direction == 0) {
		return start;
	}
	best_price = (dominant_direction > 0) ?
		max_i32(candles[start].open_pips, candles[start].close_pips) :
		min_i32(candles[start].open_pips, candles[start].close_pips);
	for (c = start; c <= end; ++c) {
		int sign = sign_i32(features[c].b);
		int32_t price = (dominant_direction > 0) ?
			max_i32(candles[c].open_pips, candles[c].close_pips) :
			min_i32(candles[c].open_pips, candles[c].close_pips);

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

static uint32_t find_relative_trend_start_index(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction)
{
	uint32_t best_index = start;
	int32_t best_price;
	uint32_t c;

	if (candles == NULL || features == NULL || end < start || dominant_direction == 0) {
		return start;
	}
	best_price = (dominant_direction > 0) ?
		min_i32(candles[start].open_pips, candles[start].close_pips) :
		max_i32(candles[start].open_pips, candles[start].close_pips);
	for (c = start; c <= end; ++c) {
		int sign = sign_i32(features[c].b);
		int32_t price = (dominant_direction > 0) ?
			min_i32(candles[c].open_pips, candles[c].close_pips) :
			max_i32(candles[c].open_pips, candles[c].close_pips);

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

static int32_t compute_relative_trend_price_amplitude(const Candle *candles,
	const CandleFeat *features,
	uint32_t start,
	uint32_t end,
	int dominant_direction,
	uint32_t *out_start,
	uint32_t *out_end)
{
	uint32_t actual_start = start;
	uint32_t actual_end = end;

	if (candles == NULL || features == NULL || end < start || dominant_direction == 0) {
		return 0;
	}
	while (actual_start <= end && sign_i32(features[actual_start].b) != dominant_direction) {
		actual_start += 1u;
	}
	if (actual_start > end) {
		return 0;
	}
	while (actual_end > actual_start && sign_i32(features[actual_end].b) != dominant_direction) {
		actual_end -= 1u;
	}
	actual_end = find_relative_trend_terminal_index(candles, features, actual_start, actual_end, dominant_direction);
	actual_start = find_relative_trend_start_index(candles, features, actual_start, actual_end, dominant_direction);
	if (out_start != NULL) {
		*out_start = actual_start;
	}
	if (out_end != NULL) {
		*out_end = actual_end;
	}
	if (dominant_direction > 0) {
		return max_i32(candles[actual_end].open_pips, candles[actual_end].close_pips) -
			min_i32(candles[actual_start].open_pips, candles[actual_start].close_pips);
	}
	return max_i32(candles[actual_start].open_pips, candles[actual_start].close_pips) -
		min_i32(candles[actual_end].open_pips, candles[actual_end].close_pips);
}

static void normalize_relative_trend_tokens(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL ||
		(params->wave_model != WAVE_MODEL_RELATIVE_2_STAGE &&
		 !is_relative_seeded_family((uint32_t)params->wave_model))) {
		return;
	}
	for (i = 0u; i < *count; ++i) {
		WaveToken range_token;
		uint32_t old_start;
		uint32_t old_end;
		uint32_t new_start;
		uint32_t new_end;
		int dominant_direction;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND || tokens[i].dominant_direction == 0) {
			continue;
		}
		old_start = tokens[i].start_candle;
		old_end = tokens[i].end_candle;
		new_start = old_start;
		new_end = old_end;
		dominant_direction = tokens[i].dominant_direction;

		while (new_start <= old_end && sign_i32(features[new_start].b) != dominant_direction) {
			new_start += 1u;
		}
		if (new_start > old_end) {
			force_token_class_span(candles, features, candle_count, params, &tokens[i], RELATIVE_CLASS_RANGE);
			mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			continue;
		}
		(void)compute_relative_trend_price_amplitude(candles,
			features,
			new_start,
			new_end,
			dominant_direction,
			&new_start,
			&new_end);
		if (tokens[i].seed_start_candle >= new_start &&
			tokens[i].seed_start_candle <= new_end &&
			tokens[i].seed_end_candle >= tokens[i].seed_start_candle &&
			new_start < tokens[i].seed_start_candle) {
			uint32_t candidate_start;
			uint32_t best_seeded_start = tokens[i].seed_start_candle;
			int32_t min_seed_step = max_i32(params->relative_range_der_step_max + 120, params->relative_trend_der_step_min - 120);
			int32_t min_seed_body = max_i32(params->relative_trend_der_body_min - 140, params->norm_scale / 3);
			int32_t min_seed_body_eff = max_i32(params->relative_trend_body_eff_min - 80, params->relative_range_body_eff_max);

			for (candidate_start = tokens[i].seed_start_candle; candidate_start > new_start; --candidate_start) {
				WaveSegmentEval seed_prefix_eval;
				uint32_t trial_start = candidate_start - 1u;

				if (sign_i32(features[trial_start].b) != dominant_direction) {
					continue;
				}
				compute_segment_eval(candles,
					features,
					candle_count,
					params,
					trial_start,
					tokens[i].seed_end_candle,
					&seed_prefix_eval);
				if (seed_prefix_eval.best_class != RELATIVE_CLASS_TREND ||
					seed_prefix_eval.dominant_direction != dominant_direction ||
					seed_prefix_eval.relative_directional_efficiency_step < min_seed_step ||
					seed_prefix_eval.relative_directional_efficiency_body < min_seed_body ||
					seed_prefix_eval.relative_avg_body_efficiency < min_seed_body_eff ||
					seed_prefix_eval.relative_body_direction_share < 700 ||
					seed_prefix_eval.relative_overlap_ratio > params->relative_trend_overlap_max + 80 ||
					seed_prefix_eval.sign_change_density > max_i32(params->relative_trend_sign_flip_max - 120, 0)) {
					continue;
				}
				best_seeded_start = trial_start;
			}
			if (best_seeded_start > new_start) {
				new_start = best_seeded_start;
			}
		}
		if (new_start == old_start && new_end == old_end) {
			continue;
		}
		if (old_start < new_start) {
			if (i > 0u &&
				tokens[i - 1u].class_id == RELATIVE_CLASS_RANGE &&
				tokens[i - 1u].end_candle + 1u == old_start) {
				tokens[i - 1u].end_candle = new_start - 1u;
				force_token_class_span(candles, features, candle_count, params, &tokens[i - 1u], RELATIVE_CLASS_RANGE);
				mark_token_reduction(&tokens[i - 1u], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			} else {
				memset(&range_token, 0, sizeof(range_token));
				range_token.start_candle = old_start;
				range_token.end_candle = new_start - 1u;
				force_token_class_span(candles, features, candle_count, params, &range_token, RELATIVE_CLASS_RANGE);
				range_token.debug_reduction_flags = WAVE_TOKEN_REDUCTION_SPLIT_EDGE;
				range_token.debug_reduction_count = 1u;
				insert_token_at(tokens, count, i, &range_token);
				i += 1u;
			}
		}
		tokens[i].start_candle = new_start;
		tokens[i].end_candle = new_end;
		force_token_class_span(candles, features, candle_count, params, &tokens[i], RELATIVE_CLASS_TREND);
		mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
		if (new_end < old_end) {
			if (i + 1u < *count &&
				tokens[i + 1u].class_id == RELATIVE_CLASS_RANGE &&
				old_end + 1u == tokens[i + 1u].start_candle) {
				tokens[i + 1u].start_candle = new_end + 1u;
				force_token_class_span(candles, features, candle_count, params, &tokens[i + 1u], RELATIVE_CLASS_RANGE);
				mark_token_reduction(&tokens[i + 1u], WAVE_TOKEN_REDUCTION_SPLIT_EDGE);
			} else {
				memset(&range_token, 0, sizeof(range_token));
				range_token.start_candle = new_end + 1u;
				range_token.end_candle = old_end;
				force_token_class_span(candles, features, candle_count, params, &range_token, RELATIVE_CLASS_RANGE);
				range_token.debug_reduction_flags = WAVE_TOKEN_REDUCTION_SPLIT_EDGE;
				range_token.debug_reduction_count = 1u;
				insert_token_at(tokens, count, i + 1u, &range_token);
			}
		}
	}
}

static void normalize_relative_trend_seed_bounds(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL ||
		(params->wave_model != WAVE_MODEL_RELATIVE_2_STAGE &&
		 !is_relative_seeded_family((uint32_t)params->wave_model))) {
		return;
	}
#if WAVE_DEBUG_SEEDED_PASS1_ONLY || WAVE_DEBUG_SEEDED_PASS1_GROWN_ONLY
	return;
#endif
	(void)candle_count;
	for (i = 0u; i < count; ++i) {
		uint32_t seed_start;
		uint32_t seed_end;
		uint32_t new_start;
		uint32_t new_end;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND || tokens[i].dominant_direction == 0) {
			continue;
		}
		seed_start = tokens[i].seed_start_candle;
		seed_end = tokens[i].seed_end_candle;
		if (seed_end < seed_start ||
			seed_start < tokens[i].start_candle ||
			seed_end > tokens[i].end_candle) {
			seed_start = tokens[i].start_candle;
			seed_end = tokens[i].end_candle;
		}
		if (compute_relative_trend_price_amplitude(candles,
			features,
			seed_start,
			seed_end,
			tokens[i].dominant_direction,
			&new_start,
			&new_end) <= 0) {
			if (compute_relative_trend_price_amplitude(candles,
				features,
				tokens[i].start_candle,
				tokens[i].end_candle,
				tokens[i].dominant_direction,
				&new_start,
				&new_end) <= 0) {
				continue;
			}
		}
		tokens[i].seed_start_candle = new_start;
		tokens[i].seed_end_candle = new_end;
	}
}

static uint32_t run_relative_iterative_refinement(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	WaveKPI *kpi)
{
	uint32_t outer_iter;
	uint32_t changed_total = 0u;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL || kpi == NULL) {
		return 0u;
	}
	if (params->wave_model != WAVE_MODEL_RELATIVE_ITERATIVE) {
		return 0u;
	}
	for (outer_iter = 0u; outer_iter < 8u; ++outer_iter) {
		uint32_t i;
		int changed = 0;

		for (i = 0u; i < *count; ++i) {
			recompute_token(candles, features, candle_count, params, &tokens[i]);
		}
		normalize_relative_trend_tokens(candles, features, candle_count, params, tokens, count);
		if (split_relative_ranges_by_edge_trim(candles, features, candle_count, params, tokens, count) > 0) {
			changed = 1;
		}
		if (split_relative_edge_trends(candles, features, candle_count, params, tokens, count) > 0) {
			changed = 1;
		}
		if (demote_relative_micro_trends(candles, features, candle_count, params, tokens, count) > 0) {
			changed = 1;
		}
		if (reduce_relative_choppy_runs(candles, features, candle_count, params, tokens, count) > 0) {
			changed = 1;
		}
		if (merge_relative_trend_across_small_noise(candles, features, candle_count, params, tokens, count) > 0) {
			changed = 1;
		}
		if (extend_relative_trend_boundaries(candles, features, candle_count, params, tokens, *count) > 0) {
			changed = 1;
		}
		if (resolve_relative_same_direction_trends(candles, features, candle_count, params, tokens, count, kpi) > 0) {
			changed = 1;
		}
		enforce_relative_two_stage_trend_edges(candles, features, candle_count, params, tokens, *count);
		normalize_relative_trend_seed_bounds(candles, features, candle_count, params, tokens, *count);
		if (!changed) {
			break;
		}
		changed_total += 1u;
	}
	return changed_total;
}

static int split_relative_range_by_edge_trim(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	uint32_t index)
{
	static const uint32_t trim_options[3][2] = {
		{1u, 0u},
		{0u, 1u},
		{1u, 1u}
	};
	WaveToken original;
	WaveSegmentEval best_eval;
	WaveToken parts[3];
	uint32_t best_start = 0u;
	uint32_t best_end = 0u;
	uint32_t best_trim_left = 0u;
	uint32_t best_trim_right = 0u;
	uint32_t part_count = 0u;
	int has_best = 0;
	uint32_t option_index;
	uint32_t write_index;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL ||
		index >= *count || tokens[index].class_id != RELATIVE_CLASS_RANGE ||
		(!is_relative_seeded_family((uint32_t)params->wave_model) &&
		 params->wave_model != WAVE_MODEL_RELATIVE_2_STAGE)) {
		return 0;
	}
	original = tokens[index];
	for (option_index = 0u; option_index < 3u; ++option_index) {
		uint32_t trim_left = trim_options[option_index][0];
		uint32_t trim_right = trim_options[option_index][1];
		uint32_t start;
		uint32_t end;
		WaveSegmentEval eval;

		if (original.start_candle + trim_left > original.end_candle) {
			continue;
		}
		start = original.start_candle + trim_left;
		end = original.end_candle - trim_right;
		if (end < start || (end - start + 1u) < (uint32_t)max_i32((int32_t)params->lmin_candles, 3)) {
			continue;
		}
		compute_segment_eval(candles, features, candle_count, params, start, end, &eval);
		if (eval.best_class != RELATIVE_CLASS_TREND ||
			!relative_span_has_consistent_edges(candles, features, start, end, eval.dominant_direction) ||
			!relative_span_is_trend_valid(&eval, params) ||
			!is_seed_relative_trend_for_pass(&eval, params, 2u)) {
			continue;
		}
		if (!has_best ||
			eval.class_error[RELATIVE_CLASS_TREND] < best_eval.class_error[RELATIVE_CLASS_TREND] ||
			(eval.class_error[RELATIVE_CLASS_TREND] == best_eval.class_error[RELATIVE_CLASS_TREND] &&
			 abs_i32(eval.relative_net_move_raw) > abs_i32(best_eval.relative_net_move_raw))) {
			best_eval = eval;
			best_start = start;
			best_end = end;
			best_trim_left = trim_left;
			best_trim_right = trim_right;
			has_best = 1;
		}
	}
	if (!has_best) {
		return 0;
	}
	if (best_trim_left > 0u) {
		memset(&parts[part_count], 0, sizeof(parts[part_count]));
		parts[part_count].start_candle = original.start_candle;
		parts[part_count].end_candle = best_start - 1u;
		force_token_class_span(candles, features, candle_count, params, &parts[part_count], RELATIVE_CLASS_RANGE);
		parts[part_count].debug_reduction_flags = WAVE_TOKEN_REDUCTION_SPLIT_EDGE;
		parts[part_count].debug_reduction_count = 1u;
		part_count += 1u;
	}
	memset(&parts[part_count], 0, sizeof(parts[part_count]));
	parts[part_count].start_candle = best_start;
	parts[part_count].end_candle = best_end;
	fill_token_from_eval(&parts[part_count], &best_eval, RELATIVE_CLASS_TREND, params);
	parts[part_count].seed_start_candle = best_start;
	parts[part_count].seed_end_candle = best_end;
	parts[part_count].seed_pass = 2u;
	parts[part_count].debug_reduction_flags = WAVE_TOKEN_REDUCTION_SPLIT_EDGE;
	parts[part_count].debug_reduction_count = 1u;
	part_count += 1u;
	if (best_trim_right > 0u) {
		memset(&parts[part_count], 0, sizeof(parts[part_count]));
		parts[part_count].start_candle = best_end + 1u;
		parts[part_count].end_candle = original.end_candle;
		force_token_class_span(candles, features, candle_count, params, &parts[part_count], RELATIVE_CLASS_RANGE);
		parts[part_count].debug_reduction_flags = WAVE_TOKEN_REDUCTION_SPLIT_EDGE;
		parts[part_count].debug_reduction_count = 1u;
		part_count += 1u;
	}
	tokens[index] = parts[0];
	for (write_index = 1u; write_index < part_count; ++write_index) {
		insert_token_at(tokens, count, index + write_index, &parts[write_index]);
	}
	return 1;
}

static int split_relative_ranges_by_edge_trim(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return 0;
	}
	for (i = 0u; i < *count; ++i) {
		if (split_relative_range_by_edge_trim(candles, features, candle_count, params, tokens, count, i) > 0) {
			changed = 1;
			i = (i > 0u) ? (i - 1u) : 0u;
		}
	}
	return changed;
}

static int extend_consecutive_with_adjacent_edges(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;
	int changed = 0;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	for (i = 0u; i < *count; ++i) {
		int local_changed;

		if (tokens[i].class_id != PRIMITIVE_CLASS_CONSECUTIVE) {
			continue;
		}

		do {
			local_changed = 0;
			while (i > 0u && tokens[i - 1u].end_candle + 1u == tokens[i].start_candle) {
				uint32_t candidate_start = tokens[i].start_candle - 1u;

				if (!primitive_span_is_strict_consecutive(candles,
					features,
					candle_count,
					params,
					candidate_start,
					tokens[i].end_candle,
					NULL)) {
					break;
				}
				tokens[i].start_candle = candidate_start;
				recompute_token(candles, features, candle_count, params, &tokens[i]);
				mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_EXTEND_CONSEC_EDGE);
				if (tokens[i - 1u].start_candle == tokens[i - 1u].end_candle) {
					remove_token_at(tokens, count, i - 1u);
					i -= 1u;
				} else {
					tokens[i - 1u].end_candle -= 1u;
					reclassify_token_span(candles, features, candle_count, params, &tokens[i - 1u]);
				}
				local_changed = 1;
				changed = 1;
			}
			while (i + 1u < *count && tokens[i].end_candle + 1u == tokens[i + 1u].start_candle) {
				uint32_t candidate_end = tokens[i].end_candle + 1u;

				if (!primitive_span_is_strict_consecutive(candles,
					features,
					candle_count,
					params,
					tokens[i].start_candle,
					candidate_end,
					NULL)) {
					break;
				}
				tokens[i].end_candle = candidate_end;
				recompute_token(candles, features, candle_count, params, &tokens[i]);
				mark_token_reduction(&tokens[i], WAVE_TOKEN_REDUCTION_EXTEND_CONSEC_EDGE);
				if (tokens[i + 1u].start_candle == tokens[i + 1u].end_candle) {
					remove_token_at(tokens, count, i + 1u);
				} else {
					tokens[i + 1u].start_candle += 1u;
					reclassify_token_span(candles, features, candle_count, params, &tokens[i + 1u]);
				}
				local_changed = 1;
				changed = 1;
			}
		} while (local_changed);
	}
	return changed;
}

static int apply_retracement_rules(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count,
	WaveKPI *kpi)
{
	uint32_t i;

	for (i = 0; i < *count; ++i) {
		if (tokens[i].class_id != WAVE_CLASS_RETRACEMENT) {
			continue;
		}
		if (i == 0u || i + 1u >= *count) {
			WaveSegmentEval eval;

			kpi->rule_retracement_edge_count += 1u;
			compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, tokens[i].end_candle, &eval);
			fill_token_from_eval(&tokens[i], &eval, (uint32_t)fallback_class_for_eval(&eval, 1), params);
			return 1;
		}
		if (tokens[i - 1u].class_id == WAVE_CLASS_RETRACEMENT) {
			WaveSegmentEval eval;

			kpi->rule_retracement_chain_count += 1u;
			compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, tokens[i].end_candle, &eval);
			fill_token_from_eval(&tokens[i], &eval, (uint32_t)fallback_class_for_eval(&eval, 1), params);
			return 1;
		}
		if (!is_trend_class(tokens[i - 1u].class_id) || !is_trend_class(tokens[i + 1u].class_id)) {
			WaveSegmentEval eval;

			kpi->rule_retracement_context_count += 1u;
			compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, tokens[i].end_candle, &eval);
			fill_token_from_eval(&tokens[i], &eval, (uint32_t)fallback_class_for_eval(&eval, 1), params);
			return 1;
		}
		if (tokens[i - 1u].dominant_direction == 0 ||
			tokens[i - 1u].dominant_direction != tokens[i + 1u].dominant_direction ||
			tokens[i].dominant_direction == tokens[i - 1u].dominant_direction) {
			WaveSegmentEval eval;

			kpi->rule_retracement_direction_count += 1u;
			compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, tokens[i].end_candle, &eval);
			fill_token_from_eval(&tokens[i], &eval, (uint32_t)fallback_class_for_eval(&eval, 1), params);
			return 1;
		}
		if (tokens[i - 1u].net_move_raw > 0) {
			int32_t retrace_strength = (int32_t)(((int64_t)tokens[i].net_move_raw * params->norm_scale) /
				(int64_t)max_i32(tokens[i - 1u].net_move_raw, 1));

			if (retrace_strength < params->retrace_break_threshold) {
				WaveSegmentEval eval;
				uint32_t merged_class_id;

				kpi->retracement_threshold_rejected_count += 1u;
				compute_segment_eval(candles, features, candle_count, params, tokens[i - 1u].start_candle, tokens[i + 1u].end_candle, &eval);
				merged_class_id = select_merged_trend_class(&eval, params);
				if (merged_class_id < WAVE_CLASS_COUNT) {
					merge_token_range(candles, features, candle_count, params, tokens, count, i - 1u, i + 1u, merged_class_id);
					return 1;
				}
			}
		}
	}
	return 0;
}

static void compute_boundary_confidence(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t count,
	WaveKPI *kpi)
{
	uint32_t i;
	int64_t boundary_conf_sum = 0;

	for (i = 0; i < count; ++i) {
		tokens[i].boundary_confidence = 0;
	}
	for (i = 0; i + 1u < count; ++i) {
		WaveSegmentEval merged_eval;
		int32_t boundary_conf;

		compute_segment_eval(candles, features, candle_count, params, tokens[i].start_candle, tokens[i + 1u].end_candle, &merged_eval);
		boundary_conf = merged_eval.best_fit - (tokens[i].fit_error + tokens[i + 1u].fit_error);
		tokens[i].boundary_confidence = boundary_conf;
		boundary_conf_sum += boundary_conf;
		if (boundary_conf < params->boundary_low_conf_threshold) {
			kpi->low_boundary_confidence_count += 1u;
		}
	}
	if (count > 1u) {
		kpi->avg_boundary_confidence = (float)boundary_conf_sum / (float)(count - 1u);
	}
}

static void enforce_relative_two_stage_trend_edges(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t token_count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL ||
		params->wave_model != WAVE_MODEL_RELATIVE_2_STAGE) {
		return;
	}
	for (i = 0u; i < token_count; ++i) {
		WaveSegmentEval eval;

		if (tokens[i].class_id != RELATIVE_CLASS_TREND) {
			continue;
		}
		if (relative_span_has_consistent_edges(candles,
			features,
			tokens[i].start_candle,
			tokens[i].end_candle,
			tokens[i].dominant_direction)) {
			continue;
		}
		compute_segment_eval(candles,
			features,
			candle_count,
			params,
			tokens[i].start_candle,
			tokens[i].end_candle,
			&eval);
		fill_token_from_eval(&tokens[i], &eval, RELATIVE_CLASS_RANGE, params);
	}
}

static int merge_relative_trend_across_small_noise(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t *count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL || count == NULL) {
		return -1;
	}
	for (i = 0u; i + 2u < *count; ++i) {
		WaveSegmentEval merged_eval;
		int32_t split_total;

		if (!is_relative_trend_class(tokens[i].class_id, params) ||
			tokens[i + 1u].class_id != RELATIVE_CLASS_RANGE ||
			!is_relative_trend_class(tokens[i + 2u].class_id, params)) {
			continue;
		}
		if (tokens[i].dominant_direction == 0 ||
			tokens[i].dominant_direction != tokens[i + 2u].dominant_direction ||
			tokens[i + 1u].duration_candles > 3u ||
			abs_i32(tokens[i + 1u].net_move_raw) > max_i32(params->min_wave_move_pips, 1)) {
			continue;
		}
		compute_segment_eval(candles,
			features,
			candle_count,
			params,
			tokens[i].start_candle,
			tokens[i + 2u].end_candle,
			&merged_eval);
		if (!relative_span_is_trend_valid(&merged_eval, params) ||
			!relative_class_id_is_valid(merged_eval.best_class) ||
			merged_eval.best_class != RELATIVE_CLASS_TREND) {
			continue;
		}
		split_total = tokens[i].fit_error + tokens[i + 1u].fit_error + tokens[i + 2u].fit_error + params->segment_penalty_lambda;
		if (merged_eval.class_error[RELATIVE_CLASS_TREND] > split_total) {
			continue;
		}
		merge_token_range(candles, features, candle_count, params, tokens, count, i, i + 2u, RELATIVE_CLASS_TREND);
		return 1;
	}
	return 0;
}

static int extend_relative_trend_boundaries(const Candle *candles,
	const CandleFeat *features,
	uint32_t candle_count,
	const WaveParams *params,
	WaveToken *tokens,
	uint32_t count)
{
	uint32_t i;

	if (candles == NULL || features == NULL || params == NULL || tokens == NULL) {
		return -1;
	}
	for (i = 0u; i < count; ++i) {
		WaveSegmentEval eval;

		if (!is_relative_trend_class(tokens[i].class_id, params)) {
			continue;
		}
		if (i > 0u) {
			compute_segment_eval(candles,
				features,
				candle_count,
				params,
				tokens[i].start_candle - 1u,
				tokens[i].end_candle,
				&eval);
			if (relative_span_is_trend_valid(&eval, params) &&
				tokens[i - 1u].class_id == RELATIVE_CLASS_RANGE &&
				tokens[i - 1u].end_candle + 1u == tokens[i].start_candle &&
				tokens[i - 1u].duration_candles > 1u &&
				eval.class_error[RELATIVE_CLASS_TREND] <= tokens[i].fit_error + 120) {
				tokens[i - 1u].end_candle -= 1u;
				recompute_token(candles, features, candle_count, params, &tokens[i - 1u]);
				tokens[i].start_candle -= 1u;
				fill_token_from_eval(&tokens[i], &eval, RELATIVE_CLASS_TREND, params);
				return 1;
			}
		}
		if (tokens[i].end_candle + 1u < candle_count) {
			compute_segment_eval(candles,
				features,
				candle_count,
				params,
				tokens[i].start_candle,
				tokens[i].end_candle + 1u,
				&eval);
			if (relative_span_is_trend_valid(&eval, params) &&
				i + 1u < count &&
				tokens[i + 1u].class_id == RELATIVE_CLASS_RANGE &&
				tokens[i].end_candle + 1u == tokens[i + 1u].start_candle &&
				tokens[i + 1u].duration_candles > 1u &&
				eval.class_error[RELATIVE_CLASS_TREND] <= tokens[i].fit_error + 120) {
				tokens[i + 1u].start_candle += 1u;
				recompute_token(candles, features, candle_count, params, &tokens[i + 1u]);
				tokens[i].end_candle += 1u;
				fill_token_from_eval(&tokens[i], &eval, RELATIVE_CLASS_TREND, params);
				return 1;
			}
		}
	}
	return 0;
}

static void compute_kpis(const WaveParams *params,
	WaveToken *tokens,
	uint32_t count,
	uint32_t candle_count,
	WaveKPI *kpi)
{
	uint32_t i;
	int64_t ambiguity_sum = 0;
	int64_t length_sum = 0;
	int64_t long_err_sum = 0;
	uint32_t class_len_counts[WAVE_CLASS_COUNT];

	memset(kpi->class_candle_coverage, 0, sizeof(kpi->class_candle_coverage));
	memset(kpi->class_avg_length, 0, sizeof(kpi->class_avg_length));
	memset(class_len_counts, 0, sizeof(class_len_counts));
	for (i = 0; i < count; ++i) {
		uint32_t cid = tokens[i].class_id;

		ambiguity_sum += tokens[i].ambiguity_margin;
		length_sum += tokens[i].duration_candles;
		if (tokens[i].ambiguity_margin < params->ambiguity_low_margin_threshold) {
			kpi->low_margin_token_count += 1u;
		}
		if (tokens[i].duration_candles == 1u) {
			kpi->pct_one_candle_tokens += 1.0f;
		}
		if (tokens[i].duration_candles == 2u) {
			kpi->pct_two_candle_tokens += 1.0f;
		}
		if (tokens[i].duration_candles + 1u >= params->lmax_candles) {
			kpi->long_token_count += 1u;
			long_err_sum += tokens[i].fit_error;
		}
		if (tokens[i].weak_fit) {
			kpi->weak_fit_count += 1u;
		}
		if (params->wave_model != WAVE_MODEL_PRIMITIVE && cid < WAVE_CLASS_COUNT) {
			kpi->class_candle_coverage[cid] += tokens[i].duration_candles;
			kpi->class_avg_length[cid] += (float)tokens[i].duration_candles;
			class_len_counts[cid] += 1u;
		}
		if (is_relative_class_wave_model((uint32_t)params->wave_model)) {
			kpi->avg_relative_directional_efficiency_step += (float)tokens[i].relative_directional_efficiency_step;
			kpi->avg_relative_directional_efficiency_body += (float)tokens[i].relative_directional_efficiency_body;
			kpi->avg_relative_retracement_ratio += (float)tokens[i].relative_retracement_ratio;
			kpi->avg_relative_overlap_ratio += (float)tokens[i].relative_overlap_ratio;
			kpi->avg_relative_body_efficiency += (float)tokens[i].relative_avg_body_efficiency;
			if (cid == RELATIVE_CLASS_TREND) {
				kpi->relative_trend_count += 1u;
				if (tokens[i].seed_pass == 1u) {
					kpi->seeded_pass1_trend_count += 1u;
				} else if (tokens[i].seed_pass == 2u) {
					kpi->seeded_pass2_trend_count += 1u;
				}
				if (tokens[i].relative_directional_efficiency_step < params->relative_trend_der_step_min) {
					kpi->relative_trend_low_efficiency_count += 1u;
				}
				if (tokens[i].relative_retracement_ratio > params->relative_trend_retracement_max) {
					kpi->relative_trend_high_retracement_count += 1u;
				}
			} else if (cid == RELATIVE_CLASS_RANGE) {
				kpi->relative_range_count += 1u;
				if (tokens[i].dominant_direction > 0) {
					kpi->relative_range_updrift_count += 1u;
				} else if (tokens[i].dominant_direction < 0) {
					kpi->relative_range_downdrift_count += 1u;
				}
			}
		}
	}
	if (count > 0u) {
		kpi->avg_ambiguity_margin = (float)ambiguity_sum / (float)count;
		kpi->avg_token_length = (float)length_sum / (float)count;
		kpi->tokens_per_1000_candles = ((float)count * 1000.0f) / (float)max_i32((int32_t)candle_count, 1);
		kpi->pct_one_candle_tokens = (kpi->pct_one_candle_tokens * 100.0f) / (float)count;
		kpi->pct_two_candle_tokens = (kpi->pct_two_candle_tokens * 100.0f) / (float)count;
		kpi->pct_near_lmax_tokens = ((float)kpi->long_token_count * 100.0f) / (float)count;
		if (is_relative_class_wave_model((uint32_t)params->wave_model)) {
			kpi->avg_relative_directional_efficiency_step /= (float)count;
			kpi->avg_relative_directional_efficiency_body /= (float)count;
			kpi->avg_relative_retracement_ratio /= (float)count;
			kpi->avg_relative_overlap_ratio /= (float)count;
			kpi->avg_relative_body_efficiency /= (float)count;
		}
	}
	if (kpi->long_token_count > 0u) {
		kpi->avg_long_token_error = (float)long_err_sum / (float)kpi->long_token_count;
	}
	for (i = 0; i < WAVE_CLASS_COUNT; ++i) {
		if (class_len_counts[i] > 0u) {
			kpi->class_avg_length[i] /= (float)class_len_counts[i];
		}
	}
}

int wave_dp_run(uint32_t dataset_id, const WaveParams *params, volatile float *progress, volatile int *cancel_flag, char *err, size_t err_sz)
{
	DatasetRecord *rec;
	WaveParams resolved;
	uint64_t run_start_ms;
	uint64_t stage_start_ms;
	uint32_t n;
	WaveToken *tokens = NULL;
	WaveToken *layer1_tokens = NULL;
	uint32_t token_count = 0;
	uint32_t layer1_token_count = 0u;
	int64_t total_fit_error = 0;
	int rc;
	uint32_t i;
	uint32_t stage_iter = 0u;

	(void)cancel_flag;
	resolved = default_wave_params();
	merge_wave_params(&resolved, params);
	run_start_ms = monotonic_ms();
	if (!gpu_dispatch_available()) {
		set_err(err, err_sz, "HIP backend unavailable");
		return -1;
	}
	if (ensure_features(dataset_id, &rec) != 0) {
		set_err(err, err_sz, "feature preparation failed");
		return -2;
	}
	n = rec->meta.feature_count;
	if (n == 0) {
		set_err(err, err_sz, "dataset has no candles");
		return -3;
	}
	if (validate_params(&resolved, n) != 0) {
		set_err(err, err_sz, "invalid wave params");
		return -4;
	}
	tokens = (WaveToken *)malloc((size_t)n * sizeof(*tokens));
	if (tokens == NULL) {
		free(tokens);
		set_err(err, err_sz, "allocation failed");
		return -5;
	}
	trainer_log_info("wave inference start dataset_id=%u candles=%u wave_model=%d",
		dataset_id,
		n,
		(int)resolved.wave_model);
	if (progress != NULL) {
		*progress = 0.10f;
	}
	stage_start_ms = monotonic_ms();
	rc = build_trend_priority_tokens(rec->candles,
		rec->features,
		n,
		&resolved,
		tokens,
		&token_count,
		&layer1_tokens,
		&layer1_token_count);
	if (rc != 0) {
		trainer_log_error("build_trend_priority_tokens failed rc=%d wave_model=%u", rc, (uint32_t)resolved.wave_model);
		free(layer1_tokens);
		free(tokens);
		set_err(err, err_sz, "wave segmentation failed");
		return -6;
	}
	log_wave_stage_done(dataset_id, "build_trend_priority_tokens", 1u, token_count, stage_start_ms);
	if (progress != NULL) {
		*progress = 0.55f;
	}
	stage_start_ms = monotonic_ms();
	for (i = 0; i < token_count; ++i) {
		recompute_token(rec->candles, rec->features, n, &resolved, &tokens[i]);
	}
	log_wave_stage_done(dataset_id, "recompute_initial_tokens", token_count, token_count, stage_start_ms);
	if (resolved.wave_model == WAVE_MODEL_PRIMITIVE) {
		uint32_t stage_iter;

		stage_start_ms = monotonic_ms();
		if (split_primitive_mixed_tokens(rec->candles, rec->features, n, &resolved, tokens, &token_count) != 0) {
			free(layer1_tokens);
			free(tokens);
			set_err(err, err_sz, "primitive split failed");
			return -6;
		}
		log_wave_stage_done(dataset_id, "split_primitive_mixed_tokens", 1u, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (split_primitive_internal_trends(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			/* Repeatedly peel the strongest interior consecutive run from mixed alternating spans. */
			stage_iter += 1u;
			if ((stage_iter % 100u) == 0u) {
				log_wave_stage_iter(dataset_id, "split_primitive_internal_trends", stage_iter, token_count, stage_start_ms);
			}
		}
		log_wave_stage_done(dataset_id, "split_primitive_internal_trends", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (split_primitive_edge_trends(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			/* Keep extracting edge breakouts until no alternating token contains a stronger edge trend. */
			stage_iter += 1u;
			if ((stage_iter % 100u) == 0u) {
				log_wave_stage_iter(dataset_id, "split_primitive_edge_trends", stage_iter, token_count, stage_start_ms);
			}
		}
		log_wave_stage_done(dataset_id, "split_primitive_edge_trends", stage_iter, token_count, stage_start_ms);

		stage_start_ms = monotonic_ms();
		coalesce_maximal_primitive_spans(rec->candles, rec->features, n, &resolved, tokens, &token_count);
		log_wave_stage_done(dataset_id, "coalesce_maximal_primitive_spans", 1u, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (expand_consecutive_boundaries(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			/* Consecutive runs may absorb one same-direction boundary candle from adjacent alternating spans. */
			stage_iter += 1u;
			if ((stage_iter % 100u) == 0u) {
				log_wave_stage_iter(dataset_id, "expand_consecutive_boundaries", stage_iter, token_count, stage_start_ms);
			}
		}
		log_wave_stage_done(dataset_id, "expand_consecutive_boundaries", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (collapse_separator_runs(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			/* Separator islands should resolve into a fitted primitive wave before neighbor cleanup. */
			stage_iter += 1u;
			if ((stage_iter % 100u) == 0u) {
				log_wave_stage_iter(dataset_id, "collapse_separator_runs", stage_iter, token_count, stage_start_ms);
			}
		}
		log_wave_stage_done(dataset_id, "collapse_separator_runs", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (merge_separator_into_consecutive(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			/* Same-direction separator candles should be absorbed into adjacent consecutive runs when the merged span stays consecutive. */
			stage_iter += 1u;
			if ((stage_iter % 100u) == 0u) {
				log_wave_stage_iter(dataset_id, "merge_separator_into_consecutive", stage_iter, token_count, stage_start_ms);
			}
		}
		log_wave_stage_done(dataset_id, "merge_separator_into_consecutive", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (merge_separator_into_alternating(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			/* Final cleanup: absorb fragmentation separators into alternating spans when degradation stays bounded. */
			stage_iter += 1u;
			if ((stage_iter % 100u) == 0u) {
				log_wave_stage_iter(dataset_id, "merge_separator_into_alternating", stage_iter, token_count, stage_start_ms);
			}
		}
		log_wave_stage_done(dataset_id, "merge_separator_into_alternating", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (bridge_neutral_primitive_tokens(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			uint32_t j;

			stage_iter += 1u;
			if ((stage_iter % 100u) == 0u) {
				log_wave_stage_iter(dataset_id, "bridge_neutral_primitive_tokens", stage_iter, token_count, stage_start_ms);
			}
			for (j = 0; j < token_count; ++j) {
				recompute_token(rec->candles, rec->features, n, &resolved, &tokens[j]);
			}
		}
		log_wave_stage_done(dataset_id, "bridge_neutral_primitive_tokens", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		rc = extend_consecutive_with_adjacent_edges(rec->candles, rec->features, n, &resolved, tokens, &token_count);
		if (rc < 0) {
			free(layer1_tokens);
			free(tokens);
			set_err(err, err_sz, "primitive consecutive edge extension failed");
			return -6;
		}
		stage_iter = (rc > 0) ? 1u : 0u;
		if (rc > 0) {
			uint32_t j;

			for (j = 0; j < token_count; ++j) {
				recompute_token(rec->candles, rec->features, n, &resolved, &tokens[j]);
			}
		}
		log_wave_stage_done(dataset_id, "extend_consecutive_with_adjacent_edges", stage_iter, token_count, stage_start_ms);
	}

	memset(&rec->waves.kpi, 0, sizeof(rec->waves.kpi));
	if (resolved.wave_model == WAVE_MODEL_RELATIVE) {
		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (merge_relative_trend_across_small_noise(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			stage_iter += 1u;
		}
		log_wave_stage_done(dataset_id, "merge_relative_trend_across_small_noise", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (extend_relative_trend_boundaries(rec->candles, rec->features, n, &resolved, tokens, token_count) > 0) {
			stage_iter += 1u;
		}
		log_wave_stage_done(dataset_id, "extend_relative_trend_boundaries", stage_iter, token_count, stage_start_ms);
	} else if (resolved.wave_model != WAVE_MODEL_LAYERED &&
		!is_relative_wave_model((uint32_t)resolved.wave_model) &&
		resolved.wave_model != WAVE_MODEL_PRIMITIVE) {
		stage_start_ms = monotonic_ms();
		merge_same_direction_trends(rec->candles, rec->features, n, &resolved, tokens, &token_count, &rec->waves.kpi);
		merge_v_reversals(rec->candles, rec->features, n, &resolved, tokens, &token_count, &rec->waves.kpi);
		log_wave_stage_done(dataset_id, "merge_trend_rules", 1u, token_count, stage_start_ms);
	}
	while (resolved.wave_model == WAVE_MODEL_LEGACY &&
		apply_retracement_rules(rec->candles, rec->features, n, &resolved, tokens, &token_count, &rec->waves.kpi)) {
		merge_same_direction_trends(rec->candles, rec->features, n, &resolved, tokens, &token_count, &rec->waves.kpi);
		merge_v_reversals(rec->candles, rec->features, n, &resolved, tokens, &token_count, &rec->waves.kpi);
	}
	for (i = 0; i < token_count; ++i) {
		recompute_token(rec->candles, rec->features, n, &resolved, &tokens[i]);
	}
	if (resolved.wave_model != WAVE_MODEL_LAYERED) {
		normalize_relative_trend_tokens(rec->candles, rec->features, n, &resolved, tokens, &token_count);
	}
	if (is_relative_wave_model((uint32_t)resolved.wave_model)) {
		while (absorb_adjacent_one_candle_same_direction_ranges(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			for (i = 0; i < token_count; ++i) {
				recompute_token(rec->candles, rec->features, n, &resolved, &tokens[i]);
			}
		}
	#if !WAVE_DEBUG_SEEDED_DISABLE_PASS2
		while (recover_directional_relative_ranges_as_trends(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
		}
	#endif
		normalize_relative_trend_tokens(rec->candles, rec->features, n, &resolved, tokens, &token_count);
	}
#if !WAVE_DEBUG_SEEDED_PASS1_ONLY && !WAVE_DEBUG_SEEDED_PASS1_GROWN_ONLY
	if (resolved.wave_model == WAVE_MODEL_RELATIVE_ITERATIVE) {
		stage_start_ms = monotonic_ms();
		stage_iter = run_relative_iterative_refinement(rec->candles,
			rec->features,
			n,
			&resolved,
			tokens,
			&token_count,
			&rec->waves.kpi);
		log_wave_stage_done(dataset_id, "run_relative_iterative_refinement", stage_iter, token_count, stage_start_ms);
	} else if (resolved.wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		resolved.wave_model == WAVE_MODEL_RELATIVE_2_STAGE) {
	#if !WAVE_DEBUG_SEEDED_DISABLE_PASS2
		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (split_relative_ranges_by_edge_trim(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			stage_iter += 1u;
		}
		log_wave_stage_done(dataset_id, "split_relative_ranges_by_edge_trim", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (split_relative_edge_trends(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			stage_iter += 1u;
		}
		log_wave_stage_done(dataset_id, "split_relative_edge_trends", stage_iter, token_count, stage_start_ms);
	#endif

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (demote_relative_micro_trends(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			stage_iter += 1u;
		}
		log_wave_stage_done(dataset_id, "demote_relative_micro_trends", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (reduce_relative_choppy_runs(rec->candles, rec->features, n, &resolved, tokens, &token_count) > 0) {
			stage_iter += 1u;
		}
		log_wave_stage_done(dataset_id, "reduce_relative_choppy_runs", stage_iter, token_count, stage_start_ms);

		stage_iter = 0u;
		stage_start_ms = monotonic_ms();
		while (resolve_relative_same_direction_trends(rec->candles, rec->features, n, &resolved, tokens, &token_count, &rec->waves.kpi) > 0) {
			stage_iter += 1u;
		}
		log_wave_stage_done(dataset_id, "resolve_relative_same_direction_trends", stage_iter, token_count, stage_start_ms);
	}
#endif
	if (resolved.wave_model != WAVE_MODEL_LAYERED) {
		enforce_relative_two_stage_trend_edges(rec->candles, rec->features, n, &resolved, tokens, token_count);
		normalize_relative_trend_seed_bounds(rec->candles, rec->features, n, &resolved, tokens, token_count);
	}
	compute_boundary_confidence(rec->candles, rec->features, n, &resolved, tokens, token_count, &rec->waves.kpi);
	compute_knn_wave_metrics(&resolved, tokens, token_count);
	compute_kpis(&resolved, tokens, token_count, n, &rec->waves.kpi);

	total_fit_error = (int64_t)resolved.segment_penalty_lambda * (int64_t)token_count;
	memset(rec->waves.class_counts, 0, sizeof(rec->waves.class_counts));
	for (i = 0; i < token_count; ++i) {
		total_fit_error += tokens[i].fit_error;
		if (resolved.wave_model != WAVE_MODEL_PRIMITIVE &&
			tokens[i].class_id < WAVE_CLASS_COUNT) {
			rec->waves.class_counts[tokens[i].class_id] += 1u;
		}
	}
	total_fit_error +=
		(int64_t)rec->waves.kpi.rule_retracement_edge_count * resolved.transition_penalty_retracement_edge +
		(int64_t)rec->waves.kpi.rule_retracement_context_count * resolved.transition_penalty_retracement_context +
		(int64_t)rec->waves.kpi.rule_retracement_direction_count * resolved.transition_penalty_retracement_direction +
		(int64_t)rec->waves.kpi.rule_retracement_chain_count * resolved.transition_penalty_retracement_chain +
		(int64_t)rec->waves.kpi.same_direction_trend_kept_count * resolved.transition_penalty_same_dir_trend_split +
		(int64_t)rec->waves.kpi.opposite_direction_v_reversal_rejected_count * resolved.transition_penalty_opp_dir_trend_no_v;

	free(rec->waves.tokens);
	rec->waves.tokens = tokens;
	rec->waves.token_count = token_count;
	free(rec->waves.layer1_tokens);
	rec->waves.layer1_tokens = layer1_tokens;
	rec->waves.layer1_token_count = layer1_token_count;
	rec->waves.total_fit_error = total_fit_error;
	rec->waves.params = resolved;
	if (progress != NULL) {
		*progress = 1.0f;
	}
	trainer_log_info("wave inference complete dataset_id=%u tokens=%u total_fit_error=%lld weak_fit=%u low_margin=%u elapsed_ms=%llu",
		dataset_id,
		token_count,
		(long long)total_fit_error,
		rec->waves.kpi.weak_fit_count,
		rec->waves.kpi.low_margin_token_count,
		(unsigned long long)(monotonic_ms() - run_start_ms));

	return 0;
}
