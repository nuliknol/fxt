#include "core_api.h"
#include "internal_store.h"

#include <stdlib.h>

static void build_block_summary(const int32_t *block, uint32_t q, BlockSummary *out)
{
	uint32_t j;
	int32_t last_sign = 0;
	int32_t bull_run = 0;
	int32_t bear_run = 0;
	int32_t range_sum = 0;

	out->bull_count = 0;
	out->bear_count = 0;
	out->sign_changes = 0;
	out->max_bull_run = 0;
	out->max_bear_run = 0;
	out->net_body = 0;
	out->gross_body = 0;
	out->avg_range = 0;

	for (j = 0; j < q; ++j) {
		int32_t body = block[j * 4u + 2u];
		int32_t range = block[j * 4u + 3u];
		int32_t sign = 0;

		if (body > 0) {
			sign = 1;
			out->bull_count += 1;
			bull_run += 1;
			bear_run = 0;
		} else if (body < 0) {
			sign = -1;
			out->bear_count += 1;
			bear_run += 1;
			bull_run = 0;
		} else {
			bull_run = 0;
			bear_run = 0;
		}
		if (sign != 0 && last_sign != 0 && sign != last_sign) {
			out->sign_changes += 1;
		}
		if (sign != 0) {
			last_sign = sign;
		}
		if (bull_run > out->max_bull_run) {
			out->max_bull_run = bull_run;
		}
		if (bear_run > out->max_bear_run) {
			out->max_bear_run = bear_run;
		}
		out->net_body += body;
		out->gross_body += abs(body);
		range_sum += range;
	}
	if (q > 0u) {
		out->avg_range = range_sum / (int32_t)q;
	}
}

int core_build_character_blocks(uint32_t dataset_id, uint32_t q)
{
	DatasetRecord *rec;
	uint32_t n_blocks;
	uint32_t dim;
	int32_t *data;
	BlockSummary *summaries;
	uint32_t i;
	uint32_t j;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -1;
	}
	if (rec->features == NULL || rec->meta.feature_count == 0) {
		return -2;
	}
	if (q < 1 || q > rec->meta.feature_count) {
		return -3;
	}

	n_blocks = rec->meta.feature_count - q + 1;
	dim = q * 4;

	free(rec->chars.data);
	rec->chars.data = NULL;
	free(rec->chars.summaries);
	rec->chars.summaries = NULL;

	data = (int32_t *)malloc((size_t)n_blocks * (size_t)dim * sizeof(int32_t));
	summaries = (BlockSummary *)malloc((size_t)n_blocks * sizeof(*summaries));
	if (data == NULL || summaries == NULL) {
		free(data);
		free(summaries);
		return -4;
	}

	for (i = 0; i < n_blocks; ++i) {
		for (j = 0; j < q; ++j) {
			const CandleFeat *f = &rec->features[i + j];
			uint32_t base = i * dim + j * 4;
			data[base + 0] = f->u;
			data[base + 1] = f->l;
			data[base + 2] = f->b;
			data[base + 3] = f->r;
		}
		build_block_summary(&data[i * dim], q, &summaries[i]);
	}

	rec->chars.n_blocks = n_blocks;
	rec->chars.q = q;
	rec->chars.dim = dim;
	rec->chars.data = data;
	rec->chars.summaries = summaries;
	rec->meta.char_block_count = n_blocks;
	rec->meta.q = q;
	return 0;
}
