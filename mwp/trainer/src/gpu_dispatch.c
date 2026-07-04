#include "gpu_dispatch.h"

#ifndef TRAINER_ENABLE_HIP_BACKEND
#define TRAINER_ENABLE_HIP_BACKEND 0
#endif

#if TRAINER_ENABLE_HIP_BACKEND
int hip_distance_tile_impl(const CharMatrix *m,
	uint32_t row_start,
	uint32_t row_count,
	uint32_t col_start,
	uint32_t col_count,
	const DistanceConfig *cfg,
	int32_t *out_dist);
int hip_wave_dp_impl(const Candle *candles,
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
	int64_t *out_total_fit_error);
int hip_wave_seed_scan_impl(const Candle *candles,
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
	uint32_t *out_duration);
void hip_distance_shutdown_impl(void);
#endif

int gpu_dispatch_available(void)
{
#if TRAINER_ENABLE_HIP_BACKEND
	return 1;
#else
	return 0;
#endif
}

int gpu_distance_tile(const CharMatrix *m,
	uint32_t row_start,
	uint32_t row_count,
	uint32_t col_start,
	uint32_t col_count,
	const DistanceConfig *cfg,
	int32_t *out_dist)
{
	if (m == NULL || cfg == NULL || out_dist == NULL) {
		return -1;
	}
#if TRAINER_ENABLE_HIP_BACKEND
	return hip_distance_tile_impl(m, row_start, row_count, col_start, col_count, cfg, out_dist);
#else
	(void)m;
	(void)row_start;
	(void)row_count;
	(void)col_start;
	(void)col_count;
	(void)cfg;
	(void)out_dist;
	return -100;
#endif
}

int gpu_wave_dp(const Candle *candles,
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
	if (candles == NULL || features == NULL || params == NULL || out_prev_start == NULL || out_class_id == NULL ||
		out_fit_error == NULL || out_net_move_raw == NULL || out_retracement_ratio == NULL ||
		out_sign_change_density == NULL || out_directional_purity_body == NULL || out_range_growth_ratio == NULL ||
		out_context_range == NULL || out_total_fit_error == NULL) {
		return -1;
	}
#if TRAINER_ENABLE_HIP_BACKEND
	return hip_wave_dp_impl(candles,
		features,
		candle_count,
		params,
		out_prev_start,
		out_class_id,
		out_fit_error,
		out_net_move_raw,
		out_retracement_ratio,
		out_sign_change_density,
		out_directional_purity_body,
		out_range_growth_ratio,
		out_context_range,
		out_total_fit_error);
#else
	(void)candles;
	(void)features;
	(void)candle_count;
	(void)params;
	(void)out_prev_start;
	(void)out_class_id;
	(void)out_fit_error;
	(void)out_net_move_raw;
	(void)out_retracement_ratio;
	(void)out_sign_change_density;
	(void)out_directional_purity_body;
	(void)out_range_growth_ratio;
	(void)out_context_range;
	(void)out_total_fit_error;
	return -100;
#endif
}

int gpu_wave_seed_scan(const Candle *candles,
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
	if (candles == NULL || features == NULL || params == NULL ||
		out_best_end == NULL || out_quality_raw == NULL || out_net_move_raw == NULL ||
		out_price_amplitude_raw == NULL || out_retracement_ratio == NULL || out_overlap_ratio == NULL ||
		out_wick_to_body_ratio == NULL || out_strongest_leg_raw == NULL || out_duration == NULL) {
		return -1;
	}
#if TRAINER_ENABLE_HIP_BACKEND
	return hip_wave_seed_scan_impl(candles,
		features,
		candle_count,
		params,
		range_start,
		range_end,
		seed_pass,
		out_best_end,
		out_quality_raw,
		out_net_move_raw,
		out_price_amplitude_raw,
		out_retracement_ratio,
		out_overlap_ratio,
		out_wick_to_body_ratio,
		out_strongest_leg_raw,
		out_duration);
#else
	(void)candles;
	(void)features;
	(void)candle_count;
	(void)params;
	(void)range_start;
	(void)range_end;
	(void)seed_pass;
	(void)out_best_end;
	(void)out_quality_raw;
	(void)out_net_move_raw;
	(void)out_price_amplitude_raw;
	(void)out_retracement_ratio;
	(void)out_overlap_ratio;
	(void)out_wick_to_body_ratio;
	(void)out_strongest_leg_raw;
	(void)out_duration;
	return -100;
#endif
}

void gpu_dispatch_shutdown(void)
{
#if TRAINER_ENABLE_HIP_BACKEND
	hip_distance_shutdown_impl();
#endif
}
