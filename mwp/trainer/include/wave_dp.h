#ifndef WAVE_DP_H
#define WAVE_DP_H

#include "api_types.h"

#include <stddef.h>

int wave_dp_run(uint32_t dataset_id, const WaveParams *params, volatile float *progress, volatile int *cancel_flag, char *err, size_t err_sz);

#endif
