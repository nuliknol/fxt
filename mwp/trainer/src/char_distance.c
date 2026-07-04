#include "core_api.h"

#include <stddef.h>
#include <stdlib.h>

static void mirror_vec(const int32_t *src, int32_t *dst, uint32_t dim)
{
	uint32_t i;
	for (i = 0; i < dim; i += 4) {
		dst[i + 0] = src[i + 1];
		dst[i + 1] = src[i + 0];
		dst[i + 2] = -src[i + 2];
		dst[i + 3] = src[i + 3];
	}
}

static float distance_one(const int32_t *a, const int32_t *b, uint32_t dim, const DistanceConfig *cfg)
{
	uint32_t i;
	float sum = 0.0f;

	for (i = 0; i < dim; ++i) {
		int32_t diff = a[i] - b[i];
		int mismatch = (abs(diff) > cfg->delta) ? 1 : 0;
		if (!cfg->weighted) {
			sum += (float)mismatch;
			continue;
		}
		switch (i % 4) {
		case 0:
			sum += cfg->w_upper * mismatch;
			break;
		case 1:
			sum += cfg->w_lower * mismatch;
			break;
		case 2:
			sum += cfg->w_body * mismatch;
			break;
		default:
			sum += cfg->w_range * mismatch;
			break;
		}
	}
	return sum;
}

int core_distance_block_pair(const CharMatrix *m, uint32_t a_idx, uint32_t b_idx, const DistanceConfig *cfg, float *out_dist)
{
	const int32_t *a;
	const int32_t *b;
	float direct;
	float mirrored;
	int32_t mirror_buf[4096];

	if (m == NULL || cfg == NULL || out_dist == NULL) {
		return -1;
	}
	if (a_idx >= m->n_blocks || b_idx >= m->n_blocks) {
		return -2;
	}
	if (m->dim == 0 || m->data == NULL) {
		return -3;
	}
	if (m->dim > (sizeof(mirror_buf) / sizeof(mirror_buf[0]))) {
		return -4;
	}

	a = &m->data[(size_t)a_idx * m->dim];
	b = &m->data[(size_t)b_idx * m->dim];
	direct = distance_one(a, b, m->dim, cfg);
	if (!cfg->mirrored_mode) {
		*out_dist = direct;
		return 0;
	}
	mirror_vec(b, mirror_buf, m->dim);
	mirrored = distance_one(a, mirror_buf, m->dim, cfg);
	*out_dist = (direct < mirrored) ? direct : mirrored;
	return 0;
}
