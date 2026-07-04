#ifndef GPU_DISPATCH_H
#define GPU_DISPATCH_H

#include "api_types.h"

int gpu_dispatch_available(void);
void gpu_dispatch_shutdown(void);
int gpu_distance_tile(const CharMatrix *m,
	uint32_t row_start,
	uint32_t row_count,
	uint32_t col_start,
	uint32_t col_count,
	const DistanceConfig *cfg,
	int32_t *out_dist);
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
	int64_t *out_total_fit_error);
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
	uint32_t *out_duration);

#endif
