#ifndef MICRO_HARVEST_H
#define MICRO_HARVEST_H

#include <stdint.h>

#include "api_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int micro_harvest_run(
	const Candle *candles,
	int32_t candle_count,
	const WaveToken *waves,
	int32_t wave_count,
	const MicroHarvestSearchConfig *config,
	MicroHarvestResult *out_result
);

int micro_harvest_run_replay(
	uint32_t replay_dataset_id,
	const WaveParams *wave_params,
	const MicroHarvestSearchConfig *config,
	MicroHarvestResult *out_result,
	volatile float *progress
);

void micro_harvest_result_free(MicroHarvestResult *result);

#ifdef __cplusplus
}
#endif

#endif
