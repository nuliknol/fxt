#ifndef WAVE_PREDICTOR_H
#define WAVE_PREDICTOR_H

#include <stddef.h>
#include <stdint.h>

int wave_predictor_encode_json(uint32_t dataset_id,
	uint32_t anchor_token,
	uint32_t input_len,
	uint32_t horizon,
	int with_eval,
	char *out,
	size_t out_cap);

int wave_predictor_encode_base_probs_json(uint32_t dataset_id,
	char *out,
	size_t out_cap);

#endif
